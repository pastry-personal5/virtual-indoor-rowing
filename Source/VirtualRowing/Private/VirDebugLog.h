#pragma once

#include "CoreMinimal.h"

// TEMPORARY Milestone 3 diagnostic (2026-09-17): traces StartupModule/probe execution
// via raw POSIX stdio (bypassing FFileHelper/IFileManager entirely, since we cannot
// yet rule out those UE subsystems being unready at the point this fires) to a fixed
// path outside any engine-managed Saved/ tree. No probe result or engine log file has
// appeared anywhere despite the compiled code being confirmed present in the shipped
// binary. Remove once Milestone 3 acceptance-gate item 3 is unblocked.
void VirDebugLog(const FString &Message);
