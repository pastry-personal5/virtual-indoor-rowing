# Phase 1 Milestone 4: `WorkoutRuntime` live session loop

Status: Implemented (2026-09-19) — automated checks pass (simulator evidence only); real-PM5 confirmation and the interactive `--journal` TUI run are owner-run and outstanding
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-19

## Relationship to the delivery plan

This is the fourth milestone scoped under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It sits between build-order step 4 ("SQLite journal/chunks/recovery/outbox", Milestone 3) and step 5 ("Minimal Unreal HUD, boat distance-to-spline, stroke animation states"): it builds the engine-independent consumer that [Milestone 3](03-milestone-3-local-data-session-journal.md) explicitly deferred ("no `WorkoutRuntime`/session-orchestration consumer yet to drive that loop correctly"), so the step 5 Unreal work becomes a thin view over a tested snapshot API.

`WorkoutRuntime` is named in `docs/architecture/02-system-architecture.md` ("plan validation, cue scheduling, PM programming orchestration, summaries"). This milestone implements only the Just Row (FR-004) slice of it: session orchestration, journaling, gap handling, and summaries. Plan validation, cue scheduling, and PM programming are Phase 2 (FR-005).

Nothing in the repository depends on `LocalData` or the `FRowingSessionStateMachine` yet except tests and `Tools/durability-spike`; this milestone is their first real consumer.

## Owner-confirmed scoping decisions (2026-09-19)

- **Scope split.** Milestone 4 is `WorkoutRuntime` only. The Unreal HUD, boat distance-to-spline, and stroke animation are Milestone 5.
- **Threading.** Single-threaded and poll-driven, matching `IRowingMachine::TryPollEvent`. The caller owns `Tick()`. Journal writes are synchronous behind an `IJournalSink` seam; an asynchronous wrapper is deferred to Milestone 5, when a game thread exists.
- **Snapshot delivery.** Pull: `GetSnapshot()` returns the latest immutable snapshot after `Tick()`. No observer callbacks.
- **Summary payload.** A new `session_summary` Protobuf message (totals, averages, gap count) replaces the opaque `FSessionSummary::MetricsPayload` bytes as the runtime's encoded payload.
- **Start.** A session moves Created to Active on the first valid sample after the device reports ready, not on `DeviceReady` alone.
- **Link loss.** The runtime keeps its own reconnect window (default 60 s, configurable) as the outer bound and ends the session `Interrupted` when it elapses, regardless of what the `Concept2PM` reconnect policy is doing. A user `End()` during the window also ends it sooner.
- **Completion.** User `End()` completes the session. Device-reported `ERowingWorkoutState::Complete` ends it Completed (new reason `DeviceCompleted`); device-reported `Terminated` ends it Aborted (new reason `DeviceTerminated`). A PM5 may instead reset directly to `WaitingToBegin`; after a session has become Active, a newer sample reporting both `WaitingToBegin` and inactive rowing also ends it Completed, even when reset time/distance makes the metric payload unusable. Stale-sequence samples never end a session. `Paused` and `Resting` never end a session. No idle timeout exists in this milestone: it was to be off by default until a real-PM5 run confirms idle behavior, so it was not built (see implementation notes).
- **Sample filtering.** Samples carrying `Duplicate`, `TimeRegression`, `DistanceRegression`, or `UnsupportedValue` quality flags (already set by the `RowingDevice` normalization) are rejected from the journal and the snapshot and counted. `MissingField`, `SourceGap`, `DeviceReconnected`, `LateCorrection`, and `Outlier` are journaled as-is because they are provenance the record should keep.
- **Chunk flush.** Buffered samples flush to `sample_chunks` after N samples or T seconds, whichever comes first, and are force-flushed on every session state change and at end.
- **Contract changes are additive only.** New proto messages/fields and enum values; no field number is reused.
- **Real-PM5 run is non-blocking.** The milestone closes on simulator evidence; the real Model D/PM5 confirmation stays flagged as outstanding and owner-run, like Milestone 2's.
- **Driver: `pm5-tui`.** The TUI feeds `WorkoutSession` so the owner has a real-PM5 path without a new tool root. Its journal is sealed with the `LocalDataMac` Keychain cipher and lives in a Git-ignored, owner-only directory. Ordinary TUI output stays normalized/aggregate.

