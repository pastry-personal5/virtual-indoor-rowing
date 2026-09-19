#pragma once

#include "RowingSim/ReplayRowingMachine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RowingSim
{
	struct FGoldenTelemetryFixture
	{
		std::string Name;
		std::vector<FReplayTelemetryFrame> Frames;
		std::uint64_t DurationMs = 0;
		std::uint64_t FinalDistanceMm = 0;
	};

	// Generated deterministically from synthetic integer targets; contains no
	// hardware capture.
	FGoldenTelemetryFixture
	MakeNoRowingFixture(std::uint64_t DurationMs = 30000);
	FGoldenTelemetryFixture MakeEasy30SecondFixture();
	FGoldenTelemetryFixture Make500mSprintFixture();
	FGoldenTelemetryFixture Make2000mRaceFixture();
	FGoldenTelemetryFixture Make30MinuteSteadyStateFixture();
	FGoldenTelemetryFixture MakeIntervalFixture();
	FGoldenTelemetryFixture MakeAbruptStopFixture();
	FGoldenTelemetryFixture MakePacketLossFixture();
	// A short steady row whose final frame reports the device workout state
	// Complete / Terminated. Synthetic and simulator-only: no real PM5 capture
	// has confirmed what a Just Row reports at its end
	// (docs/phase-1/04-milestone-4-workout-runtime.md).
	FGoldenTelemetryFixture MakeDeviceCompletedFixture();
	FGoldenTelemetryFixture MakeDeviceTerminatedFixture();

	// Synthetic stroke-by-stroke run of the given duration with randomized
	// (but plausibly bounded) per-stroke rate, power, and distance.
	// Deterministic per Seed — same Seed and DurationMinutes always produce
	// an identical fixture, consistent with this file's other fixtures
	// being generated deterministically from synthetic integer targets.
	FGoldenTelemetryFixture
	MakeRandomStrokeFixture(std::uint64_t DurationMinutes,
							std::uint32_t Seed = 1);

	FMockMachineScenario MakeSyntheticIndoorRowerScenario();
} // namespace RowingSim
