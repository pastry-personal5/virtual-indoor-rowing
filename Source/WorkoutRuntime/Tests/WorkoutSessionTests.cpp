#include "WorkoutRuntime/WorkoutSession.h"

#include "WorkoutRuntime/LocalDataJournalSink.h"
#include "WorkoutRuntime/SessionWireMapping.h"
#include "pm5_sim/MockRowingMachine.h"
#include "pm5_sim/ReplayRowingMachine.h"
#include "pm5_sim/TelemetryFixtures.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

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

	// In-memory IJournalSink that records what the runtime asked to persist and
	// can be made to fail, so runtime behavior is testable without SQLite.
	class FFakeSink final : public IJournalSink
	{
	  public:
		struct FEvent
		{
			LocalData::EJournalEventKind Kind;
			std::uint64_t Sequence;
			std::string Payload;
		};

		bool bFailAppend = false;
		int CreateSessionCount = 0;
		std::vector<ERowingSessionState> States;
		std::vector<FEvent> Events;
		std::vector<std::vector<FRowingMetricSample>> Chunks;
		std::vector<std::string> SummaryPayloads;

		void CreateSession(const LocalData::FSessionRecord &) override
		{
			++CreateSessionCount;
		}
		void UpdateSessionState(const FRowingSessionId &, ERowingSessionState State) override
		{
			States.push_back(State);
		}
		void RecordEvent(const FRowingSessionId &, LocalData::EJournalEventKind Kind, std::uint64_t Sequence, std::uint64_t, const std::string &Payload) override
		{
			Events.push_back({Kind, Sequence, Payload});
		}
		void RecordCapability(const FRowingSessionId &, std::uint64_t Sequence, std::uint64_t, const FRowingMachineInfo &) override
		{
			Events.push_back({LocalData::EJournalEventKind::CapabilityObserved, Sequence, {}});
		}
		void AppendSamples(const FRowingSessionId &, const std::vector<FRowingMetricSample> &Samples) override
		{
			if (bFailAppend)
				throw std::runtime_error("injected append failure");
			Chunks.push_back(Samples);
		}
		void WriteSummary(const FRowingSessionId &, const std::string &Payload, std::uint32_t) override
		{
			SummaryPayloads.push_back(Payload);
		}

		std::size_t SampleCount() const
		{
			std::size_t Count = 0;
			for (const auto &Chunk : Chunks)
				Count += Chunk.size();
			return Count;
		}
		std::size_t EventCount(LocalData::EJournalEventKind Kind) const
		{
			std::size_t Count = 0;
			for (const FEvent &Event : Events)
				Count += Event.Kind == Kind ? 1 : 0;
			return Count;
		}
	};

	// A stand-in for the CryptoKit adapter (see LocalDataJournalTests.cpp): only
	// needed because summaries require a cipher. Not a real cipher.
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

	FRowingMetricSample MakeSample(std::uint64_t Index,
								   ERowingWorkoutState WorkoutState = ERowingWorkoutState::Active,
								   ERowingState RowingState = ERowingState::Active,
								   FRowingQualityFlags Flags = ToRowingQualityFlags(ERowingQualityFlag::None))
	{
		FRowingMetricSample Sample;
		Sample.SourceElapsedMs = Index * 100;
		Sample.DistanceMm = Index * 1700;
		Sample.StrokeRateDeciSpm = 240;
		Sample.WorkoutState = WorkoutState;
		Sample.RowingState = RowingState;
		Sample.StrokeState = ERowingStrokeState::Drive;
		Sample.QualityFlags = Flags;
		return Sample;
	}

	FWorkoutSessionDependencies MakeDependencies(IRowingMachine &Machine, IJournalSink &Sink)
	{
		FWorkoutSessionDependencies Deps;
		Deps.Machine = &Machine;
		Deps.Sink = &Sink;
		Deps.UnixTimeMs = []
		{ return std::uint64_t{1758067200000ULL}; };
		Deps.RandomByte = [Next = std::uint8_t{0}]() mutable
		{ return Next++; };
		return Deps;
	}

	// A connected mock machine, a session over it, and a virtual clock shared by
	// both. Step() advances time; Publish() delivers one device fact.
	struct FHarness
	{
		explicit FHarness(IJournalSink &Sink, FWorkoutSessionConfig Config = {})
			: Machine(std::make_unique<pm5_sim::FMockRowingMachine>(pm5_sim::MakeSyntheticIndoorRowerScenario()))
		{
			Machine->Connect();
			Session = std::make_unique<FWorkoutSession>(MakeDependencies(*Machine, Sink), std::move(Config));
			Session->Tick(Now);
		}

		void Step(std::uint64_t Ms)
		{
			Now += Ms * NsPerMs;
			Machine->AdvanceTo(Now);
			Session->Tick(Now);
		}

		void Publish(FRowingMetricSample Sample)
		{
			Machine->PublishTelemetry(std::move(Sample));
			Session->Tick(Now);
		}

		// One sample every 100 ms, as a steady row.
		void Row(std::uint64_t FirstIndex, std::uint64_t Count)
		{
			for (std::uint64_t Index = FirstIndex; Index < FirstIndex + Count; ++Index)
			{
				Step(100);
				Publish(MakeSample(Index));
			}
		}

		std::unique_ptr<pm5_sim::FMockRowingMachine> Machine;
		std::unique_ptr<FWorkoutSession> Session;
		std::uint64_t Now = 0;
	};

	// Drives a replay fixture at 100 ms steps through and past its duration.
	void RunReplay(const pm5_sim::FGoldenTelemetryFixture &Fixture, FWorkoutSession *&SessionOut, FFakeSink &Sink, std::unique_ptr<pm5_sim::FReplayRowingMachine> &MachineOut, std::unique_ptr<FWorkoutSession> &Owner)
	{
		MachineOut = std::make_unique<pm5_sim::FReplayRowingMachine>(pm5_sim::MakeSyntheticIndoorRowerScenario(), Fixture.Frames);
		MachineOut->Connect();
		Owner = std::make_unique<FWorkoutSession>(MakeDependencies(*MachineOut, Sink));
		SessionOut = Owner.get();
		SessionOut->Tick(0);
		for (std::uint64_t TimeMs = 0; TimeMs <= Fixture.DurationMs; TimeMs += 100)
		{
			MachineOut->AdvanceTo(TimeMs * NsPerMs);
			SessionOut->Tick(TimeMs * NsPerMs);
		}
	}

	std::filesystem::path MakeTempDatabasePath(const char *const TestName)
	{
		std::random_device Random;
		return std::filesystem::temp_directory_path() /
			   (std::string("workout_runtime_tests_") + TestName + "_" + std::to_string(Random()) + ".sqlite3");
	}

	void RemoveDatabase(const std::filesystem::path &Path)
	{
		std::error_code Ignored;
		std::filesystem::remove(Path, Ignored);
		std::filesystem::remove(Path.string() + "-wal", Ignored);
		std::filesystem::remove(Path.string() + "-shm", Ignored);
	}

	void workout_session_journals_samples_from_simulator()
	{
		FFakeSink Sink;
		std::unique_ptr<pm5_sim::FReplayRowingMachine> Machine;
		std::unique_ptr<FWorkoutSession> Owner;
		FWorkoutSession *Session = nullptr;
		const auto Fixture = pm5_sim::MakeEasy30SecondFixture();
		RunReplay(Fixture, Session, Sink, Machine, Owner);

		EXPECT_TRUE(Session->GetSnapshot().State == ERowingSessionState::Active);
		EXPECT_TRUE(Session->End(31000 * NsPerMs));
		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::Ended);
		EXPECT_TRUE(Snapshot.Disposition == ERowingSessionDisposition::Completed);
		EXPECT_TRUE(Snapshot.EndReason == ERowingSessionStateReason::UserCompleted);

		// Frame 0 reports an inactive rower, so 300 of the 301 frames are official.
		EXPECT_TRUE(Snapshot.AcceptedSampleCount == 300);
		EXPECT_TRUE(Sink.SampleCount() == 300);
		EXPECT_TRUE(Sink.Chunks.size() > 1);
		std::uint64_t Expected = Sink.Chunks.front().front().Sequence;
		bool bContiguous = true;
		for (const auto &Chunk : Sink.Chunks)
		{
			EXPECT_TRUE(Chunk.size() <= FWorkoutSessionConfig{}.FlushSampleCount);
			for (const FRowingMetricSample &Sample : Chunk)
				bContiguous = bContiguous && Sample.Sequence == Expected++;
		}
		EXPECT_TRUE(bContiguous);

		EXPECT_TRUE(Sink.CreateSessionCount == 1);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Started) == 1);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::CapabilityObserved) == 1);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Completed) == 1);
		EXPECT_TRUE(Sink.Events.front().Kind == LocalData::EJournalEventKind::Started);
		EXPECT_TRUE(Sink.Events.back().Kind == LocalData::EJournalEventKind::Completed);
		EXPECT_TRUE(Sink.States.back() == ERowingSessionState::Ended);

		EXPECT_TRUE(Sink.SummaryPayloads.size() == 1);
		const FWorkoutSummary Summary = WorkoutRuntime::Private::ParseSessionSummary(Sink.SummaryPayloads.front());
		EXPECT_TRUE(Summary.TotalDistanceMm == 66000);
		EXPECT_TRUE(Summary.ElapsedMs == 30000);
		EXPECT_TRUE(Summary.AcceptedSampleCount == 300);
		EXPECT_TRUE(Summary.AverageStrokeRateDeciSpm == 200);
		EXPECT_TRUE(Summary.AveragePaceMsPer500M == 227272);
		EXPECT_TRUE(!Summary.MaxHeartRateBpm.has_value());
	}

	void workout_session_activates_on_first_valid_sample()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Step(100);
		Harness.Publish(MakeSample(1, ERowingWorkoutState::WaitingToBegin, ERowingState::Inactive));
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Created);
		EXPECT_TRUE(Harness.Session->GetSnapshot().LatestSample.has_value());
		EXPECT_TRUE(Harness.Session->GetSnapshot().AcceptedSampleCount == 0);
		EXPECT_TRUE(Sink.CreateSessionCount == 0);
		EXPECT_TRUE(Sink.Events.empty());

		Harness.Step(100);
		Harness.Publish(MakeSample(2));
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Active);
		EXPECT_TRUE(Sink.CreateSessionCount == 1);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Started) == 1);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::CapabilityObserved) == 1);
		EXPECT_TRUE(Harness.Session->GetSnapshot().AcceptedSampleCount == 1);
	}

	void workout_session_ends_unstarted_session_without_persisting()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Step(100);
		Harness.Publish(MakeSample(1, ERowingWorkoutState::WaitingToBegin, ERowingState::Inactive));
		EXPECT_TRUE(Harness.Session->End(Harness.Now));
		EXPECT_TRUE(Harness.Session->GetSnapshot().Disposition == ERowingSessionDisposition::Aborted);
		EXPECT_TRUE(Sink.CreateSessionCount == 0);
		EXPECT_TRUE(Sink.Events.empty());
		EXPECT_TRUE(Sink.SummaryPayloads.empty());
		EXPECT_TRUE(!Harness.Session->End(Harness.Now));
	}

	void workout_session_rejects_flagged_samples_from_journal_and_snapshot()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 3);
		const std::uint64_t DistanceBefore = Harness.Session->GetSnapshot().LatestSample->DistanceMm;

		Harness.Step(100);
		Harness.Publish(MakeSample(4, ERowingWorkoutState::Active, ERowingState::Active, ToRowingQualityFlags(ERowingQualityFlag::Duplicate)));
		EXPECT_TRUE(Harness.Session->GetSnapshot().RejectedSampleCount == 1);
		EXPECT_TRUE(Harness.Session->GetSnapshot().LatestSample->DistanceMm == DistanceBefore);

		// Outlier is provenance, not a reason to discard a device fact.
		Harness.Step(100);
		Harness.Publish(MakeSample(5, ERowingWorkoutState::Active, ERowingState::Active, ToRowingQualityFlags(ERowingQualityFlag::Outlier)));
		EXPECT_TRUE(Harness.Session->GetSnapshot().RejectedSampleCount == 1);
		EXPECT_TRUE(Harness.Session->GetSnapshot().AcceptedSampleCount == 4);
		Harness.Session->End(Harness.Now);
		EXPECT_TRUE(Sink.SampleCount() == 4);
	}

	void workout_session_freezes_distance_on_link_loss()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 5);
		const std::uint64_t Frozen = Harness.Session->GetSnapshot().LatestSample->DistanceMm;
		EXPECT_TRUE(Frozen == 5 * 1700);

		Harness.Machine->SimulateLinkLoss();
		Harness.Step(3000);
		const FWorkoutSnapshot &Snapshot = Harness.Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::ConnectionLost);
		EXPECT_TRUE(Snapshot.LatestSample->DistanceMm == Frozen);
		EXPECT_TRUE(Sink.States.back() == ERowingSessionState::ConnectionLost);
		// The pre-loss samples were flushed before the state change was journaled.
		EXPECT_TRUE(Sink.SampleCount() == 5);
	}

	void workout_snapshot_marks_stale_during_gap()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 3);
		EXPECT_TRUE(!Harness.Session->GetSnapshot().bInputFrozen);
		const std::uint64_t RevisionBefore = Harness.Session->GetSnapshot().Revision;

		Harness.Machine->SimulateLinkLoss();
		Harness.Step(100);
		EXPECT_TRUE(Harness.Session->GetSnapshot().bInputFrozen);
		EXPECT_TRUE(Harness.Session->GetSnapshot().Revision > RevisionBefore);

		Harness.Step(1000);
		Harness.Machine->Connect();
		Harness.Publish(MakeSample(20));
		EXPECT_TRUE(!Harness.Session->GetSnapshot().bInputFrozen);
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Active);
	}

	void workout_session_resumes_without_synthesized_meters_after_reconnect()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 5);
		Harness.Machine->SimulateLinkLoss();
		Harness.Step(2000);
		Harness.Machine->Connect();
		Harness.Session->Tick(Harness.Now);
		// Reconnecting produced no official meters.
		EXPECT_TRUE(Harness.Session->GetSnapshot().LatestSample->DistanceMm == 5 * 1700);

		Harness.Publish(MakeSample(40));
		const FWorkoutSnapshot &Snapshot = Harness.Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::Active);
		EXPECT_TRUE(Snapshot.LatestSample->DistanceMm == 40 * 1700);
		EXPECT_TRUE(HasRowingQualityFlag(Snapshot.LatestSample->QualityFlags, ERowingQualityFlag::DeviceReconnected));
		EXPECT_TRUE(Snapshot.GapCount == 1);
		EXPECT_TRUE(Snapshot.TotalGapMs >= 2000);

		Harness.Session->End(Harness.Now);
		// Only the device's own six facts were journaled; nothing fills the gap.
		EXPECT_TRUE(Sink.SampleCount() == 6);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::LinkGap) == 1);
		const auto GapEvent = std::find_if(Sink.Events.begin(), Sink.Events.end(), [](const FFakeSink::FEvent &Event)
										   { return Event.Kind == LocalData::EJournalEventKind::LinkGap; });
		const WorkoutRuntime::Private::FLinkGap Gap = WorkoutRuntime::Private::ParseLinkGap(GapEvent->Payload);
		EXPECT_TRUE(Gap.bDeviceReported);
		EXPECT_TRUE(Gap.GapDurationMs >= 2000);
		const FWorkoutSummary Summary = WorkoutRuntime::Private::ParseSessionSummary(Sink.SummaryPayloads.front());
		EXPECT_TRUE(Summary.GapCount == 1);
		EXPECT_TRUE(Summary.TotalDistanceMm == 40 * 1700);
	}

	void workout_session_resumes_after_stale_link_without_disconnect()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 3);
		// No telemetry for over the mock's 500 ms stale deadline.
		Harness.Step(800);
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::ConnectionLost);
		Harness.Publish(MakeSample(10));
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Active);
		EXPECT_TRUE(Harness.Session->GetSnapshot().GapCount == 1);
		const auto GapEvent = std::find_if(Sink.Events.begin(), Sink.Events.end(), [](const FFakeSink::FEvent &Event)
										   { return Event.Kind == LocalData::EJournalEventKind::LinkGap; });
		EXPECT_TRUE(GapEvent != Sink.Events.end());
		EXPECT_TRUE(!WorkoutRuntime::Private::ParseLinkGap(GapEvent->Payload).bDeviceReported);
	}

	void workout_session_interrupts_after_reconnect_window()
	{
		FFakeSink Sink;
		FWorkoutSessionConfig Config;
		Config.ReconnectWindowMs = 1000;
		FHarness Harness(Sink, Config);
		Harness.Row(1, 3);
		Harness.Machine->SimulateLinkLoss();
		Harness.Step(100);
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::ConnectionLost);
		Harness.Step(500);
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::ConnectionLost);
		Harness.Step(600);

		const FWorkoutSnapshot &Snapshot = Harness.Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::Ended);
		EXPECT_TRUE(Snapshot.Disposition == ERowingSessionDisposition::Interrupted);
		EXPECT_TRUE(Snapshot.EndReason == ERowingSessionStateReason::ReconnectWindowElapsed);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Interrupted) == 1);
		EXPECT_TRUE(Sink.States.back() == ERowingSessionState::Ended);
		EXPECT_TRUE(Sink.SummaryPayloads.size() == 1);
	}

	void workout_session_completes_on_device_complete_state()
	{
		FFakeSink Sink;
		std::unique_ptr<pm5_sim::FReplayRowingMachine> Machine;
		std::unique_ptr<FWorkoutSession> Owner;
		FWorkoutSession *Session = nullptr;
		RunReplay(pm5_sim::MakeDeviceCompletedFixture(), Session, Sink, Machine, Owner);

		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::Ended);
		EXPECT_TRUE(Snapshot.Disposition == ERowingSessionDisposition::Completed);
		EXPECT_TRUE(Snapshot.EndReason == ERowingSessionStateReason::DeviceCompleted);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Completed) == 1);
		// The terminal sample itself is journaled before the session ends.
		EXPECT_TRUE(Sink.Chunks.back().back().WorkoutState == ERowingWorkoutState::Complete);
		EXPECT_TRUE(Sink.SummaryPayloads.size() == 1);
	}

	void workout_session_aborts_on_device_terminated_state()
	{
		FFakeSink Sink;
		std::unique_ptr<pm5_sim::FReplayRowingMachine> Machine;
		std::unique_ptr<FWorkoutSession> Owner;
		FWorkoutSession *Session = nullptr;
		RunReplay(pm5_sim::MakeDeviceTerminatedFixture(), Session, Sink, Machine, Owner);

		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		EXPECT_TRUE(Snapshot.State == ERowingSessionState::Ended);
		EXPECT_TRUE(Snapshot.Disposition == ERowingSessionDisposition::Aborted);
		EXPECT_TRUE(Snapshot.EndReason == ERowingSessionStateReason::DeviceTerminated);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::Aborted) == 1);
	}

	void workout_session_ignores_paused_and_resting_for_completion()
	{
		FFakeSink Sink;
		FHarness Harness(Sink);
		Harness.Row(1, 2);
		Harness.Step(100);
		Harness.Publish(MakeSample(3, ERowingWorkoutState::Paused));
		Harness.Step(100);
		Harness.Publish(MakeSample(4, ERowingWorkoutState::Resting));
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Active);
		EXPECT_TRUE(Harness.Session->GetSnapshot().AcceptedSampleCount == 4);

		Harness.Session->End(Harness.Now);
		EXPECT_TRUE(Harness.Session->GetSnapshot().EndReason == ERowingSessionStateReason::UserCompleted);
		EXPECT_TRUE(Sink.SampleCount() == 4);
	}

	void workout_session_flushes_chunk_on_sample_count()
	{
		FFakeSink Sink;
		FWorkoutSessionConfig Config;
		Config.FlushSampleCount = 5;
		Config.FlushIntervalMs = 60000;
		FHarness Harness(Sink, Config);

		Harness.Row(1, 4);
		EXPECT_TRUE(Sink.Chunks.empty());
		Harness.Row(5, 1);
		EXPECT_TRUE(Sink.Chunks.size() == 1);
		EXPECT_TRUE(Sink.Chunks.back().size() == 5);
		Harness.Row(6, 5);
		EXPECT_TRUE(Sink.Chunks.size() == 2);
	}

	void workout_session_flushes_chunk_on_elapsed_time()
	{
		FFakeSink Sink;
		FWorkoutSessionConfig Config;
		Config.FlushSampleCount = 1000;
		Config.FlushIntervalMs = 250;
		FHarness Harness(Sink, Config);

		// The buffer opens at the first sample (t=100 ms); at t=300 ms, when the third
		// sample is about to arrive, it is still under the interval.
		Harness.Row(1, 3);
		EXPECT_TRUE(Sink.Chunks.empty());
		// The tick at t=400 ms sees 300 ms of age with three samples held, so it flushes those three.
		Harness.Step(100);
		EXPECT_TRUE(Sink.Chunks.size() == 1);
		EXPECT_TRUE(Sink.Chunks.back().size() == 3);
	}

	void workout_session_write_failure_keeps_local_row_valid()
	{
		FFakeSink Sink;
		FWorkoutSessionConfig Config;
		Config.FlushSampleCount = 3;
		Config.FlushIntervalMs = 200;
		FHarness Harness(Sink, Config);
		Sink.bFailAppend = true;
		Harness.Row(1, 12);

		const FWorkoutSnapshot &Failing = Harness.Session->GetSnapshot();
		EXPECT_TRUE(Failing.State == ERowingSessionState::Active);
		EXPECT_TRUE(!Failing.bJournalHealthy);
		EXPECT_TRUE(Failing.JournalErrorCount > 0);
		EXPECT_TRUE(Failing.AcceptedSampleCount == 12);
		EXPECT_TRUE(Failing.LatestSample->DistanceMm == 12 * 1700);
		EXPECT_TRUE(Sink.SampleCount() == 0);

		// Once the journal recovers, everything held back is written.
		Sink.bFailAppend = false;
		Harness.Row(13, 3);
		Harness.Session->End(Harness.Now);
		EXPECT_TRUE(Sink.SampleCount() == 15);
		EXPECT_TRUE(Harness.Session->GetSnapshot().DroppedSampleCount == 0);
	}

	void workout_session_bounds_buffer_when_journal_keeps_failing()
	{
		FFakeSink Sink;
		FWorkoutSessionConfig Config;
		Config.MaxBufferedSamples = 10;
		FHarness Harness(Sink, Config);
		Sink.bFailAppend = true;
		Harness.Row(1, 25);
		Harness.Session->End(Harness.Now);

		EXPECT_TRUE(Harness.Session->GetSnapshot().DroppedSampleCount == 25);
		EXPECT_TRUE(Harness.Session->GetSnapshot().State == ERowingSessionState::Ended);
		EXPECT_TRUE(Sink.SampleCount() == 0);
	}

	void workout_session_recovers_after_kill_mid_row()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FRowingSessionId Id;
		std::uint64_t FlushedSamples = 0;
		{
			FTestCipher Cipher;
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			FLocalDataJournalSink Sink(Writer);
			FWorkoutSessionConfig Config;
			Config.FlushSampleCount = 10;
			FHarness Harness(Sink, Config);
			Harness.Row(1, 25);
			Id = Harness.Session->GetSnapshot().SessionId;
			EXPECT_TRUE(Harness.Session->GetSnapshot().bJournalHealthy);
			FlushedSamples = 20;
			// Scope exit without End() is the kill: 5 buffered samples are lost.
		}

		const LocalData::FLocalDataRecoveryReport Report = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(Report.RecoveredAfterUncleanExit);
		const auto Row = LocalData::ReadSession(Path, Id);
		EXPECT_TRUE(Row.has_value());
		EXPECT_TRUE(Row->State == ERowingSessionState::Ended);

		FTestCipher Cipher;
		const auto Chunks = LocalData::ReadSampleChunks(Path, Id.ToCanonicalString(), &Cipher);
		std::uint64_t Recovered = 0;
		for (const auto &Chunk : Chunks)
			Recovered += Chunk.Samples.size();
		EXPECT_TRUE(Recovered == FlushedSamples);
		RemoveDatabase(Path);
	}

	void workout_session_journals_through_local_data_and_reads_back()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FRowingSessionId Id;
		{
			FTestCipher Cipher;
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			FLocalDataJournalSink Sink(Writer);
			FHarness Harness(Sink);
			Harness.Row(1, 8);
			Harness.Machine->SimulateLinkLoss();
			Harness.Step(1000);
			Harness.Machine->Connect();
			Harness.Publish(MakeSample(30));
			Harness.Session->End(Harness.Now);
			Id = Harness.Session->GetSnapshot().SessionId;
			EXPECT_TRUE(Harness.Session->GetSnapshot().bJournalHealthy);
		}

		FTestCipher Cipher;
		const auto Row = LocalData::ReadSession(Path, Id);
		EXPECT_TRUE(Row.has_value());
		EXPECT_TRUE(Row->State == ERowingSessionState::Ended);
		EXPECT_TRUE(Row->Source == "just-row");
		const auto Summary = LocalData::ReadLatestSessionSummary(Path, Id, Cipher);
		EXPECT_TRUE(Summary.has_value());
		const FWorkoutSummary Decoded = WorkoutRuntime::Private::ParseSessionSummary(Summary->MetricsPayload);
		EXPECT_TRUE(Decoded.TotalDistanceMm == 30 * 1700);
		EXPECT_TRUE(Decoded.GapCount == 1);
		RemoveDatabase(Path);
	}

	void session_summary_wire_mapping_round_trips()
	{
		FWorkoutSummary Full;
		Full.TotalDistanceMm = 2'000'000;
		Full.ElapsedMs = 480'000;
		Full.AcceptedSampleCount = 4700;
		Full.RejectedSampleCount = 3;
		Full.GapCount = 2;
		Full.TotalGapMs = 5400;
		Full.AveragePaceMsPer500M = 120'000;
		Full.AveragePowerW = 190;
		Full.AverageStrokeRateDeciSpm = 245;
		Full.MaxHeartRateBpm = 171;
		Full.StrokeCount = 1900;
		Full.Calories = 410;
		const FWorkoutSummary Decoded = WorkoutRuntime::Private::ParseSessionSummary(WorkoutRuntime::Private::SerializeSessionSummary(Full));
		EXPECT_TRUE(Decoded.TotalDistanceMm == Full.TotalDistanceMm);
		EXPECT_TRUE(Decoded.ElapsedMs == Full.ElapsedMs);
		EXPECT_TRUE(Decoded.AcceptedSampleCount == Full.AcceptedSampleCount);
		EXPECT_TRUE(Decoded.RejectedSampleCount == Full.RejectedSampleCount);
		EXPECT_TRUE(Decoded.GapCount == Full.GapCount);
		EXPECT_TRUE(Decoded.TotalGapMs == Full.TotalGapMs);
		EXPECT_TRUE(Decoded.AveragePaceMsPer500M == Full.AveragePaceMsPer500M);
		EXPECT_TRUE(Decoded.AveragePowerW == Full.AveragePowerW);
		EXPECT_TRUE(Decoded.AverageStrokeRateDeciSpm == Full.AverageStrokeRateDeciSpm);
		EXPECT_TRUE(Decoded.MaxHeartRateBpm == Full.MaxHeartRateBpm);
		EXPECT_TRUE(Decoded.StrokeCount == Full.StrokeCount);
		EXPECT_TRUE(Decoded.Calories == Full.Calories);

		// A metric the device never reported stays absent rather than becoming zero.
		const FWorkoutSummary Sparse = WorkoutRuntime::Private::ParseSessionSummary(WorkoutRuntime::Private::SerializeSessionSummary(FWorkoutSummary{}));
		EXPECT_TRUE(!Sparse.AveragePowerW.has_value());
		EXPECT_TRUE(!Sparse.MaxHeartRateBpm.has_value());
		EXPECT_TRUE(!Sparse.StrokeCount.has_value());
	}

	void link_gap_wire_mapping_round_trips_and_rejects_unknown_version()
	{
		const WorkoutRuntime::Private::FLinkGap Decoded = WorkoutRuntime::Private::ParseLinkGap(WorkoutRuntime::Private::SerializeLinkGap({1234, true}));
		EXPECT_TRUE(Decoded.GapDurationMs == 1234);
		EXPECT_TRUE(Decoded.bDeviceReported);

		// contract_version = 2 on an otherwise valid message.
		std::string Bytes = WorkoutRuntime::Private::SerializeLinkGap({1, false});
		Bytes[1] = 2;
		bool bThrew = false;
		try
		{
			WorkoutRuntime::Private::ParseLinkGap(Bytes);
		}
		catch (const WorkoutRuntime::Private::FSessionWireMappingError &)
		{
			bThrew = true;
		}
		EXPECT_TRUE(bThrew);
	}

	// pm5-tui drains the machine itself and forwards each event; the session must
	// behave the same and must not need to poll the machine.
	void workout_session_ingests_forwarded_events_without_polling_the_machine()
	{
		FFakeSink Sink;
		pm5_sim::FMockRowingMachine Machine(pm5_sim::MakeSyntheticIndoorRowerScenario());
		Machine.Connect();
		FWorkoutSession Session(MakeDependencies(Machine, Sink));

		std::uint64_t Now = 0;
		const auto Forward = [&]
		{
			FRowingMachineEvent Event;
			while (Machine.TryPollEvent(Event))
				Session.Ingest(Event);
			Session.Tick(Now);
		};
		Forward();
		for (std::uint64_t Index = 1; Index <= 5; ++Index)
		{
			Now += 100 * NsPerMs;
			Machine.AdvanceTo(Now);
			Machine.PublishTelemetry(MakeSample(Index));
			Forward();
		}

		EXPECT_TRUE(Session.GetSnapshot().State == ERowingSessionState::Active);
		EXPECT_TRUE(Session.GetSnapshot().AcceptedSampleCount == 5);
		EXPECT_TRUE(Session.End(Now));
		EXPECT_TRUE(Sink.SampleCount() == 5);
		EXPECT_TRUE(Sink.EventCount(LocalData::EJournalEventKind::CapabilityObserved) == 1);

		// Once Ended, forwarded events are ignored.
		Session.Ingest(FRowingMachineEvent{1, Now, MakeSample(9)});
		EXPECT_TRUE(Session.GetSnapshot().AcceptedSampleCount == 5);
	}

	void workout_session_requires_complete_dependencies()
	{
		FFakeSink Sink;
		FWorkoutSessionDependencies Deps;
		Deps.Sink = &Sink;
		bool bThrew = false;
		try
		{
			FWorkoutSession Session(Deps);
		}
		catch (const std::invalid_argument &)
		{
			bThrew = true;
		}
		EXPECT_TRUE(bThrew);
	}
} // namespace

