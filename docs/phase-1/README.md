# Delivery Phase 1: walking skeleton

Status: Complete — Pass (2026-09-20). All non-deferred Phase 1 exit-gate evidence is verified in [the evidence report](04-evidence-report.md); ADR-deferred evidence remains assigned to Phase 4.
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-20

## Purpose

Delivery Phase 1 turns the Phase 0 diagnostic evidence into the first end-to-end product path: an unsigned, ad-hoc-built internal macOS app that connects to a real PM5, rows a gray-box route, shows live metrics, durably journals every session, survives offline/cloud absence, and uploads to a minimal development API. This is the "walking skeleton" — thin, but end-to-end and real, not a diagnostic tool.

## Current development journal policy

Per [ADR-0012](../adr/0012-plaintext-development-single-user-journals.md),
development and single-user journals are owner-only plaintext SQLite/WAL data
from 2026-09-20 onward. This changes neither local durability nor the privacy
handling of workout data; journals remain private and uncommitted. Encryption
at rest remains the required future posture for multi-user and release scopes.

## Relationship to the product delivery plan

This packet will contain one or more bounded milestones within [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks). As with Phase 0, a milestone's completion records progress only; the delivery Phase 1 exit gate is a separate evaluation against all of that phase's milestones together, recorded the same way Phase 0's was in [the phase-0 evidence report](../phase-0/04-evidence-report.md).

Phase 0 exited on 2026-09-17 per [ADR-0010](../adr/0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md): Milestones 1–3, Milestone 4 Spike A (steps 1–3), and Milestone 4 Spike B provided the retained evidence; Milestone 4 Spike A step 4, Spike C, and the realtime spike were explicitly descoped to the Phase 4 exit gate rather than blocking Phase 1's start.

That ADR also moved the design-partner cohort item off the Phase 0 exit gate and onto this phase's exit gate. [ADR-0011](../adr/0011-defer-design-partner-cohort-to-phase-4.md) subsequently deferred it again, to Phase 4's exit gate, because recruitment is not feasible on the current timeline. Recruiting and observing that cohort is therefore not required to close this phase.

## Deliverable

Per [the delivery plan](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks): an unsigned, ad-hoc-built internal macOS app (per [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md), launched via explicit Gatekeeper right-click-Open) connects to the PM5, rows a gray-box route, shows live metrics, journals every session, survives offline/cloud absence, and uploads to a minimal development API.

## Build order (from the delivery plan)

1. Repository rules, pinned manifest, CI, stock engine, module boundaries.
2. `RowingCore` types/units/states and Protobuf contract generation.
3. PM simulator, protocol codec, CoreBluetooth adapter, capability handshake.
4. SQLite journal/chunks/recovery/outbox.
5. Minimal Unreal HUD, boat distance-to-spline, stroke animation states.
6. Control API session endpoint, PostgreSQL, object upload, worker validation.
7. Unsigned ad-hoc build/package/verify pipeline and redacted diagnostics; Developer ID signing/notarization is a Phase 4 build-order item per ADR-0008.

Milestones are scoped individually immediately before implementation; Milestones 1–10 are recorded below.

## In scope (once milestones are scoped)

- A real, non-diagnostic `RowingCore` domain (types, units, state machines) and its Protobuf contract, versioned per `docs/architecture/06-data-and-protocols.md`.
- Product `RowingDevice`/`Concept2PM` control path reused from the Phase 0 diagnostic foundation, promoted from diagnostic-only to a stable public contract where Phase 0 marked it diagnostic-only (e.g. the Milestone 4 Spike A workout contract).
- The full `LocalData` journal/outbox schema (`sessions`, `sync_outbox`, `cloud_links`), building on the Phase 0 Milestone 4 Spike B `schema_migrations`/`journal_events`/`sample_chunks` subset.
- A minimal Unreal product UI: HUD, gray-box route, boat distance-to-spline, stroke animation — first real `RowingUI`/`RowingWorld` work, not a diagnostic host.
- A minimal control-plane API and object upload path (`Services/` — still `planned` in the repository layout table and created only as this phase authorizes it; `Contracts/proto/` moved to `exists` as of Milestone 1, scoped to the one `rowing/v1/session.proto` file added there).
- Unsigned ad-hoc build/package/verify for the product app, distinct from the Milestone 3 empty diagnostic Shipping host.

## Out of scope

- Developer ID signing, notarization, and the Sparkle update channel — Phase 4 per ADR-0008.
- The Bluetooth TCC permission-scenario matrix and the fresh-install/permission-denial/repair pass — Phase 4 per ADR-0009.
- Real-PM5 managed-workout acceptance runs, the visual-performance spike, and the realtime spike — Phase 4 per ADR-0010.
- Accounts, cloud sync, billing, social features, and ranked racing — Phase 3/4.
- Production content pipeline, polished onboarding, structured workout plans/cues, and FIT export — Phase 2.

