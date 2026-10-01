# Phase 2 Milestone 6: Deterministic boat movement and camera presentation

Status: In progress — native contracts/runtime/tooling, fresh-module build, external cook, and unsigned package verified; editor-authored Han path, live automation, and signed activation evidence blocked pending live Unreal MCP/owner review

Owner: Client/content and technical art

Last reviewed: 2026-09-27

## Purpose and bounded outcome

Implement common deterministic presentation for Standard and Han. Reauthor the
presentation path for `route.han-river.5k` so that its new 0 m
position is 200 m geographically north of the boat's current visible Unreal
pose. The boat follows a reviewed river-centre path, dynamically aligns to its
tangent, uses subtle cosmetic hull motion, and is viewed from selectable
starboard rear-quarter cameras. After the athlete has rowed and then remains
stopped for ten seconds, the camera shows a slow looping river reveal.

Milestone 6 does not select, replace, tune, or accept Milestone 5 water
treatment. It performs regression checks only: the final hierarchy must retain
attached, bounded, one-effect-per-event, expiring wakes and ripples. Standard
preserves its closed 2 km geometry and gains the same common path, movement,
camera, and reveal behavior; its lap wrapping remains intact.

The result establishes a signed, engine-independent presentation path for Han
and shared boat/camera defaults for future non-Standard maps. It does not pass
the Phase 2 exit gate, the warm six-minute
[Milestone 9](15-milestone-9-six-minute-shipping-thermal-run.md) gate, or any
whole `FR-*` or `QA-*` requirement.

## Reviewed baseline and prerequisites

The implementation starts from these repository facts, not from an assumed
greenfield design:

- `RouteDefinitionV1` currently carries identity, length, checkpoints,
  compatibility, and its canonical metadata hash, but no path geometry.
- `AGrayBoxCourseActor` currently generates an open 33-point sine path for
  every non-closed route, scales official route distance over Unreal's spline
  length, and owns a hard-coded non-Standard camera state machine.
- `CourseRuntime` already owns bounded PM-backed prediction and open-route
  endpoint behavior without depending on Unreal. It is the correct home for
  deterministic path sampling and presentation state; `RowingCore` remains
  untouched.
- `UWorkoutHudWidget` already owns keyboard preprocessing and the course actor
  deliberately binds no input. Camera cycling must preserve that boundary.
- the app-state database `rowing.sqlite3` contains content state but no
  preferences table. The append-only workout database is the separate
  `workout-journal.sqlite3` and must not be opened or written by this feature.
- the representative-scene review required before Han asset mutation is
  recorded as passed. Milestone 2 remains in progress, so the exact runtime map
  and mounted-content identity used by this milestone must still be pinned in
  its evidence.
- a fresh mounted-Han Shipping canary is still required. Source, native, and
  editor work may proceed independently, but the milestone cannot pass without
  a v2 signed package that downloads, activates on the next launch, mounts, and
  is visibly identified in the packaged app.

Before any path or map mutation, use Unreal MCP to record the baseline described
below. If the configured Unreal MCP server cannot answer a read-only editor
query, leave editor-owned work blocked and continue only engine-independent
source, tests, tooling, and documentation. If the Editor crashes, stop using
Unreal MCP and notify the owner immediately.

## Fixed product decisions

