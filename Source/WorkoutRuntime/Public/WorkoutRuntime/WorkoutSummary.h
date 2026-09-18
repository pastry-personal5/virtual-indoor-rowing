#pragma once

#include <cstdint>
#include <optional>

// Domain form of rowing.v1.SessionSummary. Optional fields are absent when the
// device never supplied the metric; nothing here is synthesized.
struct FWorkoutSummary
{
	std::uint64_t TotalDistanceMm = 0;
	std::uint64_t ElapsedMs = 0;
	std::uint64_t AcceptedSampleCount = 0;
	std::uint64_t RejectedSampleCount = 0;
	std::uint32_t GapCount = 0;
	std::uint64_t TotalGapMs = 0;
	std::optional<std::uint32_t> AveragePaceMsPer500M;
	std::optional<std::uint32_t> AveragePowerW;
	std::optional<std::uint32_t> AverageStrokeRateDeciSpm;
	std::optional<std::uint32_t> MaxHeartRateBpm;
	std::optional<std::uint64_t> StrokeCount;
	std::optional<std::uint32_t> Calories;
};
