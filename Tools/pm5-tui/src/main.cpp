#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "RotatingFileLogger.h"
#include "RunMetricsWriter.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef VIR_SOURCE_REVISION
#define VIR_SOURCE_REVISION "unknown"
#endif

namespace
{
	using FSteadyClock = std::chrono::steady_clock;

	struct FDisplaySnapshot
	{
		std::optional<FSteadyClock::time_point> LastTelemetryReceived;
		std::optional<FSteadyClock::time_point> LastStateChanged;
		ERowingConnectionReason LastTransitionReason = ERowingConnectionReason::None;
		std::uint64_t SampleCount = 0;
		std::uint64_t CorrectionCount = 0;
		std::uint64_t StaleCount = 0;
		std::uint64_t FaultCount = 0;
		std::uint64_t DuplicateSampleCount = 0;
		std::uint64_t SourceGapSampleCount = 0;
		std::uint64_t TimeRegressionSampleCount = 0;
		std::uint64_t DistanceRegressionSampleCount = 0;
		std::uint64_t MissingFieldSampleCount = 0;
		std::uint64_t LastSampleSequence = 0;
	};

	const char *ToString(ERowingMachineKind Value)
	{
		switch (Value)
		{
		case ERowingMachineKind::Unknown:
			return "Unknown";
		case ERowingMachineKind::IndoorRower:
			return "IndoorRower";
		case ERowingMachineKind::SkiErg:
			return "SkiErg";
		case ERowingMachineKind::BikeErg:
			return "BikeErg";
		}
		return "Unknown";
	}

	const char *ToString(ERowingMachineSupportState Value)
	{
		switch (Value)
		{
		case ERowingMachineSupportState::Allowed:
			return "Allowed";
		case ERowingMachineSupportState::Warn:
			return "Warn";
		case ERowingMachineSupportState::Blocked:
			return "Blocked";
		}
		return "Blocked";
	}

	const char *ToString(ERowingConnectionReason Value)
	{
		switch (Value)
		{
		case ERowingConnectionReason::None:
			return "None";
		case ERowingConnectionReason::UserRequested:
			return "UserRequested";
		case ERowingConnectionReason::OperationStarted:
			return "OperationStarted";
		case ERowingConnectionReason::ReadinessConfirmed:
			return "ReadinessConfirmed";
		case ERowingConnectionReason::TelemetryTimedOut:
			return "TelemetryTimedOut";
		case ERowingConnectionReason::TelemetryResumed:
			return "TelemetryResumed";
		case ERowingConnectionReason::LinkLost:
			return "LinkLost";
		case ERowingConnectionReason::ReconnectStarted:
			return "ReconnectStarted";
		case ERowingConnectionReason::CapabilityRejected:
			return "CapabilityRejected";
		case ERowingConnectionReason::OperationFailed:
			return "OperationFailed";
		case ERowingConnectionReason::Shutdown:
			return "Shutdown";
		case ERowingConnectionReason::DiagnosticObservationConfirmed:
			return "DiagnosticObservationConfirmed";
		}
		return "Unknown";
	}

	const char *ToString(ERowingConnectionState Value)
	{
		switch (Value)
		{
		case ERowingConnectionState::Idle:
			return "Idle";
		case ERowingConnectionState::Scanning:
			return "Scanning";
		case ERowingConnectionState::Connecting:
			return "Connecting";
		case ERowingConnectionState::Discovering:
			return "Discovering";
		case ERowingConnectionState::ReadingIdentity:
			return "ReadingIdentity";
		case ERowingConnectionState::Subscribing:
			return "Subscribing";
		case ERowingConnectionState::Ready:
			return "Ready";
		case ERowingConnectionState::Stale:
			return "Stale";
		case ERowingConnectionState::Reconnecting:
			return "Reconnecting";
		case ERowingConnectionState::Unsupported:
			return "Unsupported";
		case ERowingConnectionState::Failed:
			return "Failed";
		case ERowingConnectionState::PermissionDenied:
			return "PermissionDenied";
		case ERowingConnectionState::DiagnosticOnly:
			return "DiagnosticOnly";
		}
		return "Unknown";
	}