| Topic | Decision |
|---|---|
| Start | The new route start, not a staging pose, is 200 m geographically north of the current visible 0 m Han boat transform. |
| Route | Keep the stable `route.han-river.5k` identifier, westbound and open, with signed v2 geometry of exactly 5,000 m. Its final authored position is the nearest integer-millimetre representation of the owner target `(-455000.000333, -124999.999865, 0)` Unreal cm: `(-454999.99999997, -124999.97271864, 0)` cm. The reviewed runtime-map water footprint remains a separate visual-coverage concern. |
| Control points | Author 21 signed points, including the start and finish, distributed through bends, clear bridge spans, and useful composition beats. Each point has a text label containing its `CP` number and cumulative distance from the start, rounded up to a whole metre and grouped with thousands separators. Elevated beacon geometry is absent, and the saved `HanRouteGate_CP*` map actors are hidden before the mounted level becomes visible. Legacy 40-point markers remain removed. |
| Direction | Follow the signed path tangent with bounded, damped yaw; there is no steering, collision avoidance, current, or drift. |
| Hull motion | Use subtle roll, pitch, bob, and stroke heave derived only from reversible presentation values. |
| Normal camera | Starboard rear-quarter, world-stabilized, with Close/Medium/Wide presets; Medium is the default. |
| Preset control | On either course, non-repeating `C` cycles presets before, during, or after rowing, including during the reveal. A two-second HUD toast confirms the choice; there is no HUD camera button. |
| Stop trigger | Only after the first valid active-rowing sample, then ten continuous seconds of fresh PM-reported inactivity while the input remains connected and unfrozen. |
| Reveal | Shared 30-second starboard loop: 10 s outbound, 10 s hold, 10 s return, then repeat without another dwell while the athlete remains stopped. |
| Resume | A valid active-rowing sample interrupts any reveal phase and eases to the selected chase preset over 2 s. |
| Invalid input | Pause, stale/frozen input, disconnect/reconnect, completion, termination, route replacement, and endpoint entry cancel the dwell/loop, cut to a static selected chase pose, and reset the dwell. Reconnect never restores elapsed dwell or camera phase. |
| Finish | Do not start or continue a stop reveal after the measured PM distance reaches the 5 km presentation endpoint. |
| Reduced motion | Remove hull roll/pitch/bob/heave and all automatic camera travel. After an otherwise valid stop dwell, cut once to a static starboard-wide view and cut back when rowing resumes or eligibility is lost. |
| HUD/audio | Keep the complete workout HUD visible and do not change audio. |
| Water carry-over | Do not select or alter water treatment. Regression-check wake/ripple attachment, pool limits, one-effect-per-event behavior, and expiry only. Performance timing remains Milestone 9. |
| Compatibility | Publish the Han path as signed route-schema v2 data. Downloaded v1 Han content is incompatible with the new client and yields to Standard; built-in Standard remains the sole v1 route exception. |
| Future maps | Every downloaded non-Standard route must supply its own reviewed path. Shared camera, motion, and reveal defaults apply unless a later scoped milestone changes them. |

`C` is available while the course/HUD input surface owns keyboard input. It
must not consume typing or shortcuts owned by a modal or text-entry surface.
An unavailable/fallback Han tile selects Standard; Standard still supports `C`
for this feature.

## Requirement traceability

This milestone changes behavior covered by the following architecture
requirements. Its evidence is incremental and cannot be relabeled as a pass of
the whole requirement.

| Requirement | Changed behavior | Milestone evidence |
|---|---|---|
| FR-004 Just Row | A valid Han path remains offline-capable; absent, old, invalid, or failed Han data selects Standard without affecting the workout. | Native compatibility/fallback tests and packaged old-v1, corrupt-v2, offline-Standard, and mounted-v2 cases. |
| FR-006 responsive virtual boat and avatar | Signed distance-to-path mapping, tangent heading, bounded hull motion, and the selected chase/reveal camera. | Native presentation tests, Unreal automation, full-route packaged review, and no-backfill fault cases. |
| FR-013 content catalog and secure updates | Route-schema v2, canonical path bytes, compatibility, signed packaging, activation, withdrawal, and rollback behavior. | Contract/golden tests, content canary, immutable signed v2 release, and mounted-package identity. |
| QA-010 accessibility | Keyboard camera selection, non-color toast copy, full-HUD legibility, and reduced-motion suppression. | Keyboard/focus automation and normal/large-text/high-contrast/reduced-motion packaged captures. |
| QA-001 render responsiveness | The final visual configuration becomes the input to Milestone 9. | This milestone records no frame/thermal pass; Milestone 9 owns the measured proxy and the product-level 60-minute interval remains open. |

## Authority, safety, and dependency constraints

- PM5 distance, pace, power, workout state, completion, and local-journal
  records remain authoritative. Boat, camera, input, preference, content, and
  water presentation have no write path to those facts.
- `CourseRuntime` derives reversible presentation from immutable snapshots. No
  path, hull, camera, or water state is backfilled after a hitch, gap, stale
  interval, disconnect, reconnect, recovered seek, or content handover.
- `RowingCore` stays independent of content, Unreal, SQLite, and camera types.
  Signed path values belong to `ContentRuntime`; deterministic path and camera
  state belong to `CourseRuntime`; Unreal converts the resulting plain numeric
  snapshot to game-thread transforms.
- The route is a virtual presentation waterline, not a navigational chart or a
  claim that physical rowing is permitted or safe on the Han River.
- Downloaded content remains signed, validated, and data-only. It may describe
  positions, tangents, and lookup values but cannot contain Blueprint bytecode,
  script, code, a movement controller, or a raw package-path selection.
