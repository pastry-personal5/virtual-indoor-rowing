#include "RunMetricsJson.h"

#include "RunMetricsNames.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace PM5Tui::Json
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

	void AppendEventMonotonicTimestamp(std::ostringstream &Output,
									   std::uint64_t TimestampNs)
	{
		Output << ",\"monotonic_timestamp_ns\":";
		if (TimestampNs == 0)
			Output << "null";
		else
			Output << TimestampNs;
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
} // namespace PM5Tui::Json