	const char *ToString(ERowingFaultCode Value)
	{
		switch (Value)
		{
		case ERowingFaultCode::Permission:
			return "Permission";
		case ERowingFaultCode::ScanTimeout:
			return "ScanTimeout";
		case ERowingFaultCode::ConnectionTimeout:
			return "ConnectionTimeout";
		case ERowingFaultCode::Disconnected:
			return "Disconnected";
		case ERowingFaultCode::MissingService:
			return "MissingService";
		case ERowingFaultCode::MissingCharacteristic:
			return "MissingCharacteristic";
		case ERowingFaultCode::UnsupportedIdentity:
			return "UnsupportedIdentity";
		case ERowingFaultCode::WrongMachineType:
			return "WrongMachineType";
		case ERowingFaultCode::InvalidProperty:
			return "InvalidProperty";
		case ERowingFaultCode::InvalidPacketLength:
			return "InvalidPacketLength";
		case ERowingFaultCode::InvalidValue:
			return "InvalidValue";
		case ERowingFaultCode::QueueOverflow:
			return "QueueOverflow";
		case ERowingFaultCode::StaleTelemetry:
			return "StaleTelemetry";
		case ERowingFaultCode::InternalLifecycleError:
			return "InternalLifecycleError";
		}
		return "Unknown";
	}

	const char *ToString(ERowingWorkoutState Value)
	{
		switch (Value)
		{
		case ERowingWorkoutState::Unknown:
			return "Unknown";
		case ERowingWorkoutState::WaitingToBegin:
			return "WaitingToBegin";
		case ERowingWorkoutState::Active:
			return "Active";
		case ERowingWorkoutState::Paused:
			return "Paused";
		case ERowingWorkoutState::Resting:
			return "Resting";
		case ERowingWorkoutState::Complete:
			return "Complete";
		case ERowingWorkoutState::Terminated:
			return "Terminated";
		}
		return "Unknown";
	}

	const char *ToString(ERowingState Value)
	{
		switch (Value)
		{
		case ERowingState::Unknown:
			return "Unknown";
		case ERowingState::Inactive:
			return "Inactive";
		case ERowingState::Active:
			return "Active";
		}
		return "Unknown";
	}

	const char *ToString(ERowingStrokeState Value)
	{
		switch (Value)
		{
		case ERowingStrokeState::Unknown:
			return "Unknown";
		case ERowingStrokeState::Waiting:
			return "Waiting";
		case ERowingStrokeState::Drive:
			return "Drive";
		case ERowingStrokeState::Dwell:
			return "Dwell";
		case ERowingStrokeState::Recovery:
			return "Recovery";
		}
		return "Unknown";
	}

	template <typename T>
	std::string OptionalValue(const std::optional<T> &Value)
	{
		return Value ? std::to_string(*Value) : "—";
	}

	std::string FormatQualityFlags(FRowingQualityFlags Flags)
	{
		if (Flags == ToRowingQualityFlags(ERowingQualityFlag::None))
			return "None";
		std::string Result;
		const auto Append = [&Result, Flags](ERowingQualityFlag Flag, const char *Name)
		{
			if (!HasRowingQualityFlag(Flags, Flag))
				return;
			if (!Result.empty())
				Result += ',';
			Result += Name;
		};
		Append(ERowingQualityFlag::MissingField, "MissingField");
		Append(ERowingQualityFlag::SourceGap, "SourceGap");
		Append(ERowingQualityFlag::Duplicate, "Duplicate");
		Append(ERowingQualityFlag::TimeRegression, "TimeRegression");
		Append(ERowingQualityFlag::DistanceRegression, "DistanceRegression");
		Append(ERowingQualityFlag::DeviceReconnected, "DeviceReconnected");
		Append(ERowingQualityFlag::LateCorrection, "LateCorrection");
		Append(ERowingQualityFlag::UnsupportedValue, "UnsupportedValue");
		Append(ERowingQualityFlag::Outlier, "Outlier");
		return Result.empty() ? "Unknown" : Result;
	}