## Implementation defaults settled during scoping (owner may veto)

These follow from reading the existing code and were not separately asked:

- **New `EJournalEventKind` values, additive.** The journal today has only `Started`/`Completed`/`Interrupted`/`Aborted`/`RecoveredAfterUncleanExit`, and `ScanAndRecover` recognizes terminal events by name (`Source/LocalData/Private/LocalDataRecovery.cpp`). The runtime needs to journal capability facts and link gaps, so it adds `CapabilityObserved` and `LinkGap`. Recovery treats any other kind as non-terminal, so existing databases and the durability harness are unaffected. Payloads are the existing `DeviceCapabilityObserved` wire message and a small gap record.
- **A `Started` journal event is written on activation.** `ScanAndRecover` only ends a session that has a `Started` event with no later terminal event, so process-kill recovery depends on it.
- **New state reasons, additive:** `DeviceCompleted`, `DeviceTerminated`, and `ReconnectWindowElapsed` (`DeviceCompleted` takes Active to Ended/Completed, `DeviceTerminated` takes Active to Ended/Aborted, and `ReconnectWindowElapsed` takes ConnectionLost to Ended/Interrupted), mirrored in `session.proto` by name.
- **Link-loss mapping.** Entering `Stale`, `Reconnecting`, or `Failed` from `Ready` maps to `LinkLost`; `ConnectionRestored` maps to `LinkRestored` and its `GapDurationMs` is recorded. Official distance in the snapshot is frozen during the gap and resumes only from the device's own next valid sample — never interpolated.
- **`MetricCorrected` and `StrokeMetricsObserved`.** Corrections replace the target sample in the pending chunk buffer only if it has not flushed; otherwise they are counted and ignored (no journal rewrite). Stroke metrics are carried into the snapshot for the Milestone 5 stroke animation and are not journaled.
- **`IJournalSink` adapter.** A thin adapter over `FLocalDataJournalWriter` lives in `WorkoutRuntime`; tests use an in-memory fake sink, so only the adapter test touches SQLite and no runtime test touches Keychain.

## In scope