int main()
{
	workout_session_journals_samples_from_simulator();
	workout_session_activates_on_first_valid_sample();
	workout_session_ends_unstarted_session_without_persisting();
	workout_session_rejects_flagged_samples_from_journal_and_snapshot();
	workout_session_freezes_distance_on_link_loss();
	workout_snapshot_marks_stale_during_gap();
	workout_session_resumes_without_synthesized_meters_after_reconnect();
	workout_session_resumes_after_stale_link_without_disconnect();
	workout_session_interrupts_after_reconnect_window();
	workout_session_completes_on_device_complete_state();
	workout_session_aborts_on_device_terminated_state();
	workout_session_ignores_paused_and_resting_for_completion();
	workout_session_flushes_chunk_on_sample_count();
	workout_session_flushes_chunk_on_elapsed_time();
	workout_session_write_failure_keeps_local_row_valid();
	workout_session_bounds_buffer_when_journal_keeps_failing();
	workout_session_recovers_after_kill_mid_row();
	workout_session_journals_through_local_data_and_reads_back();
	session_summary_wire_mapping_round_trips();
	link_gap_wire_mapping_round_trips_and_rejects_unknown_version();
	workout_session_ingests_forwarded_events_without_polling_the_machine();
	workout_session_requires_complete_dependencies();

	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
