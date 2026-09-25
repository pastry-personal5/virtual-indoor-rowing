# Delivery Phase 2: solo alpha and content pipeline

Status: In progress — Milestones 1, 3, and 4 are owner-complete; Milestone 2 is in progress; the Phase 2 exit gate is not passed
Owner: CTO / product
Last reviewed: 2026-09-23

## Outcome

Phase 2 turns the completed Phase 1 walking skeleton into a repeatable,
polished offline solo product for an internal alpha. It adds accessible
onboarding, app-guided training, local history/value, one production route,
and a trustworthy content-delivery path without weakening PM authority, local
durability, or offline completion.

This packet records scoped Phase 2 work and does not claim that the phase exit
gate has passed.

## Confirmed scope

| Area | Phase 2 decision |
|---|---|---|
| Alpha | CEO on the reference Model D/PM5 for four weeks; at least four completed rows and a recorded willingness-to-pay judgment |
| Route | A downloadable 5 km Han River vertical slice plus a cooked-in 2 km Standard fallback route; both are peer course choices when Han River is usable |
| Training | One beginner-friendly, fixed-time, app-guided plan; no PM-managed programming |
| Local value | Chronological history, pace trend, private personal-best ghost, and user-selected FIT export |
| Product UX | Skippable safety/setup, Settings, exertion HUD polish, text scaling, high contrast, and safe-mode boot |
| Content | Public-read internal static catalog with a versioned signed manifest, resumable download, validation, next-launch atomic activation, rollback, and Standard fallback |
| Evidence | 100 varied workouts, with deterministic simulator coverage plus reference-hardware evidence; CEO approves training/safety copy |

Downloaded content is data only. It must be signature-, hash-, size-, path-,
and compatibility-validated before activation; it cannot contain executable
code, scripts, native plug-ins, or Blueprint bytecode. A content, renderer, or
network failure must leave the local menu, safe route, and local workout usable.

## Scoped milestone

| Milestone | Status | Bounded outcome |
|---|---|
| [Phase 2 Milestone 1](01-milestone-1-han-river-content-fallback.md) — Han River content delivery with Standard fallback | Complete — owner decision | A data-only, signed Han River delivery path with a cooked Standard route that remains usable through content, renderer, network, storage, and catalog failure. |

| [Phase 2 Milestone 2](05-milestone-2-han-river-wiring-and-production.md) — Wire and enhance Han River course assets | In progress | Load the mounted Han content at runtime, allow mid-session course switching, and raise the whole 5 km route to production quality with an optional audio bed. |

| [Phase 2 Milestone 3](07-milestone-3-enhanced-water.md) — Enhanced water surface and animation | Complete — owner decision | A calm, realistic, world-anchored animated water surface on the built-in kit and the Han level, presentation-only, with no current, wake, or PM-fact dependency. |

| [Phase 2 Milestone 4](08-milestone-4-blender-building-import.md) — Import Blender buildings into the Han River course | Complete — owner decision | The Blender building-import milestone is closed; importing several additional OSM maps remains separately tracked follow-up content work. |

| [Phase 2 Milestone 5](12-realistic-water-refinement-plan.md) — Photorealistic water refinement | In progress — boat and rower geometry applied | Compare scene reflections, filtered water normals, approved bitmap detail, and restrained hull and oar interaction on Standard and mounted Han, with packaged visual, six-minute thermal/performance, and reduced-motion evidence. |

| [Phase 2 Milestone 10](06-milestone-10-development-loop.md) — Shorten the development and content-iteration loops | Planned — not started | Editor-safe Shipping builds, an uncooked Development run, a loose-level course reload, and a one-command local Han publish, without touching the Shipping trust boundary. |

Only the listed milestones are currently scoped. Later Phase 2 milestones are planned
individually, before work begins; no speculative sequencing or scope is
recorded here. Completing a milestone records incremental progress only and
does not pass the Phase 2 exit gate.

## Follow-up plans

- Import several additional OSM maps as separately scoped, review-gated Han
  content work. The existing Area 01 OSM runbooks remain the applicable
  acquisition, provenance, staging, and import controls.

## Runbooks

- [Content-origin runbook](02-content-origin-runbook.md) — publication,
  withdrawal, and rollback of signed Han content.
- [Han River packaging runbook](11-han-river-packaging-runbook.md) — produce
  the external IoStore cook and signed `HanRiver.vircontent` release.
- [Han Area 01 OSM-to-Unreal runbook](09-han-area-01-osm-unreal-runbook.md)
- [Han Area 01 OSM-to-Blender-to-Unreal runbook](10-han-area-01-osm-blender-unreal-runbook.md)
- [Milestone 5 six-minute thermal run](14-milestone-5-six-minute-thermal-run.md)
  — reference-Mac Shipping measurement worksheet.

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
Before the Phase 2 exit gate, resolve the existing QA-010 accessibility scope in the
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