- Standard stays usable offline and whenever Han content is absent,
  incompatible, corrupt, expired, withdrawn, fails validation, fails to mount,
  or loses its streamed level.
- Unreal world and UI code consumes snapshots only on the game thread. No
  download, activation, mount, route replacement, or scene rebuild occurs
  during an active workout.
- Camera preferences use the owner-only app-state database under ADR-0012. They
  are non-secret and must never be copied into the workout journal, diagnostics
  bundle, or content package.

## Baseline capture and geographic proof

The phrase "200 m geographically north" remains a target for later geographic
evidence, but is not a prerequisite for adding or publishing the internal Han
path under [ADR-0016](../adr/0016-owner-approved-han-path-publication.md). When
that evidence is collected, record one baseline entry containing:

- source revision; reviewed runtime-map package path and asset identity;
- activated content ID/version/catalog revision when applicable;
- current visible 0 m boat world transform in centimetres;
- the level/georeference transform, coordinate reference system, axis order,
  origin, scale, and the reviewed calibration control points used to convert
  world coordinates to geographic coordinates; and
- the derived WGS84/ENU coordinate of the visible 0 m pose.

The current visible pose is authoritative for this one offset decision. The
currently disagreeing GeoJSON and Banpo beat coordinates are evidence of drift,
not alternate start authorities.

Project the baseline into the reviewed local east/north/up frame, add exactly
`(east=0 m, north=+200 m, up=0 m)`, and project the result back into the same
Unreal level frame. The authoring/export tool should record both coordinates,
the transform version, and its calculation result. The later evidence target is
horizontal north displacement `200.0 m ±2.0 m`, absolute east drift at most
`2.0 m`, and vertical drift at most `0.5 m`; it does not block the owner-
approved internal path publication.

Do the edit in a review/staging map through Unreal MCP, export the canonical
path source, validate it, and only then promote the reviewed result to the
runtime map. An editor-only helper spline must not remain in the cooked map.
Never patch `.umap` or `.uasset` bytes directly.

## Route-schema v2 contract

### Wire evolution and compatibility

Keep Protobuf field numbers 1–11 of `RouteDefinitionV1` unchanged and reserved
to their current meanings. Add an optional presentation-path message at a new
field number and set `schema_version=2`/`compatibility.route_schema=2` for the
new Han definition. The generated `RouteDefinitionV1` type name is historical;
the schema field determines semantics. Do not renumber fields, reuse removed
numbers, or silently reinterpret a v1 value.

The content-manifest and envelope schema can remain v1 because existing
clients parse unknown route fields and then fail closed on
`compatibility.route_schema=2` before activation. The new client must:

- construct built-in Standard as route schema v1 internally;
- accept downloaded non-Standard content only at route schema v2 or later
  explicitly supported schema;
- classify downloaded v1 Han as `content.route_schema_incompatible`, retain
  its immutable bytes for diagnostics and controlled cleanup, and select
  Standard;
- advance the application/content compatibility build coherently rather than
  leaving `UContentSubsystem`'s current build `1` as an unrelated magic value;
  and
- update the fixture, canary, release packager, source validator, inventory,
  manifest compatibility, golden bytes, and unknown-field tests together.

Compatibility acceptance is explicit:

| Client/content combination | Required result |
|---|---|
| New client + valid signed v2 Han | Activate on the next launch, mount, select by stable route ID, and use the signed path. |
| New client + installed v1 Han only | Report route-schema incompatibility and use Standard; do not synthesize a path from the old GeoJSON or sine curve. |
| Old client + v2 catalog | Reject the incompatible route schema and keep its already valid old content or Standard; do not stage or activate v2. |
| New client + corrupt/invalid v2 active candidate | Try only a valid compatible v2 last-known-good candidate; otherwise use Standard and expose a stable safe error. |
| New client + withdrawn v2 | Stop new Han selection under the existing withdrawal policy and retain Standard. |

Because a new client deliberately rejects v1 Han, the first v2 release cannot
roll back to v1 Han on that client. A bad v2 release is withdrawn to Standard
or replaced by a higher-revision corrected v2 release. The origin must not
overwrite an existing immutable v2 URL or decrease the accepted catalog
revision.

### Canonical path data

The v2 presentation-path section contains:

