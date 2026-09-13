#pragma once

#include "RowingCore/RowingTelemetry.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace PM5Tui
{
	class FRunMetricsWriter
	{
	  public:
		explicit FRunMetricsWriter(std::filesystem::path InDirectory,
								   std::string_view SourceRevision);
		~FRunMetricsWriter();

		FRunMetricsWriter(const FRunMetricsWriter &) = delete;
		FRunMetricsWriter &operator=(const FRunMetricsWriter &) = delete;

		void RecordMetricSample(const FRowingMetricSample &Sample);
		void RecordMetricCorrection(std::uint64_t TargetSampleSequence,
									const FRowingMetricSample &Sample);
		void RecordRunStopped();
		bool IsAvailable() const;
		std::filesystem::path CurrentMetricsPath() const;

	  private:
		void WriteRecord(std::string_view Record);

		std::filesystem::path MetricsPath;
		mutable std::mutex Mutex;
		std::ofstream Output;
	};
} // namespace PM5Tui
