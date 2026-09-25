# Milestone 5: six-minute Shipping thermal run

Status: Blocked at mounted-Han prerequisite; no run recorded
Owner: Client/content and technical art
Date: 2026-09-25

This is the [Milestone 5](12-realistic-water-refinement-plan.md) measurement
packet under [ADR-0014](../adr/0014-six-minute-milestone-5-thermal-gate.md).
It tests the water decision over six warm minutes on each route. It does not
certify the product-level 60-minute QA-001 interval or the Phase 4 soak.
The owner reports that the current app says Han content is not mounted. Resolve
the signed-content install/activation or mount failure using the
[packaging runbook](11-han-river-packaging-runbook.md#han-content-is-not-mounted)
before collecting a Han timing result.

## Artifacts and setup

1. Use the approved reference Apple-silicon Mac, its normal cooling and power
   conditions, a 2560×1600 display, and the High preset. Record OS, hardware,
   display refresh rate, ambient conditions, power source, and any background
   workload. Keep resolution, preset, camera, and content unchanged within a
   comparison. Disable any adaptive setting that would silently lower the
   measured resolution; record the setting actually used.
2. Run `make unreal-package-verify` from the repository root and record the
   app SHA-256. The current packaged app is
   `Build/unreal-shipping/archive/Mac/VirtualRowing-Mac-Shipping.app`.
   Record the Han trio's three SHA-256 values and the exact activated
   content/catalog revision. The 2026-09-25 candidate app digest is
   `e9fc85252e718060c38a8f18c380bb8aad917994e1ec3ec118c174a9c0603f6a`;
   the candidate external cook is `Build/han-cook/HanRiver-m5-20260925/`.
   Its pak/utoc/ucas digests, respectively, are
   `4b1709b351d48ef0b7c82a7e029b2a9ef81a7a44bab4637a1026de27046d6712`,
   `b0a1b74114e6edc579f02a35cee28f6a1360d62490cc799ca32fc7290f382bcc`,
   and `5abf60e56b3185f69a8cd3b7bc50fe82a90fca4fb4999f42c025e85d6386ffa6`.
   Rebuild or recook if water source or content changes before the run.
3. Launch the packaged Shipping app through the normal owner workflow. Mount
   the reviewed Han package using the content pipeline, then verify the app
   presents Han rather than its Standard fallback. Record the visible route
   identity and app/content versions. Do not claim a Han result if mounting or
   activation fails.
4. Set up Unreal Insights frame/game/render timing and Metal GPU timing for
   this packaged process. Capture the macOS thermal state on a timeline. Use
   one timing source and collection method consistently across matched runs;
   note any missing metric rather than substituting an Editor viewport value.
   Keep captures and logs local, without private workout telemetry.

## Run sequence

For each route, use the same representative chase-camera course segment and
camera path for the baseline and candidate. Include the near-boat surface,
bridge reflection, and horizon; record camera positions and footage. Use
fresh normal launches without `-ReduceMotion` for the primary timing run.
Warm shaders and caches first, then mark a start timestamp and capture **six
uninterrupted minutes**. Record the end timestamp, full-scene p95 frame and
GPU time, sustained fps, game/render thread p95, hitches, and the thermal
state at start, each minute, and end. Note any clock, power, or thermal change.

| Route | Mode | Duration after warm-up | Purpose |
|---|---|---:|---|
| Standard | Normal water and effects | 6 min | Thermal and timing gate |
| Mounted Han | Normal water and effects | 6 min | Thermal and timing gate |
| Standard and mounted Han | `-ReduceMotion` | Matched visual capture | Confirm explicit reduced-motion appearance |
| Standard and mounted Han | `-HideWaterEffectsForBenchmark` | Matched timing interval | Isolate hull/oar effect cost |
| Standard and mounted Han | `-HideWaterForBenchmark` | Matched timing interval | Isolate water-surface cost |

For the two diagnostic modes, use the same route, build, camera, resolution,
High preset, warm-up, and timing interval as normal mode. Confirm visually
that the flags hide only their intended surfaces/effects before comparing
numbers. Normal minus effects-hidden estimates interaction cost;
effects-hidden minus water-hidden estimates water-surface cost. Record each
mode's GPU p95 separately; a difference of p95 values is diagnostic, not a
per-frame component timing. The reduced-motion launch is a separate visual
check and must not replace the normal Han timing run.

## Result sheet

Copy this table for each capture. Attach paths to local Insights/Metal traces
and footage in the private run record; summarize only non-private results in
[Milestone 5 evidence](13-milestone-5-evidence.md).

| Field | Recorded value |
|---|---|
| Run date, operator, route, mode, exact flags | Pending |
| App SHA-256; Han trio SHA-256; content/catalog revision | Pending |
| Mac/OS/display/High settings and camera segment | Pending |
| Warm-up end; six-minute start/end timestamps | Pending |
| Frame p95; GPU p95; game/render p95; sustained fps | Pending |
| Hitches; thermal state at 0, 1, 2, 3, 4, 5, 6 min | Pending |
| Footage and trace paths; measurement tool/settings | Pending |
| Visible artifacts or behavior; owner decision | Pending |

The Milestone 5 timing proxy passes only if **each** normal route run stays
at or below 16.5 ms GPU p95 and 16.7 ms frame p95, sustains 60 fps at
2560×1600 High, and shows no sustained thermal throttling during its full
six-minute window. Record and diagnose any miss. Visual acceptance separately
requires the owner to review reflection bands, tiling, shimmer, HUD glare,
wake size, and oar timing. A proxy pass leaves the product-level QA-001 and
Phase 4 60-minute requirements open.
