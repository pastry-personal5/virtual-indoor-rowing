# ADR-0013: Increase the reference GPU frame budget to 16.5 ms

- Status: Accepted — owner-directed budget change
- Date: 2026-09-23
- Owners: CTO / product, client/content

## Context

The [macOS/Unreal client architecture](../architecture/03-macos-unreal-client.md)
previously set a whole-scene GPU budget of 14.0 ms p95 at 2560×1600 High on
the reference Apple-silicon Mac. It was an initial headroom target, not a
measured result.
[ADR-0010](0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md) deferred
the 6-minute and 60-minute visual-performance runs to Phase 4; neither run has
established the cost of the present scene. [Phase 2 Milestone 5](../phase-2/12-realistic-water-refinement-plan.md)
now evaluates more costly reflections, bitmap detail, and boat-local hull and
oar effects for photorealistic river water.

The product's QA-001 objective remains sustained 60 fps at 2560×1600 High,
p95 frame time at most 16.7 ms, and no sustained thermal throttling. A GPU
budget close to that frame-time limit leaves less margin for variability;
independent GPU and frame-time percentiles cannot substitute for measured
end-to-end responsiveness.

## Decision

- Increase the **whole-scene GPU budget** from 14.0 ms p95 to **16.5 ms p95**
  on the reference Mac at 2560×1600 High, after shader warm-up. This is a
  ceiling for the full rendered scene, not a water-only allocation or a promise
  that a reflection method may consume the 2.5 ms difference.
- Keep QA-001 unchanged: sustained 60 fps, p95 frame time at most 16.7 ms,
  and no sustained thermal throttling. Keep the 4.0 ms game-thread and 5.0 ms
  render-thread p95 budgets. Passing the GPU budget alone never passes QA-001.
- Apply 16.5 ms to forward-looking Phase 2 content decisions and the deferred
  Phase 4 visual-performance spike. Retain the 6-minute proxy and full
  60-minute run, with Unreal Insights and Metal GPU timing, frame-time
  distribution, thermal state, build/content identifiers, and representative
  Standard and mounted Han views. Report a miss rather than silently lowering
  resolution, changing the High preset, or replacing Han with the fallback.
- Require water-on versus water-hidden GPU deltas for Milestone 5. Choose or
  scale reflection and texture features based on visual gain and whole-scene
  cost; preserve the safe route and reduced-motion behavior. This ADR does not
  approve planar reflections, Single Layer Water, a content release, or a
  Phase 2/4 exit gate.

## Consequences

- The GPU budget has 0.2 ms nominal headroom below QA-001's 16.7 ms p95
  frame-time limit, versus 2.7 ms under the previous budget. CPU work,
  synchronization, frame pacing, and thermal drift can still cause frame
  misses; the QA-001 frame and thermal measures remain independent gates.
- Content that measures between 14.0 and 16.5 ms GPU p95 may pass the revised
  GPU budget if the full scene also passes its frame, visual, and thermal
  review. No performance pass is claimed by this decision; reference-Mac
  Shipping evidence is still outstanding.
- Historical Milestone 3 and Phase 0 records preserve the 14.0 ms target that
  existed when they were written. Their deferred or unverified measurements
  remain deferred or unverified, while future acceptance uses this decision.

## Alternatives considered

- **Keep 14.0 ms:** preserves more headroom but constrains the measured
  reflection and water-detail comparison before it has produced evidence.
- **Use 16.0 ms:** retains 0.7 ms nominal GPU margin but allows less room for
  the owner-directed photorealistic water scope. The tighter margin at 16.5 ms
  is an explicit accepted risk, subject to the unchanged QA-001 gate.
- **Relax QA-001:** changes the product's 60 fps promise without performance
  evidence and is not part of this decision.

## Validation and revisit

Measure the same reference-Mac Shipping scene at 2560×1600 High for a warm
6-minute proxy and a 60-minute thermal run, with Standard and mounted Han
represented. Record p95 GPU and frame time, sustained fps, thermal state, and
water-on versus water-hidden deltas. Revisit this budget if either route fails
QA-001, if 16.5 ms proves too close to the frame limit under normal variance,
or if the approved reference hardware or rendering baseline changes. The
Phase 4 certification timing established by ADR-0010 is unchanged.
