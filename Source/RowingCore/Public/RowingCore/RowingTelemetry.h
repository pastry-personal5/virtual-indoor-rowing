#pragma once

#include <cstdint>
#include <optional>

enum class ERowingWorkoutState : std::uint8_t
{
	Unknown,
	WaitingToBegin,
	Active,
	Paused,
	Resting,
	Complete,
	Terminated
};

enum class ERowingState : std::uint8_t
{
	Unknown,
	Inactive,
	Active
};

enum class ERowingStrokeState : std::uint8_t
{
	Unknown,
	Waiting,
	Drive,
	Dwell,
	Recovery
};

enum class ERowingMetric : std::uint64_t
{
	None = 0,
	SourceElapsed = 1ULL << 0,
	Distance = 1ULL << 1,
	Speed = 1ULL << 2,
	Pace = 1ULL << 3,
	StrokeRate = 1ULL << 4,
	StrokePower = 1ULL << 5,
	AveragePower = 1ULL << 6,
	Calories = 1ULL << 7,
	HeartRate = 1ULL << 8,
	DragFactor = 1ULL << 9,
	StrokeCount = 1ULL << 10,
	WorkoutState = 1ULL << 11,
	RowingState = 1ULL << 12,
	StrokeState = 1ULL << 13
};

using FRowingMetricSet = std::uint64_t;

constexpr FRowingMetricSet ToRowingMetricSet(ERowingMetric Metric) noexcept
{
	return static_cast<FRowingMetricSet>(Metric);
}

enum class ERowingQualityFlag : std::uint32_t
{
	None = 0,
	MissingField = 1U << 0,
	SourceGap = 1U << 1,
	Duplicate = 1U << 2,
	TimeRegression = 1U << 3,
	DistanceRegression = 1U << 4,
	DeviceReconnected = 1U << 5,
	LateCorrection = 1U << 6,
	UnsupportedValue = 1U << 7,
	Outlier = 1U << 8
};

using FRowingQualityFlags = std::uint32_t;

constexpr FRowingQualityFlags
ToRowingQualityFlags(ERowingQualityFlag Flag) noexcept
{
	return static_cast<FRowingQualityFlags>(Flag);
}

constexpr FRowingQualityFlags operator|(ERowingQualityFlag Left,
										ERowingQualityFlag Right) noexcept
{
	return ToRowingQualityFlags(Left) | ToRowingQualityFlags(Right);
}

constexpr FRowingQualityFlags operator|(FRowingQualityFlags Left,
										ERowingQualityFlag Right) noexcept
{
	return Left | ToRowingQualityFlags(Right);
}

constexpr bool HasRowingQualityFlag(FRowingQualityFlags Flags,
									ERowingQualityFlag Flag) noexcept
{
	return (Flags & ToRowingQualityFlags(Flag)) != 0;
}

struct FRowingMetricSample
{
	std::uint64_t Sequence = 0;
	std::uint64_t SourceElapsedMs = 0;
	std::uint64_t ReceivedMonotonicNs = 0;
	std::uint64_t DistanceMm = 0;
	std::optional<std::uint32_t> SpeedMmPerS;
	std::optional<std::uint32_t> PaceMsPer500M;
	std::optional<std::uint32_t> StrokeRateDeciSpm;
	std::optional<std::uint32_t> StrokePowerW;
	std::optional<std::uint32_t> AveragePowerW;
	std::optional<std::uint32_t> Calories;
	std::optional<std::uint32_t> HeartRateBpm;
	std::optional<std::uint32_t> DragFactor;
	std::optional<std::uint64_t> StrokeCount;
	ERowingWorkoutState WorkoutState = ERowingWorkoutState::Unknown;
	ERowingState RowingState = ERowingState::Unknown;
	ERowingStrokeState StrokeState = ERowingStrokeState::Unknown;
	FRowingQualityFlags QualityFlags =
		ToRowingQualityFlags(ERowingQualityFlag::None);
};
