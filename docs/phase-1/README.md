# Delivery Phase 1: walking skeleton

Status: Open (per [ADR-0010](../adr/0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md), 2026-09-17); milestones are scoped one at a time, immediately before each one's implementation — Milestone 1 is scoped and implemented (see [01-milestone-1-rowingcore-domain-contract.md](01-milestone-1-rowingcore-domain-contract.md)); no further milestone scoped or started yet
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-17

## Purpose

Delivery Phase 1 turns the Phase 0 diagnostic evidence into the first end-to-end product path: an unsigned, ad-hoc-built internal macOS app that connects to a real PM5, rows a gray-box route, shows live metrics, durably journals every session, survives offline/cloud absence, and uploads to a minimal development API. This is the "walking skeleton" — thin, but end-to-end and real, not a diagnostic tool.

## Relationship to the product delivery plan

This packet will contain one or more bounded milestones within [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks). As with Phase 0, a milestone's completion records progress only; the delivery Phase 1 exit gate is a separate evaluation against all of that phase's milestones together, recorded the same way Phase 0's was in [the phase-0 evidence report](../phase-0/04-evidence-report.md).

Phase 0 exited on 2026-09-17 per [ADR-0010](../adr/0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md): Milestones 1–3, Milestone 4 Spike A (steps 1–3), and Milestone 4 Spike B provided the retained evidence; Milestone 4 Spike A step 4, Spike C, and the realtime spike were explicitly descoped to the Phase 4 exit gate rather than blocking Phase 1's start.

That ADR also moved the design-partner cohort item off the Phase 0 exit gate and onto this phase's exit gate, where the delivery plan already independently required it (see "Exit gate" below). Recruiting and observing that cohort is therefore load-bearing Phase 1 work, not a follow-on.

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

Milestones have not yet been scoped against this build order — see "Open questions" below.

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
- Design partners can attempt the supported setup without an engineer driving the UI; failures are categorized and feed the delivery Phase 2 estimate.

## Milestone scoping approach

Owner-confirmed (2026-09-17): Phase 1 milestones are **not** pre-planned as a full breakdown of the seven-step build order above. Each milestone is scoped individually, immediately before its own implementation begins — an A0 contract checkpoint per milestone, the same pattern Phase 0 Milestone 4 used per spike, applied here per milestone instead of committing to a fixed decomposition up front. The build order above is the reference sequence a milestone's scope is drawn from, not a milestone list itself.

## Milestones

- **Milestone 1 — real `RowingCore` session domain + Protobuf contract seed** (build-order step 2): implemented 2026-09-17. See [01-milestone-1-rowingcore-domain-contract.md](01-milestone-1-rowingcore-domain-contract.md) for the contract checkpoint, scope, and verification evidence.

## Open questions

- Design-partner recruitment timing within the phase: it is a hard exit-gate condition, not scheduled against the seven build-order steps above.

## Completion rule

Each milestone in this phase is complete only when its required automated checks pass and, where the milestone's scope includes real-hardware behavior, its required real Model D/PM5 hardware run meets the applicable exit criteria — the same rule Phase 0 used. Completing every milestone in this phase records progress only; the delivery Phase 1 exit gate above is evaluated separately and recorded in this packet's own evidence report once one exists.