- a path format version and owning route ID;
- an explicit route-local frame and handedness: +X forward at 0 m, +Y
  starboard, +Z up, integer millimetres, plus an integer-millimetre origin and
  fixed-point yaw that place the frame in the authored level;
- 21 ordered, uniquely identified control points, including 0 m and 5,000 m;
- for each control point, a strictly increasing `route_distance_mm`, signed
  integer position, and signed integer arrive/leave tangent vectors for a
  piecewise cubic-Hermite curve;
- a bounded arc-length lookup with monotonically increasing official
  `route_distance_mm`, segment index, and unsigned fixed-point segment
  parameter; adjacent entries are no more than 5,000 mm apart and the first/
  last entries are exactly 0/5,000,000 mm; and
- `arc_length_lookup_sha256`, calculated over the canonical lookup bytes. The
  existing `metadata_sha256` continues to cover the complete canonical route
  definition with only its own hash field cleared, so the path and lookup hash
  are themselves signed through the manifest/package chain.

Do not use serialized floating point, Unreal `FSplinePoint`, Unreal's generated
reparameterization table, or an engine-version-dependent auto-tangent as the
contract. The source compiler quantizes reviewed values to integer
millimetres/fixed point, emits deterministic bytes, and emits the same bytes on
repeat runs. `ContentRuntime` validates the bounded data; `CourseRuntime`
evaluates the Hermite segment and tangent without Unreal; the Unreal adapter
only converts the result to centimetres and builds the visual spline/transform.

The source compiler also records an independent dense arc-length audit. The
reviewed curve must measure `5,000,000 mm ±500 mm`, while the official lookup
starts at exactly 0 and ends at exactly 5,000,000 mm. The compiler may account
for sub-metre quantization in the lookup; it must not disguise a multi-metre
geometry error by simply rescaling official distance over the wrong curve.

For this route, validation rejects at least:

- a missing path, wrong owner ID/frame/version, any count outside 8–40 (the
  reviewed Han path uses exactly 21),
  duplicate/empty IDs, missing start/finish, or non-finite/unrepresentable
  conversion;
- horizontal coordinates outside ±10,000,000 mm, vertical coordinates outside
  ±1,000,000 mm, a zero tangent, discontinuity, ambiguous self-intersection,
  direction reversal, or a horizontal turn radius below 100 m;
- lookup entries with a gap over 5 m, non-monotonic distance or curve
  parameter, an invalid segment index, wrong endpoints, wrong length, or a
  mismatched lookup hash; and
- checkpoints that fall outside the route or fail to resolve monotonically
  through the lookup.

The route-source validator may sample the curve and camera envelopes against
reviewed obstacles as later evidence. Clearance, curvature, and camera-envelope
measurements do not block owner-approved internal path publication under
ADR-0016; they must not be represented as having passed until actually
recorded.

### Source and release artifacts

Add one canonical, reviewable v2 path-source file next to
`route-beats.json`. It records the owner decision and exact runtime-map
identity, route-local control points/tangents, and lookup-generation version.
Generate, do not hand-edit, the package `route.pb` from that source.

After the owner reviews the path, update as one change:

- the canonical v2 path source;
- `route-beats.json` and `han-river-5k.geojson` geographic/reference views;
- the route semantic version and content compatibility/build range;
- `route.pb`, package inventory, hashes, signature, immutable release metadata,
  and source validators; and
- any map placement metadata needed to align the streamed level with the same
  signed route frame.

Keep `route.han-river.5k` and publish the package Semantic Version supplied by
`VIR_CONTENT_VERSION` through the existing release-owner workflow.

## Engine-independent presentation ownership

`CourseRuntime` should own the state and calculations that can be tested
without Unreal:

- route-distance-to-path position and unit tangent;
- initial/snap/damped tangent yaw and discontinuity reset;
- bounded hull roll, pitch, bob, and rendered-stroke heave values;
- first-active-rowing latch, stop dwell, reveal phase/timing, preset selection,
  reduced-motion behavior, and invalid-input cancellation; and
- a plain numeric camera pose/preset/toast snapshot for the Unreal adapter.

Extend `FCourseTelemetryInput` with the accepted sample sequence or an
equivalent immutable sample-generation value. The world adapter derives it
from the workout snapshot so `CourseRuntime` can distinguish a newly observed
PM sample from the same sample presented over many render frames; it must not
infer a new row/stop fact merely because another frame elapsed.

