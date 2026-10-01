# Phase 2 Milestone 7: Biomechanical high-order sculling animation

Status: In progress — engine-independent trajectory and procedural rig implementation complete; closed-Editor build, fresh-Editor evidence, packaged-route evidence, and owner visual acceptance pending

Owner: Client/content and technical art

Last reviewed: 2026-09-29

## Purpose and bounded outcome

Replace the Milestone 6 component proxy with deterministic, authored sculling
biomechanics while preserving the existing workout and presentation contracts:

```text
PM5 stroke state -> normalized phase -> quintic biomechanical targets
                 -> fixed oar grips and body targets -> two-bone joints
                 -> rendered mesh transforms
```

The boat travels bow-first along local `+X`; the athlete faces the stern along
local `-X`. This milestone changes presentation only. It does not change PM5
authority, route coordinates, official or predicted distance, workout
persistence, reconnect policy, content compatibility, or race behavior.

The public `FCoursePresentationSnapshot` and its normalized `StrokePose`,
`SeatPose`, `TorsoPose`, `ArmsPose`, and `OarPose` fields remain stable. Their
values now come from an engine-independent fifth-order evaluator.

## Research basis and interpolation policy

Minimum-jerk interpolation is the baseline because multijoint reaching motion
is well represented by minimizing changes in acceleration: Flash and Hogan,
*The coordination of arm movements: an experimentally confirmed mathematical
model* ([PMC6565116](https://pmc.ncbi.nlm.nih.gov/articles/PMC6565116/)).

For a clamped normalized time `u`, the baseline curve is:

```text
S5(u) = 10u^3 - 15u^4 + 6u^5
```

It has exact endpoint position, zero endpoint velocity and acceleration, is
monotone, and cannot overshoot its endpoint range. Motions with authored
intermediate events use piecewise quintic Hermite segments defined by position,
velocity, and acceleration at each knot. Adjacent segments must share all three
values, producing `C2` continuity.

Unconstrained Catmull-Rom paths are forbidden for joints and oars. Final mesh
Euler rotations are not independently interpolated: the runtime interpolates
biomechanical targets, solves endpoints, then derives each segment transform.
Evaluation uses injected monotonic time and must not depend on render cadence.

## Stroke timing and normalized channels

`StrokePose=0` is catch and `StrokePose=1` is finish. Drive raises the pose;
recovery lowers it. The runtime latches the applicable measured PM5 drive or
recovery duration when that state begins. A later duration update applies only
to the next phase. When duration facts are absent, stroke-rate timing retains
the `35%` drive / `65%` recovery split and changes temporal duration only.

| Channel | Drive interval | Recovery interval | Required order |
|---|---:|---:|---|
| Seat / legs | `0.00-0.65` | `0.35-1.00` | Drive first, recover last |
| Torso | `0.35-0.88` | `0.12-0.65` | Opens after legs, rocks over after hands |
| Arms | `0.72-1.00` | `0.00-0.28` | Pulls last, recovers first |
| Oar sweep | `0.00-1.00` | `0.00-1.00` | Full monotone sweep and reversal |

Drive channels evaluate `S5((StrokePose-start)/(end-start))`. Recovery channels
evaluate the reversed path against recovery progress `1-StrokePose`; recovery
is not a separate visually unrelated curve. The oar sweep uses two `C2`
quintic-Hermite segments with zero endpoint angular velocity and one smooth
velocity maximum at mid-drive.

## Procedural biomechanical rig

`RowerRoot` is a stern-facing child of the cosmetic `VisualRoot`. The athlete
uses separate upper-arm, forearm, hand, thigh, shin, and shoe components. The
baseline dimensions are uniformly scalable and fixed during every pose:

| Segment | Length |
|---|---:|
| Thigh | 48 cm |
| Shin | 45 cm |
| Upper arm | 32 cm |
| Forearm | 27 cm |
| Torso, hip to shoulder | 58 cm |

Feet remain fixed at the sternward foot stretcher. Catch and finish seat
positions are derived from the fixed leg lengths and authored knee geometry,
not from mesh stretching. Both legs use an analytic two-bone solution with an
upward and slightly outward knee bias. The target envelope is a `62 degree`
catch knee, near-vertical catch shin, and `168 degree` finish knee.

Shoulders are derived from the hip, fixed torso length, and a target that moves
from `30 degrees` sternward lean at catch to `15 degrees` bowward layback at
finish. The torso remains one neutral fixed-length segment. Both arms solve
from those shoulders to the rendered oar grips with outward elbow bias; wrists
and fixed-length hands remain collinear with the forearms.

## Oars, hands, and water contacts

Oarlocks remain fixed relative to the boat at `(0, +/-95, 105) cm`. The oar
mesh origin is the pin, its grip is `90 cm` inboard, and its blade extends along
mesh-local `+X`. Port and starboard geometry is mirrored:

- catch is approximately `55 degrees` bowward of the transverse axis;
- finish is approximately `35 degrees` sternward of the transverse axis;
- grip positions are calculated from the final pin and shaft transforms;
- hands follow those grips; pins never follow hands; and
- the port hand crosses above starboard by approximately `4 cm` where the
  handle paths overlap.

Blade vertical motion and feathering use quintic phase windows:

| Event | Interval |
|---|---:|
| Catch entry | drive `0.00-0.04` |
| Squared and submerged | drive `0.04-0.92` |
| Extraction | drive `0.92-1.00` |
| Feather | recovery `0.00-0.10` |
| Feathered and clear | recovery `0.10-0.72` |
| Re-square | recovery `0.72-1.00` |

Blade-tip contacts are calculated from final rendered oar transforms. Only
fresh drive entry/extraction transitions may emit ripples. A feathered recovery,
stale input, disconnect, reconnect bridge, reduced motion, or missing rendered
blade must not generate drive ripples or backfill a missed contact.

## Discontinuities and fallbacks

Waiting, stale, frozen, disconnected, completed, and terminated states never
advance a synthetic stroke. Each channel returns to catch through a `500 ms`
quintic Hermite bridge initialized from its current position, velocity, and
acceleration and ending with zero velocity and acceleration.

After a previously live animation reconnects, the same bounded bridge reaches
the applicable measured state endpoint before that phase clock begins. This is
presentation-only and cannot interpolate or invent PM5 distance. A clock
regression, session replacement, or route replacement retains the existing M6
no-backfill behavior.

Reduced motion preserves correct catch and finish geometry. It continues to
suppress ambient hull motion and nonessential water effects; it does not replace
the stroke with anatomically incorrect transforms.

## Requirement traceability

| Requirement | Changed behavior | Milestone evidence |
|---|---|---|
| FR-006 responsive virtual boat and avatar | Deterministic quintic phase, articulated fixed-length athlete, fixed pins, grip-driven hands, blade phases | Native trajectory tests, fresh-Editor automation, staged pose review, packaged Standard/Han review |
| QA-001 render responsiveness | Additional procedural components and per-frame analytic joint solving | Build/package gates here; Milestone 9 retains measured Shipping performance and thermal acceptance |
| QA-010 accessibility | Correct geometry remains under reduced motion while ambient motion/effects are suppressed | Unreal automation and packaged reduced-motion captures |

This milestone supplies incremental evidence only. It cannot pass a complete
`FR-*`, `QA-*`, Phase 2, or Milestone 9 gate by itself.

## Verification

Native tests must cover:

- exact endpoint position, velocity, and acceleration for minimum-jerk and
  general quintic Hermite evaluation;
- monotonicity, no overshoot, finite zero/minimum/long-duration results, and
  `C2` continuity at each internal knot;
- identical complete-stroke poses at equivalent timestamps under 30, 60, 120,
  and irregular render intervals;
- drive legs-torso-arms order and recovery arms-torso-legs order;
- duration latching and rate-estimated timing; and
- catch/reconnect bridge continuity without PM-distance mutation.

Fresh-Editor automation must sample catch, early drive, mid-drive, finish,
hands-away, body-over, and late recovery and verify:

- fixed feet and pins; exact segment lengths; target knee/elbow envelopes;
- neutral fixed-length torso, stern-facing root, and stable port/starboard
  solutions;
- grip attachment, wrist alignment, crossover clearance, and no hull, knee, or
  hand collision;
- one smooth oar drive velocity peak and ordered entry/extraction/feather/square
  phases; and
- no recovery water contact, snapping, derivative discontinuity, or frame-rate
  dependence.

After the normal closed-Editor build, reopen a fresh Editor and inspect all
seven poses from side, overhead, chase, and reveal views. Then run:

```sh
make build && make test
make format-check
make unreal-native-app
make unreal-smoke
make unreal-shipping
make unreal-package-verify
```

Repeat packaged visual acceptance on Standard and on the exact activated,
mounted Han v2 content identity. `make han-external-cook` remains required when
the Han package inputs changed; this milestone does not itself mutate Han map
or route data.

## Acceptance and open evidence

Acceptance requires continuous motion without visible derivative changes,
snaps, stretched limbs, detached hands, moving pins, knee/hand collision,
incorrect facing, or recovery ripples. Record source revision, native test
result, unsuffixed module identity, automation result, app/content/package
identity, pose captures, and every remaining limitation in the Phase 2
changelog.

At implementation time the following remain explicitly open:

- closed-Editor `unreal-smoke` and fresh-Editor automation;
- owner side/overhead/chase/reveal visual review;
- Standard and mounted-Han unsigned Shipping/package verification; and
- Milestone 9 frame, GPU, and thermal measurements.

## Out of scope

Motion capture, runtime machine learning, skeletal/Control Rig replacement,
new athlete customization, route or water-treatment changes, PM workout
semantics, official-distance interpolation, collision gameplay, steering,
rank/race behavior, signing/notarization, and Milestone 9 performance acceptance
are out of scope.
