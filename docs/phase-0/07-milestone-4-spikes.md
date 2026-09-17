# Delivery Phase 0 Milestone 4 — managed workout, local durability, and visual performance spikes

Status: Planned
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-17

## Purpose and boundary

This milestone bundles three of the six delivery Phase 0 spikes defined in [the phased delivery plan](../architecture/10-delivery-plan.md): **managed workout**, **local durability**, and **visual performance**. It does not include the **realtime** spike, which remains separately unplanned.

Like Milestones 1–3, each spike here produces reproducible evidence or an explicit decision to descope — it does not implement a product feature. There is no `WorkoutPlan` authoring, no product session history/export, no shipped content pipeline, and no gameplay in this milestone. Completing it records Phase 0 progress only. It does not pass the delivery Phase 0 exit gate by itself: the realtime spike and the business/external actions listed in `10-delivery-plan.md` (design-partner cohort, Concept2 developer relations, licensing review, and the rest) remain separately open regardless of this milestone's outcome.

Each spike below needs a capability the current [phase-0 README](README.md) out-of-scope list excludes (CSAFE control commands, SQLite journaling, Unreal rendering/route content). That list is amended with narrow, bounded carve-outs scoped to this milestone's diagnostic evidence only — see the README changes accompanying this doc. A carve-out authorizes spike evidence, not the corresponding product feature.

## Sequencing

Owner-confirmed (2026-09-17): **Spike B (local durability) goes first.** It needs no real PM5 and no Unreal runtime, so it is the lowest-external-dependency proof of the new `LocalData` module and can start immediately. Spikes A and C follow, and may run in parallel with each other once B's `Source/LocalData` contract checkpoint has landed, since neither depends on B's implementation — only B's precedent of "spike module, minimal schema, no product persistence" informs their own scope discipline.

## Spike A — Managed workout (CSAFE)

Ties to `FR-005` (structured workouts, `docs/architecture/01-product-scope.md`) and [ADR-0002](../adr/0002-pm5-ble-integration.md)'s CSAFE decision.

**Goal:** configure and read back one distance workout, one time workout, and one interval workout on a real PM5 using only published CSAFE commands, plus an explicit reject/abort path.

**Bounded:** no `WorkoutPlan` domain object, no cue policy, no plan authoring UI. The spike is a diagnostic-only control path added to the existing `pm5-tui`/HIL surface — parallel to Milestone 1's `DiagnosticOnly`/`DiagnosticSampleObserved` precedent — rather than a production `RowingDevice` public control API. This defers committing the real managed-workout contract to a later product milestone while still proving the CSAFE program/verify transaction works.

**Work sequence:**

1. Contract checkpoint — A0 approves the minimal diagnostic program/verify command shape and its result/event shape (e.g. `ProgramDiagnosticWorkout`, `WorkoutProgramVerified`, `WorkoutProgramRejected`) before implementation begins. This is an addition to [public interfaces](02-public-interfaces.md), reviewed the same way as any other public-contract change.
2. `Concept2PMCore` CSAFE command-build and response-parse additions for configure, start, status-readback, and end/abort, using only the published Concept2 CSAFE specification.
3. Owner-confirmed (2026-09-17): bounded diagnostic command wiring extends `pm5-tui` under the existing HIL-gated path (`make hil-pm5`), reusing its owner-run real-hardware workflow rather than a new separate tool — no new always-on control surface.
4. Real-PM5 acceptance runs: one distance workout, one time workout, one interval workout, and one deliberate reject/abort.

**Acceptance gate:** for each of the three workout types, the PM5 read-back type/duration/state matches the configured request; the reject/abort path leaves the PM5 and the diagnostic session in a well-defined, observable state; no command outside the published CSAFE specification is ever sent; A0 has approved the diagnostic control contract addition.

**Owner:** A1 (`Plugins/Concept2PM`). Contract review: A0.

