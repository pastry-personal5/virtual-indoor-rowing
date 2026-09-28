#include "CourseRuntime/CoursePresentation.h"

#include <cassert>
#include <cmath>
#include <iostream>

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
