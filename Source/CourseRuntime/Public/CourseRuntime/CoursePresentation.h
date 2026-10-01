#pragma once

#include "RowingCore/RowingSession.h"
#include "RowingCore/RowingTelemetry.h"
#include "ContentRuntime/ContentManifest.h"
#include "CourseRuntime/BiomechanicalStrokeTrajectory.h"

#include <cstdint>
#include <optional>
#include <string>

inline constexpr std::uint64_t CourseLengthMm = 2'000'000ULL;
inline constexpr std::uint64_t CoursePredictionLimitNs = 250'000'000ULL;

enum class ECourseAnimationQuality : std::uint8_t
{
	Unavailable,
	Primary,
	Estimated
};

enum class ECourseCameraPreset : std::uint8_t
{
	Close,
	Medium,
	Wide
};

enum class ECourseRevealPhase : std::uint8_t
{
	Chase,
	Dwell,
	Outbound,
	Hold,
	Returning,
	Resume
};

struct FCourseVector
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
};

struct FCoursePathSample
{
	FCourseVector PositionMm;
	FCourseVector UnitTangent{1.0, 0.0, 0.0};
};

struct FCourseCameraParameters
{
	double AftMm = 12'000.0;
	double StarboardMm = 8'000.0;
	double HeightMm = 2'200.0;
	double LookAheadMm = 10'000.0;
	double FieldOfViewDegrees = 60.0;
};

struct FCourseCameraPose
{
	FCourseVector PositionMm;
	FCourseVector LookAtMm;
	double FieldOfViewDegrees = 60.0;
};

// Hardware-neutral facts copied from an immutable workout snapshot. CourseRuntime
// never owns or mutates workout state; it derives reversible presentation only.
struct FCourseTelemetryInput
{
	FRowingSessionId SessionId;
	bool bHasSession = false;
	bool bHasValidSample = false;
	std::uint64_t AcceptedSampleGeneration = 0;
	std::uint64_t MeasuredDistanceMm = 0;
	std::optional<std::uint32_t> SpeedMmPerS;
	std::uint64_t SampleMonotonicNs = 0;
	bool bConnected = false;
	bool bFrozen = false;
	bool bStale = false;
	bool bWorkoutCompleted = false;
	bool bWorkoutTerminated = false;
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
	std::string RouteId = "route.standard.2k";
	std::uint64_t MeasuredDistanceMm = 0;
	double PredictedDistanceMm = 0.0;
	double PresentedDistanceMm = 0.0;
	std::uint64_t CompletedLap = 0;
	double WrappedCourseDistanceMm = 0.0;
	bool bRouteComplete = false;

	FCourseVector PathPositionMm;
	FCourseVector PathUnitTangent{1.0, 0.0, 0.0};
	double DampedYawRadians = 0.0;
	double HullRollDegrees = 0.0;
	double HullPitchDegrees = 0.0;
	double AmbientBobMm = 0.0;
	double StrokeHeaveMm = 0.0;
	bool bDiscontinuityReset = false;

	ECourseCameraPreset CameraPreset = ECourseCameraPreset::Medium;
	FCourseCameraParameters CameraParameters;
	FCourseCameraPose CameraPose;
	ECourseRevealPhase RevealPhase = ECourseRevealPhase::Chase;
	bool bFirstStrokeObserved = false;
	bool bReducedMotion = false;
	std::string CameraToast;
	std::uint64_t CameraToastExpiresNs = 0;

	// 0 is catch and 1 is finish. Component channels encode a deterministic
	// legs/seat -> torso -> arms drive; reading them backwards reverses recovery.
	double StrokePose = 0.0;
	double SeatPose = 0.0;
	double TorsoPose = 0.0;
	double ArmsPose = 0.0;
	double OarPose = 0.0;
	ECourseAnimationQuality AnimationQuality = ECourseAnimationQuality::Unavailable;
};

FCoursePathSample EvaluateCoursePath(const ContentRuntime::FRouteDefinition &Route,
									 double RouteDistanceMm);
FCourseCameraParameters CourseCameraPresetParameters(ECourseCameraPreset Preset) noexcept;
const char *CourseCameraPresetName(ECourseCameraPreset Preset) noexcept;