`AGrayBoxCourseActor` remains a game-thread view. Split the visible boat into a
course anchor and a child hull/rower/oar visual root. Apply path position and
damped yaw to the course anchor, cosmetic roll/pitch/bob/heave to the child,
and the camera to the unrolled course frame. This prevents hull lean, stroke
heave, and water effects from tilting the horizon or moving official progress.
The actor must continue to bind no input and open no database.

All time-based presentation uses injected monotonic time in native tests. Clamp
the integration step to the existing frame-safe presentation bound and reset
filters after a new session, route replacement, accepted large seek, stale/
disconnect interval, or non-monotonic clock. Never iterate through missed time
or create delayed strokes, bob cycles, camera phases, wakes, or splashes.

## Boat movement

Resolve the authoritative course-anchor transform first, then apply bounded
cosmetic motion to the visual child:

- damp yaw toward the signed path tangent with an approximately 0.4 s response;
- snap heading only on initial load, route replacement, an accepted large
  seek/recovery, or invalid filter history;
- derive curvature roll from signed path curvature and presented speed, low-pass
  it, and clamp it to ±1.5 degrees;
- derive pitch only from bounded presented-speed change and rendered stroke
  phase, low-pass it, and clamp it to ±0.8 degrees;
- apply at most ±2 cm of slow world-anchored ambient bob; and
- apply at most ±1 cm of additional rendered-stroke heave.

The implementation must define the gains, filter constants, seek threshold,
and phase functions as named constants with native boundary tests; "subtle" is
not a substitute for testable bounds. Presented speed and stroke phase are
reversible display inputs only. Do not integrate speed into distance, infer a
stroke from camera motion, or persist any cosmetic value.

A fresh inactive sample holds course position, settles curvature/acceleration/
stroke channels toward neutral, and may retain only the slow ambient water bob.
Paused, stale, frozen, disconnected, complete, terminated, and endpoint states
hold the last bounded course position, freeze ambient phase, settle the visual
root to neutral without catch-up, return oars safely to catch, and let existing
water effects expire. Reconnect resumes from the next accepted presentation
snapshot; it does not replay missing motion. Reduced motion forces all
roll/pitch/bob/heave outputs to zero while retaining only necessary path yaw.

Hull wakes and oar ripples continue to use the final rendered hull/blade
transforms on the game thread. They remain pooled, short-lived, non-colliding,
and incapable of affecting the route or workout. Review their timing again
after the new visual-root motion is applied.

## Camera, stop, and reveal behavior

Camera offsets use the unrolled boat-local forward/starboard frame and remain
on the starboard side:

| Preset | Aft | Starboard | Height | Look-ahead | FOV |
|---|---:|---:|---:|---:|---:|
| Close | 8 m | 4 m | 1.6 m | 8 m | 55° |
| Medium | 12 m | 8 m | 2.2 m | 10 m | 60° |
| Wide | 18 m | 12 m | 5 m | 14 m | 68° |

Blend the numeric boat-local offsets and FOV, then resolve a new world pose
each frame. Do not linearly blend old and new world locations: on a bend that
can cross the hull or port side. The normal preset blend lasts one second,
uses the shortest yaw path, maintains a level horizon, and never has a
starboard offset below the smaller endpoint offset.

An **active-rowing sample** is a valid, newly observed, connected, fresh,
unfrozen sample for the current active session with workout state `Active` and
rowing state `Active`. It sets the per-session first-stroke latch. A **stopped
sample** has the same validity conditions, workout state `Active` or `Resting`,
and rowing state `Inactive`. `Unknown`, a repeated old sample, and a sample
without the required state are neither proof of rowing nor proof of a stop.

The stop/reveal state machine is:

| State | Entry | Behavior | Exit |
|---|---|---|---|
| Chase | Default, before first stroke, or after cancellation | Hold/follow the selected chase preset. | A stopped sample after the first-stroke latch enters Dwell. |
| Dwell | First eligible stopped sample | Keep chase; accumulate monotonic time only while the latest sample remains fresh and eligible. | At 10 s enter Outbound; any active sample or ineligible state resets to Chase. |
| Outbound | Dwell completes | Ease from the current chase pose to the shared wide starboard reveal over 10 s. | At 10 s enter Hold; active rowing enters Resume. |
| Hold | Outbound completes | Hold the reveal for 10 s. | At 10 s enter Return; active rowing enters Resume. |
| Return | Hold completes | Ease to the currently selected chase preset over 10 s. | At 10 s re-enter Outbound immediately while still stopped; active rowing enters Resume. |
| Resume | Active rowing interrupts Outbound/Hold/Return | Blend from the exact current pose to the selected chase over 2 s. | At 2 s enter Chase. A new stop needs a fresh 10 s dwell. |

