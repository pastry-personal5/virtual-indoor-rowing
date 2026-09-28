# ADR-0015: Move the six-minute Shipping thermal run to Phase 2 Milestone 9

- Status: Accepted — owner-directed Phase 2 milestone reassignment
- Date: 2026-09-26
- Owners: CTO / product, client/content
- Supersedes: ADR-0014's assignment of the six-minute Shipping thermal gate to Phase 2 Milestone 5

## Context

[ADR-0014](0014-six-minute-milestone-5-thermal-gate.md) changed the
water-refinement gate from a 60-minute run to a warm six-minute Shipping
measurement per route. The measurement has not been run: the mounted-Han
canary prerequisite remains blocked. The owner has directed that this
cross-route Shipping performance and thermal work be a separately scoped
Phase 2 Milestone 9, leaving Milestone 5 focused on water treatment selection,
visual review, and reduced-motion evidence.

## Decision

- Phase 2 Milestone 9 owns the continuous, warm six-minute Shipping thermal
  measurement on the reference Mac for Standard and mounted Han, with the
  existing build/content identity, High-preset, camera, timing, thermal, and
  hitch records.
- Milestone 9 retains the existing thresholds: 16.5 ms whole-scene GPU p95,
  16.7 ms frame p95, sustained 60 fps, and no sustained thermal throttling
  during each six-minute window. It also owns the matched water-on,
  effects-hidden, and water-hidden cost captures.
- Milestone 5 no longer has the six-minute Shipping thermal run as its gate.
  It continues to provide the selected water treatment and the visual and
  reduced-motion evidence consumed by the Milestone 9 measurement.
- A passing Milestone 9 result remains Milestone 9 evidence only. It does not
  certify the product-level 60-minute QA-001 interval, pass the Phase 2 exit
  gate, or close the Phase 4 visual-performance obligation.

## Consequences

The water-selection decision and the cross-route thermal/performance proxy now
have distinct bounded gates. The existing worksheet is reissued as the
[Milestone 9 run worksheet](../phase-2/15-milestone-9-six-minute-shipping-thermal-run.md).
No measurement result, threshold, QA-001 requirement, or Phase 4 six- and
60-minute requirement changes through this reassignment.

## Validation and revisit

Use the Milestone 9 worksheet only after the packaged Shipping mounted-Han
canary succeeds. Repeat a failed run after diagnosing its cause rather than
shortening the window. Revisit the scope if the scene, hardware, or thermal
trajectory indicates that six minutes misses a material late-session
regression.
