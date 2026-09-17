# Delivery Phase 0 diagnostic changelog

This document records changes to the bounded diagnostic milestones that contribute to delivery Phase 0. It does not assert completion of the delivery Phase 0 exit gate.

## 2026-09-14

- **Phase 0 Milestones 1–2:** Classified the diagnostic packet as a bounded contribution to delivery Phase 0.
- **Phase 0 Milestones 1–2:** Renamed internal “Phase 1/2” work to Milestones 1/2 so it cannot be confused with the product-delivery phases.

## 2026-09-17

- **Phase 0 Milestone 4:** Scoped and planned. Bundles three of the six delivery Phase 0 spikes from `docs/architecture/10-delivery-plan.md`: managed workout (CSAFE configure/read-back diagnostic), local durability (synthetic-sample SQLite WAL kill/recover harness), and visual performance (non-shipping test level and run). The realtime spike is explicitly excluded from this milestone. See [Milestone 4](07-milestone-4-spikes.md).
- **Phase 0 Milestone 4:** The visual-performance run duration is set to 6 minutes, an explicit, recorded reduction from the delivery plan's stated 60-minute target. Full 60-minute thermal evidence remains open, not satisfied by this milestone. See the annotation in `docs/architecture/10-delivery-plan.md`.
- **Phase 0 README:** Added narrow, bounded out-of-scope carve-outs for the three Milestone 4 spikes (CSAFE diagnostics, synthetic-sample durability harness, non-shipping visual-performance test level); each carve-out authorizes spike evidence only, not the corresponding product feature.
- **Phase 0 Milestone 4:** Owner-confirmed sequencing and implementation-surface decisions: Spike B (local durability) goes first, needing neither real PM5 nor Unreal; Spike A's CSAFE diagnostic extends `pm5-tui`/`make hil-pm5` rather than a new tool; Spike B's kill/recover harness runs in the self-hosted CI lane rather than staying owner-run/manual; Spike C's test level extends `VirtualRowing.uproject` rather than a separate scratch project.

### Added

- **Phase 0 Milestone 4 Spike B:** Implemented the bounded `Source/LocalData` SQLite WAL-mode journal (`schema_migrations`/`journal_events`/`sample_chunks` only) and the `Tools/durability-spike` kill/recover harness. Links the macOS SDK's system `libsqlite3` (owner-confirmed 2026-09-17) rather than vendoring an amalgamation. Adds `local_data_tests` (unit-level checksum/corruption/dedup/idempotency coverage) and `durability_spike_kill_recover` (real-process `SIGKILL` at each of the three ADR-0004 boundary classes: mid-chunk write, between chunks, during the final-summary transaction), both registered as ordinary CTest tests so `make test` and the existing self-hosted CI lane pick them up with no separate CI change. Recorded the contract in [public interfaces](02-public-interfaces.md) and evidence in [the evidence report](04-evidence-report.md). Spikes A and C remain unstarted, deferred to a follow-up since their acceptance gates need real PM5 hardware and a 6-minute Unreal Insights capture.
- **Phase 0 Milestone 3:** Adopted [ADR-0009](../adr/0009-defer-bluetooth-tcc-scenario-matrix-to-phase-4.md): the human-only TCC-reset Bluetooth permission-scenario matrix (fresh Allow, Deny, Settings-repair-then-Allow, normal-launch-no-prompt) is no longer a condition of this milestone; it is rescheduled to the Phase 4 exit gate alongside the ADR-0008 signing/notarization rehearsal.
- **Phase 0 Milestone 3:** Completed. Acceptance-gate items 1 (unsigned native/Unreal build, format check, package verification) and 2 (self-hosted Apple-silicon CI on `main`) were already met on 2026-09-16; with item 3 deferred per ADR-0009, the milestone is closed.
- **Delivery plan:** Removed "fresh install/permission denial/repair pass" from the Phase 1 exit gate and added the Bluetooth TCC permission-scenario matrix to the Phase 4 exit gate, per ADR-0009. Updated `docs/architecture/02-system-architecture.md` and `docs/architecture/00-executive-review.md` to match.

## 2026-09-16

- **Phase 0 Milestone 3:** Added the bounded unsigned Unreal Shipping toolchain host, package verification, explicit Bluetooth-permission probe, and protected credential-owner release procedure. The signing/notarization/Gatekeeper evidence remains pending.
- **Phase 0 Milestone 3:** Added the reference volume's approved UE 5.8 discovery location, deterministic staged-app tree hashing in retained package-verification output, and explicit Hardened Runtime inspection alongside the existing `get-task-allow` rejection. These are verifier hardening changes only; the required CI and TCC evidence remains pending.
- **Phase 0 Milestone 3:** Adopted [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md): the protected Developer ID signing/notarization/Gatekeeper execution is no longer a condition of this milestone's completion. Rescoped the acceptance gate to the unsigned portion only and reclassified that evidence-report row from Pending to Deferred to Phase 4.
