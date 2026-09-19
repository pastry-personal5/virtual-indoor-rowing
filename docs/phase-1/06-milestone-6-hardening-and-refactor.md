# Phase 1 Milestone 6: hardening, refactor, and chores

Status: In progress (2026-09-19) — bugs, `RowingSim` rename and chores done; the three file splits are not started (see "Implementation notes")
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-19

## Relationship to the delivery plan

This is the sixth milestone scoped under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It sits between [Milestone 5](05-milestone-5-unreal-hud.md) and the real-device milestone that Milestone 5 split out. It adds no product behavior and maps to no build-order step: it fixes defects found in Milestones 4 and 5, does mechanical refactors that keep later milestones reviewable, and clears recorded chores. Its purpose is to hand the real-device milestone a journal path whose known defects are fixed.

## Owner-confirmed scoping decisions (2026-09-19)

- **All five deferred findings are fixed here** (see "Bugs"), including the `pm5-tui` driver issues, rather than leaving the driver for the real-device milestone.
- **Refactors are mechanical splits plus the `RowingSim` rename.** Each is its own commit with no behavior change; the existing tests are the regression evidence.

## In scope

### Bugs

Source: [Milestone 5, "Findings deferred to the real-device milestone"](05-milestone-5-unreal-hud.md#findings-deferred-to-the-real-device-milestone). Each fix lands with a failing-first behavioral test.

1. `WorkoutSession` handles the real adapter's `Ready`-before-`ConnectionRestored` order so `LinkGapRecorded.device_reported` reflects the device. The test uses the real adapter's event order, not the mock's.
2. `WorkoutSession` retries `CreateSession` after a transient first-write failure instead of setting `bPersisted` before the write.
3. `LocalDataJournalSink::WriteSummary` clears the writer's stage when a commit throws, so later writes through the same sink succeed.
4. The summary revision is tracked per session, not per sink.
5. `pm5-tui` `WorkoutJournalDriver`: `Ingest()` and `Tick()` no longer both poll the machine, and End Session mid-row no longer splits one physical workout into two journal sessions (the second session must not miss `CapabilityObserved` or report cumulative PM5 distance/time).

### Refactors (no behavior change)

- Split `Tools/pm5-tui/src/main.cpp` (about 1,900 lines) and `Tools/pm5-tui/src/RunMetricsWriter.cpp` (about 1,100 lines) into cohesive units.
- Split `Scripts/dev.py` (about 870 lines) into modules behind the same `Makefile` entry points.
- Rename the `pm5_sim/` include prefix and the `pm5-sim` CMake target to `RowingSim`, which Milestone 5 deferred to keep its move mechanical. Consumers are rewired only.

### Chores

- Add warning flags for `WorkoutRuntime` in `CMakeLists.txt`, fixing what they surface.
- `docs/architecture/09-verification-strategy.md` still names `Tools/pm5-sim`; correct it through the normal architecture-doc path.
- Update `CLAUDE.md` if the rename or module splits change a command, target name, or layout entry.
- Phase 1 README milestone entry and changelog.

## Out of scope

- Real CoreBluetooth wiring, the PM5 picker, and Keychain-sealed journaling in the app (the real-device milestone).
- Route, boat distance-to-spline, and stroke animation.
- Latency instrumentation.
- Changes to any accepted ADR or to normative architecture text beyond the stale path above.
- New features or behavior changes hidden inside a refactor commit.

## Verification plan

- `make build`, `make test`, and `make format-check` after each commit; a refactor commit must pass with no test edits other than the rename's path changes.
- `make doctor`, then `make unreal-smoke` after the rename and after any change to `native-app` inputs.
- `python3 Scripts/check_public_header_dependencies.py` (run by `make test`).
- `git diff --check` and `git status --short` before handoff.
- Per repo policy, `make unreal-shipping` and packaged-app launches stay owner-run.
- Simulator and unit evidence only. Real-PM5 behavior is not claimed; the `Ready`/`Restored` fix is verified against the real adapter's event order in a test, not against hardware.

## Risks

- The rename touches many consumers and the `native-app` archive; a missed target breaks `make unreal-smoke` even when `make test` passes.
- Splitting `main.cpp` risks subtle changes to TUI startup, logging, or metrics ordering; `StatusDiagnosticsSmoke.py` and the existing pm5-tui tests must stay green.
- Fix 5 changes what the journal records for a mid-row End Session; the intended semantics need confirming against Milestone 4's session-lifecycle notes before coding.

## Implementation notes (2026-09-19)

### Done

- Bugs 1-4: `FWorkoutSession` waits for `FRowingConnectionRestored` after `Ready` (host-measured fallback on the next sample or a fully drained `Tick`); `CreateSession` is a pending record retried on the next write; `FLocalDataJournalWriter::AbandonStaged()` is new and `FLocalDataJournalSink::WriteSummary` retries a failed commit once, then abandons the stage; summary revisions are per session. Tests: `workout_session_records_device_gap_when_ready_precedes_restored`, `workout_session_restores_host_measured_when_ready_arrives_without_restored`, `workout_session_retries_create_session_after_transient_failure`, `local_data_sink_numbers_summary_revisions_per_session`, `local_data_abandoned_stage_does_not_wedge_later_writes`.
- Bug 5: `FWorkoutSessionConfig::bPollMachine` (the driver sets it false; `workout_session_tick_does_not_poll_when_polling_is_disabled`), a mid-row End Session holds the next session until a sample shows the row stopped, and each new session gets the last machine info replayed so it journals `CapabilityObserved`.
- `RowingSim` rename (include prefix, namespace, CMake target, and `rowing_sim_contract_tests`/`rowing_sim_integration_tests`); dated docs keep the old names.
- Chores: `WorkoutRuntime` compiles with `-Wall -Wextra -Wpedantic` with no new warnings; `09-verification-strategy.md` path corrected.

### Verified and not verified

- `make test` (23 tests), `make format-check`, `make doctor`, `make native-app` and `make unreal-smoke` pass after the rename. Simulator and unit evidence only.
- Not automatically tested: the `WriteSummary` commit-retry path (SQLite offers no way to make `COMMIT` fail with the transaction still open, so only `AbandonStaged()` itself is tested), and the `pm5-tui` driver's End Session hold and info replay (the driver opens a Keychain-backed journal, so it has no unit target; needs an owner-run `make pm5-tui-journal` check).

### Not started: file splits

Splitting `pm5-tui`'s `main.cpp`/`RunMetricsWriter.cpp` and `Scripts/dev.py` is pending an owner decision. `Scripts/test_unreal_packaging.py` loads `dev.py` as one module and patches its functions (`dev.run`, `dev.capture`, ...), so a module split would change what those patches affect and need test edits, against this milestone's "no test edits" rule for refactor commits.