	std::string FormatMetric(std::string_view Prefix, const FRowingMetricSample &Sample)
	{
		std::ostringstream Output;
		Output << Prefix << " sequence=" << Sample.Sequence
			   << " elapsed_ms=" << Sample.SourceElapsedMs
			   << " distance_mm=" << Sample.DistanceMm
			   << " speed_mm_per_s=" << OptionalValue(Sample.SpeedMmPerS)
			   << " pace_ms_per_500m=" << OptionalValue(Sample.PaceMsPer500M)
			   << " stroke_rate_deci_spm=" << OptionalValue(Sample.StrokeRateDeciSpm)
			   << " stroke_power_w=" << OptionalValue(Sample.StrokePowerW)
			   << " average_power_w=" << OptionalValue(Sample.AveragePowerW)
			   << " calories=" << OptionalValue(Sample.Calories)
			   << " heart_rate_bpm=" << OptionalValue(Sample.HeartRateBpm)
			   << " drag_factor=" << OptionalValue(Sample.DragFactor)
			   << " stroke_count=" << OptionalValue(Sample.StrokeCount)
			   << " quality_flags=" << FormatQualityFlags(Sample.QualityFlags)
			   << " workout_state=" << ToString(Sample.WorkoutState)
			   << " rowing_state=" << ToString(Sample.RowingState)
			   << " stroke_state=" << ToString(Sample.StrokeState);
		return Output.str();
	}

	void PrintMetric(std::string_view Prefix, const FRowingMetricSample &Sample)
	{
		std::cout << FormatMetric(Prefix, Sample) << '\n';
	}

	void LogConnectionStateChanged(const FRowingConnectionStateChanged &Changed,
								   PM5Tui::FRotatingFileLogger &Logger)
	{
		Logger.Log(PM5Tui::ELogLevel::Info,
				   "event=ConnectionStateChanged previous=" +
					   std::string(ToString(Changed.PreviousState)) + " state=" +
					   ToString(Changed.NewState) + " reason=" +
					   ToString(Changed.Reason));
	}

	void LogTelemetryStale(const FRowingTelemetryStale &Stale,
						   PM5Tui::FRotatingFileLogger &Logger)
	{
		Logger.Log(PM5Tui::ELogLevel::Warning,
				   "event=TelemetryStale last_sequence=" +
					   std::to_string(Stale.LastSequence) + " age_ms=" +
					   std::to_string(Stale.AgeMs));
	}

