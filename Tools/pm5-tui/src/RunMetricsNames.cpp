#include "RunMetricsNames.h"

namespace PM5Tui::Json
{
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

	const char *ToString(Concept2PM::EPacketError Value)
	{
		switch (Value)
		{
		case Concept2PM::EPacketError::None:
			return "none";
		case Concept2PM::EPacketError::UnknownCharacteristic:
			return "unknown_characteristic";
		case Concept2PM::EPacketError::LengthNotApproved:
			return "length_not_approved";
		case Concept2PM::EPacketError::InvalidValue:
			return "invalid_value";
		}
		return "invalid_value";
	}

	const char *ToString(EPM5ProbeCaptureStopReason Value)
	{
		switch (Value)
		{
		case EPM5ProbeCaptureStopReason::None:
			return "none";
		case EPM5ProbeCaptureStopReason::DurationLimit:
			return "duration_limit";
		case EPM5ProbeCaptureStopReason::PacketLimit:
			return "packet_limit";
		case EPM5ProbeCaptureStopReason::RunStopped:
			return "run_stopped";
		}
		return "unknown";
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

	const char *ToString(ERowingFaultSeverity Value)
	{
		switch (Value)
		{
		case ERowingFaultSeverity::Info:
			return "Info";
		case ERowingFaultSeverity::Warning:
			return "Warning";
		case ERowingFaultSeverity::Recoverable:
			return "Recoverable";
		case ERowingFaultSeverity::Terminal:
			return "Terminal";
		}
		return "Unknown";
	}

	const char *ToString(ERowingOperation Value)
	{
		switch (Value)
		{
		case ERowingOperation::None:
			return "None";
		case ERowingOperation::Scan:
			return "Scan";
		case ERowingOperation::Connect:
			return "Connect";
		case ERowingOperation::Discover:
			return "Discover";
		case ERowingOperation::ReadIdentity:
			return "ReadIdentity";
		case ERowingOperation::Subscribe:
			return "Subscribe";
		case ERowingOperation::ReceiveTelemetry:
			return "ReceiveTelemetry";
		case ERowingOperation::Reconnect:
			return "Reconnect";
		case ERowingOperation::Disconnect:
			return "Disconnect";
		case ERowingOperation::Shutdown:
			return "Shutdown";
		}
		return "Unknown";
	}

	const char *ToString(EPM5CallbackStage Value)
	{
		switch (Value)
		{
		case EPM5CallbackStage::Connection:
			return "connection";
		case EPM5CallbackStage::Disconnection:
			return "disconnection";
		case EPM5CallbackStage::ServiceDiscovery:
			return "service_discovery";
		case EPM5CallbackStage::IdentityCharacteristicDiscovery:
			return "identity_characteristic_discovery";
		case EPM5CallbackStage::TelemetryCharacteristicDiscovery:
			return "telemetry_characteristic_discovery";
		case EPM5CallbackStage::IdentityRead:
			return "identity_read";
		case EPM5CallbackStage::TelemetryValueUpdate:
			return "telemetry_value_update";
		case EPM5CallbackStage::NotificationSubscription:
			return "notification_subscription";
		case EPM5CallbackStage::StatusRateWrite:
			return "status_rate_write";
		case EPM5CallbackStage::WorkoutProgramControlWrite:
			return "workout_program_control_write";
		case EPM5CallbackStage::WorkoutProgramControlRead:
			return "workout_program_control_read";
		}
		return "unknown";
	}

	const char *ToString(EPM5ErrorDomain Value)
	{
		switch (Value)
		{
		case EPM5ErrorDomain::None:
			return "none";
		case EPM5ErrorDomain::CoreBluetooth:
			return "core_bluetooth";
		case EPM5ErrorDomain::CoreBluetoothATT:
			return "core_bluetooth_att";
		case EPM5ErrorDomain::Foundation:
			return "foundation";
		case EPM5ErrorDomain::Other:
			return "other";
		}
		return "unknown";
	}

	const char *ToString(EPM5TuiRunAction Value)
	{
		switch (Value)
		{
		case EPM5TuiRunAction::ScanStarted:
			return "scan_started";
		case EPM5TuiRunAction::ScanStopped:
			return "scan_stopped";
		case EPM5TuiRunAction::CandidateSelected:
			return "candidate_selected";
		case EPM5TuiRunAction::ConnectRequested:
			return "connect_requested";
		case EPM5TuiRunAction::DisconnectRequested:
			return "disconnect_requested";
		case EPM5TuiRunAction::ProgramDistanceWorkoutRequested:
			return "program_distance_workout_requested";
		case EPM5TuiRunAction::ProgramTimeWorkoutRequested:
			return "program_time_workout_requested";
		case EPM5TuiRunAction::ProgramTimeIntervalWorkoutRequested:
			return "program_time_interval_workout_requested";
		case EPM5TuiRunAction::AbortWorkoutRequested:
			return "abort_workout_requested";
		}
		return "unknown";
	}

	const char *ToString(Concept2PM::EDiagnosticWorkoutKind Value)
	{
		switch (Value)
		{
		case Concept2PM::EDiagnosticWorkoutKind::Distance:
			return "distance";
		case Concept2PM::EDiagnosticWorkoutKind::Time:
			return "time";
		case Concept2PM::EDiagnosticWorkoutKind::TimeInterval:
			return "time_interval";
		}
		return "unknown";
	}

	const char *ToString(Concept2PM::EDiagnosticWorkoutProgramRejectReason Value)
	{
		switch (Value)
		{
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::MalformedResponse:
			return "malformed_response";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::PM5Nak:
			return "pm5_nak";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::WrongCommand:
			return "wrong_command";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::Timeout:
			return "timeout";
		case Concept2PM::EDiagnosticWorkoutProgramRejectReason::Other:
			return "other";
		}
		return "unknown";
	}

	const char *ToString(ERowingStrokeMetricsSource Value)
	{
		switch (Value)
		{
		case ERowingStrokeMetricsSource::KinematicsAndForce:
			return "kinematics_and_force";
		case ERowingStrokeMetricsSource::PowerAndProjection:
			return "power_and_projection";
		}
		return "unknown";
	}
} // namespace PM5Tui::Json
