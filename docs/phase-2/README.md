# Delivery Phase 2: solo alpha and content pipeline

Status: Planned — not started  
Owner: CTO / product  
Last reviewed: 2026-09-20

## Purpose

Phase 2 turns the Phase 1 walking skeleton into a repeatable offline solo
rowing product for an internal alpha. It delivers one downloadable,
production-quality Han River route, structured app-guided training, local
history and replay, and a release-blocking content-integrity path without
weakening the PM5, local-journal, or offline-completion authorities.

This is a planning packet only. It neither starts a milestone nor asserts that
the Phase 2 exit gate has passed.

## Confirmed product decisions

| Topic | Decision |
|---|---|
| Alpha cohort | One internal participant: the CEO, using one reference Model D/PM5 setup |
| Alpha duration and success | Four weeks alongside final hardening; at least four completed rows and a recorded yes/no willingness-to-pay judgment |
| Route | One downloadable, production-quality representation of the Han River in Seoul; research, rights, safety, and content references are a planning task before production begins |
| Safe fallback | A minimal, low-detail outdoor training scene cooked into the app |
| Structured training | One beginner-friendly one-minute fixed-time plan; app-guided only. PM-managed workout programming is deferred |
| Ghosts | Local personal-best ghost on the same route; fastest completed route time, or greatest distance for fixed-time workouts; comparisons may cross workout types |
| History | Local chronological history plus pace trends |
| FIT | Generate a user-selected local FIT file on request; no account or automatic third-party upload |
| Onboarding | Safety/setup is skippable and remains available from Settings |
| Content delivery | Controlled internal test catalog; signed manifest, resumable download, validation, activation, rollback, and built-in safe fallback |
| Copy review | CEO provides rowing-domain and product approval for training and safety copy |
| Workout evidence | 100 varied workouts: at least one on the reference PM5 and the remainder deterministic simulator scenarios |

## Architecture guardrails

- PM5 facts, rather than app timers, animation, or ghost motion, remain the
  authority for official distance, time, pace, power, and stroke data.
- An app-guided plan records app plan facts separately from PM facts, labels
  the mode clearly, and never represents the PM as programmed.
- Downloaded content is data only: verify signature, hash, size,
  compatibility, and safe paths before mount; do not execute downloaded code,
  scripts, native plug-ins, or Blueprint bytecode.
- A failed download, content verification, renderer, or catalog request must
  still leave the local menu, the safe route, and a locally durable workout
  usable offline.
- Ghost replay is private/local in this phase. It is derived only from a
  finalized source session and is presentation, never official measurement.
- FIT uses the standard macOS save panel and writes only the file the user
  selects.
- Phase 2 artifacts remain unsigned/ad-hoc and manually installed under
  ADR-0008. Content signatures are separate from executable signing.

## Proposed milestone sequence

Milestone numbers are provisional until each scope checkpoint. Each milestone
has one bounded gate; their completion does not pass the delivery-phase gate.