	void RecordSample(const FRowingMetricSample &Sample, FDisplaySnapshot &Snapshot)
	{
		++Snapshot.SampleCount;
		Snapshot.LastSampleSequence = Sample.Sequence;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::Duplicate))
			++Snapshot.DuplicateSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::SourceGap))
			++Snapshot.SourceGapSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::TimeRegression))
			++Snapshot.TimeRegressionSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::DistanceRegression))
			++Snapshot.DistanceRegressionSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::MissingField))
			++Snapshot.MissingFieldSampleCount;
	}

	void LogTuiStopped(const FDisplaySnapshot &Snapshot,
					   const IRowingMachine *Machine,
					   PM5Tui::FRotatingFileLogger &Logger)
	{
		std::ostringstream Message;
		Message << "event=TuiStopped samples=" << Snapshot.SampleCount
				<< " corrections=" << Snapshot.CorrectionCount
				<< " stale_events=" << Snapshot.StaleCount
				<< " faults=" << Snapshot.FaultCount
				<< " duplicate_samples=" << Snapshot.DuplicateSampleCount
				<< " source_gap_samples=" << Snapshot.SourceGapSampleCount
				<< " time_regression_samples=" << Snapshot.TimeRegressionSampleCount
				<< " distance_regression_samples=" << Snapshot.DistanceRegressionSampleCount
				<< " missing_field_samples=" << Snapshot.MissingFieldSampleCount
				<< " last_sample_sequence=" << Snapshot.LastSampleSequence;
		if (Machine)
		{
			const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
			Message << " event_queue_high_water=" << Diagnostics.EventQueue.HighWaterMark
					<< " event_queue_overflow=" << Diagnostics.EventQueue.OverflowCount;
			if (Diagnostics.AcquisitionQueue)
				Message << " acquisition_queue_high_water="
						<< Diagnostics.AcquisitionQueue->HighWaterMark
						<< " acquisition_queue_overflow="
						<< Diagnostics.AcquisitionQueue->OverflowCount;
		}
		Logger.Log(PM5Tui::ELogLevel::Info, Message.str());
	}

	void PrintEvent(const FRowingMachineEvent &Event,
					PM5Tui::FRotatingFileLogger &Logger,
					PM5Tui::FRunMetricsWriter &Metrics,
					FDisplaySnapshot &Snapshot)
	{
		if (const auto *Info = std::get_if<FRowingMachineInfo>(&Event.Payload))
		{
			Logger.Log(PM5Tui::ELogLevel::Info, "event=MachineInfoObserved model=" + Info->Model + " hardware=" + Info->HardwareVersion + " firmware=" + Info->FirmwareVersion + " machine_kind=" + ToString(Info->MachineKind) + " support_state=" + ToString(Info->SupportState));
			std::cout << "identity model=" << Info->Model << " hardware=" << Info->HardwareVersion
					  << " firmware=" << Info->FirmwareVersion << " machine_kind=" << ToString(Info->MachineKind)
					  << " support_state=" << ToString(Info->SupportState) << '\n';
		}
		else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
		{
			Snapshot.LastTelemetryReceived = FSteadyClock::now();
			RecordSample(*Sample, Snapshot);
			Metrics.RecordMetricSample(*Sample);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
			PrintMetric("METRIC SAMPLE", *Sample);
		}
		else if (const auto *Changed = std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
		{
			Snapshot.LastStateChanged = FSteadyClock::now();
			Snapshot.LastTransitionReason = Changed->Reason;
			LogConnectionStateChanged(*Changed, Logger);
			std::cout << "connection previous=" << ToString(Changed->PreviousState)
					  << " state=" << ToString(Changed->NewState)
					  << " reason=" << ToString(Changed->Reason) << '\n';
		}
		else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Event.Payload))
		{
			++Snapshot.StaleCount;
			LogTelemetryStale(*Stale, Logger);
			std::cout << "stale last_sequence=" << Stale->LastSequence
					  << " age_ms=" << Stale->AgeMs << '\n';
		}
		else if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload))
		{
			++Snapshot.FaultCount;
			Logger.Log(PM5Tui::ELogLevel::Warning,
					   "event=FaultObserved code=" + std::string(ToString(Fault->Code)));
			std::cout << "fault code=" << ToString(Fault->Code)
					  << " state=" << ToString(Fault->ConnectionState)
					  << " detail=" << Fault->DiagnosticText << '\n';
		}
		else if (const auto *Correction = std::get_if<FRowingMetricCorrection>(&Event.Payload))
		{
			++Snapshot.CorrectionCount;
			const auto &Sample = Correction->CorrectedSample;
			Metrics.RecordMetricCorrection(Correction->TargetSampleSequence, Sample);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=MetricCorrected target_sample_sequence=" +
						   std::to_string(Correction->TargetSampleSequence) + ' ' +
						   FormatMetric("telemetry", Sample));
			std::cout << "CORRECTED METRIC SAMPLE target_sample_sequence=" << Correction->TargetSampleSequence
					  << " corrected_sample_sequence=" << Sample.Sequence << " elapsed_ms=" << Sample.SourceElapsedMs
					  << " distance_mm=" << Sample.DistanceMm << " heart_rate_bpm=" << OptionalValue(Sample.HeartRateBpm)
					  << " quality_flags=" << (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::LateCorrection) ? "LateCorrection" : "None")
					  << " workout_state=" << ToString(Sample.WorkoutState) << " rowing_state=" << ToString(Sample.RowingState)
					  << " stroke_state=" << ToString(Sample.StrokeState) << " last_telemetry_age_ms="
					  << (Snapshot.LastTelemetryReceived ? "0" : "—") << '\n';
		}
	}

	void DrainEvents(IRowingMachineDiscovery &Discovery,
					 IRowingMachine *Machine,
					 std::vector<FRowingMachineDescriptor> &Candidates,
					 PM5Tui::FRotatingFileLogger &Logger,
					 PM5Tui::FRunMetricsWriter &Metrics,
					 FDisplaySnapshot &Snapshot)
	{
		FRowingMachineEvent Event;
		while (Discovery.TryPollDiscoveryEvent(Event))
		{
			if (const auto *Candidate = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
			{
				Candidates.push_back(*Candidate);
				std::cout << "candidate=" << Candidates.size() << " name=\"" << Candidate->DisplayLabel << "\"\n";
			}
			else
				PrintEvent(Event, Logger, Metrics, Snapshot);
		}
		if (Machine)
			while (Machine->TryPollEvent(Event))
				PrintEvent(Event, Logger, Metrics, Snapshot);
	}

	void PrintStatus(const IRowingMachine *Machine, const FDisplaySnapshot &Snapshot, std::size_t CandidateCount)
	{
		const auto Now = FSteadyClock::now();
		const auto AgeMs = [&Now](const std::optional<FSteadyClock::time_point> &Timestamp)
		{
			return Timestamp
					   ? std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
											Now - *Timestamp)
											.count())
					   : std::string("—");
		};
		std::cout << "status: discovery candidates=" << CandidateCount
				  << " last_transition_reason=" << ToString(Snapshot.LastTransitionReason)
				  << " last_telemetry_age_ms=" << AgeMs(Snapshot.LastTelemetryReceived);
		if (Snapshot.LastStateChanged)
			std::cout << " state_age_ms=" << AgeMs(Snapshot.LastStateChanged);
		if (Machine)
		{
			const auto Diagnostics = Machine->GetDiagnostics();
			std::cout << " event_queue=" << Diagnostics.EventQueue.CurrentDepth << '/' << Diagnostics.EventQueue.Capacity;
			std::cout << (Diagnostics.AcquisitionQueue ? " acquisition_queue=available" : " acquisition_queue=unavailable");
		}
		else
			std::cout << " event_queue=unavailable acquisition_queue=unavailable";
		std::cout << '\n';
	}

	class FInteractiveTui
	{
	  public:
		FInteractiveTui()
			: Logger("Logs/pm5-tui"), Metrics("Metrics/pm5-tui", VIR_SOURCE_REVISION),
			  Discovery(CreateConcept2PMDiscovery())
		{
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
		}

		int Run()
		{
			ftxui::App Screen = ftxui::App::TerminalOutput();
			auto Component = BuildComponent(Screen);
			ftxui::Loop Loop(&Screen, Component);
			while (!Loop.HasQuitted())
			{
				if (DrainEvents())
					Screen.PostEvent(ftxui::Event::Custom);
				Loop.RunOnce();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (Machine)
				Machine->Disconnect();
			LogTuiStopped(Snapshot, Machine.get(), Logger);
			Metrics.RecordRunStopped();
			return 0;
		}

	  private:
		static constexpr std::size_t MaxHistoryEntries = 8;

		PM5Tui::FRotatingFileLogger Logger;
		PM5Tui::FRunMetricsWriter Metrics;
		std::unique_ptr<IRowingMachineDiscovery> Discovery;
		std::unique_ptr<IRowingMachine> Machine;
		std::vector<FRowingMachineDescriptor> Candidates;
		std::vector<std::string> CandidateLabels{"No candidates. Press Scan."};
		std::vector<std::string> History{"Ready. Press Scan to discover a PM5."};
		FDisplaySnapshot Snapshot;
		std::string Identity = "No device selected";
		std::string LatestTelemetry = "No valid telemetry received";
		std::string LatestFault = "None";
		int SelectedCandidate = 0;

		void AddHistory(std::string Entry)
		{
			if (History.size() == MaxHistoryEntries)
				History.erase(History.begin());
			History.push_back(std::move(Entry));
		}

		void AddCandidate(const FRowingMachineDescriptor &Candidate)
		{
			if (Candidates.empty())
				CandidateLabels.clear();
			Candidates.push_back(Candidate);
			CandidateLabels.push_back(Candidate.DisplayLabel);
			AddHistory("Candidate " + std::to_string(Candidates.size()) + " discovered");
		}

		void RecordEvent(const FRowingMachineEvent &Event)
		{
			if (const auto *Info = std::get_if<FRowingMachineInfo>(&Event.Payload))
			{
				Logger.Log(PM5Tui::ELogLevel::Info,
						   "event=MachineInfoObserved model=" + Info->Model +
							   " hardware=" + Info->HardwareVersion +
							   " firmware=" + Info->FirmwareVersion +
							   " machine_kind=" + ToString(Info->MachineKind) +
							   " support_state=" + ToString(Info->SupportState));
				Identity = "Model " + Info->Model + " | Hardware " +
						   Info->HardwareVersion + " | Firmware " + Info->FirmwareVersion +
						   " | " + ToString(Info->MachineKind) + " | " +
						   ToString(Info->SupportState);
				AddHistory("Identity read: " +
						   std::string(ToString(Info->SupportState)));
			}
			else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
			{
				Snapshot.LastTelemetryReceived = FSteadyClock::now();
				RecordSample(*Sample, Snapshot);
				Metrics.RecordMetricSample(*Sample);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
				LatestTelemetry = FormatMetric("METRIC SAMPLE", *Sample);
			}
			else if (const auto *Changed =
						 std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
			{
				Snapshot.LastStateChanged = FSteadyClock::now();
				Snapshot.LastTransitionReason = Changed->Reason;
				LogConnectionStateChanged(*Changed, Logger);
				AddHistory("Connection: " + std::string(ToString(Changed->PreviousState)) +
						   " → " + ToString(Changed->NewState) + " (" +
						   ToString(Changed->Reason) + ")");
			}
			else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Event.Payload))
			{
				++Snapshot.StaleCount;
				LogTelemetryStale(*Stale, Logger);
				LatestFault = "Stale telemetry: age " + std::to_string(Stale->AgeMs) + " ms";
				AddHistory(LatestFault);
			}
			else if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload))
			{
				++Snapshot.FaultCount;
				Logger.Log(PM5Tui::ELogLevel::Warning,
						   "event=FaultObserved code=" +
							   std::string(ToString(Fault->Code)));
				LatestFault = std::string(ToString(Fault->Code)) + ": " +
							  Fault->DiagnosticText;
				AddHistory("Fault: " + std::string(ToString(Fault->Code)));
			}
			else if (const auto *Correction =
						 std::get_if<FRowingMetricCorrection>(&Event.Payload))
			{
				++Snapshot.CorrectionCount;
				Metrics.RecordMetricCorrection(Correction->TargetSampleSequence,
											   Correction->CorrectedSample);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricCorrected target_sample_sequence=" +
							   std::to_string(Correction->TargetSampleSequence) + ' ' +
							   FormatMetric("telemetry", Correction->CorrectedSample));
				LatestTelemetry = FormatMetric("CORRECTED METRIC SAMPLE",
											   Correction->CorrectedSample);
				AddHistory("Late metric correction received");
			}
		}

		bool DrainEvents()
		{
			bool Drained = false;
			FRowingMachineEvent Event;
			while (Discovery->TryPollDiscoveryEvent(Event))
			{
				Drained = true;
				if (const auto *Candidate = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
					AddCandidate(*Candidate);
				else
					RecordEvent(Event);
			}
			if (Machine)
				while (Machine->TryPollEvent(Event))
				{
					Drained = true;
					RecordEvent(Event);
				}
			return Drained;
		}

		std::string ConnectionSummary() const
		{
			const auto Now = FSteadyClock::now();
			const auto Age = [&Now](const std::optional<FSteadyClock::time_point> &Timestamp)
			{
				return Timestamp
						   ? std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
												Now - *Timestamp)
												.count()) +
								 " ms"
						   : std::string("—");
			};
			std::string Result = "Last transition: " +
								 std::string(ToString(Snapshot.LastTransitionReason)) +
								 " | telemetry age: " + Age(Snapshot.LastTelemetryReceived);
			if (Machine)
			{
				const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
				Result += " | event queue: " +
						  std::to_string(Diagnostics.EventQueue.CurrentDepth) + "/" +
						  std::to_string(Diagnostics.EventQueue.Capacity);
			}
			return Result;
		}

		void StartScan()
		{
			Candidates.clear();
			CandidateLabels = {"Scanning for Concept2 PM5 devices..."};
			SelectedCandidate = 0;
			Discovery->StartScan();
			AddHistory("Scan started");
		}

		void SelectCandidate()
		{
			if (SelectedCandidate < 0 ||
				static_cast<std::size_t>(SelectedCandidate) >= Candidates.size())
			{
				LatestFault = "Select a discovered candidate before connecting";
				return;
			}
			Discovery->StopScan();
			Machine = Discovery->CreateMachine(Candidates[SelectedCandidate].Id);
			AddHistory("Candidate " + std::to_string(SelectedCandidate + 1) + " selected");
		}

		void Connect()
		{
			if (!Machine)
			{
				LatestFault = "Select a discovered candidate before connecting";
				return;
			}
			Machine->Connect();
			AddHistory("Connection requested");
		}

		ftxui::Component BuildComponent(ftxui::App &Screen)
		{
			const auto Scan = ftxui::Button("Scan", [this]
											{ StartScan(); });
			const auto Stop = ftxui::Button("Stop scan", [this]
											{ Discovery->StopScan(); AddHistory("Scan stopped"); });
			const auto Select = ftxui::Button("Select", [this]
											  { SelectCandidate(); });
			const auto ConnectButton = ftxui::Button("Connect", [this]
													 { Connect(); });
			const auto Disconnect = ftxui::Button("Disconnect", [this]
												  { if (Machine) Machine->Disconnect(); });
			const auto Quit = ftxui::Button("Quit", [&Screen]
											{ Screen.Exit(); });
			const auto Menu = ftxui::Menu(&CandidateLabels, &SelectedCandidate);
			const auto Controls = ftxui::Container::Vertical({
				ftxui::Container::Horizontal({Scan, Stop, Select, ConnectButton, Disconnect, Quit}),
				Menu,
			});
			auto Root = ftxui::Renderer(Controls, [this, Controls]
										{
				ftxui::Elements HistoryElements;
				for (const std::string &Entry : History)
					HistoryElements.push_back(ftxui::text(Entry));
				return ftxui::vbox({
					ftxui::text("PM5 Diagnostic") | ftxui::bold,
					ftxui::text("Controls: Tab/Shift-Tab to focus, Enter to activate, q to quit."),
					ftxui::separator(),
					ftxui::text("Identity: " + Identity),
					ftxui::text(ConnectionSummary()),
					ftxui::text("Fault / warning: " + LatestFault),
					ftxui::separator(),
					ftxui::text("Candidates") | ftxui::bold,
					Controls->Render(),
					ftxui::separator(),
					ftxui::text("Latest telemetry") | ftxui::bold,
					ftxui::paragraph(LatestTelemetry),
					ftxui::separator(),
					ftxui::text("Recent events") | ftxui::bold,
					ftxui::vbox(std::move(HistoryElements)),
				}) | ftxui::border; });
			return Root | ftxui::CatchEvent([&Screen](ftxui::Event Event)
											{
				if (Event == ftxui::Event::Character('q'))
				{
					Screen.Exit();
					return true;
				}
				return false; });
		}
	};
} // namespace