Changing the preset in Chase blends immediately and shows `Camera: Close`,
`Camera: Medium`, or `Camera: Wide` for two seconds. Changing it during
Outbound/Hold/Return updates the next chase destination and toast but does not
alter the shared reveal pose or restart a phase. Key repeats do nothing.

Invalid data during Dwell resets it and remains in chase. Pause, stale/frozen
input, disconnect, completion, and measured endpoint entry during an active
reveal finish the current complete outbound/hold/return cycle before returning
to chase and requiring a fresh dwell. Route replacement cuts before unloading;
explicit termination cuts immediately and clears reveal state. The first-stroke
latch survives a transient disconnect within the same session, but dwell/phase
progress does not. A new session clears the latch. Standard lap boundaries do
not clear eligibility. Before the first stroke, keep the selected chase
composition.

Reduced motion runs the same eligibility and ten-second dwell so behavior is
predictable, but never enters a moving phase: it cuts once to the static shared
reveal and holds only while fresh telemetry remains validly stopped. It cuts
back on activity, invalidity, pause, completion, termination, endpoint entry,
or route replacement. The manual `View surroundings` action is removed.

## Input, toast, and preference persistence

Extend the HUD-level input processor, not `AGrayBoxCourseActor`, to handle
non-repeating `C`. Route the request through `UCourseSubsystem` to the
engine-independent state. Preserve Escape/focus behavior and do not activate,
move, or hide an End Session action. The toast is hit-test-invisible, uses
text rather than color alone, does not reflow the metric cells, respects the
same text-scale/high-contrast settings as the HUD, and expires from monotonic
time even if persistence fails.

Add a forward-only LocalData migration after the current schema version for a
typed singleton table such as:

```text
presentation_preferences
  singleton = 1
  camera_preset = close | medium | wide
  updated_at
```

Use an engine-independent `FPresentationPreferencesRepository` against the
owner-only app-state database `rowing.sqlite3`. Load the value during normal
pre-menu settings bootstrap; missing, malformed, or unreadable data yields
Medium and a redacted diagnostic. Publish the in-memory selection immediately
on `C`, then coalesce and write it on a non-game-thread serialized worker owned
by a game-instance presentation-settings adapter. `UCourseSubsystem` consumes
only its immutable setting snapshot. A busy, read-only, full-disk, permission,
migration, or commit failure leaves the in-session preset usable, keeps
Medium/last successfully loaded value as the next-launch fallback, and cannot
affect `workout-journal.sqlite3` or workout completion.

Tests must migrate every existing schema version, reject invalid database
values, prove restart persistence, prove rapid cycling coalesces safely, and
inject write/commit failure. Do not add a generic unbounded key/value store for
this one setting.

## Milestone 5 regression boundary

Do not select, replace, tune, or visually accept water material treatment in
this milestone. Verify only that the visual-root split preserves existing wake
and ripple transforms, pool limits, one effect per visible entry/exit, stale or
reconnect no-backfill, and expiry. `-HideWaterEffectsForBenchmark` and
`-HideWaterForBenchmark` remain functional regression checks. Milestone 9 owns
water/effects cost deltas, thermal proxy, and QA-001 acceptance.

## Work order and gates

| Order | Deliverable | Gate |
|---:|---|---|
| 1 | Read-only Unreal MCP health query and immutable map-identity capture | Owner decision is bound to the exact reviewed runtime-map bytes. |
| 2 | Route-schema v2 types, compatibility policy, canonical compiler, parser limits, lookup hashing, and golden fixtures | Native positive/negative/unknown-field tests; old Han safely selects Standard. |
| 3 | v2 source, release, inventory, canary, and package tooling | Repeat generation is byte-identical; schema/build/version fields agree across every artifact. |
| 4 | Review-map path authoring and optional path/camera-envelope audit | Owner path review and exact map identity; later audit results are recorded only when available. |
| 5 | `CourseRuntime` path evaluator, heading/hull outputs, camera state machine, and discontinuity resets | Pure native tests with injected time; no Unreal, SQLite, network, or journal dependency. |
| 6 | Unreal anchor/visual-root application, camera presets, HUD `C` input/toast, and typed preference repository/adapter | Fresh-module Unreal automation; actor stays input/database-free; persistence fault tests pass. |
| 7 | Wake/ripple hierarchy regression only | Attached, bounded, one-effect-per-event, expiring effects on both courses; no water-treatment decision. |
| 8 | Closed-Editor native build, fresh Editor automation, external Han cook, restricted signed v2 release, and unsigned Shipping app | Normal owner workflow only; no hot reload or suffixed module evidence. |
| 9 | Download, next-launch activation, mounted-Han Shipping canary, full-route review, and evidence close | Package identity proves v2 mounted Han; all milestone checklist rows pass or remain explicitly open. |

