#include "RunMetricsWriter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace PM5Tui
{
	namespace
	{
		std::string UtcTimestamp()
		{
			const auto Now = std::chrono::system_clock::now();
			const auto Milliseconds =
				std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch());
			const std::time_t Seconds = std::chrono::system_clock::to_time_t(Now);
			std::tm UtcTime{};
			gmtime_r(&Seconds, &UtcTime);
			std::ostringstream Result;
			Result << std::put_time(&UtcTime, "%Y-%m-%dT%H:%M:%S") << '.'
				   << std::setw(3) << std::setfill('0') << (Milliseconds.count() % 1000)
				   << 'Z';
			return Result.str();
		}

		std::uint64_t MonotonicNowNs()
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		std::string EscapeJson(std::string_view Value)
		{
			std::ostringstream Result;
			for (const unsigned char Character : Value)
			{
				switch (Character)
				{
				case '"':
					Result << "\\\"";
					break;
				case '\\':
					Result << "\\\\";
					break;
				case '\n':
					Result << "\\n";
					break;
				case '\r':
					Result << "\\r";
					break;
				case '\t':
					Result << "\\t";
					break;
				default:
					if (Character < 0x20)
						Result << "\\u" << std::hex << std::setw(4)
							   << std::setfill('0') << static_cast<unsigned int>(Character)
							   << std::dec;
					else
						Result << static_cast<char>(Character);
				}
			}
			return Result.str();
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

		void AppendEventMonotonicTimestamp(std::ostringstream &Output,
										   std::uint64_t TimestampNs)
		{
			Output << ",\"monotonic_timestamp_ns\":";
			if (TimestampNs == 0)
				Output << "null";
			else
				Output << TimestampNs;
		}

		template <typename T>
		void AppendOptional(std::ostringstream &Output,
							std::string_view Name,
							const std::optional<T> &Value)
		{
			Output << ",\"" << Name << "\":";
			if (Value)
				Output << *Value;
			else
				Output << "null";
		}

		void AppendSupportedMetrics(std::ostringstream &Output,
									FRowingMetricSet SupportedMetrics)
		{
			Output << ",\"supported_metrics\":[";
			bool First = true;
			const auto Append = [&Output, &First, SupportedMetrics](
									ERowingMetric Metric, std::string_view Name)
			{
				if ((SupportedMetrics & ToRowingMetricSet(Metric)) == 0)
					return;
				if (!First)
					Output << ',';
				Output << '\"' << Name << '\"';
				First = false;
			};
			Append(ERowingMetric::SourceElapsed, "source_elapsed");
			Append(ERowingMetric::Distance, "distance");
			Append(ERowingMetric::Speed, "speed");
			Append(ERowingMetric::Pace, "pace");
			Append(ERowingMetric::StrokeRate, "stroke_rate");
			Append(ERowingMetric::StrokePower, "stroke_power");
			Append(ERowingMetric::AveragePower, "average_power");
			Append(ERowingMetric::Calories, "calories");
			Append(ERowingMetric::HeartRate, "heart_rate");
			Append(ERowingMetric::DragFactor, "drag_factor");
			Append(ERowingMetric::StrokeCount, "stroke_count");
			Append(ERowingMetric::WorkoutState, "workout_state");
			Append(ERowingMetric::RowingState, "rowing_state");
			Append(ERowingMetric::StrokeState, "stroke_state");
			Append(ERowingMetric::DriveLength, "drive_length_mm");
			Append(ERowingMetric::DriveTime, "drive_time_ms");
			Append(ERowingMetric::RecoveryTime, "recovery_time_ms");
			Append(ERowingMetric::StrokeDistance, "stroke_distance_mm");
			Append(ERowingMetric::PeakDriveForce, "peak_drive_force_deci_lb");
			Append(ERowingMetric::AverageDriveForce,
				   "average_drive_force_deci_lb");
			Append(ERowingMetric::WorkPerStroke,
				   "work_per_stroke_deci_joules");
			Append(ERowingMetric::CaloriesPerHour, "calories_per_hour");
			Append(ERowingMetric::ProjectedWorkTime, "projected_work_time_ms");
			Append(ERowingMetric::ProjectedWorkDistance,
				   "projected_work_distance_mm");
			Append(ERowingMetric::ProjectedWorkOther,
				   "projected_work_other_raw");
			Output << ']';
		}

		void AppendSample(std::ostringstream &Output, const FRowingMetricSample &Sample)
		{
			Output << ",\"sequence\":" << Sample.Sequence
				   << ",\"source_elapsed_ms\":" << Sample.SourceElapsedMs
				   << ",\"received_monotonic_ns\":" << Sample.ReceivedMonotonicNs
				   << ",\"distance_mm\":" << Sample.DistanceMm;
			AppendOptional(Output, "speed_mm_per_s", Sample.SpeedMmPerS);
			AppendOptional(Output, "pace_ms_per_500m", Sample.PaceMsPer500M);
			AppendOptional(Output, "stroke_rate_deci_spm", Sample.StrokeRateDeciSpm);
			AppendOptional(Output, "stroke_power_w", Sample.StrokePowerW);
			AppendOptional(Output, "average_power_w", Sample.AveragePowerW);
			AppendOptional(Output, "calories", Sample.Calories);
			AppendOptional(Output, "heart_rate_bpm", Sample.HeartRateBpm);
			AppendOptional(Output, "drag_factor", Sample.DragFactor);
			AppendOptional(Output, "stroke_count", Sample.StrokeCount);
			Output << ",\"quality_flags\":" << Sample.QualityFlags
				   << ",\"workout_state\":\"" << ToString(Sample.WorkoutState)
				   << "\",\"rowing_state\":\"" << ToString(Sample.RowingState)
				   << "\",\"stroke_state\":\"" << ToString(Sample.StrokeState)
				   << "\"";
		}

		void AppendQueueSummary(
			std::ostringstream &Output,
			std::string_view Name,
			const std::optional<FQueueMetricsSummary> &Queue)
		{
			Output << ",\"" << Name << "\":";
			if (!Queue)
			{
				Output << "null";
				return;
			}

			Output << "{\"current_depth\":" << Queue->CurrentDepth
				   << ",\"capacity\":" << Queue->Capacity
				   << ",\"high_water_mark\":" << Queue->HighWaterMark
				   << ",\"overflow_count\":" << Queue->OverflowCount << '}';
		}

		std::optional<std::uint64_t> PercentileIntervalMs(
			const Concept2PM::FPM5CharacteristicDiagnostics &Stats,
			std::uint64_t Numerator)
		{
			if (Stats.IntervalCount == 0)
				return std::nullopt;
			const std::uint64_t Rank =
				(Stats.IntervalCount * Numerator + 99) / 100;
			std::uint64_t Seen = 0;
			for (std::size_t Index = 0; Index < Stats.IntervalHistogram.size(); ++Index)
			{
				Seen += Stats.IntervalHistogram[Index];
				if (Seen >= Rank)
					return static_cast<std::uint64_t>(Index) * 10 +
						   (Index + 1 < Stats.IntervalHistogram.size() ? 5 : 0);
			}
			return std::nullopt;
		}

		void AppendOptionalInteger(std::ostringstream &Output,
								   std::string_view Name,
								   std::optional<std::uint64_t> Value)
		{
			Output << ",\"" << Name << "\":";
			if (Value)
				Output << *Value;
			else
				Output << "null";
		}

		void AppendPM5Diagnostics(
			std::ostringstream &Output,
			const std::optional<FPM5RunDiagnostics> &Diagnostics)
		{
			Output << ",\"pm5_diagnostics\":";
			if (!Diagnostics)
			{
				Output << "null";
				return;
			}

			Output << "{\"requested_status_period_ms\":"
				   << Diagnostics->RequestedStatusPeriodMs
				   << ",\"status_rate_write_attempt_count\":"
				   << Diagnostics->StatusRateWriteAttemptCount
				   << ",\"status_rate_write_success_count\":"
				   << Diagnostics->StatusRateWriteSuccessCount
				   << ",\"status_rate_write_failure_count\":"
				   << Diagnostics->StatusRateWriteFailureCount
				   << ",\"characteristics\":[";
			bool First = true;
			for (const auto &Stats : Diagnostics->Characteristics)
			{
				if (!First)
					Output << ',';
				First = false;
				Output << "{\"characteristic_short_id\":\"0x"
					   << std::hex << std::setw(4) << std::setfill('0')
					   << Stats.Characteristic << std::dec << std::setfill(' ')
					   << "\",\"cadence_class\":\""
					   << (Stats.TracksContinuousStatusCadence()
							   ? "continuous_status"
							   : "event_driven")
					   << "\",\"properties_observed\":"
					   << (Stats.PropertiesObserved ? "true" : "false")
					   << ",\"observed_properties\":[";
				bool FirstProperty = true;
				const auto AppendProperty = [&](
												Concept2PM::EPM5CharacteristicProperty Property,
												const char *Name)
				{
					if (!Concept2PM::HasPM5CharacteristicProperty(
							Stats.ObservedProperties, Property))
						return;
					if (!FirstProperty)
						Output << ',';
					FirstProperty = false;
					Output << '"' << Name << '"';
				};
				AppendProperty(Concept2PM::EPM5CharacteristicProperty::Read, "read");
				AppendProperty(Concept2PM::EPM5CharacteristicProperty::Write, "write");
				AppendProperty(Concept2PM::EPM5CharacteristicProperty::WriteWithoutResponse,
							   "write_without_response");
				AppendProperty(Concept2PM::EPM5CharacteristicProperty::Notify, "notify");
				AppendProperty(Concept2PM::EPM5CharacteristicProperty::Indicate,
							   "indicate");
				Output << "],\"notification_enable_attempt_count\":"
					   << Stats.NotificationEnableAttemptCount
					   << ",\"notification_enable_success_count\":"
					   << Stats.NotificationEnableSuccessCount
					   << ",\"notification_enable_failure_count\":"
					   << Stats.NotificationEnableFailureCount
					   << ",\"notification_count\":" << Stats.NotificationCount
					   << ",\"first_received_monotonic_ns\":"
					   << Stats.FirstReceivedMonotonicNs
					   << ",\"last_received_monotonic_ns\":"
					   << Stats.LastReceivedMonotonicNs
					   << ",\"interval_count\":" << Stats.IntervalCount;
				AppendOptionalInteger(Output, "min_interval_ms", Stats.IntervalCount ? std::optional<std::uint64_t>(Stats.MinIntervalNs / 1'000'000ULL) : std::nullopt);
				AppendOptionalInteger(Output, "mean_interval_ms", Stats.IntervalCount ? std::optional<std::uint64_t>(Stats.IntervalTotalNs / Stats.IntervalCount / 1'000'000ULL) : std::nullopt);
				AppendOptionalInteger(Output, "max_interval_ms", Stats.IntervalCount ? std::optional<std::uint64_t>(Stats.MaxIntervalNs / 1'000'000ULL) : std::nullopt);
				AppendOptionalInteger(Output, "p50_interval_ms", PercentileIntervalMs(Stats, 50));
				AppendOptionalInteger(Output, "p95_interval_ms", PercentileIntervalMs(Stats, 95));
				Output << ",\"long_gap_threshold_ms\":";
				if (Stats.TracksContinuousStatusCadence())
					Output << 500;
				else
					Output << "null";
				Output << ",\"long_gap_count\":" << Stats.LongGapCount
					   << ",\"interval_histogram_10ms\":[";
				for (std::size_t Index = 0; Index < Stats.IntervalHistogram.size(); ++Index)
				{
					if (Index != 0)
						Output << ',';
					Output << Stats.IntervalHistogram[Index];
				}
				Output << "],\"observed_packet_lengths\":[";
				for (std::size_t LengthIndex = 0;
					 LengthIndex < Stats.ObservedPacketLengths.size();
					 ++LengthIndex)
				{
					if (LengthIndex != 0)
						Output << ',';
					const auto &Length = Stats.ObservedPacketLengths[LengthIndex];
					Output << "{\"packet_length\":" << Length.PacketLength
						   << ",\"count\":" << Length.Count << '}';
				}
				Output << "],\"approved_packet_lengths\":[";
				for (std::size_t LengthIndex = 0;
					 LengthIndex < Stats.ApprovedPacketLengths.size();
					 ++LengthIndex)
				{
					if (LengthIndex != 0)
						Output << ',';
					Output << Stats.ApprovedPacketLengths[LengthIndex];
				}
				Output << "],\"last_parser_error\":";
				if (Stats.LastParserErrorCode == Concept2PM::EPacketError::None)
					Output << "null";
				else
					Output << "{\"code\":\"" << ToString(Stats.LastParserErrorCode)
						   << "\",\"actual_packet_length\":"
						   << Stats.LastParserErrorActualLength
						   << ",\"monotonic_timestamp_ns\":"
						   << Stats.LastParserErrorMonotonicNs << '}';
				Output << ",\"parser_errors\":{\"unknown_characteristic\":"
					   << Stats.ParserErrorCounts[static_cast<std::size_t>(Concept2PM::EPacketError::UnknownCharacteristic)]
					   << ",\"length_not_approved\":"
					   << Stats.ParserErrorCounts[static_cast<std::size_t>(Concept2PM::EPacketError::LengthNotApproved)]
					   << ",\"invalid_value\":"
					   << Stats.ParserErrorCounts[static_cast<std::size_t>(Concept2PM::EPacketError::InvalidValue)]
					   << "}}";
			}
			Output << "],\"callback_errors\":[";
			for (std::size_t Index = 0; Index < Diagnostics->CallbackErrors.size(); ++Index)
			{
				if (Index != 0)
					Output << ',';
				const auto &Error = Diagnostics->CallbackErrors[Index];
				Output << "{\"stage\":\"" << ToString(Error.Stage)
					   << "\",\"characteristic_short_id\":";
				if (Error.Characteristic == 0)
					Output << "null";
				else
					Output << "\"0x" << std::hex << std::setw(4) << std::setfill('0')
						   << Error.Characteristic << std::dec << std::setfill(' ') << '\"';
				Output << ",\"count\":" << Error.Count << ",\"error_domain\":";
				if (Error.HasError)
					Output << '\"' << ToString(Error.LastErrorDomain) << '\"';
				else
					Output << "null";
				Output << ",\"error_code\":";
				if (Error.HasError)
					Output << Error.LastErrorCode;
				else
					Output << "null";
				AppendOptionalInteger(Output, "last_error_monotonic_ns", Error.HasError ? std::optional<std::uint64_t>(Error.LastErrorMonotonicNs) : std::nullopt);
				Output << '}';
			}
			const auto &Probe = Diagnostics->ProbeCapture;
			Output << "],\"probe_capture\":{\"enabled\":"
				   << (Probe.Enabled ? "true" : "false")
				   << ",\"active\":" << (Probe.Active ? "true" : "false")
				   << ",\"started_monotonic_ns\":" << Probe.StartedMonotonicNs
				   << ",\"max_capture_duration_ms\":"
				   << Probe.MaxCaptureDurationMs
				   << ",\"max_captured_packet_count\":"
				   << Probe.MaxCapturedPacketCount
				   << ",\"max_captured_payload_bytes\":"
				   << Probe.MaxCapturedPayloadBytes
				   << ",\"evidence_queue_capacity\":"
				   << Probe.EvidenceQueueCapacity
				   << ",\"evidence_queue_current_depth\":"
				   << Probe.EvidenceQueueCurrentDepth
				   << ",\"evidence_queue_high_water_mark\":"
				   << Probe.EvidenceQueueHighWaterMark
				   << ",\"observed_packet_count\":" << Probe.ObservedPacketCount
				   << ",\"captured_packet_count\":" << Probe.CapturedPacketCount
				   << ",\"captured_payload_byte_count\":"
				   << Probe.CapturedPayloadByteCount
				   << ",\"truncated_payload_count\":"
				   << Probe.TruncatedPayloadCount
				   << ",\"evidence_queue_overflow_count\":"
				   << Probe.EvidenceQueueOverflowCount
				   << ",\"limit_dropped_packet_count\":"
				   << Probe.LimitDroppedPacketCount
				   << ",\"stop_reason\":\"" << ToString(Probe.StopReason)
				   << "\"}}";
		}
	} // namespace

	FRunMetricsWriter::FRunMetricsWriter(std::filesystem::path InDirectory,
										 std::string_view SourceRevision,
										 FPM5HardwareProbeConfiguration InProbeConfiguration)
		: ProbeConfiguration(std::move(InProbeConfiguration))
	{
		RunStartedMonotonicNs = MonotonicNowNs();
		std::error_code Error;
		std::filesystem::create_directories(InDirectory, Error);
		if (Error)
			return;
		std::filesystem::permissions(InDirectory,
									 std::filesystem::perms::owner_all,
									 std::filesystem::perm_options::replace,
									 Error);
		if (Error)
			return;

		const std::string Timestamp = UtcTimestamp();
		const auto UniquePart = std::chrono::steady_clock::now().time_since_epoch().count();
		MetricsPath = InDirectory / ("pm5-tui-" + Timestamp + "-" +
									 std::to_string(UniquePart) + ".jsonl");
		Output.open(MetricsPath, std::ios::out | std::ios::app);
		if (!Output)
			return;
		std::filesystem::permissions(MetricsPath,
									 std::filesystem::perms::owner_read |
										 std::filesystem::perms::owner_write,
									 std::filesystem::perm_options::replace,
									 Error);
		if (Error)
		{
			Output.close();
			return;
		}
		std::ostringstream Started;
		Started << "{\"event\":\"run_started\",\"schema_version\":4,\"observed_at_utc\":\""
				<< EscapeJson(Timestamp) << "\",\"source_revision\":\""
				<< EscapeJson(SourceRevision) << "\",\"monotonic_timestamp_ns\":"
				<< RunStartedMonotonicNs
				<< ",\"hardware_probe\":{\"raw_telemetry_capture\":"
				<< (ProbeConfiguration.CaptureRawTelemetry ? "true" : "false")
				<< ",\"max_capture_duration_ms\":"
				<< ProbeConfiguration.MaxCaptureDurationMs
				<< ",\"max_captured_packet_count\":"
				<< ProbeConfiguration.MaxCapturedPacketCount
				<< ",\"max_captured_payload_bytes\":"
				<< ProbeConfiguration.MaxCapturedPayloadBytes
				<< ",\"evidence_queue_capacity\":"
				<< ProbeConfiguration.EvidenceQueueCapacity << "}}";
		WriteRecord(Started.str());
	}

	FRunMetricsWriter::~FRunMetricsWriter()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Output.flush();
		Output.close();
	}

	void FRunMetricsWriter::RecordDiscoveredCandidate(
		const FRowingMachineDescriptor &Candidate,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"candidate_discovered\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"kind_hint\":\"" << ToString(Candidate.KindHint) << "\"";
		AppendOptional(Record, "signal_strength_dbm", Candidate.SignalStrengthDbm);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordAction(
		EPM5TuiRunAction Action,
		std::optional<std::uint64_t> SelectionIndex)
	{
		const std::uint64_t TimestampNs = MonotonicNowNs();
		std::ostringstream Record;
		Record << "{\"event\":\"user_action\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, TimestampNs);
		Record << ",\"action\":\"" << ToString(Action) << '\"';
		AppendOptional(Record, "selection_index", SelectionIndex);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordConnectionStateChanged(
		const FRowingConnectionStateChanged &Changed,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"connection_state_changed\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"previous_state\":\"" << ToString(Changed.PreviousState)
			   << "\",\"state\":\"" << ToString(Changed.NewState)
			   << "\",\"reason\":\"" << ToString(Changed.Reason) << "\"}";
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordMachineInfo(
		const FRowingMachineInfo &Info,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"machine_info_observed\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"manufacturer\":\"" << EscapeJson(Info.Manufacturer)
			   << "\",\"model\":\"" << EscapeJson(Info.Model)
			   << "\",\"hardware_version\":\"" << EscapeJson(Info.HardwareVersion)
			   << "\",\"firmware_version\":\"" << EscapeJson(Info.FirmwareVersion)
			   << "\",\"machine_kind\":\"" << ToString(Info.MachineKind)
			   << "\",\"support_state\":\"" << ToString(Info.SupportState)
			   << '\"';
		AppendSupportedMetrics(Record, Info.SupportedMetrics);
		Record << ",\"supported_metric_flags\":" << Info.SupportedMetrics
			   << ",\"capability_profile_version\":"
			   << Info.CapabilityProfileVersion << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordTelemetryStale(
		const FRowingTelemetryStale &Stale,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"telemetry_stale\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"last_sequence\":" << Stale.LastSequence
			   << ",\"age_ms\":" << Stale.AgeMs << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordConnectionRestored(
		const FRowingConnectionRestored &Restored,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"connection_restored\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"gap_duration_ms\":" << Restored.GapDurationMs
			   << ",\"machine_kind\":\""
			   << ToString(Restored.MachineInfo.MachineKind)
			   << "\",\"support_state\":\""
			   << ToString(Restored.MachineInfo.SupportState) << "\"}";
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordFault(const FRowingFault &Fault,
										std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"fault_observed\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"code\":\"" << ToString(Fault.Code)
			   << "\",\"severity\":\"" << ToString(Fault.Severity)
			   << "\",\"operation\":\"" << ToString(Fault.Operation)
			   << "\",\"connection_state\":\""
			   << ToString(Fault.ConnectionState) << '\"';
		AppendOptional(Record, "expected_value", Fault.ExpectedValue);
		AppendOptional(Record, "actual_value", Fault.ActualValue);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordMetricSample(
		const FRowingMetricSample &Sample,
		bool DiagnosticOnly)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"" << (DiagnosticOnly ? "diagnostic_sampled" : "metric_sampled")
			   << "\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\",\"diagnostic_only\":"
			   << (DiagnosticOnly ? "true" : "false");
		AppendSample(Record, Sample);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordMetricCorrection(
		std::uint64_t TargetSampleSequence, const FRowingMetricSample &Sample)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"metric_corrected\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\",\"target_sample_sequence\":"
			   << TargetSampleSequence;
		AppendSample(Record, Sample);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordStrokeMetrics(
		const FRowingStrokeMetrics &Stroke,
		std::uint64_t MonotonicTimestampNs)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"stroke_metrics\",\"observed_at_utc\":\""
			   << UtcTimestamp() << '\"';
		AppendEventMonotonicTimestamp(Record, MonotonicTimestampNs);
		Record << ",\"source\":\"" << ToString(Stroke.Source)
			   << "\",\"source_elapsed_ms\":" << Stroke.SourceElapsedMs;
		AppendOptional(Record, "stroke_count", Stroke.StrokeCount);
		AppendOptional(Record, "cumulative_distance_mm", Stroke.CumulativeDistanceMm);
		AppendOptional(Record, "drive_length_mm", Stroke.DriveLengthMm);
		AppendOptional(Record, "drive_time_ms", Stroke.DriveTimeMs);
		AppendOptional(Record, "recovery_time_ms", Stroke.RecoveryTimeMs);
		AppendOptional(Record, "stroke_distance_mm", Stroke.StrokeDistanceMm);
		AppendOptional(Record, "peak_drive_force_deci_lb", Stroke.PeakDriveForceDeciLb);
		AppendOptional(Record, "average_drive_force_deci_lb", Stroke.AverageDriveForceDeciLb);
		AppendOptional(Record, "work_per_stroke_deci_joules", Stroke.WorkPerStrokeDeciJoules);
		AppendOptional(Record, "stroke_power_w", Stroke.StrokePowerW);
		AppendOptional(Record, "calories_per_hour", Stroke.CaloriesPerHour);
		AppendOptional(Record, "projected_work_time_ms", Stroke.ProjectedWorkTimeMs);
		AppendOptional(Record, "projected_work_distance_mm", Stroke.ProjectedWorkDistanceMm);
		AppendOptional(Record, "projected_work_other_raw", Stroke.ProjectedWorkOtherRaw);
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordProbePacket(
		const FPM5ProbePacketEvidence &Evidence)
	{
		if (!ProbeConfiguration.CaptureRawTelemetry)
			return;
		std::ostringstream Record;
		Record << "{\"event\":\"raw_telemetry_packet\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, Evidence.ReceivedMonotonicNs);
		Record << ",\"packet_sequence\":" << Evidence.PacketSequence
			   << ",\"characteristic_sequence\":"
			   << Evidence.CharacteristicSequence
			   << ",\"characteristic_short_id\":\"0x" << std::hex
			   << std::setw(4) << std::setfill('0') << Evidence.Characteristic
			   << std::dec << std::setfill(' ')
			   << "\",\"connection_state\":\""
			   << ToString(Evidence.ConnectionState)
			   << "\",\"parser_result\":\"" << ToString(Evidence.ParserResult)
			   << "\",\"approved_packet_lengths\":[";
		for (std::size_t Index = 0;
			 Index < Evidence.ApprovedPacketLengths.size();
			 ++Index)
		{
			if (Index != 0)
				Record << ',';
			Record << Evidence.ApprovedPacketLengths[Index];
		}
		Record << "],\"payload_length\":" << Evidence.OriginalPayloadLength
			   << ",\"captured_payload_length\":"
			   << Evidence.PayloadBytes.size()
			   << ",\"payload_truncated\":"
			   << (Evidence.PayloadTruncated ? "true" : "false")
			   << ",\"payload_hex\":\"";
		for (const std::uint8_t Byte : Evidence.PayloadBytes)
			Record << std::hex << std::setw(2) << std::setfill('0')
				   << static_cast<unsigned int>(Byte);
		Record << std::dec << std::setfill(' ') << "\"}";
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordWorkoutProgramEvent(
		const FWorkoutProgramEvent &Event)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"workout_program_event\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
		AppendEventMonotonicTimestamp(Record, Event.MonotonicTimestampNs);
		Record << ",\"verified\":" << (Event.Verified ? "true" : "false")
			   << ",\"was_abort\":" << (Event.WasAbort ? "true" : "false")
			   << ",\"requested_kind\":\"" << ToString(Event.RequestedSpec.Kind)
			   << '\"';
		AppendOptional(Record, "requested_distance_mm", Event.RequestedSpec.DistanceMm);
		AppendOptional(Record, "requested_duration_ms", Event.RequestedSpec.DurationMs);
		AppendOptional(Record, "requested_interval_rest_ms", Event.RequestedSpec.IntervalRestMs);
		if (Event.Verified)
		{
			Record << ",\"readback_type\":\"" << ToString(Event.Readback.Type)
				   << '\"';
			AppendOptional(Record, "readback_duration_ms", Event.Readback.DurationMs);
		}
		else
		{
			Record << ",\"reject_reason\":\"" << ToString(Event.RejectReason)
				   << '\"';
		}
		Record << '}';
		WriteRecord(Record.str());
	}

	void FRunMetricsWriter::RecordRunStopped(const FRunMetricsSummary &Summary)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"run_stopped\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\",\"sample_count\":" << Summary.SampleCount
			   << ",\"duration_ms\":"
			   << (MonotonicNowNs() - RunStartedMonotonicNs) / 1'000'000ULL
			   << ",\"final_connection_state\":\""
			   << ToString(Summary.FinalConnectionState) << '\"'
			   << ",\"diagnostic_sample_count\":" << Summary.DiagnosticSampleCount
			   << ",\"stroke_metrics_record_count\":"
			   << Summary.StrokeMetricsRecordCount
			   << ",\"correction_count\":" << Summary.CorrectionCount
			   << ",\"stale_event_count\":" << Summary.StaleEventCount
			   << ",\"fault_count\":" << Summary.FaultCount
			   << ",\"duplicate_sample_count\":" << Summary.DuplicateSampleCount
			   << ",\"source_gap_sample_count\":" << Summary.SourceGapSampleCount
			   << ",\"time_regression_sample_count\":"
			   << Summary.TimeRegressionSampleCount
			   << ",\"distance_regression_sample_count\":"
			   << Summary.DistanceRegressionSampleCount
			   << ",\"missing_field_sample_count\":"
			   << Summary.MissingFieldSampleCount
			   << ",\"last_sample_sequence\":" << Summary.LastSampleSequence
			   << ",\"reconnect_count\":" << Summary.ReconnectCount
			   << ",\"total_reconnect_gap_ms\":" << Summary.TotalReconnectGapMs
			   << ",\"longest_reconnect_gap_ms\":" << Summary.LongestReconnectGapMs;
		AppendQueueSummary(Record, "acquisition_queue", Summary.AcquisitionQueue);
		AppendQueueSummary(Record, "event_queue", Summary.EventQueue);
		AppendPM5Diagnostics(Record, Summary.PM5Diagnostics);
		Record << '}';
		WriteRecord(Record.str());
	}

	bool FRunMetricsWriter::IsAvailable() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Output.is_open() && Output.good();
	}

	std::filesystem::path FRunMetricsWriter::CurrentMetricsPath() const
	{
		return MetricsPath;
	}

	void FRunMetricsWriter::WriteRecord(std::string_view Record)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (!Output.is_open())
			return;
		Output << Record << '\n';
		Output.flush();
	}
} // namespace PM5Tui
