#include "WorkoutRuntime/RealDeviceController.h"

#include "RowingSim/MockRowingMachine.h"
#include "RowingSim/TelemetryFixtures.h"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
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

	std::unique_ptr<RowingSim::FMockRowingMachine> MakeConnectedMachine()
	{
		auto Machine = std::make_unique<RowingSim::FMockRowingMachine>(RowingSim::MakeSyntheticIndoorRowerScenario());
		Machine->Connect();
		return Machine;
	}

	// Scripted discovery that hands out mock machines and remembers which one is live.
	class FScriptedDiscovery final : public IRememberingMachineDiscovery
	{
	  public:
		FRowingCommandResult StartScan() override
		{
			++StartScans;
			return {};
		}
		FRowingCommandResult StopScan() override
		{
			return {};
		}
		bool TryPollDiscoveryEvent(FRowingMachineEvent &Out) override
		{
			if (Events.empty())
				return false;
			Out = std::move(Events.front());
			Events.pop_front();
			return true;
		}
		std::unique_ptr<IRowingMachine> CreateMachine(const FRowingMachineId &Id) override
		{
			LastCreatedId = Id;
			auto Machine = MakeConnectedMachine();
			Live = Machine.get();
			return Machine;
		}
		std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() override
		{
			if (!Relaunch)
				return nullptr;
			Live = Relaunch.get();
			return std::move(Relaunch);
		}
		void ForgetRememberedMachine() override
		{
			++Forgets;
		}
		void PushCandidate(const std::string &Key, std::int32_t Dbm)
		{
			FRowingMachineDescriptor Descriptor;
			Descriptor.Id = FRowingMachineId::FromPrivateAdapterValue(Key);
			Descriptor.SignalStrengthDbm = Dbm;
			FRowingMachineEvent Event;
			Event.Payload = Descriptor;
			Events.push_back(std::move(Event));
		}

		std::deque<FRowingMachineEvent> Events;
		std::unique_ptr<RowingSim::FMockRowingMachine> Relaunch;
		RowingSim::FMockRowingMachine *Live = nullptr;
		FRowingMachineId LastCreatedId;
		int StartScans = 0;
		int Forgets = 0;
	};

	struct FTempDirectory
	{
		FTempDirectory()
		{
			std::random_device Random;
			Path = std::filesystem::temp_directory_path() / ("real_device_controller_tests_" + std::to_string(Random()));
		}
		~FTempDirectory()
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Path, Ignored);
		}
		std::filesystem::path Path;
	};

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

	struct FHarness
	{
		FHarness()
		{
			Rebuild();
		}

		// A new controller over the same app data directory, as a relaunch would make.
		void Rebuild()
		{
			Discovery = nullptr;
			Controller.reset();
			FRealDeviceDependencies Deps;
			Deps.MakeDiscovery = [this]() -> std::unique_ptr<IRememberingMachineDiscovery>
			{
				++DiscoveriesCreated;
				auto Made = std::make_unique<FScriptedDiscovery>();
				Discovery = Made.get();
				if (bRelaunchAvailable)
				{
					Made->Relaunch = MakeConnectedMachine();
					bRelaunchAvailable = false;
				}
				return Made;
			};
			Deps.MakeCipher = [this]() -> std::unique_ptr<LocalData::IBlobCipher>
			{
				++CipherLoads;
				if (bCipherFails)
					throw LocalData::FBlobCipherError("keychain denied");
				return std::make_unique<FTestCipher>();
			};
			Deps.AppDataDirectory = Temp.Path;
			Deps.Clock = [this]
			{ return Now; };
			Deps.UnixTimeMs = []
			{ return std::uint64_t{1758067200000ULL}; };
			// Shared by every session this controller makes, so ids differ.
			Deps.RandomByte = [this]
			{ return NextRandomByte++; };
			Deps.SourceRevision = "test";
			Controller = std::make_unique<FRealDeviceController>(std::move(Deps));
		}

		void Step(std::uint64_t Ms)
		{
			Now += Ms * NsPerMs;
			if (Discovery && Discovery->Live)
				Discovery->Live->AdvanceTo(Now);
			Controller->Pump();
		}

		// Connects through the adapter's remembered-machine path and pumps once.
		void ConnectRemembered()
		{
			bRelaunchAvailable = true;
			EXPECT_TRUE(Controller->Connect());
			Step(100);
		}

		void Row(std::uint64_t First, std::uint64_t Count)
		{
			for (std::uint64_t Index = First; Index < First + Count; ++Index)
			{
				Step(100);
				Discovery->Live->PublishTelemetry(MakeSample(Index));
				Controller->Pump();
			}
		}

		FTempDirectory Temp;
		std::uint64_t Now = 1'000'000'000ULL;
		int DiscoveriesCreated = 0;
		int CipherLoads = 0;
		bool bCipherFails = false;
		bool bRelaunchAvailable = false;
		std::uint8_t NextRandomByte = 3;
		FScriptedDiscovery *Discovery = nullptr;
		std::unique_ptr<FRealDeviceController> Controller;
	};

	void controller_touches_no_bluetooth_or_keychain_until_connect()
	{
		FHarness H;
		H.Step(10'000);
		EXPECT_TRUE(H.DiscoveriesCreated == 0);
		EXPECT_TRUE(H.CipherLoads == 0);
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Idle);
		EXPECT_TRUE(H.Controller->GetSnapshot() == nullptr);
		EXPECT_TRUE(H.Controller->GetJournalStatus() == EAppJournalStatus::NotStarted);
		EXPECT_TRUE(!std::filesystem::exists(H.Temp.Path));
	}

	void controller_refuses_an_unavailable_transport_before_touching_the_keychain_or_bluetooth()
	{
		FHarness H;
		H.Controller.reset();
		FRealDeviceDependencies Deps;
		Deps.MakeDiscovery = [&]() -> std::unique_ptr<IRememberingMachineDiscovery>
		{
			++H.DiscoveriesCreated;
			return std::make_unique<FScriptedDiscovery>();
		};
		Deps.MakeCipher = [&]() -> std::unique_ptr<LocalData::IBlobCipher>
		{
			++H.CipherLoads;
			return std::make_unique<FTestCipher>();
		};
		Deps.AppDataDirectory = H.Temp.Path;
		Deps.Clock = [&]
		{ return H.Now; };
		Deps.bTransportAvailable = false;
		FRealDeviceController Controller(std::move(Deps));
		EXPECT_TRUE(!Controller.Connect());
		EXPECT_TRUE(Controller.GetMode() == ERealDeviceMode::Problem);
		EXPECT_TRUE(Controller.GetProblem() == EDeviceProblem::TransportUnavailable);
		EXPECT_TRUE(!Controller.ScanForDevices());
		EXPECT_TRUE(H.CipherLoads == 0);
		EXPECT_TRUE(H.DiscoveriesCreated == 0);
		EXPECT_TRUE(!std::filesystem::exists(H.Temp.Path));
		Controller.CancelConnect();
		EXPECT_TRUE(Controller.GetMode() == ERealDeviceMode::Idle);
	}

	void controller_opens_the_journal_before_it_creates_the_discovery()
	{
		FHarness H;
		int CipherLoadsAtDiscovery = -1;
		FRealDeviceDependencies Deps;
		H.Controller.reset();
		Deps.MakeDiscovery = [&]() -> std::unique_ptr<IRememberingMachineDiscovery>
		{
			CipherLoadsAtDiscovery = H.CipherLoads;
			return std::make_unique<FScriptedDiscovery>();
		};
		Deps.MakeCipher = [&]() -> std::unique_ptr<LocalData::IBlobCipher>
		{
			++H.CipherLoads;
			return std::make_unique<FTestCipher>();
		};
		Deps.AppDataDirectory = H.Temp.Path;
		Deps.Clock = [&]
		{ return H.Now; };
		Deps.UnixTimeMs = []
		{ return std::uint64_t{1758067200000ULL}; };
		Deps.RandomByte = []
		{ return std::uint8_t{1}; };
		FRealDeviceController Controller(std::move(Deps));
		EXPECT_TRUE(Controller.Connect());
		EXPECT_TRUE(H.CipherLoads == 0);
		EXPECT_TRUE(CipherLoadsAtDiscovery == 0);
		EXPECT_TRUE(Controller.GetJournalStatus() == EAppJournalStatus::Saving);
		EXPECT_TRUE(Controller.GetMode() == ERealDeviceMode::Starting);
	}

	void controller_requires_explicit_confirmation_to_row_without_saving()
	{
		FHarness H;
		H.bCipherFails = true;
		EXPECT_TRUE(H.Controller->Connect());
		EXPECT_TRUE(H.Controller->GetJournalStatus() == EAppJournalStatus::Saving);
		EXPECT_TRUE(H.CipherLoads == 0);
		EXPECT_TRUE(H.DiscoveriesCreated == 1);
	}

	void controller_journal_decision_can_be_retried_or_cancelled()
	{
		FHarness H;
		H.bCipherFails = true;
		EXPECT_TRUE(H.Controller->Connect());
		H.Controller->CancelConnect();
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Idle);
		EXPECT_TRUE(!H.Controller->ConfirmRowWithoutSaving());
		H.bCipherFails = false;
		EXPECT_TRUE(H.Controller->Connect());
		EXPECT_TRUE(H.CipherLoads == 0);
		EXPECT_TRUE(H.Controller->GetJournalStatus() == EAppJournalStatus::Saving);
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Starting);
	}

	void controller_rows_over_the_remembered_machine_and_journals_within_a_second()
	{
		FHarness H;
		H.ConnectRemembered();
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Attached);
		EXPECT_TRUE(H.Controller->HasSession());
		H.Row(1, 12);
		const FWorkoutSnapshot *Snapshot = H.Controller->GetSnapshot();
		EXPECT_TRUE(Snapshot != nullptr && Snapshot->State == ERowingSessionState::Active);
		EXPECT_TRUE(Snapshot != nullptr && Snapshot->bJournalHealthy);
		std::size_t Committed = 0;
		for (const LocalData::FSampleChunk &Chunk : LocalData::ReadSampleChunks(GetAppJournalDatabasePath(H.Temp.Path / "journal"), Snapshot->SessionId.ToCanonicalString()))
			Committed += Chunk.Samples.size();
		EXPECT_TRUE(Committed > 0);
	}

	void controller_scans_lists_nearest_first_and_selects()
	{
		FHarness H;
		EXPECT_TRUE(H.Controller->Connect());
		// No remembered machine: the grace period passes and the connector scans.
		H.Step(3500);
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Scanning);
		H.Discovery->PushCandidate("far", -80);
		H.Discovery->PushCandidate("near", -50);
		H.Step(100);
		const std::vector<std::string> Labels = H.Controller->GetCandidateLabels();
		EXPECT_TRUE(Labels.size() == 2);
		EXPECT_TRUE(Labels.size() == 2 && Labels[0] == "PM5 #1 (-50 dBm)" && Labels[1] == "PM5 #2 (-80 dBm)");
		EXPECT_TRUE(H.Controller->SelectDevice(0));
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Attached);
		EXPECT_TRUE(H.Controller->HasSession());
		EXPECT_TRUE(H.Controller->GetCandidateLabels().empty());
	}

	void controller_link_loss_freezes_the_session_and_keeps_draining()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		EXPECT_TRUE(H.Discovery->Live->SimulateLinkLoss());
		H.Step(100);
		const FWorkoutSnapshot *Snapshot = H.Controller->GetSnapshot();
		EXPECT_TRUE(Snapshot != nullptr && Snapshot->State == ERowingSessionState::ConnectionLost);
		EXPECT_TRUE(Snapshot != nullptr && Snapshot->bInputFrozen);
	}

	void controller_end_then_start_new_uses_a_fresh_session_id()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		EXPECT_TRUE(!H.Controller->StartNewSession());
		const std::string FirstId = H.Controller->GetSnapshot()->SessionId.ToCanonicalString();
		EXPECT_TRUE(H.Controller->EndSession());
		EXPECT_TRUE(H.Controller->GetSnapshot()->State == ERowingSessionState::Ended);
		EXPECT_TRUE(H.Controller->StartNewSession());
		EXPECT_TRUE(H.Controller->GetSnapshot()->SessionId.ToCanonicalString() != FirstId);
		EXPECT_TRUE(H.Controller->GetSnapshot()->State == ERowingSessionState::Created);
	}

	FRowingMetricSample MakeStoppedSample(std::uint64_t Index)
	{
		FRowingMetricSample Sample = MakeSample(Index);
		Sample.WorkoutState = ERowingWorkoutState::WaitingToBegin;
		Sample.RowingState = ERowingState::Inactive;
		Sample.StrokeState = ERowingStrokeState::Waiting;
		return Sample;
	}

	void controller_end_mid_row_holds_the_next_session_until_the_row_stops()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		const std::string FirstId = H.Controller->GetSnapshot()->SessionId.ToCanonicalString();
		EXPECT_TRUE(H.Controller->EndSession());
		EXPECT_TRUE(H.Controller->IsAwaitingRowStop());
		EXPECT_TRUE(H.Controller->StartNewSession());
		// The PM5 keeps reporting the ended row: none of it may reach the new session.
		H.Row(6, 8);
		EXPECT_TRUE(H.Controller->IsAwaitingRowStop());
		EXPECT_TRUE(H.Controller->GetSnapshot()->SessionId.ToCanonicalString() != FirstId);
		EXPECT_TRUE(H.Controller->GetSnapshot()->AcceptedSampleCount == 0);
		// Once the row has stopped, the next row is a new workout.
		H.Step(100);
		H.Discovery->Live->PublishTelemetry(MakeStoppedSample(14));
		H.Controller->Pump();
		EXPECT_TRUE(!H.Controller->IsAwaitingRowStop());
		H.Row(20, 5);
		EXPECT_TRUE(H.Controller->GetSnapshot()->AcceptedSampleCount > 0);
	}

	void controller_a_device_ended_row_does_not_hold_the_next_session()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		H.Step(100);
		H.Discovery->Live->PublishTelemetry(MakeStoppedSample(6));
		H.Controller->Pump();
		// The PM5 reporting the workout stopped ends the session by itself.
		EXPECT_TRUE(H.Controller->GetSnapshot()->State == ERowingSessionState::Ended);
		EXPECT_TRUE(!H.Controller->IsAwaitingRowStop());
		EXPECT_TRUE(H.Controller->StartNewSession());
		H.Row(20, 5);
		EXPECT_TRUE(H.Controller->GetSnapshot()->AcceptedSampleCount > 0);
	}

	void controller_selects_the_candidate_the_user_saw_when_the_list_re_sorts()
	{
		FHarness H;
		EXPECT_TRUE(H.Controller->Connect());
		H.Step(3500);
		H.Discovery->PushCandidate("mid", -60);
		H.Discovery->PushCandidate("far", -80);
		H.Step(100);
		const std::vector<std::uint64_t> Seen = H.Controller->GetCandidateTokens();
		EXPECT_TRUE(Seen.size() == 2);
		// The user is looking at "PM5 #1" (mid). A nearer PM5 appears and takes position 0.
		H.Discovery->PushCandidate("closer", -40);
		H.Step(100);
		EXPECT_TRUE(H.Controller->GetCandidateTokens().size() == 3);
		EXPECT_TRUE(H.Controller->GetCandidateTokens()[0] != Seen[0]);
		EXPECT_TRUE(H.Controller->SelectDeviceByToken(Seen[0]));
		EXPECT_TRUE(H.Discovery->LastCreatedId == FRowingMachineId::FromPrivateAdapterValue("mid"));
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Attached);
	}

	void controller_ignores_a_token_that_left_the_list()
	{
		FHarness H;
		EXPECT_TRUE(H.Controller->Connect());
		H.Step(3500);
		H.Discovery->PushCandidate("mid", -60);
		H.Step(100);
		EXPECT_TRUE(!H.Controller->SelectDeviceByToken(9999));
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Scanning);
	}

	void controller_shows_a_problem_when_the_discovery_cannot_be_created()
	{
		FHarness H;
		H.Controller.reset();
		FRealDeviceDependencies Deps;
		Deps.MakeDiscovery = []() -> std::unique_ptr<IRememberingMachineDiscovery>
		{ return nullptr; };
		Deps.MakeCipher = []() -> std::unique_ptr<LocalData::IBlobCipher>
		{ return std::make_unique<FTestCipher>(); };
		Deps.AppDataDirectory = H.Temp.Path;
		Deps.Clock = [&]
		{ return H.Now; };
		FRealDeviceController Controller(std::move(Deps));
		EXPECT_TRUE(!Controller.Connect());
		EXPECT_TRUE(Controller.GetMode() == ERealDeviceMode::Problem);
	}

	void controller_scanning_again_ends_the_session_and_drops_the_machine()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 15);
		EXPECT_TRUE(H.Controller->ScanForDevices());
		EXPECT_TRUE(!H.Controller->HasSession());
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Scanning);
		H.Discovery->Live = nullptr;
		EXPECT_TRUE(H.Discovery->StartScans == 1);
		// The row was ended cleanly, so the next launch has nothing to recover.
		H.Rebuild();
		EXPECT_TRUE(!H.Controller->WasInterruptedSessionRecovered());
	}

	void controller_forget_ends_the_session_and_offers_a_scan()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		H.Controller->ForgetDevice();
		EXPECT_TRUE(H.Discovery->Forgets == 1);
		EXPECT_TRUE(!H.Controller->HasSession());
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Scanning);
	}

	void controller_cancel_ends_the_session_and_returns_to_idle()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 5);
		H.Controller->CancelConnect();
		EXPECT_TRUE(H.Controller->GetMode() == ERealDeviceMode::Idle);
		EXPECT_TRUE(!H.Controller->HasSession());
		// A later Connect starts fresh and reuses the open journal.
		EXPECT_TRUE(H.Controller->Connect());
		EXPECT_TRUE(H.CipherLoads == 0);
	}

	void controller_reports_an_interrupted_session_once()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 15);
		// A killed process never runs its destructor: leak this controller on purpose.
		(void)H.Controller.release();
		H.Discovery = nullptr;
		H.Rebuild();
		EXPECT_TRUE(H.Controller->WasInterruptedSessionRecovered());
		H.Rebuild();
		EXPECT_TRUE(!H.Controller->WasInterruptedSessionRecovered());
	}

	void controller_quit_ends_the_row_cleanly()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 15);
		H.Discovery->Live = nullptr;
		H.Controller->ShutdownGracefully();
		H.Rebuild();
		EXPECT_TRUE(!H.Controller->WasInterruptedSessionRecovered());
	}

	void controller_destruction_does_not_finalize_an_active_row()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Row(1, 15);
		H.Discovery->Live = nullptr;
		H.Controller.reset();
		H.Rebuild();
		EXPECT_TRUE(H.Controller->WasInterruptedSessionRecovered());
	}

	void controller_records_display_latency_and_writes_an_aggregate()
	{
		FHarness H;
		H.ConnectRemembered();
		H.Step(100);
		H.Discovery->Live->PublishTelemetry(MakeSample(1));
		H.Now += 30 * NsPerMs;
		H.Controller->Pump();
		H.Controller->NoteDisplayApplied(H.Now);
		const FRowingSessionId SessionId = H.Controller->GetSnapshot()->SessionId;
		EXPECT_TRUE(H.Controller->GetLatency().GetCount() == 1);
		EXPECT_TRUE(H.Controller->GetLatency().GetMaxNs() == 30 * NsPerMs);
		// Without a new sample there is nothing to measure.
		H.Controller->NoteDisplayApplied(H.Now + 5 * NsPerMs);
		EXPECT_TRUE(H.Controller->GetLatency().GetCount() == 1);
		// A sample that does not change the display is discarded, not carried forward.
		H.Discovery->Live->PublishTelemetry(MakeSample(2));
		H.Controller->Pump();
		H.Controller->DiscardPendingLatency();
		H.Controller->NoteDisplayApplied(H.Now + 900 * NsPerMs);
		EXPECT_TRUE(H.Controller->GetLatency().GetCount() == 1);

		EXPECT_TRUE(H.Controller->EndSession());
		EXPECT_TRUE(H.Controller->GetLatency().GetCount() == 0);
		bool bFound = false;
		for (const auto &Entry : std::filesystem::directory_iterator(H.Temp.Path / "metrics"))
			bFound = bFound || Entry.path().filename().string().rfind("hud-latency-", 0) == 0;
		EXPECT_TRUE(bFound);
		const auto Summary = LocalData::ReadSessionLatencySummary(GetAppJournalDatabasePath(H.Temp.Path / "journal"), SessionId);
		EXPECT_TRUE(Summary.has_value());
		EXPECT_TRUE(Summary && Summary->SourceRevision == "test");
		EXPECT_TRUE(Summary && Summary->SampleCount == 1 && Summary->RetainedCount == 1 && Summary->DroppedCount == 0);
		EXPECT_TRUE(Summary && Summary->P50Ns == 30 * NsPerMs && Summary->P95Ns == 30 * NsPerMs && Summary->P99Ns == 30 * NsPerMs && Summary->MaxNs == 30 * NsPerMs);
	}
} // namespace