Native Unreal module changes require all Editors closed for the normal build and
a fresh Editor process before automation. Do not use Module Recompile/hot reload
as acceptance evidence. Signing and publication remain release-owner procedures
under the existing content-origin and packaging runbooks.

## Verification matrix

### Native and tooling coverage

Tests must cover:

- built-in Standard v1 construction; valid Han v2 canonical round trip; unknown
  optional fields; malformed, oversized, noncanonical, wrong-owner, wrong-frame,
  count, bounds, ID, tangent, continuity, self-intersection, length, lookup,
  checkpoint, metadata-hash, and compatibility failures;
- v1 client/v2 catalog and v2 client/v1 Han behavior, compatible v2
  last-known-good selection, withdrawal, corrupt active candidate, and Standard
  fallback without deleting immutable prior content;
- byte-stable path generation and golden serialization across repeat runs;
- exact 0 m/control-point/checkpoint/5,000 m sampling, monotonic progress,
  continuous position/tangent around every segment boundary, and fixed-point
  lookup endpoints;
- bounded prediction and no backfill after hitch, gap, stale, disconnect,
  reconnect, accepted seek, clock regression, route replacement, and session
  replacement;
- yaw response/snap, every roll/pitch/bob/heave clamp, stopped settling,
  endpoint hold, and complete reduced-motion suppression;
- first-stroke latch, fresh-sample definition, ten-second dwell, repeating
  10/10/10 loop, exact two-second resume, preset changes in every phase, and
  cancellation before first stroke or during unknown, repeated, paused, stale,
  frozen, disconnected, reconnecting, complete, terminated, and endpoint
  states;
- Close/Medium/Wide numeric geometry, starboard-only parameter blends,
  shortest yaw, non-repeating `C`, modal/focus non-interference, two-second
  toast, and Medium fallback; and
- preference migration from every existing schema, restart persistence,
  coalescing, corrupt value, busy/read-only/full/permission/commit failure, and
  proof that the workout journal is never opened by the preference path.

### Unreal/editor automation

With a fresh unsuffixed module, automation must prove:

- v2 points/tangents convert from millimetres to centimetres once and the
  visible Unreal path matches engine-independent samples within the declared
  tolerance;
- every signed control-point and named-checkpoint label shows cumulative
  distance rounded up to a grouped whole metre, with no elevated control-point
  beacon or saved route-gate geometry visible;
- the course anchor follows the signed path while the visual child alone gets
  hull motion; camera roll remains zero and water effects use the final rendered
  hull/blade transforms;
- all preset and reveal poses stay starboard, keep a level horizon, and do not
  cross the shell on straight or maximum-curvature segments;
- route/level handover, v2 fallback, disconnect/reconnect, seek, endpoint, and
  reduced-motion cases have no visual backfill or stale state leakage;
- the HUD keeps metrics and End Session usable, ignores repeated/modal `C`,
  shows non-color toast copy, and removes only Han's superseded reduced-motion
  `View surroundings` action; and
- Standard transforms, camera, rest behavior, selection, and endpoint behavior
  match their pre-milestone regression baseline.

### Packaged and human evidence

The packaged evidence must record app/content/toolchain identity and show:

- when geographic or clearance evidence is collected, the new-start offset and
  camera/obstacle observations are recorded without implying that ADR-0016
  made them a publication prerequisite;
- a continuous 0–5 km route pass reaches every official checkpoint in order,
  turns smoothly, and holds the exact endpoint while PM-backed HUD/journal
  activity remains live;
- each preset keeps the shell, blades, forward waterline, toast, and complete
  HUD readable at normal text, large text, and high contrast;
- first-stroke/no-stroke, stop dwell, at least two complete reveal loops,
  resume from each phase, preset change during a loop, invalid-input
  cancellation, reconnect, reduced-motion static view, and endpoint
  suppression behave as specified;
