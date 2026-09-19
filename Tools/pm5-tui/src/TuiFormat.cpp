#include "TuiFormat.h"

namespace PM5Tui
{
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

	const char *ToString(Concept2PM::EDiagnosticWorkoutKind Value)
	{
		switch (Value)
		{
		case Concept2PM::EDiagnosticWorkoutKind::Distance:
			return "Distance";
		case Concept2PM::EDiagnosticWorkoutKind::Time:
			return "Time";
		case Concept2PM::EDiagnosticWorkoutKind::TimeInterval:
			return "TimeInterval";
		}
		return "Unknown";
	}

	const char *ToString(Concept2PM::EDiagnosticWorkoutProgramRejectReason Value)
	{
		switch (Value)
		{
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::MalformedResponse:
			return "MalformedResponse";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::PM5Nak:
			return "PM5Nak";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::WrongCommand:
			return "WrongCommand";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::Timeout:
			return "Timeout";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::Other:
			return "Other";
		}
		return "Unknown";
	}

	Concept2PM::FDiagnosticWorkoutSpec MakeExampleDistanceWorkoutSpec()
	{
		Concept2PM::FDiagnosticWorkoutSpec Spec;
		Spec.Kind = Concept2PM::EDiagnosticWorkoutKind::Distance;
		Spec.DistanceMm = 2'000'000; // 2000 m.
		return Spec;
	}

	Concept2PM::FDiagnosticWorkoutSpec MakeExampleTimeWorkoutSpec()
	{
		Concept2PM::FDiagnosticWorkoutSpec Spec;
		Spec.Kind = Concept2PM::EDiagnosticWorkoutKind::Time;
		Spec.DurationMs = 1'200'000; // 20:00.
		return Spec;
	}

	Concept2PM::FDiagnosticWorkoutSpec MakeExampleTimeIntervalWorkoutSpec()
	{
		Concept2PM::FDiagnosticWorkoutSpec Spec;
		Spec.Kind = Concept2PM::EDiagnosticWorkoutKind::TimeInterval;
		Spec.DurationMs = 120'000;	  // 2:00 work.
		Spec.IntervalRestMs = 30'000; // :30 rest.
		return Spec;
	}

	std::string FormatWorkoutProgramEvent(const FWorkoutProgramEvent &Event)
	{
		std::ostringstream Text;
		if (Event.WasAbort)
		{
			Text << (Event.Verified ? "Abort verified" : "Abort rejected");
		}
		else if (Event.Verified)
		{
			Text << "Program verified: " << ToString(Event.Readback.Type);
			if (Event.Readback.DurationMs)
				Text << " duration_ms=" << *Event.Readback.DurationMs;
		}
		else
		{
			Text << "Program rejected: " << ToString(Event.RejectReason)
				 << " (requested " << ToString(Event.RequestedSpec.Kind) << ")";
		}
		return Text.str();
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
} // namespace PM5Tui
