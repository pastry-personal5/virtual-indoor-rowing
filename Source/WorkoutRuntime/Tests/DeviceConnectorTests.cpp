#include "WorkoutRuntime/DeviceConnector.h"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
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

	constexpr std::uint64_t Second = 1'000'000'000ULL;

	// Records the order objects are destroyed in, to prove the machine goes first.
	struct FLog
	{
		std::vector<std::string> Lines;
		int DiscoveriesCreated = 0;
		int StartScans = 0;
		int StopScans = 0;
		int MachinesCreated = 0;
		int Forgets = 0;
	};

	class FFakeMachine final : public IRowingMachine
	{
	  public:
		explicit FFakeMachine(FLog &InLog)
			: Log(InLog)
		{
		}
		~FFakeMachine() override
		{
			Log.Lines.push_back("machine destroyed");
		}
		FRowingCommandResult Connect() override
		{
			Log.Lines.push_back("machine connect");
			return {};
		}
		FRowingCommandResult Disconnect() override
		{
			Log.Lines.push_back("machine disconnect");
			return {};
		}
		ERowingConnectionState GetConnectionState() const override
		{
			return ERowingConnectionState::Idle;
		}
		FRowingMachineDiagnostics GetDiagnostics() const override
		{
			return {};
		}
		bool TryPollEvent(FRowingMachineEvent &) override
		{
			return false;
		}

	  private:
		FLog &Log;
	};

	class FFakeDiscovery final : public IRememberingMachineDiscovery
	{
	  public:
		explicit FFakeDiscovery(FLog &InLog)
			: Log(InLog)
		{
			++Log.DiscoveriesCreated;
		}
		~FFakeDiscovery() override
		{
			Log.Lines.push_back("discovery destroyed");
		}
		FRowingCommandResult StartScan() override
		{
			++Log.StartScans;
			return {};
		}
		FRowingCommandResult StopScan() override
		{
			++Log.StopScans;
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
		std::unique_ptr<IRowingMachine> CreateMachine(const FRowingMachineId &) override
		{
			++Log.MachinesCreated;
			return std::make_unique<FFakeMachine>(Log);
		}
		std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() override
		{
			return std::move(Relaunch);
		}
		void ForgetRememberedMachine() override
		{
			++Log.Forgets;
		}

		void PushCandidate(const std::string &Key, std::optional<std::int32_t> Dbm)
		{
			FRowingMachineDescriptor Descriptor;
			Descriptor.Id = FRowingMachineId::FromPrivateAdapterValue(Key);
			Descriptor.DisplayLabel = "Concept2 PM candidate";
			Descriptor.SignalStrengthDbm = Dbm;
			FRowingMachineEvent Event;
			Event.Payload = Descriptor;
			Events.push_back(std::move(Event));
		}
		void PushFault(ERowingFaultCode Code, ERowingConnectionState State = ERowingConnectionState::Failed)
		{
			FRowingFault Fault;
			Fault.Code = Code;
			Fault.Severity = ERowingFaultSeverity::Recoverable;
			Fault.Operation = ERowingOperation::Scan;
			Fault.ConnectionState = State;
			FRowingMachineEvent Event;
			Event.Payload = Fault;
			Events.push_back(std::move(Event));
		}

		FLog &Log;
		std::deque<FRowingMachineEvent> Events;
		std::unique_ptr<IRowingMachine> Relaunch;
	};

	// The connector owns the discovery; tests keep a raw pointer to script it.
	struct FFixture
	{
		FLog Log;
		FFakeDiscovery *Discovery = nullptr;
		std::unique_ptr<FDeviceConnector> Connector = std::make_unique<FDeviceConnector>([this]() -> std::unique_ptr<IRememberingMachineDiscovery>
																						 {
			auto Made = std::make_unique<FFakeDiscovery>(Log);
			Discovery = Made.get();
			return Made; });
	};

	void connector_creates_no_discovery_until_started()
	{
		FFixture Fixture;
		Fixture.Connector->Tick(10 * Second);
		EXPECT_TRUE(Fixture.Log.DiscoveriesCreated == 0);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Idle);
		EXPECT_TRUE(Fixture.Connector->Start(0));
		EXPECT_TRUE(Fixture.Log.DiscoveriesCreated == 1);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Starting);
		EXPECT_TRUE(!Fixture.Connector->Start(0));
		EXPECT_TRUE(Fixture.Log.DiscoveriesCreated == 1);
	}

	void connector_adopts_remembered_machine_without_scanning()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->Relaunch = std::make_unique<FFakeMachine>(Fixture.Log);
		Fixture.Connector->Tick(Second);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Attached);
		EXPECT_TRUE(Fixture.Connector->GetMachine() != nullptr);
		Fixture.Connector->Tick(30 * Second);
		EXPECT_TRUE(Fixture.Log.StartScans == 0);
	}

	void connector_scans_only_after_the_remembered_grace_period()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->Tick(2 * Second);
		EXPECT_TRUE(Fixture.Log.StartScans == 0);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Starting);
		Fixture.Connector->Tick(3 * Second);
		EXPECT_TRUE(Fixture.Log.StartScans == 1);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Scanning);
		Fixture.Connector->Tick(9 * Second);
		EXPECT_TRUE(Fixture.Log.StartScans == 1);
	}

	void connector_scans_at_once_when_the_remembered_machine_is_unavailable()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->PushFault(ERowingFaultCode::ConnectionTimeout);
		Fixture.Connector->Tick(Second / 10);
		EXPECT_TRUE(Fixture.Log.StartScans == 1);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Scanning);
		EXPECT_TRUE(Fixture.Connector->GetProblem() == EDeviceProblem::None);
	}

	void connector_lists_candidates_nearest_first_with_index_and_dbm_labels()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		Fixture.Discovery->PushCandidate("far", -80);
		Fixture.Discovery->PushCandidate("unknown", std::nullopt);
		Fixture.Discovery->PushCandidate("near", -52);
		Fixture.Connector->Tick(Second);
		const auto &Candidates = Fixture.Connector->GetCandidates();
		EXPECT_TRUE(Candidates.size() == 3);
		EXPECT_TRUE(Candidates[0].Label == "PM5 #1 (-52 dBm)");
		EXPECT_TRUE(Candidates[1].Label == "PM5 #2 (-80 dBm)");
		EXPECT_TRUE(Candidates[2].Label == "PM5 #3");

		// A stronger reading for the same PM5 re-sorts it instead of duplicating it.
		Fixture.Discovery->PushCandidate("far", -40);
		Fixture.Connector->Tick(2 * Second);
		EXPECT_TRUE(Fixture.Connector->GetCandidates().size() == 3);
		EXPECT_TRUE(Fixture.Connector->GetCandidates()[0].Label == "PM5 #1 (-40 dBm)");
	}

	void connector_bounds_the_candidate_list()
	{
		FDeviceConnectorConfig Config;
		Config.MaxCandidates = 2;
		FLog Log;
		FFakeDiscovery *Discovery = nullptr;
		FDeviceConnector Connector([&]() -> std::unique_ptr<IRememberingMachineDiscovery>
								   {
			auto Made = std::make_unique<FFakeDiscovery>(Log);
			Discovery = Made.get();
			return Made; },
								   Config);
		Connector.Start(0);
		Connector.StartScan(0);
		Discovery->PushCandidate("a", -70);
		Discovery->PushCandidate("b", -60);
		Discovery->PushCandidate("c", -50);
		Connector.Tick(Second);
		EXPECT_TRUE(Connector.GetCandidates().size() == 2);
		EXPECT_TRUE(Connector.GetCandidates()[0].Label == "PM5 #1 (-50 dBm)");
		EXPECT_TRUE(Connector.GetCandidates()[1].Label == "PM5 #2 (-60 dBm)");
	}

	void connector_select_by_token_survives_a_re_sort()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		Fixture.Discovery->PushCandidate("mid", -60);
		Fixture.Connector->Tick(Second);
		const std::uint64_t MidToken = Fixture.Connector->GetCandidates()[0].Token;
		Fixture.Discovery->PushCandidate("near", -40);
		Fixture.Connector->Tick(Second);
		EXPECT_TRUE(Fixture.Connector->GetCandidates()[0].Token != MidToken);
		EXPECT_TRUE(!Fixture.Connector->SelectByToken(MidToken + 100));
		EXPECT_TRUE(Fixture.Connector->SelectByToken(MidToken));
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Attached);
	}

	void connector_select_stops_scan_creates_and_connects_the_machine()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		Fixture.Discovery->PushCandidate("near", -50);
		Fixture.Connector->Tick(Second);
		EXPECT_TRUE(!Fixture.Connector->Select(5));
		EXPECT_TRUE(Fixture.Connector->Select(0));
		EXPECT_TRUE(Fixture.Log.StopScans == 1);
		EXPECT_TRUE(Fixture.Log.MachinesCreated == 1);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Attached);
		EXPECT_TRUE(Fixture.Connector->GetMachine() != nullptr);
		EXPECT_TRUE(Fixture.Log.Lines.back() == "machine connect");
		EXPECT_TRUE(Fixture.Connector->GetCandidates().empty());
	}

	void connector_reports_bluetooth_permission_and_power_problems_without_scanning()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->PushFault(ERowingFaultCode::Permission, ERowingConnectionState::PermissionDenied);
		Fixture.Connector->Tick(Second / 10);
		EXPECT_TRUE(Fixture.Connector->GetProblem() == EDeviceProblem::BluetoothPermission);
		// A standing problem is not overridden by the grace-period scan.
		Fixture.Connector->Tick(20 * Second);
		EXPECT_TRUE(Fixture.Log.StartScans == 0);

		FFixture Off;
		Off.Connector->Start(0);
		Off.Discovery->PushFault(ERowingFaultCode::Permission, ERowingConnectionState::Failed);
		Off.Connector->Tick(Second / 10);
		EXPECT_TRUE(Off.Connector->GetProblem() == EDeviceProblem::BluetoothNotReady);
		// Retry scans again and clears the problem.
		EXPECT_TRUE(Off.Connector->StartScan(Second));
		EXPECT_TRUE(Off.Connector->GetProblem() == EDeviceProblem::None);
		EXPECT_TRUE(Off.Log.StartScans == 1);
	}

	void connector_reports_no_device_found_after_an_empty_scan()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		Fixture.Discovery->PushFault(ERowingFaultCode::ScanTimeout, ERowingConnectionState::Idle);
		Fixture.Connector->Tick(Second);
		EXPECT_TRUE(Fixture.Connector->GetProblem() == EDeviceProblem::NoDeviceFound);
		Fixture.Discovery->PushCandidate("late", -60);
		Fixture.Connector->Tick(2 * Second);
		EXPECT_TRUE(Fixture.Connector->GetProblem() == EDeviceProblem::None);
	}

	void connector_scan_from_attached_disconnects_the_machine_first()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->Relaunch = std::make_unique<FFakeMachine>(Fixture.Log);
		Fixture.Connector->Tick(Second);
		const std::uint64_t Generation = Fixture.Connector->GetMachineGeneration();
		EXPECT_TRUE(Fixture.Connector->StartScan(2 * Second));
		EXPECT_TRUE(Fixture.Connector->GetMachine() == nullptr);
		EXPECT_TRUE(Fixture.Connector->GetMachineGeneration() != Generation);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Scanning);
		EXPECT_TRUE(Fixture.Log.Lines[0] == "machine disconnect");
		EXPECT_TRUE(Fixture.Log.Lines[1] == "machine destroyed");
	}

	void connector_forget_disconnects_forgets_and_offers_a_scan()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->Relaunch = std::make_unique<FFakeMachine>(Fixture.Log);
		Fixture.Connector->Tick(Second);
		Fixture.Connector->Forget();
		EXPECT_TRUE(Fixture.Log.Forgets == 1);
		EXPECT_TRUE(Fixture.Connector->GetMachine() == nullptr);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Scanning);
		EXPECT_TRUE(Fixture.Log.StartScans == 1);
	}

	void connector_destroys_the_machine_before_the_discovery()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Discovery->Relaunch = std::make_unique<FFakeMachine>(Fixture.Log);
		Fixture.Connector->Tick(Second);
		Fixture.Connector.reset();
		EXPECT_TRUE(Fixture.Log.Lines.size() == 3);
		EXPECT_TRUE(Fixture.Log.Lines[0] == "machine disconnect");
		EXPECT_TRUE(Fixture.Log.Lines[1] == "machine destroyed");
		EXPECT_TRUE(Fixture.Log.Lines[2] == "discovery destroyed");
	}

	void connector_stops_asking_for_a_remembered_machine_once_scanning()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		// A remembered machine that shows up after scanning began is not adopted.
		Fixture.Discovery->Relaunch = std::make_unique<FFakeMachine>(Fixture.Log);
		Fixture.Connector->Tick(Second);
		EXPECT_TRUE(Fixture.Connector->GetMachine() == nullptr);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Scanning);
	}

	void connector_ignores_late_discovery_events_once_attached()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->StartScan(0);
		Fixture.Discovery->PushCandidate("near", -50);
		Fixture.Connector->Tick(Second);
		Fixture.Connector->Select(0);
		Fixture.Discovery->PushFault(ERowingFaultCode::ScanTimeout, ERowingConnectionState::Idle);
		Fixture.Connector->Tick(2 * Second);
		EXPECT_TRUE(Fixture.Connector->GetProblem() == EDeviceProblem::None);
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Attached);
	}

	void connector_stop_returns_to_idle_and_allows_a_fresh_start()
	{
		FFixture Fixture;
		Fixture.Connector->Start(0);
		Fixture.Connector->Stop();
		EXPECT_TRUE(Fixture.Connector->GetPhase() == EDeviceConnectPhase::Idle);
		EXPECT_TRUE(Fixture.Connector->Start(5 * Second));
		EXPECT_TRUE(Fixture.Log.DiscoveriesCreated == 2);
	}
} // namespace

int main()
{
	connector_creates_no_discovery_until_started();
	connector_adopts_remembered_machine_without_scanning();
	connector_scans_only_after_the_remembered_grace_period();
	connector_scans_at_once_when_the_remembered_machine_is_unavailable();
	connector_lists_candidates_nearest_first_with_index_and_dbm_labels();
	connector_bounds_the_candidate_list();
	connector_select_by_token_survives_a_re_sort();
	connector_select_stops_scan_creates_and_connects_the_machine();
	connector_reports_bluetooth_permission_and_power_problems_without_scanning();
	connector_reports_no_device_found_after_an_empty_scan();
	connector_scan_from_attached_disconnects_the_machine_first();
	connector_forget_disconnects_forgets_and_offers_a_scan();
	connector_destroys_the_machine_before_the_discovery();
	connector_stops_asking_for_a_remembered_machine_once_scanning();
	connector_ignores_late_discovery_events_once_attached();
	connector_stop_returns_to_idle_and_allows_a_fresh_start();
	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