int main(int argc, char **argv)
{
	if (argc <= 1)
		return FInteractiveTui().Run();
	if (std::string_view(argv[1]) == "--help")
	{
		std::cout << "PM5 Diagnostic\n"
				  << "Interactive: run without arguments.\n"
				  << "Script: --script <status|scan|stop|select N|connect|disconnect|quit>...\n";
		return 0;
	}
	if (std::string_view(argv[1]) != "--script")
	{
		std::cerr << "unknown option; run with --help for usage\n";
		return 2;
	}

	PM5Tui::FRotatingFileLogger Logger("Logs/pm5-tui");
	PM5Tui::FRunMetricsWriter Metrics("Metrics/pm5-tui", VIR_SOURCE_REVISION);
	Logger.Log(PM5Tui::ELogLevel::Info, "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
	// The shipped diagnostic executable exercises only reviewed profiles. The
	// adapter's separate diagnostic-only factory remains available to targeted
	// tests and does not permit Ready.
	auto Discovery = CreateConcept2PMDiscovery();
	std::unique_ptr<IRowingMachine> Machine;
	std::vector<FRowingMachineDescriptor> Candidates;
	FDisplaySnapshot Snapshot;
	std::vector<std::string> Commands;
	for (int Index = 2; Index < argc; ++Index)
		Commands.emplace_back(argv[Index]);

	for (const std::string &Command : Commands)
	{
		if (Command.empty() || Command.find_first_not_of(" \t") == std::string::npos)
			continue;
		if (Command == "quit")
			break;
		if (Command == "status")
			PrintStatus(Machine.get(), Snapshot, Candidates.size());
		else if (Command == "scan")
		{
			Candidates.clear();
			Discovery->StartScan();
		}
		else if (Command == "stop")
			Discovery->StopScan();
		else if (Command == "connect")
		{
			if (!Machine)
				std::cout << "no device selected; use select command to choose a candidate\n";
			else
				Machine->Connect();
		}
		else if (Command == "disconnect" && Machine)
			Machine->Disconnect();
		else if (Command == "__test_identity")
		{
			FRowingMachineInfo Info;
			Info.Model = "PM5";
			Info.HardwareVersion = "test-hardware";
			Info.FirmwareVersion = "test-firmware";
			Info.MachineKind = ERowingMachineKind::IndoorRower;
			Info.SupportState = ERowingMachineSupportState::Warn;
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Info)}, Logger, Metrics, Snapshot);
		}
		else if (Command == "__test_metric_correction")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 42;
			Sample.SourceElapsedMs = 123456;
			Sample.DistanceMm = 500000;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.RowingState = ERowingState::Active;
			Sample.StrokeState = ERowingStrokeState::Recovery;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::LateCorrection);
			PrintEvent(FRowingMachineEvent{.Payload = FRowingMetricCorrection{42, std::move(Sample)}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_metric_sample")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 7;
			Sample.SourceElapsedMs = 7000;
			Sample.DistanceMm = 12300;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.RowingState = ERowingState::Active;
			Sample.StrokeState = ERowingStrokeState::Drive;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::MissingField);
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Sample)}, Logger, Metrics, Snapshot);
		}
		else if (Command == "__test_connection_state")
		{
			PrintEvent(FRowingMachineEvent{.Payload = FRowingConnectionStateChanged{
											   ERowingConnectionState::Subscribing,
											   ERowingConnectionState::Ready,
											   ERowingConnectionReason::ReadinessConfirmed}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_telemetry_stale")
		{
			PrintEvent(FRowingMachineEvent{.Payload = FRowingTelemetryStale{7, 500}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command.rfind("select ", 0) == 0)
		{
			std::istringstream Input(Command.substr(7));
			std::size_t Selection = 0;
			if (!(Input >> Selection) || Selection == 0)
				std::cout << "candidate numbers start at 1; run scan and select a listed number (for example: select 1)\n";
			else if (Selection > Candidates.size())
				std::cout << "no candidate " << Selection << "; run scan and select a listed number\n";
			else
			{
				Discovery->StopScan();
				Machine = Discovery->CreateMachine(Candidates[Selection - 1].Id);
			}
		}
		else
			std::cout << "unknown command\n";
		DrainEvents(*Discovery, Machine.get(), Candidates, Logger, Metrics, Snapshot);
	}
	LogTuiStopped(Snapshot, Machine.get(), Logger);
	Metrics.RecordRunStopped();
	return 0;
}
