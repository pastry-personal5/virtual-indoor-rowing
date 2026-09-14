#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "RotatingFileLogger.h"
#include "RunMetricsWriter.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
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

	FPM5HardwareProbeConfiguration MakeHardwareProbeConfiguration(
		bool HardwareProbeEnabled)
	{
		FPM5HardwareProbeConfiguration Configuration;
		Configuration.CaptureRawTelemetry = HardwareProbeEnabled;
		return Configuration;
	}

	struct FDisplaySnapshot
	{
		std::optional<FSteadyClock::time_point> LastTelemetryReceived;
		std::optional<FSteadyClock::time_point> LastStateChanged;
		std::optional<FRowingMetricSample> LatestSample;
		std::optional<ERowingMachineSupportState> SupportState;
		ERowingConnectionState ConnectionState = ERowingConnectionState::Idle;
		ERowingConnectionReason LastTransitionReason = ERowingConnectionReason::None;
		std::string LatestIssue = "None";
		std::uint64_t SampleCount = 0;
		std::uint64_t DiagnosticSampleCount = 0;
		std::uint64_t StrokeMetricsRecordCount = 0;
		std::uint64_t CorrectionCount = 0;
		std::uint64_t StaleCount = 0;
		std::uint64_t FaultCount = 0;
		std::uint64_t DuplicateSampleCount = 0;
		std::uint64_t SourceGapSampleCount = 0;
		std::uint64_t TimeRegressionSampleCount = 0;
		std::uint64_t DistanceRegressionSampleCount = 0;
		std::uint64_t MissingFieldSampleCount = 0;
		std::uint64_t LastSampleSequence = 0;
		std::uint64_t ReconnectCount = 0;
		std::uint64_t TotalReconnectGapMs = 0;
		std::uint64_t LongestReconnectGapMs = 0;
	};

	enum class EStatusTone : std::uint8_t
	{
		Neutral,
		Progress,
		Healthy,
		Warning,
		Critical,
		Diagnostic
	};

	struct FStatusLineView
	{
		EStatusTone Tone = EStatusTone::Neutral;
		std::string Headline;
		std::string Telemetry;
		std::string Detail;
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

	const char *ToString(EStatusTone Value)
	{
		switch (Value)
		{
		case EStatusTone::Neutral:
			return "Neutral";
		case EStatusTone::Progress:
			return "Progress";
		case EStatusTone::Healthy:
			return "Healthy";
		case EStatusTone::Warning:
			return "Warning";
		case EStatusTone::Critical:
			return "Critical";
		case EStatusTone::Diagnostic:
			return "Diagnostic";
		}
		return "Critical";
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

	std::string AgeMilliseconds(
		const std::optional<FSteadyClock::time_point> &Timestamp,
		FSteadyClock::time_point Now = FSteadyClock::now())
	{
		return Timestamp
				   ? std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
										Now - *Timestamp)
										.count())
				   : std::string("—");
	}

	FStatusLineView MakeStatusLineView(const FDisplaySnapshot &Snapshot,
									   std::size_t CandidateCount)
	{
		FStatusLineView View;
		const auto Now = FSteadyClock::now();
		const auto TelemetryAgeMs = Snapshot.LastTelemetryReceived
										? std::optional<std::int64_t>(
											  std::chrono::duration_cast<std::chrono::milliseconds>(
												  Now - *Snapshot.LastTelemetryReceived)
												  .count())
										: std::nullopt;

		switch (Snapshot.ConnectionState)
		{
		case ERowingConnectionState::Idle:
			View.Tone = EStatusTone::Neutral;
			View.Headline = "DISCONNECTED — PRESS SCAN TO FIND A PM5";
			break;
		case ERowingConnectionState::Scanning:
			View.Tone = EStatusTone::Progress;
			View.Headline = "SCANNING FOR CONCEPT2 PM5 DEVICES";
			break;
		case ERowingConnectionState::Connecting:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTING TO SELECTED PM5";
			break;
		case ERowingConnectionState::Discovering:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTED — CHECKING PM5 SERVICES";
			break;
		case ERowingConnectionState::ReadingIdentity:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTED — VERIFYING PM5 IDENTITY";
			break;
		case ERowingConnectionState::Subscribing:
			View.Tone = EStatusTone::Progress;
			View.Headline = "STARTING PM5 TELEMETRY";
			break;
		case ERowingConnectionState::Ready:
			if (!TelemetryAgeMs)
			{
				View.Tone = EStatusTone::Progress;
				View.Headline = "PM5 READY — WAITING FOR TELEMETRY";
			}
			else if (*TelemetryAgeMs <= 500)
			{
				View.Tone = EStatusTone::Healthy;
				View.Headline = "LIVE — PM5 READY";
			}
			else if (*TelemetryAgeMs <= 1500)
			{
				View.Tone = EStatusTone::Warning;
				View.Headline = "TELEMETRY LATE — CHECK THE PM5 LINK";
			}
			else
			{
				View.Tone = EStatusTone::Critical;
				View.Headline = "TELEMETRY LOST — ROWING INPUT PAUSED";
			}
			break;
		case ERowingConnectionState::Stale:
			View.Tone = EStatusTone::Critical;
			View.Headline = "STALE TELEMETRY — ROWING INPUT PAUSED";
			break;
		case ERowingConnectionState::Reconnecting:
			View.Tone = EStatusTone::Warning;
			View.Headline = "RECONNECTING TO THE REMEMBERED PM5 — INPUT PAUSED";
			break;
		case ERowingConnectionState::Unsupported:
			View.Tone = EStatusTone::Critical;
			View.Headline = "UNSUPPORTED PM5 — TELEMETRY REJECTED";
			break;
		case ERowingConnectionState::Failed:
			View.Tone = EStatusTone::Critical;
			View.Headline = "CONNECTION FAILED — REVIEW THE ISSUE BELOW";
			break;
		case ERowingConnectionState::PermissionDenied:
			View.Tone = EStatusTone::Critical;
			View.Headline = "BLUETOOTH PERMISSION REQUIRED";
			break;
		case ERowingConnectionState::DiagnosticOnly:
			View.Tone = EStatusTone::Diagnostic;
			View.Headline = "DIAGNOSTIC ONLY — NOT WORKOUT-READY DATA";
			break;
		}

		View.Telemetry = TelemetryAgeMs
							 ? "telemetry " + std::to_string(*TelemetryAgeMs) + " ms ago"
							 : "telemetry waiting";
		std::ostringstream Detail;
		Detail << "State " << ToString(Snapshot.ConnectionState)
			   << "  •  Reason " << ToString(Snapshot.LastTransitionReason);
		if (Snapshot.LatestIssue != "None")
			Detail << "  •  Last issue " << Snapshot.LatestIssue;
		Detail << "  •  " << View.Telemetry
			   << "  •  Samples " << Snapshot.SampleCount
			   << "  •  Reconnects " << Snapshot.ReconnectCount
			   << "  •  Candidates " << CandidateCount;
		if (Snapshot.SupportState)
			Detail << "  •  Support " << ToString(*Snapshot.SupportState);
		if (Snapshot.LatestSample)
			Detail << "  •  Quality "
				   << FormatQualityFlags(Snapshot.LatestSample->QualityFlags);
		View.Detail = Detail.str();
		return View;
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
		Snapshot.LatestSample = Sample;
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

	PM5Tui::FRunMetricsSummary MakeRunMetricsSummary(
		const FDisplaySnapshot &Snapshot,
		const IRowingMachine *Machine)
	{
		PM5Tui::FRunMetricsSummary Summary;
		Summary.SampleCount = Snapshot.SampleCount;
		Summary.DiagnosticSampleCount = Snapshot.DiagnosticSampleCount;
		Summary.StrokeMetricsRecordCount = Snapshot.StrokeMetricsRecordCount;
		Summary.CorrectionCount = Snapshot.CorrectionCount;
		Summary.StaleEventCount = Snapshot.StaleCount;
		Summary.FaultCount = Snapshot.FaultCount;
		Summary.DuplicateSampleCount = Snapshot.DuplicateSampleCount;
		Summary.SourceGapSampleCount = Snapshot.SourceGapSampleCount;
		Summary.TimeRegressionSampleCount = Snapshot.TimeRegressionSampleCount;
		Summary.DistanceRegressionSampleCount = Snapshot.DistanceRegressionSampleCount;
		Summary.MissingFieldSampleCount = Snapshot.MissingFieldSampleCount;
		Summary.LastSampleSequence = Snapshot.LastSampleSequence;
		Summary.ReconnectCount = Snapshot.ReconnectCount;
		Summary.TotalReconnectGapMs = Snapshot.TotalReconnectGapMs;
		Summary.LongestReconnectGapMs = Snapshot.LongestReconnectGapMs;
		Summary.FinalConnectionState =
			Machine ? Machine->GetConnectionState() : Snapshot.ConnectionState;

		if (Machine)
		{
			const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
			const auto ConvertQueue = [](const FRowingQueueDiagnostics &Queue)
			{
				return PM5Tui::FQueueMetricsSummary{
					Queue.CurrentDepth,
					Queue.Capacity,
					Queue.HighWaterMark,
					Queue.OverflowCount};
			};
			Summary.EventQueue = ConvertQueue(Diagnostics.EventQueue);
			if (Diagnostics.AcquisitionQueue)
				Summary.AcquisitionQueue = ConvertQueue(*Diagnostics.AcquisitionQueue);
			if (const auto *PM5Diagnostics =
					dynamic_cast<const IConcept2PMRunDiagnostics *>(Machine))
				Summary.PM5Diagnostics = PM5Diagnostics->GetPM5RunDiagnostics();
		}
		return Summary;
	}

	void LogTuiStopped(const PM5Tui::FRunMetricsSummary &Summary,
					   PM5Tui::FRotatingFileLogger &Logger)
	{
		std::ostringstream Message;
		Message << "event=TuiStopped samples=" << Summary.SampleCount
				<< " corrections=" << Summary.CorrectionCount
				<< " stale_events=" << Summary.StaleEventCount
				<< " faults=" << Summary.FaultCount
				<< " duplicate_samples=" << Summary.DuplicateSampleCount
				<< " source_gap_samples=" << Summary.SourceGapSampleCount
				<< " time_regression_samples=" << Summary.TimeRegressionSampleCount
				<< " distance_regression_samples=" << Summary.DistanceRegressionSampleCount
				<< " missing_field_samples=" << Summary.MissingFieldSampleCount
				<< " last_sample_sequence=" << Summary.LastSampleSequence
				<< " reconnect_count=" << Summary.ReconnectCount
				<< " total_reconnect_gap_ms=" << Summary.TotalReconnectGapMs
				<< " longest_reconnect_gap_ms=" << Summary.LongestReconnectGapMs;
		Message << " stroke_metrics_records=" << Summary.StrokeMetricsRecordCount;
		if (Summary.EventQueue)
			Message << " event_queue_high_water=" << Summary.EventQueue->HighWaterMark
					<< " event_queue_overflow=" << Summary.EventQueue->OverflowCount;
		if (Summary.AcquisitionQueue)
			Message << " acquisition_queue_high_water="
					<< Summary.AcquisitionQueue->HighWaterMark
					<< " acquisition_queue_overflow="
					<< Summary.AcquisitionQueue->OverflowCount;
		if (Summary.PM5Diagnostics &&
			Summary.PM5Diagnostics->ProbeCapture.Enabled)
		{
			const auto &Probe = Summary.PM5Diagnostics->ProbeCapture;
			Message << " probe_packets_observed=" << Probe.ObservedPacketCount
					<< " probe_packets_captured=" << Probe.CapturedPacketCount
					<< " probe_queue_overflow="
					<< Probe.EvidenceQueueOverflowCount
					<< " probe_limit_dropped=" << Probe.LimitDroppedPacketCount;
		}
		Logger.Log(PM5Tui::ELogLevel::Info, Message.str());
	}

	void LogMetricsAvailability(const PM5Tui::FRunMetricsWriter &Metrics,
								PM5Tui::FRotatingFileLogger &Logger)
	{
		if (Metrics.IsAvailable())
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=RunMetricsOpened path=" +
						   Metrics.CurrentMetricsPath().string());
		else
		{
			Logger.Log(PM5Tui::ELogLevel::Error, "event=RunMetricsUnavailable");
			std::cerr << "ERROR: per-run telemetry metrics could not be opened.\n";
		}
	}

	void PrintEvent(const FRowingMachineEvent &Event,
					PM5Tui::FRotatingFileLogger &Logger,
					PM5Tui::FRunMetricsWriter &Metrics,
					FDisplaySnapshot &Snapshot)
	{
		if (const auto *Info = std::get_if<FRowingMachineInfo>(&Event.Payload))
		{
			Snapshot.SupportState = Info->SupportState;
			Metrics.RecordMachineInfo(*Info, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Info, "event=MachineInfoObserved model=" + Info->Model + " hardware=" + Info->HardwareVersion + " firmware=" + Info->FirmwareVersion + " machine_kind=" + ToString(Info->MachineKind) + " support_state=" + ToString(Info->SupportState));
			std::cout << "identity model=" << Info->Model << " hardware=" << Info->HardwareVersion
					  << " firmware=" << Info->FirmwareVersion << " machine_kind=" << ToString(Info->MachineKind)
					  << " support_state=" << ToString(Info->SupportState) << '\n';
		}
		else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
		{
			Snapshot.LastTelemetryReceived = FSteadyClock::now();
			RecordSample(*Sample, Snapshot);
			Metrics.RecordMetricSample(*Sample, false);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
			PrintMetric("METRIC SAMPLE", *Sample);
		}
		else if (const auto *Sample =
					 std::get_if<FRowingDiagnosticSample>(&Event.Payload))
		{
			Snapshot.LastTelemetryReceived = FSteadyClock::now();
			++Snapshot.DiagnosticSampleCount;
			RecordSample(Sample->Sample, Snapshot);
			Metrics.RecordMetricSample(Sample->Sample, true);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=DiagnosticMetricSampled " +
						   FormatMetric("telemetry", Sample->Sample));
			PrintMetric("DIAGNOSTIC METRIC SAMPLE", Sample->Sample);
		}
		else if (const auto *Stroke = std::get_if<FRowingStrokeMetrics>(&Event.Payload))
		{
			++Snapshot.StrokeMetricsRecordCount;
			Metrics.RecordStrokeMetrics(*Stroke, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=StrokeMetricsObserved elapsed_ms=" +
						   std::to_string(Stroke->SourceElapsedMs) +
						   " stroke_count=" + OptionalValue(Stroke->StrokeCount) +
						   " stroke_power_w=" + OptionalValue(Stroke->StrokePowerW) +
						   " work_per_stroke_deci_joules=" +
						   OptionalValue(Stroke->WorkPerStrokeDeciJoules));
		}
		else if (const auto *Changed = std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
		{
			Metrics.RecordConnectionStateChanged(*Changed, Event.MonotonicTimestampNs);
			Snapshot.LastStateChanged = FSteadyClock::now();
			Snapshot.ConnectionState = Changed->NewState;
			Snapshot.LastTransitionReason = Changed->Reason;
			LogConnectionStateChanged(*Changed, Logger);
			std::cout << "connection previous=" << ToString(Changed->PreviousState)
					  << " state=" << ToString(Changed->NewState)
					  << " reason=" << ToString(Changed->Reason) << '\n';
		}
		else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Event.Payload))
		{
			++Snapshot.StaleCount;
			Snapshot.LatestIssue =
				"Stale telemetry (" + std::to_string(Stale->AgeMs) + " ms)";
			Metrics.RecordTelemetryStale(*Stale, Event.MonotonicTimestampNs);
			LogTelemetryStale(*Stale, Logger);
			std::cout << "stale last_sequence=" << Stale->LastSequence
					  << " age_ms=" << Stale->AgeMs << '\n';
		}
		else if (const auto *Restored = std::get_if<FRowingConnectionRestored>(&Event.Payload))
		{
			Metrics.RecordConnectionRestored(*Restored, Event.MonotonicTimestampNs);
			++Snapshot.ReconnectCount;
			Snapshot.TotalReconnectGapMs += Restored->GapDurationMs;
			Snapshot.LongestReconnectGapMs =
				std::max(Snapshot.LongestReconnectGapMs, Restored->GapDurationMs);
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=ConnectionRestored gap_ms=" +
						   std::to_string(Restored->GapDurationMs));
			std::cout << "reconnected gap_ms=" << Restored->GapDurationMs << '\n';
		}
		else if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload))
		{
			++Snapshot.FaultCount;
			Snapshot.LatestIssue = std::string(ToString(Fault->Code)) + ": " +
								   Fault->DiagnosticText;
			Metrics.RecordFault(*Fault, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Warning,
					   "event=FaultObserved code=" + std::string(ToString(Fault->Code)) +
						   " detail=" + Fault->DiagnosticText);
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
				Metrics.RecordDiscoveredCandidate(*Candidate, Event.MonotonicTimestampNs);
				Candidates.push_back(*Candidate);
				std::cout << "candidate=" << Candidates.size() << " name=\"" << Candidate->DisplayLabel << "\"\n";
			}
			else
				PrintEvent(Event, Logger, Metrics, Snapshot);
		}
		if (Machine)
		{
			while (Machine->TryPollEvent(Event))
				PrintEvent(Event, Logger, Metrics, Snapshot);
			if (auto *PM5Diagnostics =
					dynamic_cast<IConcept2PMRunDiagnostics *>(Machine))
			{
				FPM5ProbePacketEvidence Evidence;
				while (PM5Diagnostics->TryPollPM5ProbePacket(Evidence))
					Metrics.RecordProbePacket(Evidence);
			}
		}
	}

	void PrintStatus(const IRowingMachine *Machine,
					 const FDisplaySnapshot &Snapshot,
					 std::size_t CandidateCount,
					 bool HardwareProbeEnabled = false)
	{
		const FStatusLineView View = MakeStatusLineView(Snapshot, CandidateCount);
		std::cout << "status: tone=" << ToString(View.Tone)
				  << " headline=\"" << View.Headline << '"'
				  << " connection_state=" << ToString(Snapshot.ConnectionState)
				  << " discovery candidates=" << CandidateCount
				  << " last_transition_reason=" << ToString(Snapshot.LastTransitionReason)
				  << " last_telemetry_age_ms="
				  << AgeMilliseconds(Snapshot.LastTelemetryReceived);
		if (Snapshot.LastStateChanged)
			std::cout << " state_age_ms=" << AgeMilliseconds(Snapshot.LastStateChanged);
		if (Snapshot.SupportState)
			std::cout << " support_state=" << ToString(*Snapshot.SupportState);
		std::cout << " samples=" << Snapshot.SampleCount
				  << " reconnects=" << Snapshot.ReconnectCount
				  << " latest_issue=\"" << Snapshot.LatestIssue << '"';
		if (Machine)
		{
			const auto Diagnostics = Machine->GetDiagnostics();
			std::cout << " event_queue=" << Diagnostics.EventQueue.CurrentDepth << '/' << Diagnostics.EventQueue.Capacity;
			std::cout << (Diagnostics.AcquisitionQueue ? " acquisition_queue=available" : " acquisition_queue=unavailable");
		}
		else
			std::cout << " event_queue=unavailable acquisition_queue=unavailable";
		std::cout << " hardware_probe="
				  << (HardwareProbeEnabled ? "enabled" : "disabled");
		std::cout << '\n';
	}

	class FInteractiveTui
	{
	  public:
		explicit FInteractiveTui(bool InHardwareProbeEnabled = false)
			: HardwareProbeEnabled(InHardwareProbeEnabled),
			  ProbeConfiguration(
				  MakeHardwareProbeConfiguration(InHardwareProbeEnabled)),
			  Logger("Logs/pm5-tui"),
			  Metrics("Metrics/pm5-tui", VIR_SOURCE_REVISION, ProbeConfiguration),
			  Discovery(InHardwareProbeEnabled
							? CreateConcept2PMHardwareProbeDiscovery(ProbeConfiguration)
							: CreateConcept2PMDiscovery())
		{
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
			if (HardwareProbeEnabled)
				Logger.Log(PM5Tui::ELogLevel::Warning,
						   "event=HardwareProbeStarted raw_telemetry_capture=true owner_only=true");
			LogMetricsAvailability(Metrics, Logger);
		}

		int Run()
		{
			ftxui::App Screen = ftxui::App::TerminalOutput();
			auto Component = BuildComponent(Screen);
			ftxui::Loop Loop(&Screen, Component);
			while (!Loop.HasQuitted())
			{
				DrainEvents();
				// Age and liveness labels must keep moving even if the PM5 stops
				// producing events.
				Screen.PostEvent(ftxui::Event::Custom);
				Loop.RunOnce();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (Machine)
			{
				DrainEvents();
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				DrainEvents();
			}
			const auto Summary = MakeRunMetricsSummary(Snapshot, Machine.get());
			LogTuiStopped(Summary, Logger);
			Metrics.RecordRunStopped(Summary);
			if (!Metrics.IsAvailable())
				Logger.Log(PM5Tui::ELogLevel::Error,
						   "event=RunMetricsWriteFailed");
			return 0;
		}

	  private:
		static constexpr std::size_t MaxHistoryEntries = 3;

		bool HardwareProbeEnabled = false;
		FPM5HardwareProbeConfiguration ProbeConfiguration;
		PM5Tui::FRotatingFileLogger Logger;
		PM5Tui::FRunMetricsWriter Metrics;
		std::unique_ptr<IConcept2PMDiscovery> Discovery;
		std::unique_ptr<IRowingMachine> Machine;
		std::vector<FRowingMachineDescriptor> Candidates;
		std::vector<std::string> CandidateLabels{"No candidates. Press Scan."};
		std::vector<std::string> History{"Ready. Press Scan to discover a PM5."};
		FDisplaySnapshot Snapshot;
		std::string Identity = "No device selected";
		std::optional<FRowingMachineId> SelectedMachineId;
		int SelectedCandidate = 0;

		void AddHistory(std::string Entry)
		{
			if (History.size() == MaxHistoryEntries)
				History.erase(History.begin());
			History.push_back(std::move(Entry));
		}

		std::string FormatCandidateLabel(
			const FRowingMachineDescriptor &Candidate) const
		{
			std::ostringstream Label;
			if (SelectedMachineId && Candidate.Id == *SelectedMachineId)
				Label << "[SELECTED] ";
			Label << Candidate.DisplayLabel;
			if (Candidate.SignalStrengthDbm)
				Label << " | " << *Candidate.SignalStrengthDbm << " dBm";
			Label << " | " << ToString(Candidate.KindHint);
			return Label.str();
		}

		void RefreshCandidateLabels()
		{
			CandidateLabels.clear();
			if (Candidates.empty())
			{
				CandidateLabels.emplace_back("No candidates. Press Scan.");
				return;
			}
			for (const FRowingMachineDescriptor &Candidate : Candidates)
				CandidateLabels.push_back(FormatCandidateLabel(Candidate));
		}

		void AddCandidate(const FRowingMachineDescriptor &Candidate)
		{
			Candidates.push_back(Candidate);
			RefreshCandidateLabels();
			AddHistory("Candidate " + std::to_string(Candidates.size()) + " discovered");
		}

		void RecordEvent(const FRowingMachineEvent &ObservedEvent)
		{
			const auto &Payload = ObservedEvent.Payload;
			if (const auto *Info = std::get_if<FRowingMachineInfo>(&Payload))
			{
				Snapshot.SupportState = Info->SupportState;
				Metrics.RecordMachineInfo(*Info, ObservedEvent.MonotonicTimestampNs);
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
			else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Payload))
			{
				Snapshot.LastTelemetryReceived = FSteadyClock::now();
				RecordSample(*Sample, Snapshot);
				Metrics.RecordMetricSample(*Sample, false);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
			}
			else if (const auto *Sample =
						 std::get_if<FRowingDiagnosticSample>(&Payload))
			{
				Snapshot.LastTelemetryReceived = FSteadyClock::now();
				++Snapshot.DiagnosticSampleCount;
				RecordSample(Sample->Sample, Snapshot);
				Metrics.RecordMetricSample(Sample->Sample, true);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=DiagnosticMetricSampled " +
							   FormatMetric("telemetry", Sample->Sample));
			}
			else if (const auto *Stroke = std::get_if<FRowingStrokeMetrics>(&Payload))
			{
				++Snapshot.StrokeMetricsRecordCount;
				Metrics.RecordStrokeMetrics(*Stroke, ObservedEvent.MonotonicTimestampNs);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=StrokeMetricsObserved elapsed_ms=" +
							   std::to_string(Stroke->SourceElapsedMs) +
							   " stroke_count=" + OptionalValue(Stroke->StrokeCount) +
							   " stroke_power_w=" + OptionalValue(Stroke->StrokePowerW) +
							   " work_per_stroke_deci_joules=" +
							   OptionalValue(Stroke->WorkPerStrokeDeciJoules));
			}
			else if (const auto *Changed =
						 std::get_if<FRowingConnectionStateChanged>(&Payload))
			{
				Metrics.RecordConnectionStateChanged(
					*Changed, ObservedEvent.MonotonicTimestampNs);
				Snapshot.LastStateChanged = FSteadyClock::now();
				Snapshot.ConnectionState = Changed->NewState;
				Snapshot.LastTransitionReason = Changed->Reason;
				LogConnectionStateChanged(*Changed, Logger);
				AddHistory("Connection: " + std::string(ToString(Changed->PreviousState)) +
						   " → " + ToString(Changed->NewState) + " (" +
						   ToString(Changed->Reason) + ")");
			}
			else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Payload))
			{
				++Snapshot.StaleCount;
				Metrics.RecordTelemetryStale(
					*Stale, ObservedEvent.MonotonicTimestampNs);
				LogTelemetryStale(*Stale, Logger);
				Snapshot.LatestIssue =
					"Stale telemetry (" + std::to_string(Stale->AgeMs) + " ms)";
				AddHistory(Snapshot.LatestIssue);
			}
			else if (const auto *Restored =
						 std::get_if<FRowingConnectionRestored>(&Payload))
			{
				Metrics.RecordConnectionRestored(
					*Restored, ObservedEvent.MonotonicTimestampNs);
				++Snapshot.ReconnectCount;
				Snapshot.TotalReconnectGapMs += Restored->GapDurationMs;
				Snapshot.LongestReconnectGapMs =
					std::max(Snapshot.LongestReconnectGapMs, Restored->GapDurationMs);
				Logger.Log(PM5Tui::ELogLevel::Info,
						   "event=ConnectionRestored gap_ms=" +
							   std::to_string(Restored->GapDurationMs));
				AddHistory("Same-device reconnect gap: " +
						   std::to_string(Restored->GapDurationMs) + " ms");
			}
			else if (const auto *Fault = std::get_if<FRowingFault>(&Payload))
			{
				++Snapshot.FaultCount;
				Metrics.RecordFault(*Fault, ObservedEvent.MonotonicTimestampNs);
				Logger.Log(PM5Tui::ELogLevel::Warning,
						   "event=FaultObserved code=" +
							   std::string(ToString(Fault->Code)) +
							   " detail=" + Fault->DiagnosticText);
				Snapshot.LatestIssue = std::string(ToString(Fault->Code)) + ": " +
									   Fault->DiagnosticText;
				AddHistory("Fault: " + std::string(ToString(Fault->Code)));
			}
			else if (const auto *Correction =
						 std::get_if<FRowingMetricCorrection>(&Payload))
			{
				++Snapshot.CorrectionCount;
				Metrics.RecordMetricCorrection(Correction->TargetSampleSequence,
											   Correction->CorrectedSample);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricCorrected target_sample_sequence=" +
							   std::to_string(Correction->TargetSampleSequence) + ' ' +
							   FormatMetric("telemetry", Correction->CorrectedSample));
				Snapshot.LatestSample = Correction->CorrectedSample;
				AddHistory("Late metric correction received");
			}
		}

		ftxui::Element TelemetryContent() const
		{
			if (!Snapshot.LatestSample)
				return ftxui::text("Waiting for a valid PM5 telemetry sample.");

			const auto &Sample = *Snapshot.LatestSample;
			return ftxui::vbox({
				ftxui::text("Elapsed: " + std::to_string(Sample.SourceElapsedMs) + " ms"),
				ftxui::text("Distance: " + std::to_string(Sample.DistanceMm) + " mm"),
				ftxui::text("Speed: " + OptionalValue(Sample.SpeedMmPerS) + " mm/s"),
				ftxui::text("Pace: " + OptionalValue(Sample.PaceMsPer500M) + " ms/500 m"),
				ftxui::text("Stroke rate: " + OptionalValue(Sample.StrokeRateDeciSpm) + " deci-spm"),
				ftxui::text("Stroke power: " + OptionalValue(Sample.StrokePowerW) + " W"),
				ftxui::text("Average power: " + OptionalValue(Sample.AveragePowerW) + " W"),
				ftxui::text("Calories: " + OptionalValue(Sample.Calories)),
				ftxui::text("Heart rate: " + OptionalValue(Sample.HeartRateBpm) + " bpm"),
				ftxui::text("Drag factor: " + OptionalValue(Sample.DragFactor)),
				ftxui::text("Stroke count: " + OptionalValue(Sample.StrokeCount)),
				ftxui::text(std::string("Workout: ") + ToString(Sample.WorkoutState)),
				ftxui::text(std::string("Rowing: ") + ToString(Sample.RowingState)),
				ftxui::text(std::string("Stroke state: ") + ToString(Sample.StrokeState)),
				ftxui::text("Quality: " + FormatQualityFlags(Sample.QualityFlags)),
			});
		}

		void DrainEvents()
		{
			AdoptRelaunchMachine();
			FRowingMachineEvent Event;
			while (Discovery->TryPollDiscoveryEvent(Event))
			{
				if (const auto *Candidate = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
				{
					Metrics.RecordDiscoveredCandidate(*Candidate, Event.MonotonicTimestampNs);
					AddCandidate(*Candidate);
				}
				else
					RecordEvent(Event);
			}
			AdoptRelaunchMachine();
			if (Machine)
			{
				while (Machine->TryPollEvent(Event))
					RecordEvent(Event);
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
				{
					FPM5ProbePacketEvidence Evidence;
					while (PM5Diagnostics->TryPollPM5ProbePacket(Evidence))
						Metrics.RecordProbePacket(Evidence);
				}
			}
		}

		void AdoptRelaunchMachine()
		{
			if (Machine)
				return;
			Machine = Discovery->TryTakeRelaunchMachine();
			if (Machine)
				AddHistory("Adopted remembered-PM5 reconnect session");
		}

		ftxui::Element StatusLine() const
		{
			FStatusLineView View = MakeStatusLineView(Snapshot, Candidates.size());
			if (Machine)
			{
				const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
				View.Detail += "  •  Event queue " +
							   std::to_string(Diagnostics.EventQueue.CurrentDepth) + "/" +
							   std::to_string(Diagnostics.EventQueue.Capacity) +
							   " high " +
							   std::to_string(Diagnostics.EventQueue.HighWaterMark) +
							   " dropped " +
							   std::to_string(Diagnostics.EventQueue.OverflowCount);
				if (Diagnostics.AcquisitionQueue)
				{
					View.Detail += "  •  BLE queue " +
								   std::to_string(
									   Diagnostics.AcquisitionQueue->CurrentDepth) +
								   "/" +
								   std::to_string(Diagnostics.AcquisitionQueue->Capacity) +
								   " high " +
								   std::to_string(Diagnostics.AcquisitionQueue->HighWaterMark) +
								   " dropped " +
								   std::to_string(Diagnostics.AcquisitionQueue->OverflowCount);
				}
				if (Diagnostics.EventQueue.OverflowCount != 0 ||
					(Diagnostics.AcquisitionQueue &&
					 Diagnostics.AcquisitionQueue->OverflowCount != 0))
				{
					View.Tone = EStatusTone::Critical;
					View.Headline = "QUEUE OVERFLOW — TELEMETRY CAPTURE MAY BE INCOMPLETE";
				}
				if (HardwareProbeEnabled)
				{
					if (const auto *PM5Diagnostics =
							dynamic_cast<const IConcept2PMRunDiagnostics *>(Machine.get()))
					{
						const auto Probe =
							PM5Diagnostics->GetPM5RunDiagnostics().ProbeCapture;
						View.Detail += "  •  RAW packets " +
									   std::to_string(Probe.CapturedPacketCount) +
									   " dropped " +
									   std::to_string(
										   Probe.EvidenceQueueOverflowCount +
										   Probe.LimitDroppedPacketCount);
						if (!Probe.Active ||
							Probe.EvidenceQueueOverflowCount != 0 ||
							Probe.LimitDroppedPacketCount != 0)
						{
							View.Tone = EStatusTone::Critical;
							View.Headline =
								"HARDWARE PROBE CAPTURE INCOMPLETE";
						}
					}
				}
			}
			if (HardwareProbeEnabled && View.Tone != EStatusTone::Critical)
			{
				View.Tone = EStatusTone::Diagnostic;
				View.Headline = "HARDWARE PROBE • " + View.Headline;
				if (!Machine)
					View.Detail += "  •  RAW telemetry capture armed";
			}
			if (HardwareProbeEnabled && !Metrics.IsAvailable())
			{
				View.Tone = EStatusTone::Critical;
				View.Headline = "HARDWARE PROBE FILE UNAVAILABLE — DO NOT ROW";
			}

			ftxui::Color Background = ftxui::Color::GrayDark;
			ftxui::Color Foreground = ftxui::Color::White;
			switch (View.Tone)
			{
			case EStatusTone::Neutral:
				break;
			case EStatusTone::Progress:
				Background = ftxui::Color::Blue;
				break;
			case EStatusTone::Healthy:
				Background = ftxui::Color::Green;
				Foreground = ftxui::Color::Black;
				break;
			case EStatusTone::Warning:
				Background = ftxui::Color::Yellow;
				Foreground = ftxui::Color::Black;
				break;
			case EStatusTone::Critical:
				Background = ftxui::Color::Red;
				break;
			case EStatusTone::Diagnostic:
				Background = ftxui::Color::Magenta;
				break;
			}

			const auto Headline = ftxui::hbox({
									  ftxui::text("  ● " + View.Headline),
									  ftxui::filler(),
									  ftxui::text(" " + View.Telemetry + "  "),
								  }) |
								  ftxui::bold | ftxui::color(Foreground) |
								  ftxui::bgcolor(Background);
			const auto Detail = ftxui::hbox({
									ftxui::text("  " + View.Detail),
									ftxui::filler(),
								}) |
								ftxui::color(ftxui::Color::White) |
								ftxui::bgcolor(ftxui::Color::GrayDark);
			return ftxui::vbox({Headline, Detail}) |
				   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 2);
		}

		void StartScan()
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStarted);
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
				Snapshot.LatestIssue =
					"Select a discovered candidate before connecting";
				return;
			}
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::CandidateSelected,
								 static_cast<std::uint64_t>(SelectedCandidate + 1));
			Discovery->StopScan();
			const FRowingMachineDescriptor &Candidate = Candidates[SelectedCandidate];
			Machine = Discovery->CreateMachine(Candidate.Id);
			SelectedMachineId = Candidate.Id;
			RefreshCandidateLabels();
			AddHistory("Candidate " + std::to_string(SelectedCandidate + 1) + " selected");
		}

		void Connect()
		{
			if (!Machine)
			{
				Snapshot.LatestIssue =
					"Select a discovered candidate before connecting";
				return;
			}
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ConnectRequested);
			Machine->Connect();
			AddHistory("Connection requested");
		}

		void ForgetRememberedMachine()
		{
			if (Machine)
			{
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				Machine.reset();
			}
			Discovery->ForgetRememberedMachine();
			SelectedMachineId.reset();
			RefreshCandidateLabels();
			Identity = "No device selected";
			AddHistory("Remembered PM5 forgotten; disconnected");
		}

		ftxui::Component BuildComponent(ftxui::App &Screen)
		{
			const auto Scan = ftxui::Button("Scan", [this]
											{ StartScan(); });
			const auto Stop = ftxui::Button("Stop scan", [this]
											{ Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStopped); Discovery->StopScan(); AddHistory("Scan stopped"); });
			const auto Select = ftxui::Button("Select", [this]
											  { SelectCandidate(); });
			const auto ConnectButton = ftxui::Button("Connect", [this]
													 { Connect(); });
			const auto Disconnect = ftxui::Button("Disconnect", [this]
												  { if (Machine) { Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::DisconnectRequested); Machine->Disconnect(); } });
			const auto Forget = ftxui::Button("Forget PM5", [this]
											  { ForgetRememberedMachine(); });
			const auto Quit = ftxui::Button("Quit", [&Screen]
											{ Screen.Exit(); });
			const auto Menu = ftxui::Menu(&CandidateLabels, &SelectedCandidate);
			const auto Actions = ftxui::Container::Vertical({
				ftxui::Container::Horizontal({Scan, Stop, Select, ConnectButton, Disconnect}),
				ftxui::Container::Horizontal({Forget, Quit}),
			});
			const auto Controls = ftxui::Container::Vertical({
				Actions,
				Menu,
			});
			auto Root = ftxui::Renderer(Controls, [this, Actions, Menu]
										{
				ftxui::Elements HistoryElements;
				for (const std::string &Entry : History)
					HistoryElements.push_back(ftxui::text(Entry));
				auto MainPane = ftxui::vbox({
					ftxui::text(HardwareProbeEnabled
								? "PM5 Hardware Probe — RAW TELEMETRY CAPTURE ACTIVE"
								: "PM5 Diagnostic") |
						ftxui::bold,
						ftxui::text("Controls: Tab/Shift-Tab to focus, arrows to choose a PM5, Enter to activate, q to quit."),
						Actions->Render(),
						ftxui::separator(),
						ftxui::text("Telemetry") | ftxui::bold,
						ftxui::separator(),
						ftxui::text("Identity: " + Identity),
						TelemetryContent(),
						ftxui::separator(),
						ftxui::text("Recent events") | ftxui::bold,
						ftxui::vbox(std::move(HistoryElements)) |
							ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 4),
						ftxui::filler(),
				}) | ftxui::border | ftxui::flex;
				auto CandidatePane = ftxui::vbox({
					ftxui::text("PM5 devices") | ftxui::bold,
					ftxui::separator(),
					Menu->Render() | ftxui::flex,
				}) | ftxui::border | ftxui::flex;
				const bool WideLayout = ftxui::Terminal::Size().dimx >= 100;
				auto Content = WideLayout
					? ftxui::hbox({
						  MainPane | ftxui::flex,
						  CandidatePane | ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
															std::max(24, ftxui::Terminal::Size().dimx / 4)),
					  })
					: ftxui::vbox({CandidatePane | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 8),
										MainPane | ftxui::flex});
				return ftxui::vbox({Content | ftxui::flex, StatusLine()}) |
					ftxui::border | ftxui::flex; });
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
	bool HardwareProbeEnabled = false;
	int ArgumentIndex = 1;
	if (ArgumentIndex < argc &&
		std::string_view(argv[ArgumentIndex]) == "--hardware-probe")
	{
		HardwareProbeEnabled = true;
		++ArgumentIndex;
	}
	if (ArgumentIndex >= argc)
		return FInteractiveTui(HardwareProbeEnabled).Run();
	if (std::string_view(argv[ArgumentIndex]) == "--help")
	{
		std::cout << "PM5 Diagnostic\n"
				  << "Interactive: run without arguments.\n"
				  << "Hardware probe: --hardware-probe (owner-only bounded raw telemetry capture).\n"
				  << "Script: --script <status|scan|stop|select N|connect|disconnect|forget|quit>...\n";
		return 0;
	}
	if (std::string_view(argv[ArgumentIndex]) != "--script")
	{
		std::cerr << "unknown option; run with --help for usage\n";
		return 2;
	}

	const FPM5HardwareProbeConfiguration ProbeConfiguration =
		MakeHardwareProbeConfiguration(HardwareProbeEnabled);
	PM5Tui::FRotatingFileLogger Logger("Logs/pm5-tui");
	PM5Tui::FRunMetricsWriter Metrics(
		"Metrics/pm5-tui", VIR_SOURCE_REVISION, ProbeConfiguration);
	Logger.Log(PM5Tui::ELogLevel::Info, "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
	if (HardwareProbeEnabled)
		Logger.Log(PM5Tui::ELogLevel::Warning,
				   "event=HardwareProbeStarted raw_telemetry_capture=true owner_only=true");
	LogMetricsAvailability(Metrics, Logger);
	// The shipped diagnostic executable exercises only reviewed profiles. The
	// adapter's separate diagnostic-only factory remains available to targeted
	// tests and does not permit Ready.
	auto Discovery = HardwareProbeEnabled
						 ? CreateConcept2PMHardwareProbeDiscovery(ProbeConfiguration)
						 : CreateConcept2PMDiscovery();
	std::unique_ptr<IRowingMachine> Machine;
	std::vector<FRowingMachineDescriptor> Candidates;
	FDisplaySnapshot Snapshot;
	std::vector<std::string> Commands;
	for (int Index = ArgumentIndex + 1; Index < argc; ++Index)
		Commands.emplace_back(argv[Index]);

	for (const std::string &Command : Commands)
	{
		if (Command.empty() || Command.find_first_not_of(" \t") == std::string::npos)
			continue;
		if (Command == "quit")
			break;
		if (Command == "status")
			PrintStatus(
				Machine.get(), Snapshot, Candidates.size(), HardwareProbeEnabled);
		else if (Command == "scan")
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStarted);
			Candidates.clear();
			Discovery->StartScan();
		}
		else if (Command == "stop")
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStopped);
			Discovery->StopScan();
		}
		else if (Command == "connect")
		{
			if (!Machine)
				std::cout << "no device selected; use select command to choose a candidate\n";
			else
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ConnectRequested);
				Machine->Connect();
			}
		}
		else if (Command == "disconnect" && Machine)
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::DisconnectRequested);
			Machine->Disconnect();
		}
		else if (Command == "forget")
		{
			if (Machine)
			{
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				Machine.reset();
			}
			Discovery->ForgetRememberedMachine();
			std::cout << "remembered PM5 forgotten; disconnected\n";
		}
		else if (Command == "__test_identity")
		{
			FRowingMachineInfo Info;
			Info.Model = "PM5";
			Info.HardwareVersion = "test-hardware";
			Info.FirmwareVersion = "test-firmware";
			Info.MachineKind = ERowingMachineKind::IndoorRower;
			Info.SupportedMetrics =
				ToRowingMetricSet(ERowingMetric::StrokePower) |
				ToRowingMetricSet(ERowingMetric::PeakDriveForce);
			Info.SupportState = ERowingMachineSupportState::Warn;
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Info)}, Logger, Metrics, Snapshot);
		}
		else if (Command == "__test_discovered_candidate")
		{
			FRowingMachineDescriptor Candidate;
			Candidate.DisplayLabel = "private-candidate-name-must-not-be-captured";
			Candidate.SignalStrengthDbm = -54;
			Candidate.KindHint = ERowingMachineKind::IndoorRower;
			Metrics.RecordDiscoveredCandidate(Candidate, 1'234'567'890ULL);
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
		else if (Command == "__test_diagnostic_metric_sample")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 99;
			Sample.SourceElapsedMs = 99000;
			Sample.DistanceMm = 99000;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::MissingField);
			PrintEvent(FRowingMachineEvent{.Payload = FRowingDiagnosticSample{std::move(Sample)}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_stroke_metrics")
		{
			FRowingStrokeMetrics Stroke;
			Stroke.Source = ERowingStrokeMetricsSource::KinematicsAndForce;
			Stroke.SourceElapsedMs = 123450;
			Stroke.StrokeCount = 1234;
			Stroke.CumulativeDistanceMm = 678900;
			Stroke.DriveLengthMm = 1490;
			Stroke.DriveTimeMs = 1280;
			Stroke.RecoveryTimeMs = 2580;
			Stroke.StrokeDistanceMm = 1000;
			Stroke.PeakDriveForceDeciLb = 250;
			Stroke.AverageDriveForceDeciLb = 150;
			Stroke.WorkPerStrokeDeciJoules = 10000;
			PrintEvent(FRowingMachineEvent{
						   .MonotonicTimestampNs = 1'234'567'890ULL,
						   .Payload = std::move(Stroke)},
					   Logger,
					   Metrics,
					   Snapshot);
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
		else if (Command == "__test_connection_restored")
		{
			FRowingMachineInfo Info;
			Info.Model = "PM5";
			Info.MachineKind = ERowingMachineKind::IndoorRower;
			PrintEvent(FRowingMachineEvent{.Payload = FRowingConnectionRestored{
											   Info, 2750}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_fault")
		{
			FRowingFault Fault;
			Fault.Code = ERowingFaultCode::Disconnected;
			Fault.Severity = ERowingFaultSeverity::Recoverable;
			Fault.Operation = ERowingOperation::Reconnect;
			Fault.ConnectionState = ERowingConnectionState::Reconnecting;
			Fault.DiagnosticText = "redacted diagnostic detail";
			Fault.ExpectedValue = 18;
			Fault.ActualValue = 15;
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Fault)},
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
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::CandidateSelected,
									 static_cast<std::uint64_t>(Selection));
				Discovery->StopScan();
				Machine = Discovery->CreateMachine(Candidates[Selection - 1].Id);
			}
		}
		else
			std::cout << "unknown command\n";
		if (!Machine)
			Machine = Discovery->TryTakeRelaunchMachine();
		DrainEvents(*Discovery, Machine.get(), Candidates, Logger, Metrics, Snapshot);
	}
	if (Machine)
	{
		if (auto *PM5Diagnostics =
				dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
			PM5Diagnostics->FinalizePM5ProbeCapture();
		DrainEvents(*Discovery, Machine.get(), Candidates, Logger, Metrics, Snapshot);
	}
	const auto Summary = MakeRunMetricsSummary(Snapshot, Machine.get());
	LogTuiStopped(Summary, Logger);
	Metrics.RecordRunStopped(Summary);
	if (!Metrics.IsAvailable())
		Logger.Log(PM5Tui::ELogLevel::Error, "event=RunMetricsWriteFailed");
	return 0;
}
