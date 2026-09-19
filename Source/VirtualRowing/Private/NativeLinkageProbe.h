#pragma once

// Phase 1 Milestone 5 UBT linkage spike: exercises the prebuilt CMake archive
// (WorkoutRuntime, LocalData/SQLite, static protobuf/abseil, simulator) so a
// missing or mismatched symbol fails the link. Kept (behind -NativeLinkageProbe)
// because the app does not journal yet, so UWorkoutSubsystem alone would not pull
// LocalData/SQLite/protobuf into the module and their linkage would go unchecked.
bool VirNativeLinkageProbe();