1. **`Source/WorkoutRuntime`** (engine-independent; depends on `RowingCore`, `RowingDevice`, `LocalData`; never Apple or Unreal types). Public headers must pass `Scripts/check_public_header_dependencies.py`.
2. **`FWorkoutSession`**: owns the session state machine and takes an injected `IRowingMachine`, monotonic clock, wall clock, entropy source (for the UUIDv7 `FRowingSessionId`), and `IJournalSink`. `Tick()` drains device events; `End()` and `Abort()` are the user paths.
3. **`FWorkoutSnapshot`**: immutable value with session state and disposition, latest normalized metrics (distance, elapsed time, pace/500 m, watts, stroke rate, heart rate when supplied, stroke state, latest stroke metrics — FR-003), connection quality, a stale/gap flag, and counters (rejected samples, ignored corrections, gaps).
4. **Journal integration** via `IJournalSink`: `CreateSession`, `Started`/terminal events, `CapabilityObserved`, `LinkGap`, chunked samples, `UpdateSessionState`, and the two-phase final summary.
5. **Contracts**: `session_summary` message and the new state reasons in `Contracts/proto/rowing/v1/` (additive), with a private domain-to-wire mapper for the summary.
6. **Simulator fixtures** in `Tools/pm5-sim`: a short row ending in `Complete` and one ending in `Terminated`. These are synthetic and simulator-only evidence; the real PM5 has not been observed to report either at the end of a Just Row.
7. **`pm5-tui` wiring** behind an explicit flag so ordinary `make pm5-tui` behavior is unchanged, feeding `WorkoutSession` with a Keychain-sealed journal.
8. **Tests** (behavioral names), driven by `pm5-sim`'s `FMockRowingMachine`/replay fixtures:
   - `workout_session_journals_samples_from_simulator`
   - `workout_session_activates_on_first_valid_sample`
   - `workout_session_rejects_flagged_samples_from_journal_and_snapshot`
   - `workout_session_freezes_distance_on_link_loss`
   - `workout_session_resumes_without_synthesized_meters_after_reconnect`
   - `workout_session_interrupts_after_reconnect_window`
   - `workout_session_completes_on_device_complete_state`
   - `workout_session_aborts_on_device_terminated_state`
   - `workout_session_ends_when_the_device_reports_the_end_with_reset_meters`
   - `workout_session_ends_when_the_device_returns_to_waiting_to_begin`
   - `workout_session_does_not_end_from_an_out_of_order_rejected_sample`
   - `workout_session_does_not_end_from_inconsistent_waiting_state`
   - `workout_session_ignores_paused_and_resting_for_completion`
   - `workout_session_flushes_chunk_on_sample_count` and `workout_session_flushes_chunk_on_elapsed_time`
   - `workout_session_recovers_after_kill_mid_row` (real `FLocalDataJournalWriter`, reusing the `ScanAndRecover` path)
   - `workout_session_write_failure_keeps_local_row_valid`
   - `workout_snapshot_marks_stale_during_gap`
   - `session_summary_wire_mapping_round_trips`
   - Added during implementation: `workout_session_ends_unstarted_session_without_persisting`, `workout_session_resumes_after_stale_link_without_disconnect`, `workout_session_bounds_buffer_when_journal_keeps_failing`, `workout_session_journals_through_local_data_and_reads_back`, `workout_session_ingests_forwarded_events_without_polling_the_machine`, `link_gap_wire_mapping_round_trips_and_rejects_unknown_version`, `workout_session_requires_complete_dependencies`, and three `rowing_session_*` state-machine tests for the new reasons.
9. **Docs**: this checkpoint doc; the Phase 1 README milestone list; the Phase 1 CHANGELOG; `CLAUDE.md` (module boundaries and repository-layout table).

## Out of scope (explicitly, to keep this milestone bounded)

- Any Unreal, `RowingUI`, or `RowingWorld` work: HUD, boat, distance-to-spline, stroke animation — Milestone 5.
- `CourseRuntime` (route mapping and local boat prediction) — Milestone 5.
- An asynchronous or worker-thread journal writer.
- Structured workouts, plan validation, cue scheduling, and CSAFE managed-workout control — Phase 2.
- `Services/` and `sync_outbox` consumption; cloud upload.
- A journal-event reader API and journal rewrite of corrected samples.
- Plaintext-to-sealed migration of Spike B chunks.
- Confirming device `Complete`/`Terminated` behavior on a real PM5, and enabling an idle timeout by default.

## Owner action required (outside this milestone's code)

A short real Model D/PM5 run through the extended `make pm5-tui` to confirm the live loop, journaling, and the device `Complete` state on hardware. Non-blocking for closing this milestone; it stays flagged as outstanding until performed. The TUI will create a Keychain item on first sealed-journal run, and its journal directory holds athlete data: treat it as private evidence, never commit or attach it.

## Compatibility

`FLocalDataJournalWriter`, `ScanAndRecover`, and the existing `EJournalEventKind` values keep their signatures and semantics; new kinds are additive, and recovery ignores kinds it does not treat as terminal. `session.proto` gains only new enumerators. `Tools/durability-spike` and `durability_spike_kill_recover` are unchanged in behavior. `make pm5-tui` behavior is unchanged unless the new flag is passed.

## Verification plan for this milestone

- `make build` and `make test`, including the new `WorkoutRuntime` tests and regression of `local_data_tests`, `local_data_wire_mapping_tests`, `rowing_session_*`, and `durability_spike_kill_recover`.
- `make format-check`.
- `python3 Scripts/check_public_header_dependencies.py` (run by `make test`).
- `git diff --check` and `git status --short` before handoff.
- Simulator evidence only. Real-PM5 confirmation is not claimed.