int main()
{
	controller_touches_no_bluetooth_or_keychain_until_connect();
	controller_refuses_an_unavailable_transport_before_touching_the_keychain_or_bluetooth();
	controller_opens_the_journal_before_it_creates_the_discovery();
	controller_requires_explicit_confirmation_to_row_without_saving();
	controller_journal_decision_can_be_retried_or_cancelled();
	controller_rows_over_the_remembered_machine_and_journals_within_a_second();
	controller_scans_lists_nearest_first_and_selects();
	controller_link_loss_freezes_the_session_and_keeps_draining();
	controller_end_then_start_new_uses_a_fresh_session_id();
	controller_end_mid_row_holds_the_next_session_until_the_row_stops();
	controller_a_device_ended_row_does_not_hold_the_next_session();
	controller_selects_the_candidate_the_user_saw_when_the_list_re_sorts();
	controller_ignores_a_token_that_left_the_list();
	controller_shows_a_problem_when_the_discovery_cannot_be_created();
	controller_scanning_again_ends_the_session_and_drops_the_machine();
	controller_forget_ends_the_session_and_offers_a_scan();
	controller_cancel_ends_the_session_and_returns_to_idle();
	controller_reports_an_interrupted_session_once();
	controller_quit_ends_the_row_cleanly();
	controller_destruction_does_not_finalize_an_active_row();
	controller_records_display_latency_and_writes_an_aggregate();
	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
