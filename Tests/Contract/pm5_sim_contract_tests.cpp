#include "pm5_sim/MockRowingMachine.h"
#include "pm5_sim/TelemetryFixtures.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	int Failures = 0;

	void Check(bool Condition, const std::string &Message)
	{
		if (!Condition)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}

	template <typename TMachine>
	std::vector<FRowingMachineEvent> Drain(TMachine &Machine)
	{
		std::vector<FRowingMachineEvent> Events;
		FRowingMachineEvent Event;
		while (Machine.TryPollEvent(Event))
		{
			Events.push_back(std::move(Event));
		}
		return Events;
	}

	std::vector<FRowingMachineEvent>
	Drain(pm5_sim::FMockRowingMachineDiscovery &Discovery)
	{
		std::vector<FRowingMachineEvent> Events;
		FRowingMachineEvent Event;
		while (Discovery.TryPollDiscoveryEvent(Event))
		{
			Events.push_back(std::move(Event));
		}
		return Events;
	}

	template <typename TMachine>
	void AppendDrained(TMachine &Machine,
					   std::vector<FRowingMachineEvent> &Stream)
	{
		const auto Events = Drain(Machine);
		Stream.insert(Stream.end(), Events.begin(), Events.end());
	}

	void check_event_stream_ordering_and_timestamps(
		const std::vector<FRowingMachineEvent> &Events,
		const std::string &StreamName)
	{
		for (std::size_t Index = 1; Index < Events.size(); ++Index)
		{
			Check(Events[Index].AdapterEventSequence >
					  Events[Index - 1].AdapterEventSequence,
				  StreamName + ": event sequence increases strictly");
			Check(Events[Index].MonotonicTimestampNs >=
					  Events[Index - 1].MonotonicTimestampNs,
				  StreamName + ": event timestamps are non-decreasing");
		}

		for (const FRowingMachineEvent &Event : Events)
		{
			if (const auto *Sample =
					std::get_if<FRowingMetricSample>(&Event.Payload))
			{
				Check(Event.MonotonicTimestampNs >=
						  Sample->ReceivedMonotonicNs,
					  StreamName +
						  ": metric event timestamp is not earlier than receive time");
			}
			else if (const auto *Diagnostic =
						 std::get_if<FRowingDiagnosticSample>(&Event.Payload))
			{
				Check(Event.GetKind() ==
						  ERowingMachineEventKind::DiagnosticSampleObserved,
					  StreamName +
						  ": diagnostic sample retains its distinct event kind");
				Check(Event.MonotonicTimestampNs >=
						  Diagnostic->Sample.ReceivedMonotonicNs,
					  StreamName +
						  ": diagnostic event timestamp is not earlier than receive time");
			}
		}
	}

	bool HasFault(const std::vector<FRowingMachineEvent> &Events,
				  ERowingFaultCode Code)
	{
		for (const FRowingMachineEvent &Event : Events)
		{
			if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload);
				Fault && Fault->Code == Code)
			{
				return true;
			}
		}
		return false;
	}

	const FRowingFault *FindFault(const std::vector<FRowingMachineEvent> &Events,
								  ERowingFaultCode Code)
	{
		for (const FRowingMachineEvent &Event : Events)
		{
			if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload);
				Fault && Fault->Code == Code)
			{
				return Fault;
			}
		}
		return nullptr;
	}

	std::size_t CountKind(const std::vector<FRowingMachineEvent> &Events,
						  ERowingMachineEventKind Kind)
	{
		std::size_t Count = 0;
		for (const FRowingMachineEvent &Event : Events)
		{
			Count += Event.GetKind() == Kind ? 1 : 0;
		}
		return Count;
	}

	std::optional<ERowingConnectionState>
	LastChangedState(const std::vector<FRowingMachineEvent> &Events)
	{
		std::optional<ERowingConnectionState> State;
		for (const FRowingMachineEvent &Event : Events)
		{
			if (const auto *Changed =
					std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
			{
				State = Changed->NewState;
			}
		}
		return State;
	}

	FRowingMetricSample Sample(std::uint64_t ElapsedMs,
							   std::uint64_t DistanceMm)
	{
		FRowingMetricSample Value;
		Value.SourceElapsedMs = ElapsedMs;
		Value.DistanceMm = DistanceMm;
		Value.WorkoutState = ERowingWorkoutState::Active;
		Value.RowingState = ERowingState::Active;
		Value.StrokeState = ERowingStrokeState::Recovery;
		return Value;
	}

	void permission_denial_discovery_and_explicit_selection()
	{
		auto Selected = pm5_sim::MakeSyntheticIndoorRowerScenario();
		auto Nearby = Selected;
		Nearby.Descriptor.Id = FRowingMachineId::FromPrivateAdapterValue(
			"synthetic:indoor-rower-02");
		Nearby.Descriptor.DisplayLabel = "Synthetic indoor rower B";
		Nearby.Descriptor.SignalStrengthDbm = -80;
		pm5_sim::FMockDiscoveryScenario Scenario;
		Scenario.Permission = pm5_sim::EMockPermission::Denied;
		Scenario.Machines = {Selected, Nearby};
		pm5_sim::FMockRowingMachineDiscovery Discovery(std::move(Scenario));

		Check(Discovery.StartScan().IsAccepted(),
			  "denied scan request is accepted for asynchronous reporting");
		const auto DeniedEvents = Drain(Discovery);
		Check(HasFault(DeniedEvents, ERowingFaultCode::Permission),
			  "permission denial is observable as a fault");
		Check(Discovery.CreateMachine(Selected.Descriptor.Id) == nullptr,
			  "machine cannot be created before discovery");

		Discovery.SetPermission(pm5_sim::EMockPermission::Granted);
		Check(Discovery.StartScan().IsAccepted(),
			  "scan can restart after permission repair");
		const auto FoundEvents = Drain(Discovery);
		Check(CountKind(FoundEvents,
						ERowingMachineEventKind::MachineDiscovered) == 2,
			  "scan reports both synthetic candidates");
		auto Machine = Discovery.CreateMachine(Nearby.Descriptor.Id);
		Check(Machine != nullptr,
			  "explicitly selected discovered candidate is constructible");
		if (Machine)
		{
			Check(Machine->Connect().IsAccepted(),
				  "selected candidate connects");
			Check(Machine->GetConnectionState() ==
					  ERowingConnectionState::Ready,
				  "required identity and status reach ready");
		}
	}

	void unsupported_identity_fails_closed()
	{
		auto Scenario = pm5_sim::MakeSyntheticIndoorRowerScenario();
		Scenario.Info.MachineKind = ERowingMachineKind::SkiErg;
		pm5_sim::FMockRowingMachine Machine(std::move(Scenario));
		Check(Machine.Connect().IsAccepted(),
			  "unsupported connection outcome is reported asynchronously");
		const auto Events = Drain(Machine);
		Check(Machine.GetConnectionState() ==
				  ERowingConnectionState::Unsupported,
			  "non-rower identity never reaches ready");
		Check(HasFault(Events, ERowingFaultCode::WrongMachineType),
			  "wrong kind has a stable fault category");
	}

	void empty_scan_times_out_at_the_configured_monotonic_deadline()
	{
		pm5_sim::FMockDiscoveryScenario Scenario;
		Scenario.ScanTimeoutMs = 10;
		pm5_sim::FMockRowingMachineDiscovery Discovery(std::move(Scenario));

		Check(Discovery.StartScan().IsAccepted(),
			  "empty scan starts asynchronously");
		const auto StartedEvents = Drain(Discovery);
		Check(LastChangedState(StartedEvents) == ERowingConnectionState::Scanning,
			  "empty scan reports that scanning started");
		Check(CountKind(StartedEvents, ERowingMachineEventKind::MachineDiscovered) ==
				  0,
			  "empty scan emits no synthetic candidate");
		Check(Discovery.AdvanceScanTo(9999999ULL),
			  "scan clock advances to just before its deadline");
		const auto BeforeDeadlineEvents = Drain(Discovery);
		Check(BeforeDeadlineEvents.empty(),
			  "scan remains active before the timeout deadline");
		Check(!HasFault(BeforeDeadlineEvents, ERowingFaultCode::ScanTimeout),
			  "scan timeout is not reported early");

		Check(Discovery.AdvanceScanTo(10000000ULL),
			  "scan clock advances to the exact timeout deadline");
		const auto TimedOutEvents = Drain(Discovery);
		Check(LastChangedState(TimedOutEvents) == ERowingConnectionState::Idle,
			  "timed out scan returns to idle");
		const FRowingFault *Timeout =
			FindFault(TimedOutEvents, ERowingFaultCode::ScanTimeout);
		Check(Timeout != nullptr,
			  "empty scan timeout is observable as a categorized fault");
		if (Timeout)
		{
			Check(Timeout->Severity == ERowingFaultSeverity::Recoverable,
				  "empty scan timeout is recoverable");
			Check(Timeout->Operation == ERowingOperation::Scan,
				  "empty scan timeout identifies scan as its operation");
			Check(Timeout->ConnectionState == ERowingConnectionState::Idle,
				  "timeout fault records the post-scan state");
			Check(Timeout->ExpectedValue == 10 && Timeout->ActualValue == 10,
				  "timeout fault records the configured deadline and elapsed time");
		}
		Check(!Discovery.AdvanceScanTo(11000000ULL),
			  "completed scan cannot advance past its deadline");
	}

	void connection_failure_is_asynchronous_and_terminal()
	{
		auto Scenario = pm5_sim::MakeSyntheticIndoorRowerScenario();
		Scenario.ConnectionSucceeds = false;
		pm5_sim::FMockRowingMachine Machine(std::move(Scenario));

		Check(Machine.Connect().IsAccepted(),
			  "connection attempt failure is reported asynchronously");
		const auto Events = Drain(Machine);
		Check(Machine.GetConnectionState() == ERowingConnectionState::Failed,
			  "failed connection enters a terminal failed state");
		const FRowingFault *ConnectionFailure =
			FindFault(Events, ERowingFaultCode::ConnectionTimeout);
		Check(ConnectionFailure != nullptr,
			  "failed connection emits a connection-timeout fault");
		if (ConnectionFailure)
		{
			Check(ConnectionFailure->Severity == ERowingFaultSeverity::Recoverable,
				  "connection failure remains retryable");
			Check(ConnectionFailure->Operation == ERowingOperation::Connect,
				  "connection fault identifies connect as its operation");
			Check(ConnectionFailure->ConnectionState ==
					  ERowingConnectionState::Connecting,
				  "connection fault records the state where it occurred");
		}
		Check(CountKind(Events, ERowingMachineEventKind::MachineInfoObserved) ==
				  0,
			  "connection failure never observes device identity");
		Check(!Machine.PublishTelemetry(Sample(100, 100)),
			  "failed connection cannot publish telemetry");
	}

	void malformed_identity_fails_closed_before_machine_info()
	{
		auto Scenario = pm5_sim::MakeSyntheticIndoorRowerScenario();
		Scenario.IdentityWellFormed = false;
		pm5_sim::FMockRowingMachine Machine(std::move(Scenario));

		Check(Machine.Connect().IsAccepted(),
			  "malformed identity is reported as an asynchronous outcome");
		const auto Events = Drain(Machine);
		Check(Machine.GetConnectionState() == ERowingConnectionState::Unsupported,
			  "malformed identity is rejected without readiness");
		const FRowingFault *IdentityFailure =
			FindFault(Events, ERowingFaultCode::UnsupportedIdentity);
		Check(IdentityFailure != nullptr,
			  "malformed identity emits an unsupported-identity fault");
		if (IdentityFailure)
		{
			Check(IdentityFailure->Operation == ERowingOperation::ReadIdentity,
				  "malformed identity fault identifies identity read");
			Check(IdentityFailure->Severity == ERowingFaultSeverity::Terminal,
				  "malformed identity cannot be retried as telemetry readiness");
		}
		Check(CountKind(Events, ERowingMachineEventKind::MachineInfoObserved) ==
				  0,
			  "malformed identity is never exposed as machine info");
		Check(!Machine.PublishTelemetry(Sample(100, 100)),
			  "malformed identity cannot publish telemetry");
	}

	void malformed_sample_is_reported_without_publishing_metrics()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		Check(Machine.Connect().IsAccepted(),
			  "valid synthetic machine connects before malformed sample injection");
		Drain(Machine);

		Check(Machine.RejectMalformedSample(19, 18),
			  "malformed source sample is accepted for asynchronous rejection");
		const auto Events = Drain(Machine);
		Check(Machine.GetConnectionState() == ERowingConnectionState::Unsupported,
			  "malformed source sample fails the stream closed");
		const FRowingFault *MalformedSample =
			FindFault(Events, ERowingFaultCode::InvalidPacketLength);
		Check(MalformedSample != nullptr,
			  "malformed sample emits an invalid-packet-length fault");
		if (MalformedSample)
		{
			Check(MalformedSample->Operation == ERowingOperation::ReceiveTelemetry,
				  "malformed sample fault identifies telemetry reception");
			Check(MalformedSample->ExpectedValue == 19 &&
					  MalformedSample->ActualValue == 18,
				  "malformed sample exposes lengths but no raw packet bytes");
			Check(MalformedSample->Severity == ERowingFaultSeverity::Terminal,
				  "malformed sample is terminal for this stream");
		}
		Check(CountKind(Events, ERowingMachineEventKind::MetricSampled) == 0,
			  "malformed source bytes never become normalized metric events");
		Check(!Machine.PublishTelemetry(Sample(100, 100)),
			  "malformed sample rejection prevents later metric publication");
	}

	void event_streams_are_ordered_and_metric_timestamps_cover_receive_time()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		std::vector<FRowingMachineEvent> MachineEvents;
		Machine.Connect();
		AppendDrained(Machine, MachineEvents);
		Machine.AdvanceTo(100000000ULL);
		Machine.PublishTelemetry(Sample(100, 100));
		AppendDrained(Machine, MachineEvents);
		Machine.AdvanceTo(200000000ULL);
		Machine.PublishTelemetry(Sample(200, 200));
		AppendDrained(Machine, MachineEvents);
		Machine.Disconnect();
		AppendDrained(Machine, MachineEvents);
		check_event_stream_ordering_and_timestamps(MachineEvents,
												   "mock machine stream");

		pm5_sim::FMockDiscoveryScenario DiscoveryScenario;
		DiscoveryScenario.Machines.push_back(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		pm5_sim::FMockRowingMachineDiscovery Discovery(
			std::move(DiscoveryScenario));
		std::vector<FRowingMachineEvent> DiscoveryEvents;
		Discovery.StartScan();
		AppendDrained(Discovery, DiscoveryEvents);
		Discovery.StopScan();
		AppendDrained(Discovery, DiscoveryEvents);
		check_event_stream_ordering_and_timestamps(DiscoveryEvents,
												   "discovery stream");

		auto Fixture = pm5_sim::MakeNoRowingFixture(100);
		pm5_sim::FReplayRowingMachine Replay(
			pm5_sim::MakeSyntheticIndoorRowerScenario(),
			std::move(Fixture.Frames));
		std::vector<FRowingMachineEvent> ReplayEvents;
		Replay.Connect();
		AppendDrained(Replay, ReplayEvents);
		Replay.AdvanceTo(0);
		AppendDrained(Replay, ReplayEvents);
		Replay.AdvanceTo(100000000ULL);
		AppendDrained(Replay, ReplayEvents);
		check_event_stream_ordering_and_timestamps(ReplayEvents,
												   "replay stream");
	}

	void diagnostic_sample_event_is_not_a_workout_metric_event()
	{
		// Mock/replay scenarios model an approved normalized stream and do not
		// synthesize Warn/diagnostic-only profiles; validate the public event tag.
		FRowingMachineEvent Event{1,
								  100,
								  FRowingDiagnosticSample{Sample(100, 100)}};
		const std::vector<FRowingMachineEvent> Events{Event};
		Check(CountKind(Events,
						ERowingMachineEventKind::DiagnosticSampleObserved) == 1,
			  "diagnostic-only data uses the distinct diagnostic event kind");
		Check(CountKind(Events, ERowingMachineEventKind::MetricSampled) == 0,
			  "diagnostic-only data is not labeled as a workout metric event");
		check_event_stream_ordering_and_timestamps(Events,
												   "diagnostic sample stream");
	}

	void nullable_and_unrealistic_values_are_preserved()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		Machine.Connect();
		Drain(Machine);
		FRowingMetricSample Missing = Sample(100, 100);
		Check(Machine.PublishTelemetry(Missing),
			  "nullable telemetry sample is accepted");
		auto Events = Drain(Machine);
		const FRowingMetricSample *Observed = nullptr;
		for (const auto &Event : Events)
		{
			Observed = std::get_if<FRowingMetricSample>(&Event.Payload);
			if (Observed)
			{
				break;
			}
		}
		Check(Observed && !Observed->SpeedMmPerS && !Observed->HeartRateBpm,
			  "missing metrics stay absent rather than becoming zero");

		FRowingMetricSample Outlier = Sample(200, 200);
		Outlier.SpeedMmPerS = 1000000;
		Outlier.QualityFlags =
			ToRowingQualityFlags(ERowingQualityFlag::Outlier);
		Check(Machine.PublishTelemetry(Outlier),
			  "synthetic outlier remains observable");
		Events = Drain(Machine);
		for (const auto &Event : Events)
		{
			if (const auto *Value =
					std::get_if<FRowingMetricSample>(&Event.Payload))
			{
				Check(Value->SpeedMmPerS == 1000000,
					  "simulator does not clamp an unrealistic source value");
				Check(HasRowingQualityFlag(Value->QualityFlags,
										   ERowingQualityFlag::Outlier),
					  "outlier evidence is retained");
			}
		}
	}

	void stale_data_recovers_without_synthetic_distance()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		Machine.Connect();
		Drain(Machine);
		Check(Machine.AdvanceTo(499000000ULL),
			  "time can advance before stale deadline");
		Check(Machine.GetConnectionState() == ERowingConnectionState::Ready,
			  "telemetry is not stale before 500 ms");
		Check(Machine.AdvanceTo(500000000ULL),
			  "exact stale boundary is deterministic");
		Check(Machine.GetConnectionState() == ERowingConnectionState::Stale,
			  "500 ms without status enters stale");
		Check(Machine.AdvanceTo(1500000000ULL),
			  "blocking warning deadline advances deterministically");
		Check(HasFault(Drain(Machine), ERowingFaultCode::StaleTelemetry),
			  "1.5 s deadline emits blocking stale warning");
		Check(Machine.PublishTelemetry(Sample(100, 0)),
			  "valid resumed source fact restores readiness");
		const auto Events = Drain(Machine);
		Check(Machine.GetConnectionState() == ERowingConnectionState::Ready,
			  "valid status restores ready state");
		Check(CountKind(Events, ERowingMachineEventKind::MetricSampled) == 1,
			  "recovery publishes exactly the supplied fact");
	}

	void reconnect_requires_same_identity_and_monotonic_facts()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		Machine.Connect();
		Drain(Machine);
		Machine.PublishTelemetry(Sample(1000, 100));
		Drain(Machine);
		Machine.AdvanceTo(1000000000ULL);
		Machine.SimulateLinkLoss();
		Drain(Machine);
		Machine.AdvanceTo(2000000000ULL);
		Check(Machine.Connect().IsAccepted(),
			  "same selected synthetic identity can reconnect");
		Check(Machine.GetConnectionState() ==
				  ERowingConnectionState::Subscribing,
			  "reconnect waits for source telemetry");
		Check(CountKind(Drain(Machine),
						ERowingMachineEventKind::MetricSampled) == 0,
			  "reconnect gap creates no synthetic metric");
		Check(Machine.PublishTelemetry(Sample(2000, 200)),
			  "monotonic resumed source fact completes reconnect");
		const auto Restored = Drain(Machine);
		Check(CountKind(Restored,
						ERowingMachineEventKind::ConnectionRestored) == 1,
			  "same identity emits restoration event");
		bool HasReconnectFlag = false;
		for (const auto &Event : Restored)
		{
			if (const auto *Value =
					std::get_if<FRowingMetricSample>(&Event.Payload))
			{
				HasReconnectFlag = HasRowingQualityFlag(
					Value->QualityFlags, ERowingQualityFlag::DeviceReconnected);
			}
		}
		Check(HasReconnectFlag,
			  "first restored fact carries reconnect evidence");

		auto WrongScenario = pm5_sim::MakeSyntheticIndoorRowerScenario();
		pm5_sim::FMockReconnectIdentity Wrong;
		Wrong.Id =
			FRowingMachineId::FromPrivateAdapterValue("synthetic:other-rower");
		Wrong.Info = WrongScenario.Info;
		WrongScenario.ReconnectIdentity = Wrong;
		pm5_sim::FMockRowingMachine WrongMachine(std::move(WrongScenario));
		WrongMachine.Connect();
		Drain(WrongMachine);
		WrongMachine.PublishTelemetry(Sample(1000, 100));
		Drain(WrongMachine);
		WrongMachine.SimulateLinkLoss();
		Drain(WrongMachine);
		WrongMachine.AdvanceTo(1000000000ULL);
		WrongMachine.Connect();
		const auto WrongEvents = Drain(WrongMachine);
		Check(WrongMachine.GetConnectionState() ==
				  ERowingConnectionState::Unsupported,
			  "changed identity fails reconnect closed");
		Check(HasFault(WrongEvents, ERowingFaultCode::UnsupportedIdentity),
			  "wrong reconnect identity is diagnosed");
		Check(!WrongMachine.PublishTelemetry(Sample(2000, 200)),
			  "wrong identity cannot publish telemetry");
	}

	void overflow_and_shutdown_are_explicit()
	{
		pm5_sim::FMockRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario(), 8);
		Machine.Connect();
		const auto ConnectedDiagnostics = Machine.GetDiagnostics();
		Check(!ConnectedDiagnostics.AcquisitionQueue,
			  "mock reports that it has no transport acquisition queue");
		Check(ConnectedDiagnostics.EventQueue.Capacity == 8,
			  "mock reports its configured event queue capacity");
		Check(ConnectedDiagnostics.EventQueue.CurrentDepth <=
				  ConnectedDiagnostics.EventQueue.Capacity,
			  "mock event queue depth stays within its reported capacity");
		Check(ConnectedDiagnostics.EventQueue.HighWaterMark ==
				  ConnectedDiagnostics.EventQueue.CurrentDepth,
			  "initial connection events establish the event queue high-water mark");
		Check(ConnectedDiagnostics.EventQueue.OverflowCount == 0,
			  "event queue has no overflow before pressure is applied");
		Drain(Machine);
		for (std::uint64_t Index = 1; Index <= 7; ++Index)
		{
			Machine.PublishTelemetry(Sample(Index * 100, Index * 100));
		}
		const auto OverflowDiagnostics = Machine.GetDiagnostics();
		Check(OverflowDiagnostics.EventQueue.CurrentDepth <=
				  OverflowDiagnostics.EventQueue.Capacity,
			  "reserved terminal events remain within event queue capacity");
		Check(OverflowDiagnostics.EventQueue.CurrentDepth == 8,
			  "overflow preserves the terminal state and fault in reserved slots");
		Check(OverflowDiagnostics.EventQueue.HighWaterMark ==
				  OverflowDiagnostics.EventQueue.Capacity,
			  "high-water mark includes reserved terminal events");
		Check(OverflowDiagnostics.EventQueue.OverflowCount == 1,
			  "event queue diagnostics count the latched overflow once");
		const auto Events = Drain(Machine);
		Check(Machine.HasOverflowed(), "event queue overflow is latched");
		Check(Machine.GetConnectionState() == ERowingConnectionState::Failed,
			  "overflow terminates the stream");
		Check(HasFault(Events, ERowingFaultCode::QueueOverflow),
			  "overflow is never silent");
		Check(!Machine.PublishTelemetry(Sample(900, 900)),
			  "terminal overflow rejects subsequent input");
		const auto DrainedDiagnostics = Machine.GetDiagnostics();
		Check(DrainedDiagnostics.EventQueue.CurrentDepth == 0,
			  "polling drains the current event queue depth");
		Check(DrainedDiagnostics.EventQueue.HighWaterMark == 8,
			  "draining does not erase the event queue high-water mark");

		pm5_sim::FMockRowingMachine ShutdownMachine(
			pm5_sim::MakeSyntheticIndoorRowerScenario());
		ShutdownMachine.Connect();
		Drain(ShutdownMachine);
		ShutdownMachine.Shutdown();
		Check(ShutdownMachine.Connect().Code ==
				  ERowingCommandResultCode::Shutdown,
			  "shutdown is observable in immediate command result");
		Check(!ShutdownMachine.PublishTelemetry(Sample(100, 100)),
			  "shutdown prevents later telemetry events");
	}

	void replay_forwards_event_queue_diagnostics_without_acquisition_queue()
	{
		auto Fixture = pm5_sim::MakeEasy30SecondFixture();
		pm5_sim::FReplayRowingMachine Machine(
			pm5_sim::MakeSyntheticIndoorRowerScenario(),
			std::move(Fixture.Frames),
			16);

		const auto EmptyDiagnostics = Machine.GetDiagnostics();
		Check(!EmptyDiagnostics.AcquisitionQueue,
			  "replay reports that it has no transport acquisition queue");
		Check(EmptyDiagnostics.EventQueue.Capacity == 16 &&
				  EmptyDiagnostics.EventQueue.CurrentDepth == 0,
			  "replay forwards its empty mock event queue snapshot");

		Check(Machine.Connect().IsAccepted(),
			  "replay connects before publishing fixture frames");
		const auto ConnectedDiagnostics = Machine.GetDiagnostics();
		Check(ConnectedDiagnostics.EventQueue.CurrentDepth <=
				  ConnectedDiagnostics.EventQueue.Capacity,
			  "replay diagnostics preserve event queue bounds");
		Check(ConnectedDiagnostics.EventQueue.HighWaterMark >=
				  ConnectedDiagnostics.EventQueue.CurrentDepth,
			  "replay diagnostics forward the event queue high-water mark");
		Check(ConnectedDiagnostics.EventQueue.OverflowCount == 0,
			  "replay starts without event queue overflow");
	}
} // namespace

int main()
{
	permission_denial_discovery_and_explicit_selection();
	empty_scan_times_out_at_the_configured_monotonic_deadline();
	connection_failure_is_asynchronous_and_terminal();
	malformed_identity_fails_closed_before_machine_info();
	malformed_sample_is_reported_without_publishing_metrics();
	event_streams_are_ordered_and_metric_timestamps_cover_receive_time();
	diagnostic_sample_event_is_not_a_workout_metric_event();
	unsupported_identity_fails_closed();
	nullable_and_unrealistic_values_are_preserved();
	stale_data_recovers_without_synthetic_distance();
	reconnect_requires_same_identity_and_monotonic_facts();
	overflow_and_shutdown_are_explicit();
	replay_forwards_event_queue_diagnostics_without_acquisition_queue();
	if (Failures != 0)
	{
		std::cerr << Failures << " contract assertion(s) failed\n";
		return 1;
	}
	std::cout << "pm5-sim contract scenarios passed\n";
	return 0;
}
