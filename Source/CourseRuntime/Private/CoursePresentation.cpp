#include "CourseRuntime/CoursePresentation.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr double SpringOmegaPerSecond = 16.0;
	constexpr std::uint64_t CatchReturnDurationNs = 500'000'000ULL;
	constexpr std::uint32_t DefaultDriveMs = 700;
	constexpr std::uint32_t DefaultRecoveryMs = 1300;

	double Clamp01(double Value)
	{
		return std::clamp(Value, 0.0, 1.0);
	}

	double SmoothStep(double Value)
	{
		const double T = Clamp01(Value);
		return T * T * (3.0 - 2.0 * T);
	}

	double SegmentedPose(double Pose, double Start, double End)
	{
		return SmoothStep((Pose - Start) / (End - Start));
	}

	bool IsWorkoutActive(const FCourseTelemetryInput &Input)
	{
		return Input.SessionState == ERowingSessionState::Active &&
			   Input.WorkoutState == ERowingWorkoutState::Active &&
			   Input.RowingState == ERowingState::Active;
	}

	bool IsLive(const FCourseTelemetryInput &Input)
	{
		return Input.bConnected && !Input.bFrozen && !Input.bStale &&
			   IsWorkoutActive(Input);
	}

	std::uint64_t ElapsedNs(std::uint64_t Now, std::uint64_t Then)
	{
		return Now >= Then ? Now - Then : 0;
	}
} // namespace

void FCoursePresentationRuntime::Reset() noexcept
{
	Snapshot = {};
	SessionId = {};
	bHasSession = false;
	bHasDistance = false;
	bWasLive = false;
	bCoasting = false;
	LastNowNs = 0;
	DistanceVelocityMmPerS = 0.0;
	CoastTargetMm = 0.0;
	PreviousStrokeState = ERowingStrokeState::Unknown;
	StrokeStateStartedNs = 0;
	RateCycleStartedNs = 0;
	bReturningToCatch = false;
	CatchReturnStartedNs = 0;
	CatchReturnStartPose = 0.0;
}

FCoursePresentationSnapshot FCoursePresentationRuntime::Update(
	const FCourseTelemetryInput &Input, std::uint64_t NowMonotonicNs)
{
	if (!Input.bHasSession)
	{
		Reset();
		return Snapshot;
	}
	if (!bHasSession || !(Input.SessionId == SessionId))
	{
		Reset();
		bHasSession = true;
		SessionId = Input.SessionId;
		LastNowNs = NowMonotonicNs;
	}

	const double DeltaSeconds = static_cast<double>(ElapsedNs(NowMonotonicNs, LastNowNs)) / 1'000'000'000.0;
	LastNowNs = std::max(LastNowNs, NowMonotonicNs);

	if (Input.bHasValidSample)
	{
		Snapshot.MeasuredDistanceMm = Input.MeasuredDistanceMm;
		const bool bLive = IsLive(Input);
		if (bLive)
		{
			bCoasting = false;
			const std::uint64_t AgeNs = std::min(ElapsedNs(NowMonotonicNs, Input.SampleMonotonicNs), CoursePredictionLimitNs);
			const double Speed = static_cast<double>(Input.SpeedMmPerS.value_or(0));
			Snapshot.PredictedDistanceMm = static_cast<double>(Input.MeasuredDistanceMm) + Speed * static_cast<double>(AgeNs) / 1'000'000'000.0;
			CoastTargetMm = static_cast<double>(Input.MeasuredDistanceMm) + Speed * 0.25;
		}
		else if (bWasLive && (Input.bFrozen || Input.bStale || !Input.bConnected))
		{
			// Freeze one already-bounded coast endpoint. No later disconnected frame
			// can extend it or fabricate another metre.
			Snapshot.PredictedDistanceMm = CoastTargetMm;
			bCoasting = true;
		}
		else if (bCoasting && (Input.bFrozen || Input.bStale || !Input.bConnected))
		{
			Snapshot.PredictedDistanceMm = CoastTargetMm;
		}
		else
		{
			Snapshot.PredictedDistanceMm = static_cast<double>(Input.MeasuredDistanceMm);
			bCoasting = false;
		}

		if (!bHasDistance)
		{
			Snapshot.PresentedDistanceMm = Snapshot.PredictedDistanceMm;
			DistanceVelocityMmPerS = 0.0;
			bHasDistance = true;
		}
		else if (DeltaSeconds > 0.0)
		{
			// Exact critically damped solution for a constant target over this update.
			const double Error = Snapshot.PresentedDistanceMm - Snapshot.PredictedDistanceMm;
			const double Combined = DistanceVelocityMmPerS + SpringOmegaPerSecond * Error;
			const double Decay = std::exp(-SpringOmegaPerSecond * DeltaSeconds);
			const double NewError = (Error + Combined * DeltaSeconds) * Decay;
			DistanceVelocityMmPerS = (DistanceVelocityMmPerS - SpringOmegaPerSecond * Combined * DeltaSeconds) * Decay;
			Snapshot.PresentedDistanceMm = Snapshot.PredictedDistanceMm + NewError;
		}
		bWasLive = bLive;
	}

	const double NonNegativeDistance = std::max(0.0, Snapshot.PresentedDistanceMm);
	Snapshot.CompletedLap = static_cast<std::uint64_t>(NonNegativeDistance / static_cast<double>(CourseLengthMm));
	Snapshot.WrappedCourseDistanceMm = std::fmod(NonNegativeDistance, static_cast<double>(CourseLengthMm));
	UpdateStroke(Input, NowMonotonicNs);
	return Snapshot;
}

