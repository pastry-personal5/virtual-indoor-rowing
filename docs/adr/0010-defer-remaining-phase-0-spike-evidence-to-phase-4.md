# ADR-0010: Defer remaining Phase 0 spike evidence to Phase 4 and open delivery Phase 1

- Status: Accepted (the design-partner cohort disposition below is superseded by [ADR-0011](0011-defer-design-partner-cohort-to-phase-4.md); all other decisions in this ADR remain in force)
- Date: 2026-09-17
- Owners: CTO / Principal Architect (A0)

## Context

[The delivery plan](../architecture/10-delivery-plan.md) names six Phase 0 spikes: toolchain, PM5 BLE, managed workout, local durability, visual performance, and realtime. [Phase 0 Milestones 1–3](../phase-0/README.md) closed the toolchain and PM5 BLE spikes with real-hardware evidence. [Phase 0 Milestone 4](../phase-0/07-milestone-4-spikes.md) bundled the remaining three technical spikes except realtime, which it explicitly left unplanned.

Milestone 4 progress as of this ADR:

- **Spike A (managed workout):** CSAFE command-build/response-parse, program-verify readback decoding, and `pm5-tui`/`make hil-pm5` command wiring (work-sequence steps 1–3) are implemented and unit-tested (see [the evidence report](../phase-0/04-evidence-report.md)'s Milestone 4 Spike A table). Step 4 — real-PM5 acceptance runs for one distance, one time, one interval workout, and one deliberate reject/abort — is owner-run hardware evidence that has not been performed.
- **Spike B (local durability):** complete. All kill/recover scenarios, corrupt-tail/duplicate handling, and recovery idempotency pass; only an incidental confirmation that the suite has been observed running in the self-hosted CI lane remains open, and that requires no new work, only an observed push.
- **Spike C (visual performance):** not started. No test level, no capture, no evidence-report section exists yet, at either the milestone's 6-minute proxy duration or the delivery plan's original 60-minute duration.
- **Realtime spike:** not started, per Milestone 4's own explicit exclusion.

Separately, the Phase 0 exit gate in `10-delivery-plan.md` names a design-partner cohort ("named," with "observed first-use evidence") as an exit condition, while Phase 1's own exit gate already independently requires "design partners can attempt the supported setup without an engineer driving the UI." Treating cohort recruitment as a Phase 0 gate item duplicates a requirement Phase 1 already carries and blocks starting Phase 1 on an activity (recruiting and observing external partners) that does not need to precede it — it needs to land before Phase 1 *closes*, not before it *opens*.

The owner (A0) has directed that the remaining, unfinished Phase 0 spike work be deferred to Phase 4 rather than gating the start of delivery Phase 1 on it, following the same reasoning already applied once for signing/notarization ([ADR-0008](0008-defer-macos-signing-to-phase-4.md)) and once for the Bluetooth TCC scenario matrix ([ADR-0009](0009-defer-bluetooth-tcc-scenario-matrix-to-phase-4.md)): a small internal/named-partner cohort does not yet need this evidence, and Phase 4 is where the project next stands up hardware/manual-rehearsal gates before wider or public exposure.

## Decision

- **Milestone 4 Spike A** closes on work-sequence steps 1–3 alone. Step 4 (real-PM5 acceptance runs for distance, time, interval, and reject/abort) is deferred to the Phase 4 exit gate. Until Phase 4, the evidence report records those rows as `Deferred to Phase 4 (ADR-0010)`, not `Pending`. This diagnostic-only contract may still evolve before Phase 4; deferring the hardware runs does not freeze `IConcept2PMRunDiagnostics`'s workout surface.
- **Milestone 4 Spike B** is accepted as complete. The outstanding CI-observation row remains `Pending` (not deferred) — it needs no additional work, only the next `main` push, and is not a Phase 0/1 blocker.
- **Milestone 4 Spike C** (visual performance, both the milestone's 6-minute proxy and the delivery plan's original 60-minute duration) is deferred to Phase 4 in its entirety. It is not attempted in Phase 0. Phase 1's own build order already carries lighter, functional Unreal work (minimal HUD, boat distance-to-spline, stroke animation states) that does not depend on this spike's evidence.
- **The realtime spike** is deferred to Phase 4 in its entirety, consistent with Phase 4 already being the delivery phase that stands up the race gateway/room/worker and WSS protocol under ADR-0003. No separate pre-Phase-4 realtime evidence is required.
- **The Phase 0 exit gate** in `10-delivery-plan.md` is revised: it no longer requires reproducible evidence for Spike A step 4, Spike C, or the realtime spike. It is satisfied by Milestones 1–3, Milestone 4 Spike A steps 1–3, and Milestone 4 Spike B, plus this ADR's explicit descope decision for the rest, per the exit gate's own "reproducible code/reports or an explicit decision to descope" clause.
- **The design-partner cohort item** moves off the Phase 0 exit gate. Recruitment may begin during Phase 0 or Phase 1 but is not a condition of starting Phase 1; it remains a hard condition of *closing* Phase 1, where the delivery plan already requires it.
- **Delivery Phase 0 is exited** on this revised gate, and **delivery Phase 1 (walking skeleton) is opened**, effective this ADR.

## Consequences

- Phase 1 work can begin immediately without waiting on real-PM5 managed-workout hardware runs, a visual-performance spike, a realtime spike, or a named design-partner cohort.
- Phase 4's exit gate grows three more owner-run/technical items, in addition to the ADR-0008 signing/notarization rehearsal and the ADR-0009 TCC matrix: the Spike A real-PM5 managed-workout acceptance runs, the full (6-minute and 60-minute) visual-performance evidence, and the realtime spike's original evidence bar (two load clients, 20/10 Hz, time sync, reconnect, deterministic replay/result), folded into Phase 4's realtime buildout rather than repeated as a standalone Phase 0-style spike.
- The managed-workout CSAFE contract (`ProgramDiagnosticWorkout`/`AbortDiagnosticWorkout`/`WorkoutProgramVerified`/`WorkoutProgramRejected`) and the visual/render performance budgets in `03-macos-unreal-client.md` remain unvalidated against real hardware until Phase 4; Phase 1–3 work that depends on them (e.g., Phase 2's "PM managed mode where proven," Phase 1/2 rendering) carries that unmitigated risk knowingly, the same posture ADR-0008/0009 already accepted for signing and TCC.
- Design partners are not yet named when Phase 1 begins; Phase 1's own exit gate is the forcing function that ensures they exist before Phase 1 closes.

## Alternatives considered

- **Keep all of Milestone 4 and the realtime spike as Phase 0 exit-gate blockers:** rejected; it blocks Phase 1 on owner-run hardware/performance evidence and an unstarted realtime spike that a small internal cohort does not yet need, for capabilities (managed workout, racing) whose product features are not scheduled until Phase 2/4 anyway.
- **Defer the design-partner cohort to Phase 4 as well:** rejected; Phase 1's exit gate already needs a cohort to exist, so deferring it three phases would create an internal contradiction rather than removing a redundant gate.
- **Leave Spike B's CI-observation row open as a blocker:** rejected; it requires no further work and is not evidence of a real gap, only of timing.

## Validation and revisit

Revisit if Phase 2's managed-workout scope ("PM managed mode where proven") is committed before Phase 4 closes — that would require pulling Spike A step 4 forward, not waiting for Phase 4. Revisit if Phase 1/2 rendering work repeatedly misses the frame budgets in `03-macos-unreal-client.md` without any visual-performance spike evidence to explain why. Revisit if a Phase 0–3 cohort needs to grow beyond a small named group before Phase 4, consistent with ADR-0008/0009's existing revisit triggers.