class FCoursePresentationRuntime final
{
  public:
	FCoursePresentationSnapshot Update(const FCourseTelemetryInput &Input,
									   std::uint64_t NowMonotonicNs);
	void Reset() noexcept;
	void SelectRoute(const ContentRuntime::FRouteDefinition &InRoute);
	void SetReducedMotion(bool bEnabled, std::uint64_t NowMonotonicNs) noexcept;
	void SetCameraPreset(ECourseCameraPreset Preset, std::uint64_t NowMonotonicNs) noexcept;
	void CycleCameraPreset(std::uint64_t NowMonotonicNs) noexcept;
	const ContentRuntime::FRouteDefinition &GetRoute() const noexcept
	{
		return Route;
	}
	const FCoursePresentationSnapshot &GetSnapshot() const noexcept
	{
		return Snapshot;
	}
	ECourseCameraPreset GetSelectedCameraPreset() const noexcept
	{
		return SelectedPreset;
	}

  private:
	FCoursePresentationSnapshot Snapshot;
	ContentRuntime::FRouteDefinition Route = ContentRuntime::BuiltInStandardRouteDefinition();
	FRowingSessionId SessionId;
	bool bHasSession = false;
	bool bHasDistance = false;
	bool bWasLive = false;
	bool bCoasting = false;
	bool bReducedMotion = false;
	bool bHasCourseYaw = false;
	bool bFirstStrokeObserved = false;
	bool bRevealSuppressed = false;
	bool bWasFreshValid = false;
	std::uint64_t LastNowNs = 0;
	std::uint64_t LastAcceptedSampleGeneration = 0;
	std::uint64_t RevealPhaseStartedNs = 0;
	double LastAcceptedMeasuredDistanceMm = 0.0;
	double DistanceVelocityMmPerS = 0.0;
	double CoastTargetMm = 0.0;
	double CourseYawRadians = 0.0;
	double CosmeticRollTargetDegrees = 0.0;
	double CosmeticPitchTargetDegrees = 0.0;
	double LastCosmeticSpeedMmPerS = 0.0;
	std::uint64_t LastCosmeticSpeedSampleNs = 0;
	bool bHasCosmeticSpeed = false;
	ECourseCameraPreset SelectedPreset = ECourseCameraPreset::Medium;
	ECourseRevealPhase RevealPhase = ECourseRevealPhase::Chase;
	FCourseCameraParameters CameraParameters = CourseCameraPresetParameters(ECourseCameraPreset::Medium);
	FCourseCameraParameters CameraBlendStartParameters = CameraParameters;
	FCourseCameraParameters ResumeStartParameters;
	std::uint64_t CameraBlendStartedNs = 0;
	std::string Toast;
	std::uint64_t ToastExpiresNs = 0;

	ERowingStrokeState PreviousStrokeState = ERowingStrokeState::Unknown;
	std::uint64_t StrokeStateStartedNs = 0;
	std::uint64_t RateCycleStartedNs = 0;
	std::uint32_t LatchedDriveMs = 700;
	std::uint32_t LatchedRecoveryMs = 1300;
	bool bReturningToCatch = false;
	bool bReconnectingToMeasuredPose = false;
	bool bHadLiveAnimation = false;
	std::uint64_t CatchReturnStartedNs = 0;
	FBiomechanicalStrokeState BridgeStartState;
	FBiomechanicalStrokeState CurrentStrokeState;

	void ResetSessionState() noexcept;
	void UpdateStroke(const FCourseTelemetryInput &Input,
					  std::uint64_t NowMonotonicNs);
	void UpdatePathAndMotion(const FCourseTelemetryInput &Input, bool bFreshSample, double DeltaSeconds, std::uint64_t NowMonotonicNs);
	void UpdateCamera(const FCourseTelemetryInput &Input, bool bFreshSample, std::uint64_t NowMonotonicNs);
	void EnterRevealPhase(ECourseRevealPhase Phase, std::uint64_t NowMonotonicNs) noexcept;
	void ApplyStrokeState(const FBiomechanicalStrokeState &State,
						  ECourseAnimationQuality Quality) noexcept;
	void SetStrokePose(double Pose,
					   EBiomechanicalStrokePhase Phase,
					   double PhaseDurationSeconds,
					   ECourseAnimationQuality Quality) noexcept;
};
