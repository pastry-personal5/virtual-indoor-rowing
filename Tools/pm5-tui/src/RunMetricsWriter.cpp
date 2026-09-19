#include "RunMetricsWriter.h"

#include "RunMetricsJson.h"
#include "RunMetricsNames.h"

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
	using namespace Json;
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