- wakes and blade ripples remain attached, bounded, one-per-event, and expiring
  with the final hierarchy; and
- Standard remains usable offline and retains its closed geometry/lap behavior when Han
  v1 is incompatible, v2 is absent/corrupt/withdrawn, or the level/mount fails;
  and
- visible Details/status plus recorded hashes prove the exercised route is the
  new signed, activated, mounted v2 Han package—not the editor map, generated
  sine path, a stale package, or Standard fallback.

Do not attach local journals, PM serials, raw captures containing private data,
signing material, or generated Unreal output to repository evidence.

## Required commands

Run from the repository root through `make`/`Scripts/dev.py`:

```sh
make build && make test
make format-check
make han-source-verify
make content-canary
make unreal-native-app
make unreal-smoke
make han-external-cook
make content-release-package # restricted release-owner procedure
make unreal-shipping
make unreal-package-verify
```

After the closed-Editor build, use a fresh Editor and Unreal MCP for the scoped
course automation and reviewed map/path work. Then perform the owner-run signed
content publish/download/next-launch activation and mounted-Han Shipping
canary. Record exact command, automation, package, catalog, map, and build
identities; do not summarize a partial pass as the milestone gate.

## Milestone gate checklist

Milestone 6 passes only when all of the following are recorded:

- [ ] Owner approval is bound to the exact reviewed runtime-map identity.
- [ ] Route-schema v2, deterministic source compiler, compatibility matrix,
      path bounds, lookup/hash validation, and negative tests pass.
- [ ] Owner approves the full path; any later clearance/camera-envelope report
      is recorded as supporting evidence.
- [ ] Engine-independent movement/camera tests and fresh-editor Unreal
      automation pass without hot reload.
- [ ] Camera preference migration/persistence and every failure-injection case
      pass without touching workout durability.
- [ ] Wake/ripple hierarchy regression is recorded for Standard and mounted Han;
      no Milestone 5 water-treatment acceptance is claimed.
- [ ] Standard regression and every Han fallback/incompatibility case pass.
- [ ] A new immutable signed v2 Han release downloads, next-launch activates,
      mounts, and is identified in the Shipping canary.
- [ ] Full-route, camera, stop/reveal, accessibility, and endpoint packaged
      evidence passes on the reference Mac.
- [ ] Every required command result and every still-open limitation is recorded
      in the owning Phase 2 evidence/changelog.

Milestone 9 remains responsible for the warm six-minute Shipping timing,
water/effects cost deltas, and thermal proxy. Product-level QA-001 still uses
its 60-minute interval, and neither result is implied here.

## Principal risks and rollback

| Risk | Control / rollback |
|---|---|
| Geographic source files and visible map disagree | Bind publication to the reviewed map identity; retain any later calibration as supporting evidence rather than averaging conflicting sources. |
| Unreal spline behavior changes the route | Sign explicit Hermite points/tangents and a fixed-point lookup; evaluate it in `CourseRuntime`; use Unreal only as the view adapter. |
| v2 strands an older client or bad content strands a new one | Compatibility fails closed before activation. Old clients retain their prior content/Standard; new clients withdraw bad v2 to Standard or accept a higher-revision corrected v2. |
| Camera motion causes discomfort or crosses the boat on bends | Blend boat-local parameters, validate the complete camera envelope, keep the horizon level, and require reduced-motion packaged review. |
| Stop state triggers from missing/old data | Require newly observed fresh PM state, make `Unknown` ineligible, reset phase timing on every invalid transition, and test injected clocks. |
| Preference I/O affects a workout | Keep it in the separate app-state database on a serialized worker; publish in memory first; treat every I/O failure as non-fatal. |
| Final boat motion disrupts effects | Regression-test effect attachment, pooling, event de-duplication, and expiry; defer water-treatment selection and acceptance to its owning milestone. |
| Editor or package evidence uses stale native code/content | Close Editor for native builds, reopen a fresh process, reject hot-reload evidence, and record module/app/catalog/route/package hashes. |

## Out of scope

Water-treatment selection, replacement, tuning, or visual acceptance; free
camera control; camera HUD buttons; user steering; collision or avoidance gameplay;
navigation; current/wind physics; remote boats; ranked-race behavior; new
workout semantics; route lengths other than the exact 5 km Han contract;
cutscene music; executable downloaded content; performance/thermal
certification; and the product-level 60-minute QA-001 run are out of scope.
