#include "CourseRuntime/CoursePresentation.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr double Pi = 3.14159265358979323846;
	constexpr double TwoPi = 2.0 * Pi;
	constexpr double DistanceSpringOmegaPerSecond = 16.0;
	constexpr double YawResponseSeconds = 0.4;
	constexpr double LargeSeekThresholdMm = 2'500.0;
	constexpr double MaximumMotionDeltaSeconds = 0.1;
	constexpr std::uint64_t CatchReturnDurationNs = 500'000'000ULL;
	constexpr std::uint64_t CameraBlendDurationNs = 1'000'000'000ULL;
	constexpr std::uint64_t CameraToastDurationNs = 2'000'000'000ULL;
	constexpr std::uint64_t StopDwellDurationNs = 10'000'000'000ULL;
	constexpr std::uint64_t RevealLegDurationNs = 10'000'000'000ULL;
	constexpr std::uint64_t ResumeDurationNs = 2'000'000'000ULL;
	constexpr std::uint32_t DefaultDriveMs = 700;
	constexpr std::uint32_t DefaultRecoveryMs = 1300;

	double Clamp01(double Value)
	{
		return std::clamp(Value, 0.0, 1.0);
	}

	double QuinticEase(double Value)
	{
		return FBiomechanicalStrokeTrajectory::MinimumJerk(0.0, 1.0, Value, 1.0).Position;
	}

	std::uint64_t ElapsedNs(std::uint64_t Now, std::uint64_t Then)
	{
		return Now >= Then ? Now - Then : 0;
	}

	bool IsPresentationWorkoutState(const FCourseTelemetryInput &Input)
	{
		return Input.SessionState == ERowingSessionState::Active &&
			   (Input.WorkoutState == ERowingWorkoutState::Active ||
				Input.WorkoutState == ERowingWorkoutState::Resting);
	}

	bool IsFreshValid(const FCourseTelemetryInput &Input)
	{
		return Input.bHasValidSample && Input.bConnected && !Input.bFrozen && !Input.bStale &&
			   IsPresentationWorkoutState(Input) && !Input.bWorkoutCompleted && !Input.bWorkoutTerminated;
	}

	bool IsActivelyRowing(const FCourseTelemetryInput &Input)
	{
		return IsFreshValid(Input) && Input.WorkoutState == ERowingWorkoutState::Active &&
			   Input.RowingState == ERowingState::Active;
	}

	bool IsValidlyStopped(const FCourseTelemetryInput &Input)
	{
		return IsFreshValid(Input) && Input.RowingState == ERowingState::Inactive;
	}

	double ShortestAngleDelta(double From, double To)
	{
		double Delta = std::fmod(To - From + Pi, TwoPi);
		if (Delta < 0.0)
			Delta += TwoPi;
		return Delta - Pi;
	}

	FCourseVector HermitePosition(const ContentRuntime::FRouteHermiteControlPoint &Start,
								  const ContentRuntime::FRouteHermiteControlPoint &End,
								  double T)
	{
		const double T2 = T * T;
		const double T3 = T2 * T;
		const double H00 = 2.0 * T3 - 3.0 * T2 + 1.0;
		const double H10 = T3 - 2.0 * T2 + T;
		const double H01 = -2.0 * T3 + 3.0 * T2;
		const double H11 = T3 - T2;
		return {
			H00 * Start.PositionMm.X + H10 * Start.LeaveTangentMm.X + H01 * End.PositionMm.X + H11 * End.ArriveTangentMm.X,
			H00 * Start.PositionMm.Y + H10 * Start.LeaveTangentMm.Y + H01 * End.PositionMm.Y + H11 * End.ArriveTangentMm.Y,
			H00 * Start.PositionMm.Z + H10 * Start.LeaveTangentMm.Z + H01 * End.PositionMm.Z + H11 * End.ArriveTangentMm.Z};
	}

	FCourseVector HermiteTangent(const ContentRuntime::FRouteHermiteControlPoint &Start,
								 const ContentRuntime::FRouteHermiteControlPoint &End,
								 double T)
	{
		const double T2 = T * T;
		const double H00 = 6.0 * T2 - 6.0 * T;
		const double H10 = 3.0 * T2 - 4.0 * T + 1.0;
		const double H01 = -6.0 * T2 + 6.0 * T;
		const double H11 = 3.0 * T2 - 2.0 * T;
		return {
			H00 * Start.PositionMm.X + H10 * Start.LeaveTangentMm.X + H01 * End.PositionMm.X + H11 * End.ArriveTangentMm.X,
			H00 * Start.PositionMm.Y + H10 * Start.LeaveTangentMm.Y + H01 * End.PositionMm.Y + H11 * End.ArriveTangentMm.Y,
			H00 * Start.PositionMm.Z + H10 * Start.LeaveTangentMm.Z + H01 * End.PositionMm.Z + H11 * End.ArriveTangentMm.Z};
	}

	FCourseCameraParameters Lerp(const FCourseCameraParameters &A,
								 const FCourseCameraParameters &B,
								 double Alpha)
	{
		const double T = QuinticEase(Alpha);
		auto Blend = [T](double From, double To)
		{ return From + (To - From) * T; };
		return {Blend(A.AftMm, B.AftMm), Blend(A.StarboardMm, B.StarboardMm), Blend(A.HeightMm, B.HeightMm), Blend(A.LookAheadMm, B.LookAheadMm), Blend(A.FieldOfViewDegrees, B.FieldOfViewDegrees)};
	}

	FCourseCameraParameters RevealParameters()
	{
		return {32'000.0, 18'000.0, 14'000.0, 35'000.0, 92.0};
	}

	double NormalizedPathDistance(const ContentRuntime::FRouteDefinition &Route, double DistanceMm)
	{
		if (!Route.bClosed)
			return std::clamp(DistanceMm, 0.0, static_cast<double>(Route.LengthMm));
		const double Length = static_cast<double>(Route.LengthMm);
		double Wrapped = std::fmod(DistanceMm, Length);
		return Wrapped < 0.0 ? Wrapped + Length : Wrapped;
	}

	double SignedHorizontalCurvature(const ContentRuntime::FRouteDefinition &Route, double DistanceMm)
	{
		constexpr double SampleHalfWindowMm = 1'000.0;
		const FCoursePathSample Before = EvaluateCoursePath(Route, NormalizedPathDistance(Route, DistanceMm - SampleHalfWindowMm));
		const FCoursePathSample After = EvaluateCoursePath(Route, NormalizedPathDistance(Route, DistanceMm + SampleHalfWindowMm));
		const double Dot = Before.UnitTangent.X * After.UnitTangent.X + Before.UnitTangent.Y * After.UnitTangent.Y;
		const double Cross = Before.UnitTangent.X * After.UnitTangent.Y - Before.UnitTangent.Y * After.UnitTangent.X;
		const double TraversedMm = Route.bClosed || (DistanceMm >= SampleHalfWindowMm && DistanceMm + SampleHalfWindowMm <= Route.LengthMm)
									   ? 2.0 * SampleHalfWindowMm
									   : SampleHalfWindowMm;
		return std::atan2(Cross, Dot) / TraversedMm;
	}
} // namespace

