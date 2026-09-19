#pragma once

#include "RunMetricsWriter.h"
#include "RunMetricsNames.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace PM5Tui::Json
{
	std::string UtcTimestamp();

	std::uint64_t MonotonicNowNs();

	std::string EscapeJson(std::string_view Value);

	void AppendEventMonotonicTimestamp(std::ostringstream &Output,
									   std::uint64_t TimestampNs);

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
								FRowingMetricSet SupportedMetrics);

	void AppendSample(std::ostringstream &Output, const FRowingMetricSample &Sample);

	void AppendQueueSummary(
		std::ostringstream &Output,
		std::string_view Name,
		const std::optional<FQueueMetricsSummary> &Queue);

	std::optional<std::uint64_t> PercentileIntervalMs(
		const Concept2PM::FPM5CharacteristicDiagnostics &Stats,
		std::uint64_t Numerator);

	void AppendOptionalInteger(std::ostringstream &Output,
							   std::string_view Name,
							   std::optional<std::uint64_t> Value);

	void AppendPM5Diagnostics(
		std::ostringstream &Output,
		const std::optional<FPM5RunDiagnostics> &Diagnostics);

} // namespace PM5Tui::Json
