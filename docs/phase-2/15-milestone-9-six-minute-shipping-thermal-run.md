# Milestone 9: six-minute Shipping thermal run

Status: Planned — blocked at mounted-Han canary prerequisite; no run recorded
Owner: Client/content and technical art
Date: 2026-09-26

This Phase 2 Milestone 9 packet owns the warm six-minute Shipping performance
and thermal proxy reassigned from Milestone 5 by
[ADR-0015](../adr/0015-move-six-minute-shipping-thermal-run-to-milestone-9.md).
It tests the selected water treatment on Standard and mounted Han. It does not
certify the product-level 60-minute QA-001 interval or the Phase 4 soak.
Resolve any signed-content install, activation, or mount failure through the
[packaging runbook](11-han-river-packaging-runbook.md#han-content-is-not-mounted)
before collecting a Han timing result.

## Preconditions and setup

1. Use the approved reference Apple-silicon Mac under normal cooling and power
   conditions, at 2560×1600 with the High preset. Record OS, hardware, display
   refresh rate, ambient conditions, power source, and background workload.
   Keep resolution, preset, camera, and content fixed within a comparison.
2. Run `make unreal-package-verify`, then record the app SHA-256, Han trio
   SHA-256 values, signed catalog URL/revision, offered version, and activated
   content revision. Rebuild or recook if the selected water source or content
   changes before measurement.
3. Through the normal idle-owner workflow, download, next-launch activate, and
   mount the reviewed Han package. Record the visible route, app/content
   versions, and `mounted` status. A fresh cook alone is not an activation;
   download, activation, mount, or catalog-revision failure invalidates a Han
   result. Also confirm that Standard starts with Han unavailable.
4. Capture Unreal Insights frame/game/render timing, Metal GPU timing, and a
   macOS thermal timeline for the packaged process. Keep traces and footage
   local; record only non-private summaries in repository evidence.

## Run matrix

Use the same representative chase-camera course segment and camera path for
baseline and candidate captures. Include near-boat water, a bridge reflection,
and horizon. Warm shaders and caches before every measured interval. Primary
runs use a fresh normal launch, without `-ReduceMotion`, then capture **six
uninterrupted minutes**.

| Route | Mode | Duration after warm-up | Purpose |
|---|---|---:|---|
| Standard | Normal water and effects | 6 min | Thermal and timing gate |
| Mounted Han | Normal water and effects | 6 min | Thermal and timing gate |
| Standard and mounted Han | `-ReduceMotion` | Matched visual capture | Confirm explicit reduced-motion appearance |
| Standard and mounted Han | `-HideWaterEffectsForBenchmark` | Matched timing interval | Isolate hull/oar effect cost |
| Standard and mounted Han | `-HideWaterForBenchmark` | Matched timing interval | Isolate water-surface cost |

For each capture, record start/end timestamps, full-scene frame and GPU p95,
sustained fps, game/render p95, hitches, thermal state at start and each
minute through minute six, exact flags, camera segment, build/content identity,
and trace/footage paths. Diagnostic modes must use the same route, build,
camera, resolution, High preset, warm-up, and interval as normal mode. Normal
minus effects-hidden estimates interaction cost; effects-hidden minus
water-hidden estimates water-surface cost. Those p95 differences are diagnostic
rather than per-frame component timings. The reduced-motion capture cannot
replace the normal mounted-Han timing run.

## Acceptance

Milestone 9 passes only if each normal route run is at or below 16.5 ms
whole-scene GPU p95 and 16.7 ms frame p95, sustains 60 fps at 2560×1600 High,
and shows no sustained thermal throttling for the full six-minute window.
Record and diagnose every miss. The owner separately reviews reflection bands,
tiling, shimmer, HUD glare, wake size, and oar timing. A pass leaves the
60-minute QA-001 and Phase 4 requirements open.
