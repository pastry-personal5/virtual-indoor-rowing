#pragma once

#include "pm5_sim/ReplayRowingMachine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pm5_sim
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

	// Synthetic stroke-by-stroke run of the given duration with randomized
	// (but plausibly bounded) per-stroke rate, power, and distance.
	// Deterministic per Seed — same Seed and DurationMinutes always produce
	// an identical fixture, consistent with this file's other fixtures
	// being generated deterministically from synthetic integer targets.
	FGoldenTelemetryFixture
	MakeRandomStrokeFixture(std::uint64_t DurationMinutes,
							std::uint32_t Seed = 1);

	FMockMachineScenario MakeSyntheticIndoorRowerScenario();
} // namespace pm5_sim
