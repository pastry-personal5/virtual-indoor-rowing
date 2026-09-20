#pragma once

#include "WorkoutRuntime/WorkoutSession.h"

// Session configuration for the Unreal app (Phase 1 Milestone 7). The app's
// subsystem drains the machine itself and forwards every event to Ingest(), so the
// session never polls (one poller per machine). A real-device row checkpoints below
// one second, leaving margin for batched PM5 notifications and game-thread scheduling (QA-003);
// the runtime default of 5 s is for tools that do not journal a live row.
inline FWorkoutSessionConfig MakeAppSessionConfig(const bool bRealDevice)
{
	FWorkoutSessionConfig Config;
	Config.bPollMachine = false;
	if (bRealDevice)
	{
		Config.FlushIntervalMs = 750;
		Config.FlushSampleCount = 25;
	}
	return Config;
}