void FCoursePresentationRuntime::UpdateStroke(const FCourseTelemetryInput &Input,
											  std::uint64_t NowMonotonicNs)
{
	const bool bCanAnimate = IsLive(Input);
	if (!bCanAnimate)
	{
		if (!bReturningToCatch)
		{
			bReturningToCatch = true;
			CatchReturnStartedNs = NowMonotonicNs;
			CatchReturnStartPose = Snapshot.StrokePose;
		}
		const double ReturnAlpha = static_cast<double>(ElapsedNs(NowMonotonicNs, CatchReturnStartedNs)) /
								   static_cast<double>(CatchReturnDurationNs);
		SetStrokePose(CatchReturnStartPose * (1.0 - SmoothStep(ReturnAlpha)), ECourseAnimationQuality::Unavailable);
		PreviousStrokeState = ERowingStrokeState::Unknown;
		RateCycleStartedNs = 0;
		return;
	}
	bReturningToCatch = false;

	std::uint32_t DriveMs = DefaultDriveMs;
	std::uint32_t RecoveryMs = DefaultRecoveryMs;
	if (Input.DriveTimeMs && *Input.DriveTimeMs > 0)
		DriveMs = *Input.DriveTimeMs;
	if (Input.RecoveryTimeMs && *Input.RecoveryTimeMs > 0)
		RecoveryMs = *Input.RecoveryTimeMs;
	if ((!Input.DriveTimeMs || !Input.RecoveryTimeMs) && Input.StrokeRateDeciSpm && *Input.StrokeRateDeciSpm > 0)
	{
		const double CycleMs = 600'000.0 / static_cast<double>(*Input.StrokeRateDeciSpm);
		if (!Input.DriveTimeMs)
			DriveMs = static_cast<std::uint32_t>(CycleMs * 0.35);
		if (!Input.RecoveryTimeMs)
			RecoveryMs = static_cast<std::uint32_t>(CycleMs * 0.65);
	}

	if (Input.StrokeState != ERowingStrokeState::Unknown)
	{
		if (Input.StrokeState != PreviousStrokeState)
			StrokeStateStartedNs = NowMonotonicNs;
		PreviousStrokeState = Input.StrokeState;
		const double StateElapsedMs = static_cast<double>(ElapsedNs(NowMonotonicNs, StrokeStateStartedNs)) / 1'000'000.0;
		switch (Input.StrokeState)
		{
		case ERowingStrokeState::Waiting:
			SetStrokePose(0.0, ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Drive:
			SetStrokePose(StateElapsedMs / static_cast<double>(std::max(DriveMs, 1U)), ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Dwell:
			SetStrokePose(1.0, ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Recovery:
			SetStrokePose(1.0 - StateElapsedMs / static_cast<double>(std::max(RecoveryMs, 1U)), ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Unknown:
			break;
		}
		RateCycleStartedNs = 0;
		return;
	}

	PreviousStrokeState = ERowingStrokeState::Unknown;
	if (!Input.StrokeRateDeciSpm || *Input.StrokeRateDeciSpm == 0)
	{
		SetStrokePose(0.0, ECourseAnimationQuality::Unavailable);
		RateCycleStartedNs = 0;
		return;
	}
	if (RateCycleStartedNs == 0)
		RateCycleStartedNs = NowMonotonicNs;
	const double CycleMs = 600'000.0 / static_cast<double>(*Input.StrokeRateDeciSpm);
	const double ElapsedMs = static_cast<double>(ElapsedNs(NowMonotonicNs, RateCycleStartedNs)) / 1'000'000.0;
	const double PhaseMs = std::fmod(ElapsedMs, CycleMs);
	const double DrivePartMs = CycleMs * 0.35;
	const double Pose = PhaseMs < DrivePartMs ? PhaseMs / DrivePartMs : 1.0 - (PhaseMs - DrivePartMs) / (CycleMs - DrivePartMs);
	SetStrokePose(Pose, ECourseAnimationQuality::Estimated);
}

void FCoursePresentationRuntime::SetStrokePose(double Pose,
											   ECourseAnimationQuality Quality)
{
	Snapshot.StrokePose = Clamp01(Pose);
	Snapshot.SeatPose = SegmentedPose(Snapshot.StrokePose, 0.0, 0.50);
	Snapshot.TorsoPose = SegmentedPose(Snapshot.StrokePose, 0.25, 0.78);
	Snapshot.ArmsPose = SegmentedPose(Snapshot.StrokePose, 0.58, 1.0);
	Snapshot.OarPose = SmoothStep(Snapshot.StrokePose);
	Snapshot.AnimationQuality = Quality;
}