## Exit gate (from the delivery plan)

- FR-002/003/004/007 demonstrated on real Model D/PM5 and simulator.
- 6-minute hardware run meets preliminary latency/durability; process-kill recovery evidence exists. Reduced from an originally specified 60 minutes, an explicit owner-directed scope reduction (2026-09-17); see [the delivery plan](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks). Full 60-minute latency/durability evidence remains open as later follow-on.
- No account, subscription, external integration, or multiplayer required to complete the row.

The design-partner cohort ("design partners can attempt the supported setup without an engineer driving the UI; failures are categorized") is deferred to Phase 4's exit gate per [ADR-0011](../adr/0011-defer-design-partner-cohort-to-phase-4.md); it is not a Phase 1 exit-gate requirement.

## Milestone scoping approach

Owner-confirmed (2026-09-17): Phase 1 milestones are **not** pre-planned as a full breakdown of the seven-step build order above. Each milestone is scoped individually, immediately before its own implementation begins — an A0 contract checkpoint per milestone, the same pattern Phase 0 Milestone 4 used per spike, applied here per milestone instead of committing to a fixed decomposition up front. The build order above is the reference sequence a milestone's scope is drawn from, not a milestone list itself.

## Milestones

- **Milestone 1 — real `RowingCore` session domain + Protobuf contract seed** (build-order step 2): implemented 2026-09-17. See [01-milestone-1-rowingcore-domain-contract.md](01-milestone-1-rowingcore-domain-contract.md) for the contract checkpoint, scope, and verification evidence.
- **Milestone 2 — product `RowingDevice`/`Concept2PM` capability handshake** (build-order step 3): scoped and implemented 2026-09-18. The reviewed, non-diagnostic product profile and its factory wiring turned out to already exist from Phase 0 (`Concept2PM::GetGeneratedPM5CapabilityProfiles()`, generated from `Config/PM5Capabilities.json`, already resolved by `CreateConcept2PMDiscovery()`) — the milestone's implementation work narrowed to adding acceptance/rejection tests against that real profile set (`Tests/Contract/pm5_reviewed_profile_tests.cpp`) and correcting the milestone doc's premise. Managed-workout (CSAFE) control stays diagnostic-only and telemetry wire mapping is deferred to Milestone 3. Real-PM5 confirmation run is owner-run and outstanding. See [02-milestone-2-device-capability-handshake.md](02-milestone-2-device-capability-handshake.md).
- **Milestone 3 — product `LocalData` session journal + telemetry/capability wire mapping** (build-order step 4): scoped 2026-09-18, implemented 2026-09-19 (automated checks only; no hardware needed). Promotes the Phase 0 Milestone 4 Spike B bounded journal to the full 9-table schema in `docs/architecture/03-macos-unreal-client.md`, adds AES-256-GCM/Keychain encryption at rest, and adds the `DeviceCapabilityObserved`/`MetricSampled` Protobuf wire mapping deferred by Milestone 2. No live device→journal capture loop yet — schema/mapping/encryption plus tests only. See [03-milestone-3-local-data-session-journal.md](03-milestone-3-local-data-session-journal.md).

- **Milestone 4 — `WorkoutRuntime` live session loop** (between build-order steps 4 and 5): scoped and implemented 2026-09-19. Builds the engine-independent Just Row orchestrator that Milestone 3 deferred — device events to session state machine to sealed journal, with gap handling, a pull snapshot API for the Milestone 5 HUD, and a `pm5-tui` driver. Simulator evidence only; the real-PM5 confirmation and the interactive `--journal` TUI run are owner-run and outstanding. See [04-milestone-4-workout-runtime.md](04-milestone-4-workout-runtime.md).

- **Milestone 5 — Unreal integration and live HUD, simulator-driven** (first part of build-order step 5): scoped and implemented 2026-09-19 (automated checks and a headless Automation spec; the owner confirmed the HUD in the Editor and the packaged Shipping app). The UBT linkage spike succeeded with static protobuf/abseil, so no Homebrew fallback was taken. Gets the engine-independent runtime running inside the Unreal app: UBT links prebuilt CMake static libraries (spike first, gating, including an attempt at a self-contained static protobuf/abseil built from pinned sources, falling back to the Homebrew dylibs if that fails), a game-thread `UWorkoutSubsystem` owns the session, and a code-only `UUserWidget` HUD shows the FR-003 metrics from `-SimulatorDevice` fixtures. `Tools/pm5-sim` moves to `Source/RowingSim` so the app does not depend on `Tools/`. Real CoreBluetooth wiring, the PM5 picker, and Keychain-sealed journaling in the app are a separate follow-up milestone; route, boat distance-to-spline, and stroke animation are a later one; latency instrumentation is deferred. See [05-milestone-5-unreal-hud.md](05-milestone-5-unreal-hud.md).

