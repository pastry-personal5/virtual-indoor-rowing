# ADR-0014: Use a six-minute thermal gate for Phase 2 Milestone 5

- Status: Accepted — owner-directed Milestone 5 scope change
- Date: 2026-09-25
- Owners: CTO / product, client/content
- Supersedes: The Milestone 5 duration implied by [ADR-0013](0013-increase-reference-gpu-frame-budget.md); its GPU budget and the Phase 4 duration remain in force

## Context

[ADR-0013](0013-increase-reference-gpu-frame-budget.md) pairs a warm
six-minute proxy with a full 60-minute thermal run when validating the revised
16.5 ms GPU budget. The [Milestone 5 plan](../phase-2/12-realistic-water-refinement-plan.md)
adopted the 60-minute run for its water-refinement exit gate. The owner has
directed that this milestone instead use a six-minute thermal run.

The product-level [QA-001](../architecture/01-product-scope.md) measurement
defaults to a 60-minute session after shader warm-up. The deferred Phase 4
visual-performance evidence also explicitly includes both durations under
[ADR-0010](0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md). A short
Milestone 5 run cannot establish either longer-duration result.

## Decision

- Require one **continuous, warm six-minute Shipping thermal measurement per
  route** on the reference Mac: Standard and mounted Han. Shader warm-up is
  outside the measured six minutes. Record build/content identity, display
  resolution, High preset, camera/course position, reduced-motion state,
  frame/GPU p95, sustained fps, thermal state over time, and hitches.
- Judge the Milestone 5 run against the existing 16.5 ms whole-scene GPU p95,
  16.7 ms frame p95, sustained 60 fps, and no sustained thermal-throttling
  thresholds during that six-minute window. Water and hull/oar effect deltas
  remain separate matched Shipping measurements. Record misses explicitly.
- Treat a passing six-minute result as **Milestone 5 evidence only**. It does
  not certify QA-001 over its default 60-minute interval, pass the Phase 2
  exit gate, or close the Phase 4 visual-performance obligation.
- Leave the accepted QA-001 thresholds, 16.5 ms GPU budget, product-level
  60-minute default, and deferred Phase 4 six- and 60-minute runs unchanged.

## Consequences

Milestone 5 can make a bounded water-content decision sooner. Thermal drift
after six minutes remains unmeasured until the longer certification run. A
Milestone 5 evidence report must state the duration beside each percentile and
must not call the proxy a QA-001 pass.

## Validation and revisit

Use the [six-minute run worksheet](../phase-2/14-milestone-5-six-minute-thermal-run.md)
on the packaged Shipping build with mounted Han verified before measurement.
Repeat a failed run after diagnosing its cause rather than trimming the
measurement window. Revisit this duration if the scene, hardware, or thermal
trajectory suggests that six minutes misses a material late-session regression.
