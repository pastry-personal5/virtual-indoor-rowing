#include "WorkoutRuntime/AppJournal.h"
#include "WorkoutRuntime/AppSessionConfig.h"
#include "WorkoutRuntime/LatencyStats.h"
#include "RowingSim/MockRowingMachine.h"
#include "RowingSim/TelemetryFixtures.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	constexpr std::uint64_t NsPerMs = 1000000ULL;

	// Not a real cipher (see workout_runtime_tests): summaries only need one to exist.
	class FTestCipher final : public LocalData::IBlobCipher
	{
	  public:
		std::string Seal(std::string_view Plaintext, std::string_view) override
		{
			std::string Out;
			for (const char Byte : Plaintext)
				Out.push_back(static_cast<char>(Byte ^ 0x5A));
			return Out;
		}
		std::string Open(std::string_view Sealed, std::string_view AssociatedData) override
		{
			return Seal(Sealed, AssociatedData);
		}
	};

	struct FTempDirectory
	{
		FTempDirectory()
		{
			std::random_device Random;
			Path = std::filesystem::temp_directory_path() / ("app_journal_tests_" + std::to_string(Random()));
		}
		~FTempDirectory()
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Path, Ignored);
		}
		std::filesystem::path Path;
	};

	FAppJournal::FCipherFactory TestCipherFactory()
	{
		return []() -> std::unique_ptr<LocalData::IBlobCipher>
		{ return std::make_unique<FTestCipher>(); };
	}

	FRowingMetricSample MakeSample(std::uint64_t Index)
	{
		FRowingMetricSample Sample;
		Sample.SourceElapsedMs = Index * 100;
		Sample.DistanceMm = Index * 1700;
		Sample.StrokeRateDeciSpm = 240;
		Sample.WorkoutState = ERowingWorkoutState::Active;
		Sample.RowingState = ERowingState::Active;
		Sample.StrokeState = ERowingStrokeState::Drive;
		return Sample;
	}

	// Drives a session the way the app's subsystem does: it drains the machine and
	// ingests each event, the session never polls.
	struct FRow
	{
		explicit FRow(IJournalSink &Sink)
			: Machine(std::make_unique<RowingSim::FMockRowingMachine>(RowingSim::MakeSyntheticIndoorRowerScenario()))
		{
			Machine->Connect();
			FWorkoutSessionDependencies Deps;
			Deps.Machine = Machine.get();
			Deps.Sink = &Sink;
			Deps.UnixTimeMs = []
			{ return std::uint64_t{1758067200000ULL}; };
			Deps.RandomByte = [Next = std::uint8_t{7}]() mutable
			{ return Next++; };
			Session = std::make_unique<FWorkoutSession>(std::move(Deps), MakeAppSessionConfig(true));
			Pump();
		}
		void Pump()
		{
			FRowingMachineEvent Event;
			while (Machine->TryPollEvent(Event))
				Session->Ingest(Event);
			Session->Tick(Now);
		}
		void Step(std::uint64_t Ms)
		{
			Now += Ms * NsPerMs;
			Machine->AdvanceTo(Now);
			Pump();
		}
		void Sample(std::uint64_t Index)
		{
			Machine->PublishTelemetry(MakeSample(Index));
			Pump();
		}
		std::unique_ptr<RowingSim::FMockRowingMachine> Machine;
		std::unique_ptr<FWorkoutSession> Session;
		std::uint64_t Now = 0;
	};

	void app_journal_recovery_creates_nothing_when_no_database_exists()
	{
		FTempDirectory Temp;
		const FAppJournalRecovery Recovery = RecoverInterruptedSession(Temp.Path);
		EXPECT_TRUE(!Recovery.bScanned);
		EXPECT_TRUE(!Recovery.bRecoveredInterruptedSession);
		EXPECT_TRUE(Recovery.Error.empty());
		EXPECT_TRUE(!std::filesystem::exists(Temp.Path));
	}

	void app_journal_open_creates_an_owner_only_directory_and_database()
	{
		FTempDirectory Temp;
		const std::filesystem::path Directory = Temp.Path / "journal";
		FAppJournal::FOpenResult Result = FAppJournal::Open(Directory, TestCipherFactory());
		EXPECT_TRUE(Result.Journal != nullptr);
		EXPECT_TRUE(Result.Error.empty());
		const std::filesystem::perms Permissions = std::filesystem::status(Directory).permissions();
		EXPECT_TRUE((Permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
		EXPECT_TRUE((Permissions & std::filesystem::perms::owner_all) == std::filesystem::perms::owner_all);
	}

	void app_journal_open_reports_a_key_failure_and_creates_nothing()
	{
		FTempDirectory Temp;
		FAppJournal::FOpenResult Thrown = FAppJournal::Open(Temp.Path, []() -> std::unique_ptr<LocalData::IBlobCipher>
															{ throw LocalData::FBlobCipherError("keychain denied"); });
		EXPECT_TRUE(Thrown.Journal == nullptr);
		EXPECT_TRUE(Thrown.Error == "keychain denied");
		FAppJournal::FOpenResult Null = FAppJournal::Open(Temp.Path, []() -> std::unique_ptr<LocalData::IBlobCipher>
														  { return nullptr; });
		EXPECT_TRUE(Null.Journal == nullptr);
		EXPECT_TRUE(!Null.Error.empty());
		EXPECT_TRUE(!std::filesystem::exists(Temp.Path));
	}

	void app_journal_row_checkpoints_within_one_second()
	{
		FTempDirectory Temp;
		FAppJournal::FOpenResult Opened = FAppJournal::Open(Temp.Path, TestCipherFactory());
		EXPECT_TRUE(Opened.Journal != nullptr);
		if (!Opened.Journal)
			return;
		FRow Row(Opened.Journal->GetSink());
		// Fewer samples than the count trigger: only the 1 s time trigger can flush.
		for (std::uint64_t Index = 1; Index <= 5; ++Index)
		{
			Row.Step(100);
			Row.Sample(Index);
		}
		FTestCipher Cipher;
		const std::string Id = Row.Session->GetSnapshot().SessionId.ToCanonicalString();
		EXPECT_TRUE(LocalData::ReadSampleChunks(GetAppJournalDatabasePath(Temp.Path), Id, &Cipher).empty());
		for (std::uint64_t Index = 6; Index <= 12; ++Index)
		{
			Row.Step(100);
			Row.Sample(Index);
		}
		std::size_t Committed = 0;
		for (const LocalData::FSampleChunk &Chunk : LocalData::ReadSampleChunks(GetAppJournalDatabasePath(Temp.Path), Id, &Cipher))
			Committed += Chunk.Samples.size();
		EXPECT_TRUE(Committed > 0);
	}

	void app_journal_abandoned_row_is_recovered_by_the_next_launch()
	{
		FTempDirectory Temp;
		{
			FAppJournal::FOpenResult Opened = FAppJournal::Open(Temp.Path, TestCipherFactory());
			EXPECT_TRUE(Opened.Journal != nullptr);
			if (!Opened.Journal)
				return;
			FRow Row(Opened.Journal->GetSink());
			for (std::uint64_t Index = 1; Index <= 15; ++Index)
			{
				Row.Step(100);
				Row.Sample(Index);
			}
			// No End(), no clean shutdown: the row is abandoned as a killed process leaves it.
		}
		const FAppJournalRecovery First = RecoverInterruptedSession(Temp.Path);
		EXPECT_TRUE(First.bScanned);
		EXPECT_TRUE(First.bRecoveredInterruptedSession);
		EXPECT_TRUE(First.Error.empty());
		const FAppJournalRecovery Second = RecoverInterruptedSession(Temp.Path);
		EXPECT_TRUE(Second.bScanned);
		EXPECT_TRUE(!Second.bRecoveredInterruptedSession);
	}

	void app_session_config_matches_the_journaling_policy()
	{
		const FWorkoutSessionConfig Real = MakeAppSessionConfig(true);
		EXPECT_TRUE(!Real.bPollMachine);
		EXPECT_TRUE(Real.FlushIntervalMs <= 1000);
		const FWorkoutSessionConfig Simulator = MakeAppSessionConfig(false);
		EXPECT_TRUE(!Simulator.bPollMachine);
	}

	void latency_stats_report_nearest_rank_percentiles_and_max()
	{
		FLatencyStats Stats;
		EXPECT_TRUE(Stats.GetPercentileNs(95.0) == 0);
		for (std::uint64_t Ms = 1; Ms <= 100; ++Ms)
			Stats.Record(Ms * NsPerMs);
		EXPECT_TRUE(Stats.GetCount() == 100);
		EXPECT_TRUE(Stats.GetPercentileNs(50.0) == 50 * NsPerMs);
		EXPECT_TRUE(Stats.GetPercentileNs(95.0) == 95 * NsPerMs);
		EXPECT_TRUE(Stats.GetPercentileNs(100.0) == 100 * NsPerMs);
		EXPECT_TRUE(Stats.GetMaxNs() == 100 * NsPerMs);
		const std::string Json = Stats.ToJson("abc\"123");
		EXPECT_TRUE(Json.find("\"p95_ms\":95.000") != std::string::npos);
		EXPECT_TRUE(Json.find("\"sample_count\":100") != std::string::npos);
		EXPECT_TRUE(Json.find("abc\\\"123") != std::string::npos);
		Stats.Reset();
		EXPECT_TRUE(Stats.GetCount() == 0 && Stats.GetMaxNs() == 0);
	}

	void latency_stats_bound_retained_samples_but_keep_counting()
	{
		FLatencyStats Stats;
		for (std::size_t Index = 0; Index < FLatencyStats::MaxRetainedSamples + 10; ++Index)
			Stats.Record(Index == FLatencyStats::MaxRetainedSamples + 5 ? 9 * NsPerMs : NsPerMs);
		EXPECT_TRUE(Stats.GetCount() == FLatencyStats::MaxRetainedSamples + 10);
		EXPECT_TRUE(Stats.GetDroppedCount() == 10);
		EXPECT_TRUE(Stats.GetMaxNs() == 9 * NsPerMs);
	}

	void write_owner_only_file_restricts_permissions_and_reports_errors()
	{
		FTempDirectory Temp;
		EXPECT_TRUE(WriteOwnerOnlyFile(Temp.Path / "metrics", "latency.json", "{}\n").empty());
		const std::filesystem::path File = Temp.Path / "metrics" / "latency.json";
		const std::filesystem::perms Permissions = std::filesystem::status(File).permissions();
		EXPECT_TRUE((Permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
		std::ifstream In(File);
		std::stringstream Content;
		Content << In.rdbuf();
		EXPECT_TRUE(Content.str() == "{}\n");
		// A file where the directory should be is an error, not a crash.
		EXPECT_TRUE(!WriteOwnerOnlyFile(File, "x.json", "{}").empty());
	}
} // namespace

int main()
{
	app_journal_recovery_creates_nothing_when_no_database_exists();
	app_journal_open_creates_an_owner_only_directory_and_database();
	app_journal_open_reports_a_key_failure_and_creates_nothing();
	app_journal_row_checkpoints_within_one_second();
	app_journal_abandoned_row_is_recovered_by_the_next_launch();
	app_session_config_matches_the_journaling_policy();
	latency_stats_report_nearest_rank_percentiles_and_max();
	latency_stats_bound_retained_samples_but_keep_counting();
	write_owner_only_file_restricts_permissions_and_reports_errors();
	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