| Milestone | Outcome and principal boundary | Required gate |
|---|---|---|
| Phase 2 Milestone 1 — alpha baseline and observability | Freeze the Phase 1 baseline; add reproducible reference-hardware capture for frame time, telemetry-to-HUD latency, checkpoints, queue/database health, startup, and reconnect. Extend simulator corpus and firmware/HIL evidence matrix. | Deterministic capture scripts and fixtures run in CI; reference-device measurement plan is reproducible; no regression in Phase 1 session authority/durability invariants. |
| Phase 2 Milestone 2 — onboarding, settings, and safe operation | Implement skippable onboarding, safety placement/cable/PM guidance, Settings, high-contrast and text-size controls, exertion HUD polish, and safe-mode boot to the cooked fallback route. | Unreal automation validates boot, menu/offline path, settings persistence, readable HUD, and safe-mode fallback; CEO approves displayed safety/training copy. |
| Phase 2 Milestone 3 — app-guided fixed-time workout | Add a versioned, bounded one-minute beginner plan in engine-independent workout rules, a monotonic cue clock, work/rest/finish cues as applicable, summary plan provenance, and explicit App-guided labeling. | Unit/property and simulator tests cover hitches, pause/stop/disconnect/reconnect, termination, recovery, and a PM/display distinction; no PM-managed commands are sent. |
| Phase 2 Milestone 4 — local history, pace trend, ghost, and FIT | Add stable local history/detail queries, pace trend calculation, personal-best selection and same-route ghost playback, plus on-demand user-selected FIT export. | Deterministic tests cover source-session immutability, ghost eligibility/best selection, fixed-time distance comparison, history ordering, FIT validity, selected-path export failure, and no export of an active/incomplete session. |
| Phase 2 Milestone 5 — Han River content and validator | Research and document allowed reference use; author the one production route/environment and route definition; enforce route, localization, budget, hash, safe-spawn, checkpoint, and cook checks. | Editor/cook validator rejects malformed or over-budget content; packaged route runs at the frozen reference display/scalability configuration; minimal fallback route remains independently runnable. |
| Phase 2 Milestone 6 — signed content catalog, install, and rollback | Introduce a versioned signed content manifest and controlled test catalog; implement download, resume, verify, mount/activate, last-known-good selection, rollback, and failure isolation. | Negative tests reject wrong key/signature/hash/version/path/size; interrupted download and bad activation retain the last-known-good set; missing/corrupt downloaded content enters safe mode. |
| Phase 2 Milestone 7 — alpha hardening and evidence | Run the four-week CEO alpha alongside final hardening; close performance, latency, durability, recovery, correctness, simulator corpus, HIL, and content-release evidence. | Exit-gate evidence bundle is complete or each unmet item has a documented replan; no milestone alone closes the phase. |

Milestones 1–2 establish the usable offline product and safe instrumentation.
Milestones 3–4 add the solo training and local-value loop. Milestones 5–6
make the production route safely deliverable. Milestone 7 is intentionally
cross-cutting: alpha findings may reopen prior milestones before the phase is
evaluated.

## Workstream plan

### Product, UX, and copy

- Keep setup minimal: offer safety/setup before the local menu, allow Skip,
  retain it in Settings, and never block an active row with a prompt.
- Design the exertion HUD around the authoritative PM metrics and unambiguous
  connection/data-quality state. The app-guided workout status must be clearly
  distinct from PM-programmed mode.
- Define the one-minute beginner workout’s target language, cue tone,
  prerequisites, recovery note, and stop-at-any-time wording for CEO review.
- Log alpha activation, completed rows, return rows, support interventions,
  and the CEO’s willingness-to-pay answer. Four completed rows is the stated
  minimum outcome.

### Domain, device, and local data

- Keep plans immutable/versioned and engine-independent. The workout compiler
  has no CSAFE programming path in this phase.
- Persist plan ID/version, route/content hash, explicit app-guided provenance,
  quality flags, and interval/summary facts transactionally with the local
  session.
- Build history, trend, ghost, and FIT readers as stable snapshots over
  finalized local sessions. Do not block an active session on them.
- Add simulator fixtures for fixed-time completion, stale/gapped telemetry,
  restart recovery, corrupt tail, duplicate and reordered events, PM reset,
  reconnect, and selected-file export failure.

### World, content, and performance

- Research the Han River before art production: route representation,
  landmarks, permissions/trademarks/reference licenses, safety-sensitive
  depictions, and a route segment that supports the agreed performance budget.
- Create the engine-independent route definition before asset assembly; use
  stable IDs rather than raw asset paths.
- Freeze the reference resolution, High preset, warm-cache procedure, thermal
  conditions, and capture method before performance tuning. Separate first-run
  shader/PSO results from warmed results.
- Cook the low-detail outdoor safe route into the base app independently of
  downloadable content.

### Content integrity and delivery

- Version the manifest and content schema before implementation; keep the
  content signing key and public verification key separate from app signing.
- The internal catalog may be reachable when online, but normal local menu,
  installed content, safe route, and workout completion remain offline-first.
- Stage install into a non-active location; verify fully before atomic
  activation; retain last-known-good metadata and rollback artifact.
- Make validator, signature, compatibility, hash, and safe-route failures
  release-blocking for downloadable content.

### Quality and alpha operations

- Run the reference PM5 HIL row for the end-to-end physical comparison and
  use the deterministic simulator for the other 99 or more varied sessions.
  The single real row is not a substitute for required reference-hardware
  performance, latency, durability, and reconnect captures.