## Implementation notes (2026-09-19)

Where implementation differs from or settles the scoping text above:

- **Nothing is persisted until the session first becomes Active.** The `sessions` row, the `Started` event, and any pending `CapabilityObserved` are written at activation, so an idle connected PM5, or a session ended before rowing began, leaves no database row (`workout_session_ends_unstarted_session_without_persisting`). `sessions.started_at_utc` is therefore the activation time.
- **Activation rule.** The first sample that passes filtering, arrives while the device is Ready, and reports `WorkoutState == Active` with `RowingState != Inactive`. A connected PM5 that has not started rowing still streams samples, so "first valid sample" alone would start a session for an idle machine. This rule follows the simulator fixtures and the adapter's decoding; how a real PM5 reports an idle machine is unverified.
- **No idle timeout.** It was to be configurable but off by default, so it was not built; adding it later is additive. Completion is user `End()`, device `Complete`, device `Terminated`, or the PM5's post-row transition to `WaitingToBegin` with inactive rowing. Counter-reset end samples may carry time/distance-regression flags and are used only for their trusted end state; their metric values remain rejected.
- **`Ingest()` added to `FWorkoutSession`.** `pm5-tui` drains the machine itself, and two consumers polling one queue would steal each other's events, so the session accepts forwarded events (`Ingest`) as well as draining the machine in `Tick()`.
- **Stale is a link loss.** The simulator (and, per the adapter, a real link) can go `Stale` and resume with `TelemetryResumed` and no `ConnectionRestored`. The runtime treats any non-Ready connection state during an Active session as a link loss, and records the gap with its own measured duration (`device_reported = false`) when the device supplied none.
- **Journal failures.** A failing write is counted, sets `bJournalHealthy = false` in the snapshot, and never ends the session. Buffered samples are retried at the flush interval, bounded by `MaxBufferedSamples` (dropped ones are counted); a final flush at session end that still fails counts what it drops. A failed `CreateSession` is not retried, so later writes for that session will also fail and be counted.
- **Corrections.** `MetricCorrected` replaces a sample only while it is still in the unflushed buffer; otherwise it is counted in `IgnoredCorrectionCount`. The summary's distance and averages come from the accepted samples as they stood, so a correction to the final sample updates the snapshot but the running averages are not recomputed.
- **Summary contents.** Distance and elapsed time are the last accepted device sample; average pace is derived from those two integers; average power, stroke count, and calories are the device's last reported values; average stroke rate is the mean of reported non-zero rates; unreported metrics stay absent.
- **Contracts.** `session_summary.proto` (new file) and `LinkGapRecorded` (in `session.proto`) are additive; `session.proto`'s reason enum gains three values. `LocalData` gained `EJournalEventKind::CapabilityObserved`/`LinkGap` and `FLocalDataJournalWriter::RecordCapabilityObserved`, and now links `RowingDevice` publicly because its public header names `FRowingMachineInfo`.
- **`pm5-tui` driver.** `--journal` (interactive only; `make pm5-tui-journal`) creates a Keychain item (`dev.virtualrowing.pm5-diagnostic` / `workout-journal-data-key`) on first use, journals to `Metrics/pm5-tui/journal/pm5-tui-journal.sqlite3` (owner-only directory, Git-ignored), runs `ScanAndRecover` on open, shows an "End Session" button and a journal status in the status line, logs aggregate session state only, and starts a fresh session after each one ends. It is built only where `LocalDataMac` exists. The driver itself has no automated test because it would create a login-Keychain item; its runtime contract is covered by `workout_session_ingests_forwarded_events_without_polling_the_machine`.
- **Not verified:** the interactive `--journal` TUI run, Keychain creation from the TUI bundle, and everything against a real PM5 (including whether a Just Row ends in `Complete`/`Terminated`).
