#include "RunMetricsWriter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

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
	} // namespace

	FRunMetricsWriter::FRunMetricsWriter(std::filesystem::path InDirectory,
										 std::string_view SourceRevision)
	{
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
		WriteRecord("{\"event\":\"run_started\",\"schema_version\":1,\"observed_at_utc\":\"" +
					EscapeJson(Timestamp) + "\",\"source_revision\":\"" +
					EscapeJson(SourceRevision) + "\"}");
	}

	FRunMetricsWriter::~FRunMetricsWriter()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Output.flush();
		Output.close();
	}

	void FRunMetricsWriter::RecordMetricSample(const FRowingMetricSample &Sample)
	{
		std::ostringstream Record;
		Record << "{\"event\":\"metric_sampled\",\"observed_at_utc\":\""
			   << UtcTimestamp() << "\"";
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

	void FRunMetricsWriter::RecordRunStopped()
	{
		WriteRecord("{\"event\":\"run_stopped\",\"observed_at_utc\":\"" +
					UtcTimestamp() + "\"}");
	}

	bool FRunMetricsWriter::IsAvailable() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Output.is_open();
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