FCourseCameraParameters CourseCameraPresetParameters(ECourseCameraPreset Preset) noexcept
{
	switch (Preset)
	{
	case ECourseCameraPreset::Close:
		return {8'000.0, 4'000.0, 1'600.0, 8'000.0, 55.0};
	case ECourseCameraPreset::Wide:
		return {18'000.0, 12'000.0, 5'000.0, 14'000.0, 68.0};
	case ECourseCameraPreset::Medium:
	default:
		return {12'000.0, 8'000.0, 2'200.0, 10'000.0, 60.0};
	}
}

const char *CourseCameraPresetName(ECourseCameraPreset Preset) noexcept
{
	switch (Preset)
	{
	case ECourseCameraPreset::Close:
		return "Close";
	case ECourseCameraPreset::Wide:
		return "Wide";
	case ECourseCameraPreset::Medium:
	default:
		return "Medium";
	}
}

FCoursePathSample EvaluateCoursePath(const ContentRuntime::FRouteDefinition &Route,
									 double RouteDistanceMm)
{
	if (!Route.PresentationPath || Route.PresentationPath->ControlPoints.size() < 2 ||
		Route.PresentationPath->ArcLengthLookup.size() < 2)
		throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::InvalidRoute,
													  "course path is unavailable");
	const ContentRuntime::FRoutePresentationPath &Path = *Route.PresentationPath;
	const double Distance = std::clamp(RouteDistanceMm, 0.0, static_cast<double>(Route.LengthMm));
	const auto Upper = std::lower_bound(Path.ArcLengthLookup.begin(), Path.ArcLengthLookup.end(), Distance, [](const ContentRuntime::FRouteArcLengthLookupEntry &Entry, double Value)
										{ return static_cast<double>(Entry.RouteDistanceMm) < Value; });
	const auto &After = Upper == Path.ArcLengthLookup.end() ? Path.ArcLengthLookup.back() : *Upper;
	const auto &Before = Upper == Path.ArcLengthLookup.begin() ? *Upper : *(Upper - 1);
	const double Denominator = static_cast<double>(After.RouteDistanceMm - Before.RouteDistanceMm);
	const double Alpha = Denominator > 0.0 ? (Distance - static_cast<double>(Before.RouteDistanceMm)) / Denominator : 0.0;
	const double BeforeGlobal = static_cast<double>(Before.SegmentIndex) + static_cast<double>(Before.SegmentParameterPpm) / 1'000'000.0;
	const double AfterGlobal = static_cast<double>(After.SegmentIndex) + static_cast<double>(After.SegmentParameterPpm) / 1'000'000.0;
	const double Global = BeforeGlobal + (AfterGlobal - BeforeGlobal) * Alpha;
	const std::size_t Segment = std::min(static_cast<std::size_t>(std::floor(Global)), Path.ControlPoints.size() - 2);
	const double T = Segment + 1 == Path.ControlPoints.size() - 1 && Global >= static_cast<double>(Path.ControlPoints.size() - 1)
						 ? 1.0
						 : std::clamp(Global - static_cast<double>(Segment), 0.0, 1.0);
	FCourseVector Position = HermitePosition(Path.ControlPoints[Segment], Path.ControlPoints[Segment + 1], T);
	FCourseVector Tangent = HermiteTangent(Path.ControlPoints[Segment], Path.ControlPoints[Segment + 1], T);
	const double FrameYaw = static_cast<double>(Path.RouteLocalYawMicroradians) / 1'000'000.0;
	const double CosYaw = std::cos(FrameYaw);
	const double SinYaw = std::sin(FrameYaw);
	const auto Rotate = [CosYaw, SinYaw](FCourseVector Value)
	{
		return FCourseVector{Value.X * CosYaw - Value.Y * SinYaw,
							 Value.X * SinYaw + Value.Y * CosYaw,
							 Value.Z};
	};
	Position = Rotate(Position);
	Position.X += Path.RouteLocalOriginMm.X;
	Position.Y += Path.RouteLocalOriginMm.Y;
	Position.Z += Path.RouteLocalOriginMm.Z;
	Tangent = Rotate(Tangent);
	const double TangentLength = std::hypot(std::hypot(Tangent.X, Tangent.Y), Tangent.Z);
	if (TangentLength < 1e-9)
		throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::InvalidRoute,
													  "course path tangent is zero");
	Tangent.X /= TangentLength;
	Tangent.Y /= TangentLength;
	Tangent.Z /= TangentLength;
	return {Position, Tangent};
}