- Capture artifact/build/content/contract/capability versions, fixture seed,
  reference hardware, macOS/toolchain, and test conditions with each result.
- Triage CEO-alpha findings weekly. A correctness, durability, safety, safe
  route, signature-validation, or unexplained data-loss/duplication finding
  blocks the gate until fixed or the phase is explicitly replanned.

## Phase exit gate

The delivery-plan gate now requires:

- one internal alpha participant (the CEO) can manually install the unsigned,
  ad-hoc build, pair the reference PM5, and row normal flows without
  engineering assistance over four weeks;
- QA-001, QA-002, QA-003, QA-004, QA-009, QA-010, and QA-011 pass on the
  reference setup;
- 100 or more varied workouts have no unexplained loss or duplication and
  their summaries agree with the PM display within source resolution;
- content validation and the last-known-good/safe-route behavior are
  release-blocking;
- CEO review approves training/safety copy; and
- the internal alpha records activation, return rows, support burden, at
  least four completed rows, and a yes/no willingness-to-pay judgment, after
  which founders approve or replan connected-product investment.

QA-012 is deliberately not a Phase 2 gate: ADR-0008 places executable
signing, notarization, and the update channel in Phase 4. Phase 2 instead
validates signed content manifests, content download/install/rollback, and
safe-route fallback.

## Decision required before implementation

The stated alpha accessibility preference is text sizing and high contrast
only. That conflicts with the current normative QA-010 requirement for full
setup/menu keyboard navigation, color-independent target feedback, and
subtitle/cue equivalents. Before Phase 2 implementation begins, either:

1. retain QA-010 as written and include all of its requirements; or
2. explicitly revise QA-010 and every dependent exit/verification reference
   to the narrower text-size/high-contrast scope.

This packet does not silently weaken QA-010.

## Schedule and investment checkpoints

The 6–10 week reference schedule is sequenced, not a commitment to parallel
staffing:

| Weeks | Focus | Decision point |
|---|---|---|
| 1–2 | Milestones 1–2; resolve QA-010; Han River research; alpha instrumentation; begin CEO alpha as soon as a stable normal flow is usable | Continue only if the baseline preserves local-session correctness and the content/reference path is viable |
| 3–4 | Milestones 3–4; first CEO completed rows; history/ghost/FIT evidence | Replan if app-guided cues, local history, or export threaten active-workout continuity |
| 5–7 | Milestones 5–6; route production; catalog/rollback failure testing; thermal and shader/PSO tuning | Do not admit downloadable content without validator, signature, last-known-good, and fallback evidence |
| 8–10 | Milestone 7; complete alpha, HIL/performance/durability evidence, defects, phase review | Founders approve Phase 3 investment or replan based on evidence |

## Risks and controls

| Risk | Control and trigger |
|---|---|
| One-person alpha gives weak generalizability | Treat it as internal product evidence only; preserve raw categorized findings and do not claim external validation |
| One real workout leaves little physical fault coverage | Keep simulator fault coverage deterministic; require separate HIL performance/latency/durability/reconnect captures before the gate |
| Han River source/right/reference issue | Complete research and record permitted source/reference treatment before production art; switch to an abstracted inspired route only with explicit product decision |
| Content activation corrupts a usable installation | Verify staged content before activation, retain last-known-good metadata/artifact, and test corrupt/interrupted/incompatible cases |
| App-guided plan is mistaken for PM programming | Prominent mode label, separate persisted facts, no CSAFE plan commands, and acceptance tests |
| Accessibility scope conflict | Resolve QA-010 explicitly before Milestone 2 gate; do not waive it by omission |
| Four-week alpha runs beside hardening | Weekly triage and a hard stop for correctness, safety, content-integrity, or durability regressions |

## Out of scope

- PM-managed CSAFE workout programming, time/distance/interval catalog
  expansion, cloud accounts/sync, Concept2 Logbook delivery, third-party
  automatic export, social features, and ranked racing.
- More than one polished downloadable environment/route.
- Public distribution, Developer ID signing, notarization, Sparkle, or an
  executable update channel (Phase 4 under ADR-0008).
- Automatic FIT export; it is user-initiated local file export only.
