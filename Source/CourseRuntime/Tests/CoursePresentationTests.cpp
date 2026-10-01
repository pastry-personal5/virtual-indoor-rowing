#include "CourseRuntime/CoursePresentation.h"

#include <cassert>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	constexpr std::uint64_t Second = 1'000'000'000ULL;

	FRowingSessionId MakeId(std::uint8_t Seed)
	{
		FRowingSessionId::FBytes Bytes{};
		Bytes.fill(Seed);
		return FRowingSessionId::FromBytes(Bytes);
	}

	FCourseTelemetryInput ActiveInput(std::uint64_t DistanceMm = 0, std::uint64_t Generation = 1)
	{
		FCourseTelemetryInput Input;
		Input.SessionId = MakeId(1);
		Input.bHasSession = true;
		Input.bHasValidSample = true;
		Input.AcceptedSampleGeneration = Generation;
		Input.MeasuredDistanceMm = DistanceMm;
		Input.SpeedMmPerS = 4000;
		Input.bConnected = true;
		Input.SessionState = ERowingSessionState::Active;
		Input.WorkoutState = ERowingWorkoutState::Active;
		Input.RowingState = ERowingState::Active;
		Input.StrokeState = ERowingStrokeState::Waiting;
		return Input;
	}

	FCourseTelemetryInput StoppedInput(std::uint64_t DistanceMm, std::uint64_t Generation)
	{
		auto Input = ActiveInput(DistanceMm, Generation);
		Input.SpeedMmPerS = 0;
		Input.RowingState = ERowingState::Inactive;
		Input.StrokeState = ERowingStrokeState::Waiting;
		return Input;
	}

	bool Near(double A, double B, double Epsilon = 0.001)
	{
		return std::abs(A - B) <= Epsilon;
	}

	void TestMinimumJerkAndHermiteFoundation()
	{
		const auto Before = FBiomechanicalStrokeTrajectory::MinimumJerk(2.0, 7.0, -1.0, 2.0);
		const auto After = FBiomechanicalStrokeTrajectory::MinimumJerk(2.0, 7.0, 2.0, 2.0);
		assert(Near(Before.Position, 2.0) && Near(Before.Velocity, 0.0) && Near(Before.Acceleration, 0.0));
		assert(Near(After.Position, 7.0) && Near(After.Velocity, 0.0) && Near(After.Acceleration, 0.0));
		double Previous = 2.0;
		for (int Index = 0; Index <= 1000; ++Index)
		{
			const auto State = FBiomechanicalStrokeTrajectory::MinimumJerk(2.0, 7.0, Index / 1000.0, 2.0);
			assert(State.Position >= Previous - 1.0e-12 && State.Position >= 2.0 && State.Position <= 7.0);
			Previous = State.Position;
		}

		const FQuinticKinematicState Start{3.0, 1.25, -0.5};
		const FQuinticKinematicState End{8.0, -0.75, 0.25};
		const auto ExactStart = FBiomechanicalStrokeTrajectory::QuinticHermite(Start, End, 0.0, 1.75);
		const auto ExactEnd = FBiomechanicalStrokeTrajectory::QuinticHermite(Start, End, 1.0, 1.75);
		assert(Near(ExactStart.Position, Start.Position) && Near(ExactStart.Velocity, Start.Velocity) && Near(ExactStart.Acceleration, Start.Acceleration));
		assert(Near(ExactEnd.Position, End.Position) && Near(ExactEnd.Velocity, End.Velocity) && Near(ExactEnd.Acceleration, End.Acceleration));

		const std::array<FQuinticHermiteKnot, 3> Knots{{
			{0.0, {0.0, 0.0, 0.0}},
			{0.8, {0.55, 0.9, -0.2}},
			{2.0, {1.0, 0.0, 0.0}},
		}};
		const auto AtKnot = FBiomechanicalStrokeTrajectory::PiecewiseQuinticHermite(Knots, 0.8);
		const auto BeforeKnot = FBiomechanicalStrokeTrajectory::PiecewiseQuinticHermite(Knots, 0.8 - 1.0e-7);
		const auto AfterKnot = FBiomechanicalStrokeTrajectory::PiecewiseQuinticHermite(Knots, 0.8 + 1.0e-7);
		assert(Near(AtKnot.Position, 0.55) && Near(AtKnot.Velocity, 0.9) && Near(AtKnot.Acceleration, -0.2));
		assert(Near(BeforeKnot.Position, AfterKnot.Position, 1.0e-5));
		assert(Near(BeforeKnot.Velocity, AfterKnot.Velocity, 1.0e-5));
		assert(Near(BeforeKnot.Acceleration, AfterKnot.Acceleration, 1.0e-4));

		for (double Duration : {0.0, 0.001, 0.5, 60.0 * 60.0})
		{
			const auto State = FBiomechanicalStrokeTrajectory::MinimumJerk(0.0, 1.0, 0.37, Duration);
			assert(std::isfinite(State.Position) && std::isfinite(State.Velocity) && std::isfinite(State.Acceleration));
		}
		const auto InvalidTime = FBiomechanicalStrokeTrajectory::MinimumJerk(
			0.0, 1.0, std::numeric_limits<double>::quiet_NaN(), 1.0);
		assert(Near(InvalidTime.Position, 0.0) && Near(InvalidTime.Velocity, 0.0) &&
			   Near(InvalidTime.Acceleration, 0.0));
	}

	void TestBiomechanicalChannelOrderingAndOarVelocity()
	{
		const auto EarlyDrive = FBiomechanicalStrokeTrajectory::Evaluate(0.20, EBiomechanicalStrokePhase::Drive, 1.0);
		const auto BodyDrive = FBiomechanicalStrokeTrajectory::Evaluate(0.55, EBiomechanicalStrokePhase::Drive, 1.0);
		const auto ArmDrive = FBiomechanicalStrokeTrajectory::Evaluate(0.85, EBiomechanicalStrokePhase::Drive, 1.0);
		assert(EarlyDrive.Seat.Position > 0.0 && Near(EarlyDrive.Torso.Position, 0.0) && Near(EarlyDrive.Arms.Position, 0.0));
		assert(BodyDrive.Seat.Position > BodyDrive.Torso.Position && Near(BodyDrive.Arms.Position, 0.0));
		assert(ArmDrive.Seat.Position > 0.99 && ArmDrive.Torso.Position > 0.9 && ArmDrive.Arms.Position > 0.0);

		const auto HandsAway = FBiomechanicalStrokeTrajectory::Evaluate(0.90, EBiomechanicalStrokePhase::Recovery, 2.0);
		const auto BodyOver = FBiomechanicalStrokeTrajectory::Evaluate(0.70, EBiomechanicalStrokePhase::Recovery, 2.0);
		const auto Slide = FBiomechanicalStrokeTrajectory::Evaluate(0.20, EBiomechanicalStrokePhase::Recovery, 2.0);
		assert(HandsAway.Arms.Position < 1.0 && Near(HandsAway.Torso.Position, 1.0) && Near(HandsAway.Seat.Position, 1.0));
		assert(BodyOver.Arms.Position < 0.01 && BodyOver.Torso.Position < 1.0 && Near(BodyOver.Seat.Position, 1.0));
		assert(Slide.Arms.Position < 0.01 && Slide.Torso.Position < 0.01 && Slide.Seat.Position < 1.0);

		double PreviousPosition = 0.0;
		double MaximumVelocity = -1.0;
		int MaximumIndex = -1;
		for (int Index = 0; Index <= 1000; ++Index)
		{
			const auto State = FBiomechanicalStrokeTrajectory::Evaluate(Index / 1000.0, EBiomechanicalStrokePhase::Drive, 1.0).Oar;
			assert(State.Position >= PreviousPosition - 1.0e-10 && State.Position >= 0.0 && State.Position <= 1.0);
			if (State.Velocity > MaximumVelocity)
			{
				MaximumVelocity = State.Velocity;
				MaximumIndex = Index;
			}
			PreviousPosition = State.Position;
		}
		assert(MaximumIndex >= 495 && MaximumIndex <= 505);
	}

	FCoursePresentationSnapshot SampleDriveAt(std::uint64_t TargetOffsetNs,
											  const std::vector<std::uint64_t> &FrameSteps)
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Runtime.Update(Input, Second);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, Second + 1);
		std::uint64_t Now = Second + 1;
		std::size_t StepIndex = 0;
		while (Now < Second + 1 + TargetOffsetNs)
		{
			Now = std::min(Second + 1 + TargetOffsetNs, Now + FrameSteps[StepIndex++ % FrameSteps.size()]);
			Runtime.Update(Input, Now);
		}
		return Runtime.GetSnapshot();
	}

	FCoursePresentationSnapshot SampleRecoveryAt(std::uint64_t TargetOffsetNs,
												 const std::vector<std::uint64_t> &FrameSteps)
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Input.RecoveryTimeMs = 2000;
		Runtime.Update(Input, Second);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, 2 * Second);
		Runtime.Update(Input, 3 * Second);
		Input.StrokeState = ERowingStrokeState::Recovery;
		Runtime.Update(Input, 3 * Second + 1);
		std::uint64_t Now = 3 * Second + 1;
		std::size_t StepIndex = 0;
		while (Now < 3 * Second + 1 + TargetOffsetNs)
		{
			Now = std::min(3 * Second + 1 + TargetOffsetNs, Now + FrameSteps[StepIndex++ % FrameSteps.size()]);
			Runtime.Update(Input, Now);
		}
		return Runtime.GetSnapshot();
	}

	void TestStrokeFrameRateIndependenceAndLatchedDurations()
	{
		const auto Thirty = SampleDriveAt(640'000'000ULL, {33'333'333ULL});
		const auto Sixty = SampleDriveAt(640'000'000ULL, {16'666'667ULL});
		const auto OneTwenty = SampleDriveAt(640'000'000ULL, {8'333'333ULL});
		const auto Irregular = SampleDriveAt(640'000'000ULL, {5'000'000ULL, 41'000'000ULL, 13'000'000ULL, 22'000'000ULL});
		for (const auto *Other : {&Sixty, &OneTwenty, &Irregular})
		{
			assert(Near(Thirty.StrokePose, Other->StrokePose, 1.0e-12));
			assert(Near(Thirty.SeatPose, Other->SeatPose, 1.0e-12));
			assert(Near(Thirty.TorsoPose, Other->TorsoPose, 1.0e-12));
			assert(Near(Thirty.ArmsPose, Other->ArmsPose, 1.0e-12));
			assert(Near(Thirty.OarPose, Other->OarPose, 1.0e-12));
		}
		const auto RecoveryThirty = SampleRecoveryAt(1'280'000'000ULL, {33'333'333ULL});
		const auto RecoverySixty = SampleRecoveryAt(1'280'000'000ULL, {16'666'667ULL});
		const auto RecoveryOneTwenty = SampleRecoveryAt(1'280'000'000ULL, {8'333'333ULL});
		const auto RecoveryIrregular = SampleRecoveryAt(1'280'000'000ULL, {5'000'000ULL, 41'000'000ULL, 13'000'000ULL, 22'000'000ULL});
		for (const auto *Other : {&RecoverySixty, &RecoveryOneTwenty, &RecoveryIrregular})
		{
			assert(Near(RecoveryThirty.StrokePose, Other->StrokePose, 1.0e-12));
			assert(Near(RecoveryThirty.SeatPose, Other->SeatPose, 1.0e-12));
			assert(Near(RecoveryThirty.TorsoPose, Other->TorsoPose, 1.0e-12));
			assert(Near(RecoveryThirty.ArmsPose, Other->ArmsPose, 1.0e-12));
			assert(Near(RecoveryThirty.OarPose, Other->OarPose, 1.0e-12));
		}

		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Runtime.Update(Input, Second);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, 2 * Second);
		Input.DriveTimeMs = 2000;
		auto Snapshot = Runtime.Update(Input, 2 * Second + 500'000'000ULL);
		assert(Near(Snapshot.StrokePose, 0.5));
		Input.StrokeState = ERowingStrokeState::Recovery;
		Input.RecoveryTimeMs = 2000;
		Runtime.Update(Input, 3 * Second);
		Input.RecoveryTimeMs = 4000;
		Snapshot = Runtime.Update(Input, 4 * Second);
		assert(Near(Snapshot.StrokePose, 0.5));
	}

	void TestCatchAndReconnectQuinticBridges()
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Runtime.Update(Input, Second);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, 2 * Second);
		auto Snapshot = Runtime.Update(Input, 2 * Second + 800'000'000ULL);
		const double LiveSeat = Snapshot.SeatPose;
		assert(LiveSeat > 0.9);

		Input.bStale = true;
		const auto BridgeStart = Runtime.Update(Input, 3 * Second);
		assert(Near(BridgeStart.SeatPose, LiveSeat));
		const auto BridgeMiddle = Runtime.Update(Input, 3 * Second + 250'000'000ULL);
		assert(BridgeMiddle.SeatPose > 0.0 && BridgeMiddle.SeatPose < LiveSeat);
		const auto Catch = Runtime.Update(Input, 3 * Second + 500'000'000ULL);
		assert(Near(Catch.StrokePose, 0.0) && Near(Catch.SeatPose, 0.0) && Near(Catch.OarPose, 0.0));

		Input.bStale = false;
		Input.StrokeState = ERowingStrokeState::Recovery;
		Input.RecoveryTimeMs = 2000;
		const auto ReconnectStart = Runtime.Update(Input, 4 * Second);
		assert(Near(ReconnectStart.StrokePose, 0.0));
		const auto ReconnectMiddle = Runtime.Update(Input, 4 * Second + 250'000'000ULL);
		assert(ReconnectMiddle.StrokePose > 0.0 && ReconnectMiddle.StrokePose < 1.0);
		const auto ReconnectFinish = Runtime.Update(Input, 4 * Second + 500'000'000ULL);
		assert(Near(ReconnectFinish.StrokePose, 1.0));
	}

	ContentRuntime::FRouteDefinition StraightHanRoute()
	{
		using namespace ContentRuntime;
		FRouteDefinition Route;
		Route.SchemaVersion = RouteDefinitionSchemaV2;
		Route.RouteId = "route.han-river.5k";
		Route.SemanticVersion = "2.0.0";
		Route.ContentSetId = "han-river-alpha-1";
		Route.LengthMm = 5'000'000;
		Route.bClosed = false;
		Route.Checkpoints = {
			{"sebit-lookback", 650'000},
			{"dongjak-span", 1'450'000},
			{"nodeulseom", 3'000'000},
			{"hangang-bridge", 3'650'000},
		};
		FRoutePresentationPath Path;
		Path.PathFormatVersion = PresentationPathFormatV1;
		Path.OwningRouteId = Route.RouteId;
		for (std::uint64_t Index = 0; Index < 8; ++Index)
		{
			const std::uint64_t Distance = Route.LengthMm * Index / 7;
			const std::uint64_t Previous = Index == 0 ? 0 : Route.LengthMm * (Index - 1) / 7;
			const std::uint64_t Next = Index == 7 ? Route.LengthMm : Route.LengthMm * (Index + 1) / 7;
			const std::int64_t Arrive = static_cast<std::int64_t>(Index == 0 ? Next - Distance : Distance - Previous);
			const std::int64_t Leave = static_cast<std::int64_t>(Index == 7 ? Distance - Previous : Next - Distance);
			Path.ControlPoints.push_back({"point-" + std::to_string(Index), Distance, {static_cast<std::int64_t>(Distance), 0, 0}, {Arrive, 0, 0}, {Leave, 0, 0}});
		}
		for (std::uint64_t Distance = 0;; Distance = std::min<std::uint64_t>(Route.LengthMm, Distance + 5'000))
		{
			const std::uint32_t Segment = Distance == Route.LengthMm ? 6 : static_cast<std::uint32_t>(Distance * 7 / Route.LengthMm);
			const std::uint64_t Start = Route.LengthMm * Segment / 7;
			const std::uint64_t End = Route.LengthMm * (Segment + 1) / 7;
			const std::uint32_t Parameter = Distance == Route.LengthMm ? 1'000'000 : static_cast<std::uint32_t>((Distance - Start) * 1'000'000ULL / (End - Start));
			Path.ArcLengthLookup.push_back({Distance, Segment, Parameter});
			if (Distance == Route.LengthMm)
				break;
		}
		const std::string Bytes = CanonicalArcLengthLookupBytes(Path.ArcLengthLookup);
		Path.ArcLengthLookupSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
		Route.PresentationPath = std::move(Path);
		return Route;
	}

	void TestStandardGoldenPathAndWrapping()
	{
		const auto Standard = ContentRuntime::BuiltInStandardRouteDefinition();
		assert(Standard.SchemaVersion == ContentRuntime::RouteDefinitionSchemaV1);
		assert(Standard.PresentationPath && Standard.PresentationPath->ControlPoints.size() == 33);
		const auto Start = EvaluateCoursePath(Standard, 0.0);
		const auto Quarter = EvaluateCoursePath(Standard, 500'000.0);
		const auto Half = EvaluateCoursePath(Standard, 1'000'000.0);
		assert(Near(Start.PositionMm.X, 650'000.0, 1.0) && Near(Start.PositionMm.Y, 0.0, 1.0));
		assert(Near(Start.UnitTangent.X, 0.0, 0.001) && Start.UnitTangent.Y > 0.999);
		assert(Near(Quarter.PositionMm.X, 0.0, 600.0) && Near(Quarter.PositionMm.Y, 180'000.0, 1.0));
		assert(Quarter.UnitTangent.X < -0.999 && Near(Quarter.UnitTangent.Y, 0.0, 0.001));
		assert(Near(Half.PositionMm.X, -650'000.0, 1.0) && Near(Half.PositionMm.Y, 0.0, 200.0));

		for (const std::uint64_t Distance : {0ULL, 250'000ULL, 1'999'000ULL, 2'000'000ULL, 6'125'000ULL})
		{
			FCoursePresentationRuntime Runtime;
			auto Input = ActiveInput(Distance);
			Input.SampleMonotonicNs = Second;
			const auto Snapshot = Runtime.Update(Input, Second);
			assert(Snapshot.CompletedLap == Distance / CourseLengthMm);
			assert(Near(Snapshot.WrappedCourseDistanceMm, static_cast<double>(Distance % CourseLengthMm)));
		}
	}

	void TestPredictionSeekAndHolds()
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput(100'000, 1);
		Input.SampleMonotonicNs = Second;
		auto Snapshot = Runtime.Update(Input, Second + 100'000'000ULL);
		assert(Near(Snapshot.PredictedDistanceMm, 100'400.0));
		Input.AcceptedSampleGeneration = 2;
		Input.MeasuredDistanceMm = 101'000;
		Input.SampleMonotonicNs = 2 * Second;
		Snapshot = Runtime.Update(Input, 2 * Second);
		assert(!Snapshot.bDiscontinuityReset);
		for (int Index = 1; Index <= 10; ++Index)
			Snapshot = Runtime.Update(Input, 2 * Second + Index * 100'000'000ULL);
		assert(std::abs(Snapshot.PresentedDistanceMm - Snapshot.PredictedDistanceMm) < 10.0);

		Input.AcceptedSampleGeneration = 3;
		Input.MeasuredDistanceMm = 120'000;
		Input.SampleMonotonicNs = 4 * Second;
		Snapshot = Runtime.Update(Input, 4 * Second);
		assert(Snapshot.bDiscontinuityReset);
		assert(Near(Snapshot.PresentedDistanceMm, 120'000.0));

		Input.bConnected = false;
		Input.bFrozen = true;
		Snapshot = Runtime.Update(Input, 5 * Second);
		assert(Near(Snapshot.PresentedDistanceMm, 120'000.0));
		assert(Near(Runtime.Update(Input, 8 * Second).PresentedDistanceMm, 120'000.0));
	}

	void TestOpenEndpointAndPath()
	{
		FCoursePresentationRuntime Runtime;
		Runtime.SelectRoute(StraightHanRoute());
		auto Input = ActiveInput(5'250'000);
		Input.SampleMonotonicNs = Second;
		const auto Snapshot = Runtime.Update(Input, Second);
		assert(Snapshot.MeasuredDistanceMm == 5'250'000);
		assert(Snapshot.WrappedCourseDistanceMm == 5'000'000);
		assert(Snapshot.bRouteComplete);
		assert(Near(Snapshot.PathPositionMm.X, 5'000'000.0, 1.0));
		assert(Snapshot.PathUnitTangent.X > 0.999);
	}

	void TestCheckpointPathSampling()
	{
		auto VerifyCheckpoints = [](const ContentRuntime::FRouteDefinition &Route)
		{
			assert(!Route.Checkpoints.empty());
			for (const ContentRuntime::FRouteCheckpoint &Checkpoint : Route.Checkpoints)
			{
				FCoursePresentationRuntime Runtime;
				Runtime.SelectRoute(Route);
				auto Input = ActiveInput(Checkpoint.DistanceMm);
				Input.SpeedMmPerS = 0;
				Input.SampleMonotonicNs = Second;
				const FCoursePresentationSnapshot Snapshot = Runtime.Update(Input, Second);
				const FCoursePathSample Expected = EvaluateCoursePath(Route, static_cast<double>(Checkpoint.DistanceMm));
				assert(Near(Snapshot.WrappedCourseDistanceMm, static_cast<double>(Checkpoint.DistanceMm)));
				assert(Near(Snapshot.PathPositionMm.X, Expected.PositionMm.X));
				assert(Near(Snapshot.PathPositionMm.Y, Expected.PositionMm.Y));
				assert(Near(Snapshot.PathPositionMm.Z, Expected.PositionMm.Z));
				assert(Near(Snapshot.PathUnitTangent.X, Expected.UnitTangent.X));
				assert(Near(Snapshot.PathUnitTangent.Y, Expected.UnitTangent.Y));
				assert(Near(Snapshot.PathUnitTangent.Z, Expected.UnitTangent.Z));
				const double ExpectedYaw = std::atan2(Expected.UnitTangent.Y, Expected.UnitTangent.X);
				assert(Near(Snapshot.DampedYawRadians, ExpectedYaw));
			}
		};

		VerifyCheckpoints(ContentRuntime::BuiltInStandardRouteDefinition());
		VerifyCheckpoints(StraightHanRoute());
	}

	void TestStrokeSourcesMotionBoundsAndReducedMotion()
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Input.RecoveryTimeMs = 2000;
		Runtime.Update(Input, Second);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, Second + 100'000'000ULL);
		const auto EarlyDrive = Runtime.Update(Input, Second + 350'000'000ULL);
		assert(Near(EarlyDrive.StrokePose, 0.25));
		assert(EarlyDrive.SeatPose > 0.0 && Near(EarlyDrive.ArmsPose, 0.0));
		assert(std::abs(EarlyDrive.HullRollDegrees) <= 1.5);
		assert(std::abs(EarlyDrive.HullPitchDegrees) <= 0.8);
		assert(std::abs(EarlyDrive.AmbientBobMm) <= 20.0);
		assert(std::abs(EarlyDrive.StrokeHeaveMm) <= 10.0);

		Runtime.SetReducedMotion(true, 2 * Second);
		const auto Reduced = Runtime.Update(Input, 2 * Second);
		assert(Near(Reduced.HullRollDegrees, 0.0) && Near(Reduced.HullPitchDegrees, 0.0));
		assert(Near(Reduced.AmbientBobMm, 0.0) && Near(Reduced.StrokeHeaveMm, 0.0));
	}

	void TestCosmeticsUsePathCurvatureAndSpeedChange()
	{
		FCoursePresentationRuntime StraightRuntime;
		StraightRuntime.SelectRoute(StraightHanRoute());
		auto Straight = ActiveInput(100'000, 1);
		Straight.SpeedMmPerS = 0;
		Straight.SampleMonotonicNs = Second;
		auto Snapshot = StraightRuntime.Update(Straight, Second);
		Snapshot = StraightRuntime.Update(Straight, Second + 100'000'000ULL);
		assert(Near(Snapshot.HullRollDegrees, 0.0));
		assert(Near(Snapshot.HullPitchDegrees, 0.0));

		FCoursePresentationRuntime CurvedRuntime;
		auto Curved = ActiveInput(100'000, 1);
		Curved.SpeedMmPerS = 20'000;
		Curved.SampleMonotonicNs = Second;
		CurvedRuntime.Update(Curved, Second);
		Snapshot = CurvedRuntime.Update(Curved, Second + 100'000'000ULL);
		assert(std::abs(Snapshot.HullRollDegrees) > 0.01);
		Curved.AcceptedSampleGeneration = 2;
		Curved.SpeedMmPerS = 22'000;
		Curved.SampleMonotonicNs = 2 * Second;
		Snapshot = CurvedRuntime.Update(Curved, 2 * Second);
		assert(std::abs(Snapshot.HullPitchDegrees) > 0.01);
	}

	void TestCamerasToastAndReveal()
	{
		FCoursePresentationRuntime Runtime;
		auto Active = ActiveInput(100'000, 1);
		Active.SampleMonotonicNs = Second;
		auto Snapshot = Runtime.Update(Active, Second);
		assert(Snapshot.bFirstStrokeObserved);
		assert(Snapshot.CameraPreset == ECourseCameraPreset::Medium);
		assert(Near(Snapshot.CameraParameters.AftMm, 12'000.0));
		assert(Snapshot.CameraParameters.StarboardMm > 0.0);

		Runtime.CycleCameraPreset(2 * Second);
		assert(Runtime.GetSelectedCameraPreset() == ECourseCameraPreset::Wide);
		Snapshot = Runtime.Update(Active, 2 * Second);
		assert(Snapshot.CameraPreset == ECourseCameraPreset::Wide);
		assert(Snapshot.CameraToast == "Camera: Wide");
		Snapshot = Runtime.Update(Active, 4 * Second + 1);
		assert(Snapshot.CameraToast.empty());

		auto Stopped = StoppedInput(100'000, 2);
		Stopped.SampleMonotonicNs = 5 * Second;
		Snapshot = Runtime.Update(Stopped, 5 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Dwell);
		FCoursePresentationRuntime RestingRuntime;
		RestingRuntime.Update(ActiveInput(100'000, 1), Second);
		auto Resting = StoppedInput(100'000, 2);
		Resting.WorkoutState = ERowingWorkoutState::Resting;
		Snapshot = RestingRuntime.Update(Resting, 2 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Dwell);
		Snapshot = Runtime.Update(Stopped, 15 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Outbound);
		Snapshot = Runtime.Update(Stopped, 20 * Second);
		assert(Near(Snapshot.CameraParameters.AftMm, 25'000.0, 1.0));
		assert(Near(Snapshot.CameraParameters.StarboardMm, 15'000.0, 1.0));
		Snapshot = Runtime.Update(Stopped, 25 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Hold);
		Snapshot = Runtime.Update(Stopped, 35 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Returning);

		Runtime.SetCameraPreset(ECourseCameraPreset::Close, 36 * Second);
		Snapshot = Runtime.Update(Stopped, 40 * Second);
		assert(Snapshot.CameraPreset == ECourseCameraPreset::Close);
		assert(Snapshot.CameraParameters.StarboardMm > 4'000.0);
		Snapshot = Runtime.Update(Stopped, 45 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Outbound);
		Snapshot = Runtime.Update(Stopped, 50 * Second);
		assert(Snapshot.CameraParameters.AftMm > 8'000.0);

		Active.AcceptedSampleGeneration = 3;
		Active.SampleMonotonicNs = 51 * Second;
		Snapshot = Runtime.Update(Active, 51 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Resume);
		const double ResumeAft = Snapshot.CameraParameters.AftMm;
		Active.AcceptedSampleGeneration = 4;
		Active.SampleMonotonicNs = 52 * Second;
		Snapshot = Runtime.Update(Active, 52 * Second);
		assert(Snapshot.CameraParameters.AftMm < ResumeAft);
		Active.AcceptedSampleGeneration = 5;
		Active.SampleMonotonicNs = 53 * Second;
		Snapshot = Runtime.Update(Active, 53 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Chase);
		assert(Near(Snapshot.CameraParameters.AftMm, 8'000.0));
	}

	void TestInvalidRevealCancelsImmediatelyAndReconnectDwellsAgain()
	{
		FCoursePresentationRuntime Runtime;
		auto Active = ActiveInput(10'000, 1);
		Runtime.Update(Active, Second);
		auto Stopped = StoppedInput(10'000, 2);
		Runtime.Update(Stopped, 2 * Second);
		Runtime.Update(Stopped, 12 * Second);
		auto Snapshot = Runtime.Update(Stopped, 17 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Outbound);
		Stopped.bStale = true;
		Snapshot = Runtime.Update(Stopped, 18 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Chase);

		Stopped.bStale = false;
		Stopped.AcceptedSampleGeneration = 3;
		Snapshot = Runtime.Update(Stopped, 19 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Dwell);
		Snapshot = Runtime.Update(Stopped, 28 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Dwell);
		Snapshot = Runtime.Update(Stopped, 29 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Outbound);

		auto Resumed = ActiveInput(10'000, 4);
		Snapshot = Runtime.Update(Resumed, 30 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Resume);
		Resumed.bConnected = false;
		Snapshot = Runtime.Update(Resumed, 31 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Chase);

		FCoursePresentationRuntime CompletionRuntime;
		auto CompletionActive = ActiveInput(10'000, 1);
		CompletionRuntime.Update(CompletionActive, Second);
		auto CompletionStopped = StoppedInput(10'000, 2);
		CompletionRuntime.Update(CompletionStopped, 2 * Second);
		CompletionRuntime.Update(CompletionStopped, 12 * Second);
		Snapshot = CompletionRuntime.Update(CompletionStopped, 17 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Outbound);
		CompletionStopped.bWorkoutCompleted = true;
		Snapshot = CompletionRuntime.Update(CompletionStopped, 18 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Chase);
	}

	void TestReducedRevealAndLapBoundaryLatch()
	{
		FCoursePresentationRuntime Runtime;
		Runtime.SetReducedMotion(true, 0);
		auto Active = ActiveInput(1'999'000, 1);
		Runtime.Update(Active, Second);
		auto Stopped = StoppedInput(2'001'000, 2);
		auto Snapshot = Runtime.Update(Stopped, 2 * Second);
		assert(Snapshot.CompletedLap == 1 && Snapshot.bFirstStrokeObserved);
		Snapshot = Runtime.Update(Stopped, 12 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Hold);
		assert(Near(Snapshot.CameraParameters.AftMm, 32'000.0));
		Stopped.AcceptedSampleGeneration = 3;
		Stopped.bConnected = false;
		Snapshot = Runtime.Update(Stopped, 13 * Second);
		assert(Snapshot.RevealPhase == ECourseRevealPhase::Chase);
	}
} // namespace

int main()
{
	TestMinimumJerkAndHermiteFoundation();
	TestBiomechanicalChannelOrderingAndOarVelocity();
	TestStrokeFrameRateIndependenceAndLatchedDurations();
	TestCatchAndReconnectQuinticBridges();
	TestStandardGoldenPathAndWrapping();
	TestPredictionSeekAndHolds();
	TestOpenEndpointAndPath();
	TestCheckpointPathSampling();
	TestStrokeSourcesMotionBoundsAndReducedMotion();
	TestCosmeticsUsePathCurvatureAndSpeedChange();
	TestCamerasToastAndReveal();
	TestInvalidRevealCancelsImmediatelyAndReconnectDwellsAgain();
	TestReducedRevealAndLapBoundaryLatch();
	std::cout << "course presentation tests passed\n";
}