## Spike B — Local durability (SQLite WAL journal)

Ties to `FR-007` and `QA-003` (`docs/architecture/01-product-scope.md`) and [ADR-0004](../adr/0004-offline-first-session-journal.md).

**Goal:** prove that an append-only, one-second checksummed-chunk SQLite WAL journal at 10 Hz survives a process kill at each of: mid-chunk write, between chunks, and during the final-summary transaction. Recovery must lose at most the uncommitted second and must explicitly mark `recovered_after_unclean_exit` — this is ADR-0004's validation section, restated as this spike's acceptance test.

**Bounded:** introduces the already-"planned" `LocalData` module (`CLAUDE.md` repository-layout table) as `Source/LocalData`, with only the schema subset this spike needs: `schema_migrations`, `journal_events`, `sample_chunks`. The full `sessions`, `sync_outbox`, and `cloud_links` tables from [the macOS/Unreal client architecture](../architecture/03-macos-unreal-client.md) are Phase 1+ and out of scope here. The spike uses synthetic samples shaped like `FRowingMetricSample` (reusing `RowingCore` telemetry types and a `pm5-sim`-style generator) — it needs no real PM5 and no Unreal runtime.

**Work sequence:**

1. Contract checkpoint — A0 approves the minimal `Source/LocalData` write/recovery interface (append chunk, commit summary+outbox transaction, scan-and-recover) and its dependency direction (`LocalData` depends on `RowingCore` telemetry types only; nothing depends on `LocalData` yet).
2. Implement the WAL-mode writer: one-second CRC32C-checksummed chunks, a bounded durable-writer queue off the harness's main thread, and the scan/truncate-incomplete-tail recovery routine.
3. Build a bounded spike harness under `Tools/durability-spike` (matching the existing `Tools/pm5-tui`, `Tools/pm5-sim` pattern) that runs a synthetic 10 Hz sample loop and can be killed at a requested write-boundary offset by an external test driver, then rerun to exercise recovery.
4. Deterministic tests: kill at each of the three boundary classes above, corrupt-tail injection, and duplicate-chunk detection.
5. Owner-confirmed (2026-09-17): wire the kill/recover harness into the self-hosted CI lane (alongside `make test`) rather than leaving it owner-run/manual — it needs no real hardware, so it can run deterministically on every `main` push the same way the native quality gates do.

**Acceptance gate:** for each of the three kill points, rerun proves no data loss beyond the uncommitted last second, `recovered_after_unclean_exit` is set, every committed chunk's checksum validates, and reopening never crashes or hangs. Corrupt-tail and duplicate scenarios behave as ADR-0004 describes (truncate incomplete tail; deduplicate on reconciliation). The CI lane runs this suite on every push and its result is part of the milestone's recorded evidence, not a separate manual step.

**Owner:** A2 (new module) with A7 for build/target wiring. Contract review: A0.

## Spike C — Visual performance (6-minute proxy run)

Ties to `FR-006` and `QA-001` (`docs/architecture/01-product-scope.md`) and the frame-budget table in [the macOS/Unreal client architecture](../architecture/03-macos-unreal-client.md) (game thread 4.0 ms p95, render thread 5.0 ms p95, GPU 14.0 ms p95).

**Goal:** one representative water route plus one detailed boat/avatar placeholder, at 2560×1600/60 fps, for a **6-minute** run on the reference M5 Max MacBook Pro.

**Explicit scope reduction:** `10-delivery-plan.md`'s spike table originally specified 60 minutes. At the owner's explicit direction, this milestone records evidence at 6 minutes instead — see the accompanying annotation in `10-delivery-plan.md`. A 6-minute pass is a reduced-duration proxy, not equivalent evidence to a 60-minute thermal soak; it does not close the full-duration measurement, which remains open as later follow-on before any decision that depends on sustained thermal behavior.

