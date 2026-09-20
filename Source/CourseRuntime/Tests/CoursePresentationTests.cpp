#include "CourseRuntime/CoursePresentation.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
	FRowingSessionId MakeId(std::uint8_t Seed)
	{
		FRowingSessionId::FBytes Bytes{};
		Bytes.fill(Seed);
		return FRowingSessionId::FromBytes(Bytes);
	}

	FCourseTelemetryInput ActiveInput(std::uint64_t DistanceMm = 0)
	{
		FCourseTelemetryInput Input;
		Input.SessionId = MakeId(1);
		Input.bHasSession = true;
		Input.bHasValidSample = true;
		Input.MeasuredDistanceMm = DistanceMm;
		Input.SpeedMmPerS = 4000;
		Input.bConnected = true;
		Input.SessionState = ERowingSessionState::Active;
		Input.WorkoutState = ERowingWorkoutState::Active;
		Input.RowingState = ERowingState::Active;
		Input.StrokeState = ERowingStrokeState::Waiting;
		return Input;
	}

	bool Near(double A, double B, double Epsilon = 0.001)
	{
		return std::abs(A - B) <= Epsilon;
	}

	void TestCourseWrapping()
	{
		for (const std::uint64_t Distance : {0ULL, 250'000ULL, 1'000'000ULL, 1'999'000ULL, 2'000'000ULL, 2'250'000ULL, 6'125'000ULL, 9'000'000'000ULL})
		{
			FCoursePresentationRuntime Runtime;
			auto Input = ActiveInput(Distance);
			Input.SampleMonotonicNs = 1'000'000'000ULL;
			const auto Snapshot = Runtime.Update(Input, 1'000'000'000ULL);
			assert(Snapshot.MeasuredDistanceMm == Distance);
			assert(Snapshot.CompletedLap == Distance / CourseLengthMm);
			assert(Near(Snapshot.WrappedCourseDistanceMm, static_cast<double>(Distance % CourseLengthMm)));
		}
	}

	void TestPredictionAndSpring()
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput(100'000);
		Input.SampleMonotonicNs = 1'000'000'000ULL;
		auto Snapshot = Runtime.Update(Input, 1'100'000'000ULL);
		assert(Near(Snapshot.PredictedDistanceMm, 100'400.0));
		Input.MeasuredDistanceMm = 110'000;
		Input.SampleMonotonicNs = 1'000'000'000ULL;
		Snapshot = Runtime.Update(Input, 1'500'000'000ULL);
		assert(Near(Snapshot.PredictedDistanceMm, 111'000.0));
		assert(Snapshot.PresentedDistanceMm > 100'400.0 && Snapshot.PresentedDistanceMm < 111'000.0);
		Snapshot = Runtime.Update(Input, 1'800'000'000ULL);
		assert(std::abs(Snapshot.PresentedDistanceMm - Snapshot.PredictedDistanceMm) < 600.0);

		Input.bConnected = false;
		Input.bFrozen = true;
		const auto Disconnected = Runtime.Update(Input, 1'850'000'000ULL);
		assert(Near(Disconnected.PredictedDistanceMm, 111'000.0));
		const auto Stopped = Runtime.Update(Input, 3'000'000'000ULL);
		assert(Near(Stopped.PredictedDistanceMm, Disconnected.PredictedDistanceMm));

		// Backwards clocks produce zero elapsed time.
		const double Before = Stopped.PresentedDistanceMm;
		assert(Near(Runtime.Update(Input, 2'000'000'000ULL).PresentedDistanceMm, Before));

		Input.SessionId = MakeId(2);
		Input.bConnected = true;
		Input.bFrozen = false;
		Input.MeasuredDistanceMm = 42'000;
		Input.SampleMonotonicNs = 4'000'000'000ULL;
		assert(Near(Runtime.Update(Input, 4'000'000'000ULL).PresentedDistanceMm, 42'000.0));
	}

	void TestOpenRouteEndpointDoesNotCapOfficialProgress()
	{
		FCoursePresentationRuntime Runtime;
		ContentRuntime::FRouteDefinition Han;
		Han.SchemaVersion = ContentRuntime::RouteDefinitionSchemaV1;
		Han.RouteId = "route.han-river.5k";
		Han.LengthMm = 5'000'000;
		Han.bClosed = false;
		Runtime.SelectRoute(Han);
		auto Input = ActiveInput(5'250'000);
		Input.SampleMonotonicNs = 1'000'000'000ULL;
		const auto Snapshot = Runtime.Update(Input, 1'000'000'000ULL);
		assert(Snapshot.RouteId == "route.han-river.5k");
		assert(Snapshot.MeasuredDistanceMm == 5'250'000);
		assert(Snapshot.PredictedDistanceMm == 5'250'000);
		assert(Snapshot.PresentedDistanceMm == 5'250'000);
		assert(Snapshot.WrappedCourseDistanceMm == 5'000'000);
		assert(Snapshot.bRouteComplete);
		assert(Snapshot.CompletedLap == 0);

		Input.MeasuredDistanceMm = 6'000'000;
		const auto Continued = Runtime.Update(Input, 2'000'000'000ULL);
		assert(Continued.MeasuredDistanceMm == 6'000'000);
		assert(Continued.WrappedCourseDistanceMm == 5'000'000);
		assert(Continued.bRouteComplete);
	}

	void TestStrokeSourcesAndBiomechanics()
	{
		FCoursePresentationRuntime Runtime;
		auto Input = ActiveInput();
		Input.DriveTimeMs = 1000;
		Input.RecoveryTimeMs = 2000;
		Runtime.Update(Input, 1'000'000'000ULL);
		Input.StrokeState = ERowingStrokeState::Drive;
		Runtime.Update(Input, 1'100'000'000ULL);
		const auto EarlyDrive = Runtime.Update(Input, 1'350'000'000ULL);
		assert(Near(EarlyDrive.StrokePose, 0.25));
		assert(EarlyDrive.SeatPose > 0.0);
		assert(Near(EarlyDrive.TorsoPose, 0.0));
		assert(Near(EarlyDrive.ArmsPose, 0.0));
		const auto LateDrive = Runtime.Update(Input, 1'850'000'000ULL);
		assert(LateDrive.SeatPose > LateDrive.TorsoPose);
		assert(LateDrive.TorsoPose > LateDrive.ArmsPose);

		Input.StrokeState = ERowingStrokeState::Dwell;
		assert(Near(Runtime.Update(Input, 2'100'000'000ULL).StrokePose, 1.0));
		Input.StrokeState = ERowingStrokeState::Recovery;
		Runtime.Update(Input, 2'200'000'000ULL);
		const auto Recovery = Runtime.Update(Input, 3'200'000'000ULL);
		assert(Near(Recovery.StrokePose, 0.5));
		assert(Near(Recovery.ArmsPose, 0.0));
		assert(Recovery.TorsoPose > 0.0 && Recovery.SeatPose > Recovery.TorsoPose);

		Input.StrokeState = ERowingStrokeState::Unknown;
		Input.StrokeRateDeciSpm = 300;
		const auto EstimatedStart = Runtime.Update(Input, 4'000'000'000ULL);
		assert(EstimatedStart.AnimationQuality == ECourseAnimationQuality::Estimated);
		const auto EstimatedDrive = Runtime.Update(Input, 4'350'000'000ULL);
		assert(Near(EstimatedDrive.StrokePose, 0.5));

		Input.bStale = true;
		const double StartReturn = Runtime.Update(Input, 4'400'000'000ULL).StrokePose;
		const auto Returned = Runtime.Update(Input, 4'900'000'000ULL);
		assert(StartReturn > 0.0);
		assert(Near(Returned.StrokePose, 0.0));
		assert(Returned.AnimationQuality == ECourseAnimationQuality::Unavailable);

		Input.bStale = false;
		Input.StrokeRateDeciSpm.reset();
		assert(Runtime.Update(Input, 5'000'000'000ULL).AnimationQuality == ECourseAnimationQuality::Unavailable);
	}

	void TestStrokeFallbackTimingAndStops()
	{
		// State is primary even without detailed timings: stroke rate supplies a
		// 35/65 cycle, then the fixed 700/1300 ms defaults supply the last fallback.
		FCoursePresentationRuntime RateRuntime;
		auto RateInput = ActiveInput();
		RateInput.StrokeRateDeciSpm = 300;
		RateInput.StrokeState = ERowingStrokeState::Drive;
		RateRuntime.Update(RateInput, 1'000'000'000ULL);
		assert(Near(RateRuntime.Update(RateInput, 1'350'000'000ULL).StrokePose, 0.5));

		FCoursePresentationRuntime DefaultRuntime;
		auto DefaultInput = ActiveInput();
		DefaultInput.StrokeState = ERowingStrokeState::Drive;
		DefaultRuntime.Update(DefaultInput, 1'000'000'000ULL);
		assert(Near(DefaultRuntime.Update(DefaultInput, 1'350'000'000ULL).StrokePose, 0.5));

		auto VerifyReturn = [](auto StopInput)
		{
			FCoursePresentationRuntime Runtime;
			auto Input = ActiveInput();
			Input.StrokeState = ERowingStrokeState::Dwell;
			assert(Near(Runtime.Update(Input, 1'000'000'000ULL).StrokePose, 1.0));
			StopInput(Input);
			assert(Near(Runtime.Update(Input, 1'100'000'000ULL).StrokePose, 1.0));
			const auto End = Runtime.Update(Input, 1'600'000'000ULL);
			assert(Near(End.StrokePose, 0.0));
			assert(End.AnimationQuality == ECourseAnimationQuality::Unavailable);
		};
		VerifyReturn([](FCourseTelemetryInput &Input)
					 { Input.WorkoutState = ERowingWorkoutState::Paused; });
		VerifyReturn([](FCourseTelemetryInput &Input)
					 { Input.WorkoutState = ERowingWorkoutState::Resting; });
		VerifyReturn([](FCourseTelemetryInput &Input)
					 { Input.SessionState = ERowingSessionState::Ended; });
		VerifyReturn([](FCourseTelemetryInput &Input)
					 { Input.bConnected = false; });
		VerifyReturn([](FCourseTelemetryInput &Input)
					 { Input.bStale = true; });
	}
} // namespace

int main()
{
	TestCourseWrapping();
	TestPredictionAndSpring();
	TestOpenRouteEndpointDoesNotCapOfficialProgress();
	TestStrokeSourcesAndBiomechanics();
	TestStrokeFallbackTimingAndStops();
	std::cout << "course presentation tests passed\n";
}
