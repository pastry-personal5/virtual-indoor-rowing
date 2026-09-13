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

	FMockMachineScenario MakeSyntheticIndoorRowerScenario();
} // namespace pm5_sim