**Bounded:** a spike-only, non-shipping test level with placeholder meshes — not `Content/` production assets, not part of a content pipeline (still "planned," unauthorized by this milestone). No gameplay logic, no PM integration, no UMG/CommonUI HUD; Unreal's built-in stat commands and Insights capture are sufficient instrumentation.

**Work sequence:**

1. Scope checkpoint — A0 confirms the narrow rendering/content carve-out in the phase-0 README covers exactly this bounded test level and nothing broader.
2. Owner-confirmed (2026-09-17): the test level extends the existing `VirtualRowing.uproject` diagnostic host rather than a separate scratch project, reusing the pinned toolchain, package-verify, and CI lane already proven in Milestone 3. It is added as one throwaway level/content set, clearly marked non-shipping and excluded from the Milestone 3 empty-launch-map contract (`/Engine/Maps/Entry` remains the default launch map; this level is opt-in only).
3. Build the level: representative water surface plus placeholder boat/avatar meshes, sized qualitatively against the per-route Niagara/translucency/skeletal-mesh budget guidance in `03-macos-unreal-client.md` (the full automated content validator remains Phase 2 work).
4. Capture a 6-minute run at 2560×1600 with Unreal Insights and Metal tooling: game-thread, render-thread, and GPU p95, frame-time histogram, and thermal state.
5. Record redacted aggregate evidence only — no raw trace file is committed, consistent with the existing rule against committing generated Unreal directories.

**Acceptance gate:** over the 6-minute run, game thread ≤4.0 ms p95, render thread ≤5.0 ms p95, GPU ≤14.0 ms p95, sustained 60 fps, and no observed thermal throttling in that window — or an explicit recorded miss with margin. The evidence report states plainly that this is a 6-minute measurement, not a 60-minute one.

**Owner:** A3 (Unreal client/content — first work assigned into this previously-unassigned slot; see [architecture and ownership](01-architecture-and-ownership.md)). Scope review: A0. Integration review: A8.

## Overall acceptance gate

Milestone 4 is complete when each spike's acceptance gate above is met and recorded in [the evidence report](04-evidence-report.md) using the existing status vocabulary (`Pass`, `Pending`, `Deferred`), with source revision, host/hardware class, and timestamp. Missing evidence is recorded as `Pending`, never inferred from a partial run.

Milestone 4's completion does not pass the delivery Phase 0 exit gate, which evaluates all Phase 0 spikes and business/external actions together. After this milestone, the realtime spike remains the only unplanned technical spike, and the business/external actions in `10-delivery-plan.md` remain open.

## Risks

| Risk | Early signal | Response | Owner |
|---|---|---|---|
| CSAFE transaction leaves PM5 in an ambiguous programmed state | Read-back mismatch or timeout after configure/start | Fail closed, record redacted state, do not retry with unpublished commands | A1 |
| WAL kill/recover behavior differs from measured expectations under real disk conditions | Checksum failure or loss beyond the uncommitted second | Treat as a real defect, not a harness bug; do not weaken the chunk/commit boundary to pass | A2 |
| 6-minute run does not predict 60-minute thermal throttling | Longer informal runs show drift the 6-minute window misses | Record the limitation explicitly in evidence; do not present 6-minute evidence as satisfying the original 60-minute requirement | A3/A0 |
| Rendering/content carve-out scope creeps toward product content | Test level accumulates gameplay logic or production-quality assets | A0/A8 reject; keep the level throwaway and delete or replace before any later content-pipeline milestone | A0/A8 |

## Explicitly out of scope

- Realtime spike (Go race-room loop, load clients, time sync) — separate future milestone.
- Any product `WorkoutPlan`, session history, export, or content-pipeline feature — these spikes produce evidence only.
- Full 60-minute visual-performance thermal evidence — left open, not satisfied by this milestone's 6-minute run.
- Business/external actions (design-partner cohort, Concept2 developer relations, licensing/legal review, launch-market assessment) — unchanged by this milestone.
