#pragma once

#include "RowingCore/RowingSession.h"
#include "RowingCore/RowingTelemetry.h"

#include <cstdint>
#include <optional>

inline constexpr std::uint64_t CourseLengthMm = 2'000'000ULL;
inline constexpr std::uint64_t CoursePredictionLimitNs = 250'000'000ULL;

enum class ECourseAnimationQuality : std::uint8_t
{
	Unavailable,
	Primary,
	Estimated
};

// Hardware-neutral facts copied from an immutable workout snapshot. CourseRuntime
// never owns or mutates workout state; it derives reversible presentation only.
struct FCourseTelemetryInput
{
	FRowingSessionId SessionId;
	bool bHasSession = false;
	bool bHasValidSample = false;
	std::uint64_t MeasuredDistanceMm = 0;
	std::optional<std::uint32_t> SpeedMmPerS;
	std::uint64_t SampleMonotonicNs = 0;
	bool bConnected = false;
	bool bFrozen = false;
	bool bStale = false;
	ERowingSessionState SessionState = ERowingSessionState::Created;
	ERowingWorkoutState WorkoutState = ERowingWorkoutState::Unknown;
	ERowingState RowingState = ERowingState::Unknown;
	ERowingStrokeState StrokeState = ERowingStrokeState::Unknown;
	std::optional<std::uint32_t> StrokeRateDeciSpm;
	std::optional<std::uint32_t> DriveTimeMs;
	std::optional<std::uint32_t> RecoveryTimeMs;
};

struct FCoursePresentationSnapshot
{
	std::uint64_t MeasuredDistanceMm = 0;
	double PredictedDistanceMm = 0.0;
	double PresentedDistanceMm = 0.0;
	std::uint64_t CompletedLap = 0;
	double WrappedCourseDistanceMm = 0.0;

	// 0 is catch and 1 is finish. Component channels encode a deterministic
	// legs/seat -> torso -> arms drive; reading them backwards reverses recovery.
	double StrokePose = 0.0;
	double SeatPose = 0.0;
	double TorsoPose = 0.0;
	double ArmsPose = 0.0;
	double OarPose = 0.0;
	ECourseAnimationQuality AnimationQuality = ECourseAnimationQuality::Unavailable;
};

class FCoursePresentationRuntime final
{
  public:
	FCoursePresentationSnapshot Update(const FCourseTelemetryInput &Input,
									   std::uint64_t NowMonotonicNs);
	void Reset() noexcept;
	const FCoursePresentationSnapshot &GetSnapshot() const noexcept
	{
		return Snapshot;
	}

  private:
	FCoursePresentationSnapshot Snapshot;
	FRowingSessionId SessionId;
	bool bHasSession = false;
	bool bHasDistance = false;
	bool bWasLive = false;
	bool bCoasting = false;
	std::uint64_t LastNowNs = 0;
	double DistanceVelocityMmPerS = 0.0;
	double CoastTargetMm = 0.0;

	ERowingStrokeState PreviousStrokeState = ERowingStrokeState::Unknown;
	std::uint64_t StrokeStateStartedNs = 0;
	std::uint64_t RateCycleStartedNs = 0;
	bool bReturningToCatch = false;
	std::uint64_t CatchReturnStartedNs = 0;
	double CatchReturnStartPose = 0.0;

	void UpdateStroke(const FCourseTelemetryInput &Input,
					  std::uint64_t NowMonotonicNs);
	void SetStrokePose(double Pose, ECourseAnimationQuality Quality);
};