void FCoursePresentationRuntime::ResetSessionState() noexcept
{
	Snapshot = {};
	Snapshot.RouteId = Route.RouteId;
	Snapshot.CameraPreset = SelectedPreset;
	Snapshot.CameraParameters = CameraParameters;
	Snapshot.bReducedMotion = bReducedMotion;
	SessionId = {};
	bHasSession = false;
	bHasDistance = false;
	bWasLive = false;
	bCoasting = false;
	bHasCourseYaw = false;
	bFirstStrokeObserved = false;
	bRevealSuppressed = false;
	bWasFreshValid = false;
	LastNowNs = 0;
	LastAcceptedSampleGeneration = 0;
	RevealPhaseStartedNs = 0;
	LastAcceptedMeasuredDistanceMm = 0.0;
	DistanceVelocityMmPerS = 0.0;
	CoastTargetMm = 0.0;
	CourseYawRadians = 0.0;
	CosmeticRollTargetDegrees = 0.0;
	CosmeticPitchTargetDegrees = 0.0;
	LastCosmeticSpeedMmPerS = 0.0;
	LastCosmeticSpeedSampleNs = 0;
	bHasCosmeticSpeed = false;
	RevealPhase = ECourseRevealPhase::Chase;
	CameraParameters = CourseCameraPresetParameters(SelectedPreset);
	CameraBlendStartParameters = CameraParameters;
	ResumeStartParameters = CameraParameters;
	CameraBlendStartedNs = 0;
	Toast.clear();
	ToastExpiresNs = 0;
	PreviousStrokeState = ERowingStrokeState::Unknown;
	StrokeStateStartedNs = 0;
	RateCycleStartedNs = 0;
	LatchedDriveMs = DefaultDriveMs;
	LatchedRecoveryMs = DefaultRecoveryMs;
	bReturningToCatch = false;
	bReconnectingToMeasuredPose = false;
	bHadLiveAnimation = false;
	CatchReturnStartedNs = 0;
	BridgeStartState = {};
	CurrentStrokeState = {};
}

void FCoursePresentationRuntime::Reset() noexcept
{
	ResetSessionState();
}

void FCoursePresentationRuntime::SelectRoute(const ContentRuntime::FRouteDefinition &InRoute)
{
	const bool bBuiltInStandard = InRoute.RouteId == "route.standard.2k" &&
								  InRoute.SchemaVersion == ContentRuntime::RouteDefinitionSchemaV1 && InRoute.bClosed;
	if (InRoute.RouteId.empty() || InRoute.LengthMm == 0 || !InRoute.PresentationPath ||
		(!bBuiltInStandard && InRoute.SchemaVersion != ContentRuntime::RouteDefinitionSchemaV2))
		throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::InvalidRoute,
													  "course runtime requires a compatible deterministic route path");
	Route = InRoute;
	ResetSessionState();
}

void FCoursePresentationRuntime::SetReducedMotion(bool bEnabled, std::uint64_t NowMonotonicNs) noexcept
{
	if (bReducedMotion == bEnabled)
		return;
	bReducedMotion = bEnabled;
	Snapshot.bReducedMotion = bEnabled;
	Snapshot.HullRollDegrees = 0.0;
	Snapshot.HullPitchDegrees = 0.0;
	Snapshot.AmbientBobMm = 0.0;
	Snapshot.StrokeHeaveMm = 0.0;
	if (RevealPhase != ECourseRevealPhase::Chase && RevealPhase != ECourseRevealPhase::Dwell)
		EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
}

void FCoursePresentationRuntime::SetCameraPreset(ECourseCameraPreset Preset,
												 std::uint64_t NowMonotonicNs) noexcept
{
	if (Preset != ECourseCameraPreset::Close && Preset != ECourseCameraPreset::Medium && Preset != ECourseCameraPreset::Wide)
		Preset = ECourseCameraPreset::Medium;
	SelectedPreset = Preset;
	Snapshot.CameraPreset = SelectedPreset;
	if (NowMonotonicNs != 0)
	{
		Toast = std::string("Camera: ") + CourseCameraPresetName(Preset);
		ToastExpiresNs = NowMonotonicNs + CameraToastDurationNs;
		Snapshot.CameraToast = Toast;
		Snapshot.CameraToastExpiresNs = ToastExpiresNs;
	}
	if (RevealPhase == ECourseRevealPhase::Chase || RevealPhase == ECourseRevealPhase::Dwell)
	{
		CameraBlendStartParameters = CameraParameters;
		CameraBlendStartedNs = NowMonotonicNs;
	}
}