- **Milestone 6 — hardening, refactor, and chores** (no build-order step): scoped and implemented 2026-09-19 (automated checks only). Fixes the five defects deferred from Milestones 4 and 5 (real-adapter `Ready`/`Restored` order, `CreateSession` retry, `WriteSummary` commit failure, per-session summary revision, `pm5-tui` driver polling and mid-row End Session), does mechanical splits of `pm5-tui`'s `main.cpp`/`RunMetricsWriter.cpp` and `Scripts/dev.py`, renames `pm5-sim` to `RowingSim`, and clears recorded chores. No new product behavior. See [06-milestone-6-hardening-and-refactor.md](06-milestone-6-hardening-and-refactor.md).

- **Milestone 7 — real PM5 wiring in the Unreal app** (completes the CoreBluetooth adapter's use in the product app; first hardware evidence toward FR-002/003/007 and the exit-gate run; FR-004 still needs the route milestone): scoped and implemented 2026-09-19 (automated checks, Editor-target link and a headless Automation spec; real-PM5 and packaged-app checks are owner-run). The adapter links into the app; a click-to-connect flow (Bluetooth is only created on user action, never in the Editor), a nearest-first PM5 picker and device panel, an owner-only journal with launch recovery and an explicit journal-unavailable policy, a sub-second checkpoint target, and session-linked latency instrumentation. ADR-0012 supersedes the milestone's original Keychain-sealed development-journal policy. See [07-milestone-7-real-pm5-app-wiring.md](07-milestone-7-real-pm5-app-wiring.md).

- **Milestone 8 — gray-box course, boat, and stroke presentation** (completes build-order step 5; FR-004/FR-006 presentation slice): implemented 2026-09-19. Adds engine-independent `CourseRuntime`, a Game/PIE-only `UCourseSubsystem`, runtime-generated 2 km oval and primitive single-scull proxy, bounded distance prediction/correction, PM-state and rate-fallback stroke motion, rigid side camera, the conditional `Estimated stroke motion` HUD label, a `state_missing` simulator fixture, `course_runtime_tests`, and `VirtualRowing.CoursePresentation` Automation coverage. The world remains reversible presentation: HUD/journal distance is cumulative PM authority while only spline position wraps. Packaged simulator visual confirmation is outstanding; real-PM5 proof remains part of the combined phase exit gate. See [08-milestone-8-gray-box-course-presentation.md](08-milestone-8-gray-box-course-presentation.md).

- **Milestone 9 — local development session-sync vertical slice** (build-order step 6, development infrastructure only): implemented local durability and contract evidence. Adds the versioned OpenAPI/protobuf export contracts, loopback bootstrap service, atomic terminal event/state/sealed-summary/outbox commit, encrypted ordered journal readback, owner-only Compose bootstrap helper, NSURLSession transfer coordinator, and a digest-validating development worker whose projection is observable through the API. Compose/package end-to-end evidence remains owner-run and is not represented as Phase 1 exit evidence. See [09-milestone-9-development-session-sync.md](09-milestone-9-development-session-sync.md).

- **Milestone 10 — persistent development session sync** (completion of the development-infrastructure portion of build-order step 6): completed on automated implementation evidence. Uses PostgreSQL for session lifecycle/idempotency/worker leases and MinIO for identity-confined immutable objects, with a loopback-only presigned upload grant and durable worker status. It remains development-only; Compose and packaged-app follow-up evidence are owner-run. See [10-milestone-10-persistent-development-sync.md](10-milestone-10-persistent-development-sync.md).

- **Milestone 11 — closeout and sync-hardening chores**: completed on automated evidence. Reconciles the Phase 1 packet, adds the exit-gate evidence matrix, adds the forward-safe development-sync migration, and exposes `make phase1-check` and `make development-sync-test`. It does not execute or waive owner-run Phase 1 exit evidence. See [11-milestone-11-closeout-and-sync-hardening-chores.md](11-milestone-11-closeout-and-sync-hardening-chores.md).

## Open questions

The Phase 1 exit-gate evidence report is pending owner-run hardware, packaged-app, and environment-specific checks; this does not change the implementation status of completed milestones.

## Completion rule

Each milestone in this phase is complete only when its required automated checks pass and, where the milestone's scope includes real-hardware behavior, its required real Model D/PM5 hardware run meets the applicable exit criteria — the same rule Phase 0 used. Completing every milestone in this phase records progress only; the delivery Phase 1 exit gate above is evaluated separately and recorded in this packet's own evidence report once one exists.
