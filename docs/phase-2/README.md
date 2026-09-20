# Delivery Phase 2: solo alpha and content pipeline

Status: Planned — not started
Owner: CTO / product
Last reviewed: 2026-09-20

## Outcome

Phase 2 turns the completed Phase 1 walking skeleton into a repeatable,
polished offline solo product for an internal alpha. It adds accessible
onboarding, app-guided training, local history/value, one production route,
and a trustworthy content-delivery path without weakening PM authority, local
durability, or offline completion.

This packet is a plan, not authorization to start a milestone or a claim that
the phase gate has passed.

## Confirmed scope

| Area | Phase 2 decision |
|---|---|
| Alpha | CEO on the reference Model D/PM5 for four weeks; at least four completed rows and a recorded willingness-to-pay judgment |
| Route | One downloadable Han River representation plus a cooked, low-detail outdoor safe route |
| Training | One beginner-friendly, fixed-time, app-guided plan; no PM-managed programming |
| Local value | Chronological history, pace trend, private personal-best ghost, and user-selected FIT export |
| Product UX | Skippable safety/setup, Settings, exertion HUD polish, text scaling, high contrast, and safe-mode boot |
| Content | Controlled internal catalog with a versioned signed manifest, resumable download, validation, atomic activation, rollback, and fallback |
| Evidence | 100 varied workouts, with deterministic simulator coverage plus reference-hardware evidence; CEO approves training/safety copy |

Downloaded content is data only. It must be signature-, hash-, size-, path-,
and compatibility-validated before activation; it cannot contain executable
code, scripts, native plug-ins, or Blueprint bytecode. A content, renderer, or
network failure must leave the local menu, safe route, and local workout usable.

## Proposed milestones

| Milestone | Bounded outcome |
|---|---|
| 1 — alpha baseline and observability | Reproducible performance, latency, durability, startup, reconnect, simulator, and HIL evidence baseline without regressing Phase 1 invariants. |
| 2 — onboarding, settings, and safe operation | Accessible setup/settings, exertion HUD polish, and safe-mode fallback. |
| 3 — app-guided fixed-time workout | Versioned plan, monotonic cues, explicit app-guided provenance, and no CSAFE plan commands. |
| 4 — local history, ghost, and FIT | Finalized-session history/detail, pace trend, local PB ghost, and on-demand selected-file export. |
| 5 — Han River content and validator | Rights/reference record, route definition, production environment, and cook/budget/route validation. |
| 6 — signed content catalog, install, and rollback | Staged download, verification, atomic activation, last-known-good selection, and safe failure behavior. |
| 7 — alpha hardening and evidence | Four-week alpha and consolidated exit-gate evidence; findings may reopen earlier work. |

Each milestone needs its own scope checkpoint and gate. This sequence may be
replanned without changing the delivery-phase outcome or its normative gate.

## Exit gate

- The internal alpha participant can manually install the unsigned ad-hoc build,
  pair the reference PM5, and complete normal rows for four weeks without
  engineering assistance.
- QA-001/002/003/004/009/010/011 pass on the reference setup.
- At least 100 varied workouts have no unexplained loss or duplication and
  agree with the PM display within source resolution.
- Content validation and last-known-good/safe-route behavior are release-blocking.
- Training/safety copy has rowing-domain and product approval; activation,
  return rows, support burden, and willingness to pay are recorded for the
  investment decision.

QA-012 is excluded: executable signing, notarization, and the update channel
begin in Phase 4 under [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md).
Before Milestone 2, resolve the existing QA-010 accessibility scope in the
[product scope](../architecture/01-product-scope.md); it cannot be weakened by
omission.

## Out of scope

PM-managed workout programming, accounts/sync, automatic third-party export,
social features, ranked racing, additional polished routes, public
distribution, executable signing/notarization, Sparkle, and an executable
update channel.

See the normative [delivery plan](../architecture/10-delivery-plan.md),
[product scope](../architecture/01-product-scope.md), and
[Phase 2 changelog](CHANGELOG.md).