void FCoursePresentationRuntime::CycleCameraPreset(std::uint64_t NowMonotonicNs) noexcept
{
	ECourseCameraPreset Next = ECourseCameraPreset::Close;
	if (SelectedPreset == ECourseCameraPreset::Close)
		Next = ECourseCameraPreset::Medium;
	else if (SelectedPreset == ECourseCameraPreset::Medium)
		Next = ECourseCameraPreset::Wide;
	SetCameraPreset(Next, NowMonotonicNs);
}

FCoursePresentationSnapshot FCoursePresentationRuntime::Update(
	const FCourseTelemetryInput &Input, std::uint64_t NowMonotonicNs)
{
	if (!Input.bHasSession)
	{
		ResetSessionState();
		return Snapshot;
	}
	if (!bHasSession || !(Input.SessionId == SessionId))
	{
		ResetSessionState();
		bHasSession = true;
		SessionId = Input.SessionId;
		LastNowNs = NowMonotonicNs;
	}

	const double DeltaSeconds = std::min(MaximumMotionDeltaSeconds,
										 static_cast<double>(ElapsedNs(NowMonotonicNs, LastNowNs)) / 1'000'000'000.0);
	LastNowNs = std::max(LastNowNs, NowMonotonicNs);
	const bool bFreshSample = Input.bHasValidSample &&
							  (!bHasDistance || Input.AcceptedSampleGeneration != LastAcceptedSampleGeneration);
	const bool bLargeSeek = bFreshSample && bHasDistance &&
							std::abs(static_cast<double>(Input.MeasuredDistanceMm) - LastAcceptedMeasuredDistanceMm) > LargeSeekThresholdMm;

	if (Input.bHasValidSample)
	{
		Snapshot.MeasuredDistanceMm = Input.MeasuredDistanceMm;
		const bool bLive = IsFreshValid(Input);
		if (bLive)
		{
			const std::uint64_t AgeNs = std::min(ElapsedNs(NowMonotonicNs, Input.SampleMonotonicNs), CoursePredictionLimitNs);
			const double Speed = static_cast<double>(Input.SpeedMmPerS.value_or(0));
			Snapshot.PredictedDistanceMm = static_cast<double>(Input.MeasuredDistanceMm) + Speed * static_cast<double>(AgeNs) / 1'000'000'000.0;
		}
		else
		{
			// Invalid, paused, disconnected, completed, and terminated inputs hold the
			// last PM-backed position. Presentation never catches up through a gap.
			Snapshot.PredictedDistanceMm = static_cast<double>(Input.MeasuredDistanceMm);
		}
		if (!bHasDistance || bLargeSeek || !bLive)
		{
			Snapshot.PresentedDistanceMm = Snapshot.PredictedDistanceMm;
			DistanceVelocityMmPerS = 0.0;
			bHasDistance = true;
		}
		else if (DeltaSeconds > 0.0)
		{
			const double Error = Snapshot.PresentedDistanceMm - Snapshot.PredictedDistanceMm;
			const double Combined = DistanceVelocityMmPerS + DistanceSpringOmegaPerSecond * Error;
			const double Decay = std::exp(-DistanceSpringOmegaPerSecond * DeltaSeconds);
			const double NewError = (Error + Combined * DeltaSeconds) * Decay;
			DistanceVelocityMmPerS = (DistanceVelocityMmPerS - DistanceSpringOmegaPerSecond * Combined * DeltaSeconds) * Decay;
			Snapshot.PresentedDistanceMm = Snapshot.PredictedDistanceMm + NewError;
		}
		bWasLive = bLive;
	}

	const double NonNegativeDistance = std::max(0.0, Snapshot.PresentedDistanceMm);
	const double RouteLength = static_cast<double>(Route.LengthMm);
	if (Route.bClosed)
	{
		Snapshot.CompletedLap = static_cast<std::uint64_t>(NonNegativeDistance / RouteLength);
		Snapshot.WrappedCourseDistanceMm = std::fmod(NonNegativeDistance, RouteLength);
		Snapshot.bRouteComplete = false;
	}
	else
	{
		Snapshot.CompletedLap = 0;
		Snapshot.WrappedCourseDistanceMm = std::min(NonNegativeDistance, RouteLength);
		Snapshot.bRouteComplete = Snapshot.MeasuredDistanceMm >= Route.LengthMm;
	}
	if (bFreshSample)
	{
		LastAcceptedSampleGeneration = Input.AcceptedSampleGeneration;
		LastAcceptedMeasuredDistanceMm = static_cast<double>(Input.MeasuredDistanceMm);
	}
	UpdateStroke(Input, NowMonotonicNs);
	Snapshot.bDiscontinuityReset = bLargeSeek;
	UpdatePathAndMotion(Input, bFreshSample || bLargeSeek, DeltaSeconds, NowMonotonicNs);
	UpdateCamera(Input, bFreshSample, NowMonotonicNs);
	return Snapshot;
}

void FCoursePresentationRuntime::UpdatePathAndMotion(const FCourseTelemetryInput &Input,
													 bool bFreshSample,
													 double DeltaSeconds,
													 std::uint64_t NowMonotonicNs)
{
	const FCoursePathSample Path = EvaluateCoursePath(Route, Snapshot.WrappedCourseDistanceMm);
	Snapshot.PathPositionMm = Path.PositionMm;
	Snapshot.PathUnitTangent = Path.UnitTangent;
	const double TargetYaw = std::atan2(Path.UnitTangent.Y, Path.UnitTangent.X);
	const bool bAcceptedSeek = bFreshSample && Snapshot.bDiscontinuityReset;
	if (!bHasCourseYaw || bAcceptedSeek)
	{
		CourseYawRadians = TargetYaw;
		bHasCourseYaw = true;
	}
	else if (DeltaSeconds > 0.0)
	{
		const double Alpha = 1.0 - std::exp(-DeltaSeconds / YawResponseSeconds);
		CourseYawRadians += ShortestAngleDelta(CourseYawRadians, TargetYaw) * Alpha;
	}
	Snapshot.DampedYawRadians = CourseYawRadians;

	const bool bCosmeticActive = IsActivelyRowing(Input) && !bReducedMotion && !Snapshot.bRouteComplete;
	const double SettleAlpha = DeltaSeconds > 0.0 ? 1.0 - std::exp(-DeltaSeconds / 0.25) : 0.0;
	const double PhaseSeconds = static_cast<double>(NowMonotonicNs % 20'000'000'000ULL) / 1'000'000'000.0;
	if (bFreshSample && Input.SpeedMmPerS)
	{
		const double SpeedMmPerS = static_cast<double>(*Input.SpeedMmPerS);
		const std::uint64_t SpeedSampleNs = Input.SampleMonotonicNs == 0 ? NowMonotonicNs : Input.SampleMonotonicNs;
		if (bCosmeticActive)
		{
			const double Curvature = SignedHorizontalCurvature(Route, Snapshot.WrappedCourseDistanceMm);
			// Bank derives from the signed geometric curvature and PM-backed speed;
			// 9,810 mm/s^2 is gravity in the same units as telemetry.
			CosmeticRollTargetDegrees = std::atan(SpeedMmPerS * SpeedMmPerS * Curvature / 9'810.0) * 180.0 / Pi;
			if (bHasCosmeticSpeed && SpeedSampleNs > LastCosmeticSpeedSampleNs)
			{
				const double Seconds = static_cast<double>(SpeedSampleNs - LastCosmeticSpeedSampleNs) / 1'000'000'000.0;
				const double AccelerationMmPerS2 = (SpeedMmPerS - LastCosmeticSpeedMmPerS) / Seconds;
				CosmeticPitchTargetDegrees = 0.25 * std::atan(AccelerationMmPerS2 / 9'810.0) * 180.0 / Pi;
			}
			else
				CosmeticPitchTargetDegrees = 0.0;
		}
		LastCosmeticSpeedMmPerS = SpeedMmPerS;
		LastCosmeticSpeedSampleNs = SpeedSampleNs;
		bHasCosmeticSpeed = true;
	}
	if (!bCosmeticActive)
	{
		CosmeticRollTargetDegrees = 0.0;
		CosmeticPitchTargetDegrees = 0.0;
	}
	const double RollTarget = bCosmeticActive ? CosmeticRollTargetDegrees : 0.0;
	const double PitchTarget = bCosmeticActive ? CosmeticPitchTargetDegrees : 0.0;
	const double BobTarget = bCosmeticActive ? 20.0 * std::sin(PhaseSeconds * TwoPi / 5.0) : 0.0;
	const double HeaveTarget = bCosmeticActive ? 10.0 * std::sin(Pi * Snapshot.StrokePose) : 0.0;
	Snapshot.HullRollDegrees += (RollTarget - Snapshot.HullRollDegrees) * SettleAlpha;
	Snapshot.HullPitchDegrees += (PitchTarget - Snapshot.HullPitchDegrees) * SettleAlpha;
	Snapshot.AmbientBobMm += (BobTarget - Snapshot.AmbientBobMm) * SettleAlpha;
	Snapshot.StrokeHeaveMm += (HeaveTarget - Snapshot.StrokeHeaveMm) * SettleAlpha;
	Snapshot.HullRollDegrees = std::clamp(Snapshot.HullRollDegrees, -1.5, 1.5);
	Snapshot.HullPitchDegrees = std::clamp(Snapshot.HullPitchDegrees, -0.8, 0.8);
	Snapshot.AmbientBobMm = std::clamp(Snapshot.AmbientBobMm, -20.0, 20.0);
	Snapshot.StrokeHeaveMm = std::clamp(Snapshot.StrokeHeaveMm, -10.0, 10.0);
	if (bReducedMotion)
	{
		Snapshot.HullRollDegrees = 0.0;
		Snapshot.HullPitchDegrees = 0.0;
		Snapshot.AmbientBobMm = 0.0;
		Snapshot.StrokeHeaveMm = 0.0;
	}
}

void FCoursePresentationRuntime::EnterRevealPhase(ECourseRevealPhase Phase,
												  std::uint64_t NowMonotonicNs) noexcept
{
	RevealPhase = Phase;
	RevealPhaseStartedNs = NowMonotonicNs;
}

void FCoursePresentationRuntime::UpdateCamera(const FCourseTelemetryInput &Input,
											  bool bFreshSample,
											  std::uint64_t NowMonotonicNs)
{
	const bool bValid = IsFreshValid(Input) && !Snapshot.bRouteComplete;
	const bool bActive = IsActivelyRowing(Input);
	const bool bStopped = IsValidlyStopped(Input) && !Snapshot.bRouteComplete;
	if (bFreshSample && bActive)
		bFirstStrokeObserved = true;

	if (Input.bWorkoutTerminated)
	{
		bFirstStrokeObserved = false;
		bRevealSuppressed = true;
		EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
	}
	else if (Input.bWorkoutCompleted || Snapshot.bRouteComplete)
	{
		bRevealSuppressed = true;
		EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
	}
	else if (bFreshSample && bActive && RevealPhase != ECourseRevealPhase::Chase && RevealPhase != ECourseRevealPhase::Resume)
	{
		ResumeStartParameters = CameraParameters;
		EnterRevealPhase(ECourseRevealPhase::Resume, NowMonotonicNs);
	}
	else if (!bValid)
	{
		// A gap, pause, reconnect, or stale sample cancels every reveal phase,
		// including Resume. Never carry camera travel across invalid PM input.
		EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
	}
	else if (!bWasFreshValid && bValid && RevealPhase == ECourseRevealPhase::Dwell)
	{
		EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
	}

	const FCourseCameraParameters Chase = CourseCameraPresetParameters(SelectedPreset);
	const FCourseCameraParameters Reveal = RevealParameters();
	const double PhaseAlpha = [&]
	{
		const std::uint64_t Duration = RevealPhase == ECourseRevealPhase::Resume ? ResumeDurationNs : RevealLegDurationNs;
		return static_cast<double>(ElapsedNs(NowMonotonicNs, RevealPhaseStartedNs)) / static_cast<double>(Duration);
	}();

	switch (RevealPhase)
	{
	case ECourseRevealPhase::Chase:
		if (CameraBlendStartedNs != 0 && ElapsedNs(NowMonotonicNs, CameraBlendStartedNs) < CameraBlendDurationNs)
			CameraParameters = Lerp(CameraBlendStartParameters, Chase, static_cast<double>(ElapsedNs(NowMonotonicNs, CameraBlendStartedNs)) / static_cast<double>(CameraBlendDurationNs));
		else
			CameraParameters = Chase;
		if (bFirstStrokeObserved && bFreshSample && bStopped && !bRevealSuppressed)
			EnterRevealPhase(ECourseRevealPhase::Dwell, NowMonotonicNs);
		break;
	case ECourseRevealPhase::Dwell:
		CameraParameters = Chase;
		if (!bStopped || !bValid || bRevealSuppressed)
			EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
		else if (ElapsedNs(NowMonotonicNs, RevealPhaseStartedNs) >= StopDwellDurationNs)
		{
			if (bReducedMotion)
			{
				CameraParameters = Reveal;
				EnterRevealPhase(ECourseRevealPhase::Hold, NowMonotonicNs);
			}
			else
				EnterRevealPhase(ECourseRevealPhase::Outbound, NowMonotonicNs);
		}
		break;
	case ECourseRevealPhase::Outbound:
		CameraParameters = Lerp(Chase, Reveal, PhaseAlpha);
		if (PhaseAlpha >= 1.0)
			EnterRevealPhase(ECourseRevealPhase::Hold, NowMonotonicNs);
		break;
	case ECourseRevealPhase::Hold:
		CameraParameters = Reveal;
		if (bReducedMotion)
		{
			if (!bStopped || !bValid || bRevealSuppressed)
				EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
		}
		else if (PhaseAlpha >= 1.0)
			EnterRevealPhase(ECourseRevealPhase::Returning, NowMonotonicNs);
		break;
	case ECourseRevealPhase::Returning:
		CameraParameters = Lerp(Reveal, Chase, PhaseAlpha);
		if (PhaseAlpha >= 1.0)
		{
			CameraParameters = Chase;
			if (!bRevealSuppressed && bStopped && bValid)
				EnterRevealPhase(ECourseRevealPhase::Outbound, NowMonotonicNs);
			else
			{
				EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
			}
		}
		break;
	case ECourseRevealPhase::Resume:
		CameraParameters = Lerp(ResumeStartParameters, Chase, PhaseAlpha);
		if (PhaseAlpha >= 1.0)
			EnterRevealPhase(ECourseRevealPhase::Chase, NowMonotonicNs);
		break;
	}

	bWasFreshValid = bValid;
	Snapshot.CameraPreset = SelectedPreset;
	Snapshot.CameraParameters = CameraParameters;
	Snapshot.RevealPhase = RevealPhase;
	Snapshot.bFirstStrokeObserved = bFirstStrokeObserved;
	Snapshot.bReducedMotion = bReducedMotion;
	Snapshot.CameraToast = NowMonotonicNs < ToastExpiresNs ? Toast : std::string();
	Snapshot.CameraToastExpiresNs = ToastExpiresNs;

	const double ForwardX = std::cos(CourseYawRadians);
	const double ForwardY = std::sin(CourseYawRadians);
	const double StarboardX = -ForwardY;
	const double StarboardY = ForwardX;
	Snapshot.CameraPose.PositionMm = {
		Snapshot.PathPositionMm.X - ForwardX * CameraParameters.AftMm + StarboardX * CameraParameters.StarboardMm,
		Snapshot.PathPositionMm.Y - ForwardY * CameraParameters.AftMm + StarboardY * CameraParameters.StarboardMm,
		Snapshot.PathPositionMm.Z + CameraParameters.HeightMm};
	Snapshot.CameraPose.LookAtMm = {
		Snapshot.PathPositionMm.X + ForwardX * CameraParameters.LookAheadMm,
		Snapshot.PathPositionMm.Y + ForwardY * CameraParameters.LookAheadMm,
		Snapshot.PathPositionMm.Z};
	Snapshot.CameraPose.FieldOfViewDegrees = CameraParameters.FieldOfViewDegrees;
}

void FCoursePresentationRuntime::UpdateStroke(const FCourseTelemetryInput &Input,
											  std::uint64_t NowMonotonicNs)
{
	const bool bCanAnimate = IsActivelyRowing(Input);
	if (!bCanAnimate)
	{
		if (!bReturningToCatch)
		{
			bReturningToCatch = true;
			bReconnectingToMeasuredPose = false;
			CatchReturnStartedNs = NowMonotonicNs;
			BridgeStartState = CurrentStrokeState;
		}
		const double DurationSeconds = static_cast<double>(CatchReturnDurationNs) / 1'000'000'000.0;
		const double ReturnAlpha = static_cast<double>(ElapsedNs(NowMonotonicNs, CatchReturnStartedNs)) /
								   static_cast<double>(CatchReturnDurationNs);
		FBiomechanicalStrokeState ReturnState;
		ReturnState.Stroke = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Stroke, {}, ReturnAlpha, DurationSeconds);
		ReturnState.Seat = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Seat, {}, ReturnAlpha, DurationSeconds);
		ReturnState.Torso = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Torso, {}, ReturnAlpha, DurationSeconds);
		ReturnState.Arms = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Arms, {}, ReturnAlpha, DurationSeconds);
		ReturnState.Oar = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Oar, {}, ReturnAlpha, DurationSeconds);
		ApplyStrokeState(ReturnState, ECourseAnimationQuality::Unavailable);
		PreviousStrokeState = ERowingStrokeState::Unknown;
		RateCycleStartedNs = 0;
		return;
	}

	std::uint32_t DriveMs = Input.DriveTimeMs.value_or(DefaultDriveMs);
	std::uint32_t RecoveryMs = Input.RecoveryTimeMs.value_or(DefaultRecoveryMs);
	if ((!Input.DriveTimeMs || !Input.RecoveryTimeMs) && Input.StrokeRateDeciSpm && *Input.StrokeRateDeciSpm > 0)
	{
		const double CycleMs = 600'000.0 / static_cast<double>(*Input.StrokeRateDeciSpm);
		if (!Input.DriveTimeMs)
			DriveMs = static_cast<std::uint32_t>(CycleMs * 0.35);
		if (!Input.RecoveryTimeMs)
			RecoveryMs = static_cast<std::uint32_t>(CycleMs * 0.65);
	}
	DriveMs = std::max(DriveMs, 1U);
	RecoveryMs = std::max(RecoveryMs, 1U);

	if (bReturningToCatch && bHadLiveAnimation)
	{
		bReturningToCatch = false;
		bReconnectingToMeasuredPose = true;
		CatchReturnStartedNs = NowMonotonicNs;
		BridgeStartState = CurrentStrokeState;
		LatchedDriveMs = DriveMs;
		LatchedRecoveryMs = RecoveryMs;
	}
	else
		bReturningToCatch = false;

	if (bReconnectingToMeasuredPose)
	{
		const bool bFinishEndpoint = Input.StrokeState == ERowingStrokeState::Dwell ||
									 Input.StrokeState == ERowingStrokeState::Recovery;
		const EBiomechanicalStrokePhase TargetPhase = bFinishEndpoint
														  ? EBiomechanicalStrokePhase::Recovery
														  : EBiomechanicalStrokePhase::Drive;
		FBiomechanicalStrokeState Target = FBiomechanicalStrokeTrajectory::Evaluate(
			bFinishEndpoint ? 1.0 : 0.0,
			TargetPhase,
			static_cast<double>(bFinishEndpoint ? LatchedRecoveryMs : LatchedDriveMs) / 1000.0);
		auto Stop = [](FQuinticKinematicState State)
		{
			State.Velocity = 0.0;
			State.Acceleration = 0.0;
			return State;
		};
		Target.Stroke = Stop(Target.Stroke);
		Target.Seat = Stop(Target.Seat);
		Target.Torso = Stop(Target.Torso);
		Target.Arms = Stop(Target.Arms);
		Target.Oar = Stop(Target.Oar);
		const double DurationSeconds = static_cast<double>(CatchReturnDurationNs) / 1'000'000'000.0;
		const double Alpha = static_cast<double>(ElapsedNs(NowMonotonicNs, CatchReturnStartedNs)) /
							 static_cast<double>(CatchReturnDurationNs);
		FBiomechanicalStrokeState Bridged;
		Bridged.Stroke = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Stroke, Target.Stroke, Alpha, DurationSeconds);
		Bridged.Seat = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Seat, Target.Seat, Alpha, DurationSeconds);
		Bridged.Torso = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Torso, Target.Torso, Alpha, DurationSeconds);
		Bridged.Arms = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Arms, Target.Arms, Alpha, DurationSeconds);
		Bridged.Oar = FBiomechanicalStrokeTrajectory::QuinticHermite(BridgeStartState.Oar, Target.Oar, Alpha, DurationSeconds);
		ApplyStrokeState(Bridged, Input.StrokeState == ERowingStrokeState::Unknown ? ECourseAnimationQuality::Estimated : ECourseAnimationQuality::Primary);
		if (Alpha < 1.0)
			return;
		bReconnectingToMeasuredPose = false;
		PreviousStrokeState = Input.StrokeState;
		StrokeStateStartedNs = NowMonotonicNs;
		RateCycleStartedNs = Input.StrokeState == ERowingStrokeState::Unknown ? NowMonotonicNs : 0;
	}
	bHadLiveAnimation = true;

	if (Input.StrokeState != ERowingStrokeState::Unknown)
	{
		if (Input.StrokeState != PreviousStrokeState)
		{
			StrokeStateStartedNs = NowMonotonicNs;
			if (Input.StrokeState == ERowingStrokeState::Drive)
				LatchedDriveMs = DriveMs;
			else if (Input.StrokeState == ERowingStrokeState::Recovery)
				LatchedRecoveryMs = RecoveryMs;
		}
		PreviousStrokeState = Input.StrokeState;
		const double StateElapsedMs = static_cast<double>(ElapsedNs(NowMonotonicNs, StrokeStateStartedNs)) / 1'000'000.0;
		switch (Input.StrokeState)
		{
		case ERowingStrokeState::Waiting:
			SetStrokePose(0.0, EBiomechanicalStrokePhase::Drive, static_cast<double>(LatchedDriveMs) / 1000.0, ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Drive:
			SetStrokePose(StateElapsedMs / static_cast<double>(LatchedDriveMs),
						  EBiomechanicalStrokePhase::Drive,
						  static_cast<double>(LatchedDriveMs) / 1000.0,
						  ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Dwell:
			SetStrokePose(1.0, EBiomechanicalStrokePhase::Drive, static_cast<double>(LatchedDriveMs) / 1000.0, ECourseAnimationQuality::Primary);
			break;
		case ERowingStrokeState::Recovery:
			SetStrokePose(1.0 - StateElapsedMs / static_cast<double>(LatchedRecoveryMs),
						  EBiomechanicalStrokePhase::Recovery,
						  static_cast<double>(LatchedRecoveryMs) / 1000.0,
						  ECourseAnimationQuality::Primary);
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
		SetStrokePose(0.0, EBiomechanicalStrokePhase::Drive, static_cast<double>(LatchedDriveMs) / 1000.0, ECourseAnimationQuality::Unavailable);
		RateCycleStartedNs = 0;
		return;
	}
	if (RateCycleStartedNs == 0)
	{
		RateCycleStartedNs = NowMonotonicNs;
		LatchedDriveMs = DriveMs;
		LatchedRecoveryMs = RecoveryMs;
	}
	double CycleMs = static_cast<double>(LatchedDriveMs + LatchedRecoveryMs);
	std::uint64_t ElapsedCycleNs = ElapsedNs(NowMonotonicNs, RateCycleStartedNs);
	if (ElapsedCycleNs >= static_cast<std::uint64_t>(CycleMs * 1'000'000.0))
	{
		const std::uint64_t CompletedCycles = ElapsedCycleNs /
											  static_cast<std::uint64_t>(CycleMs * 1'000'000.0);
		RateCycleStartedNs += CompletedCycles * static_cast<std::uint64_t>(CycleMs * 1'000'000.0);
		LatchedDriveMs = DriveMs;
		LatchedRecoveryMs = RecoveryMs;
		CycleMs = static_cast<double>(LatchedDriveMs + LatchedRecoveryMs);
	}
	const double ElapsedMs = static_cast<double>(ElapsedNs(NowMonotonicNs, RateCycleStartedNs)) / 1'000'000.0;
	const double PhaseMs = std::fmod(ElapsedMs, CycleMs);
	if (PhaseMs < static_cast<double>(LatchedDriveMs))
		SetStrokePose(PhaseMs / static_cast<double>(LatchedDriveMs),
					  EBiomechanicalStrokePhase::Drive,
					  static_cast<double>(LatchedDriveMs) / 1000.0,
					  ECourseAnimationQuality::Estimated);
	else
		SetStrokePose(1.0 - (PhaseMs - static_cast<double>(LatchedDriveMs)) /
								static_cast<double>(LatchedRecoveryMs),
					  EBiomechanicalStrokePhase::Recovery,
					  static_cast<double>(LatchedRecoveryMs) / 1000.0,
					  ECourseAnimationQuality::Estimated);
}

void FCoursePresentationRuntime::SetStrokePose(double Pose,
											   EBiomechanicalStrokePhase Phase,
											   double PhaseDurationSeconds,
											   ECourseAnimationQuality Quality) noexcept
{
	ApplyStrokeState(FBiomechanicalStrokeTrajectory::Evaluate(
						 Clamp01(Pose), Phase, PhaseDurationSeconds),
					 Quality);
}

void FCoursePresentationRuntime::ApplyStrokeState(const FBiomechanicalStrokeState &State,
												  ECourseAnimationQuality Quality) noexcept
{
	CurrentStrokeState = State;
	Snapshot.StrokePose = Clamp01(State.Stroke.Position);
	Snapshot.SeatPose = Clamp01(State.Seat.Position);
	Snapshot.TorsoPose = Clamp01(State.Torso.Position);
	Snapshot.ArmsPose = Clamp01(State.Arms.Position);
	Snapshot.OarPose = Clamp01(State.Oar.Position);
	Snapshot.AnimationQuality = Quality;
}
