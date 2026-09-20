# Phase 1 Milestone 8: gray-box course, boat, and stroke presentation

Status: Implementation complete; full UBT/Automation and fresh packaged simulator confirmation outstanding because this agent sandbox blocks Unreal's global cache writes
Owner: Client / Unreal  
Last reviewed: 2026-09-20

## Contract checkpoint

This milestone completes build-order step 5 and supplies the gray-box presentation portion of FR-004 and FR-006. It consumes the immutable `FWorkoutSnapshot` delivered by Milestones 4, 5, and 7. It does not change the PM5 protocol, telemetry normalization, session journal, persisted schemas, or any accepted ADR.

The authority boundary is normative: PM5 cumulative distance remains the measurement shown by the HUD and written to the journal. `CourseRuntime` may predict and smooth only reversible client presentation. Its wrapped course coordinate, boat transform, and rower pose are never written back to `WorkoutRuntime` and never determine workout completion or a future race result.

## Delivered behavior

### Engine-independent presentation

`Source/CourseRuntime` is C++20 and depends only on `RowingCore`. `FCoursePresentationRuntime::Update` takes a hardware-neutral `FCourseTelemetryInput` and monotonic time and returns an immutable presentation snapshot.

- The route domain is exactly 2,000,000 mm. Measured and presented distances remain cumulative; only `WrappedCourseDistanceMm` uses modulo 2 km. `CompletedLap` preserves the lap count.
- A live sample projects its reported speed for at most 250 ms. A disconnect freezes one already-bounded coast endpoint; repeated disconnected frames cannot extend it. Frozen, stale, resting, paused, inactive, and ended input cannot create further distance.
- Presentation follows the target with the analytic critically damped solution at `omega = 16 s^-1`. The first valid sample snaps, backward clock movement advances by zero, and a new session identity resets all presentation state.
- PM stroke states are primary. Drive/recovery durations are preferred, then a 35/65 cycle derived from stroke rate, then 700/1,300 ms defaults. Unknown state with a valid rate is an explicitly `Estimated` rate-only cycle. Missing, stale, disconnected, resting, paused, or ended input cannot continue that fallback and eases to catch over 500 ms.
- Component channels enforce legs/seat first, torso second, arms last on drive. Because the same smoothstep segments are read in reverse, recovery releases arms, then torso, then seat.

`course_runtime_tests` covers route start and markers, exact wrap, multiple laps and large cumulative distance; bounded prediction, disconnect, damping, backward clocks and session replacement; PM states, timing and rate fallbacks, missing telemetry and catch return; and biomechanical ordering.

### Unreal presentation

`UCourseSubsystem` is a tickable world subsystem. It spawns only into Game and PIE worlds and refuses commandlets, Editor, preview, and other unrelated worlds. Each tick it pulls `UWorkoutSubsystem::GetSnapshot`, translates a copy into `FCourseTelemetryInput`, updates `CourseRuntime`, and applies the returned value to `AGrayBoxCourseActor`. There is no write path to the workout subsystem.

`AGrayBoxCourseActor` generates all presentation at runtime from built-in Unreal primitives:

- a closed elongated oval spline normalized to the 2 km domain, with 32 runtime-generated, visible spline-mesh edge segments;
- gray-box water and shore/reference geometry plus eight markers at 250 m intervals;
- a stationary-at-start single-scull proxy with hull, sliding seat, torso, paired arms, and symmetric oars;
- boat location and forward tangent from wrapped presented distance;
- a rigid elevated follow camera that keeps the boat, stroke motion, and nearby route geometry human-visible, with a 50 degree field of view, zero roll, no lag, and no input component.

No project `Content/`, Blueprint, skeletal mesh, animation asset, collision, audio, VFX, steering, or configurable camera was added. On session completion distance settles to the final reported target and remains there while the proxy returns to catch.

The existing HUD gained only `Estimated stroke motion`. It is visible for an active rate-only cycle and hidden for primary PM-state motion, paused/disconnected catch return, idle catch, and unavailable animation.

`VirtualRowing.CoursePresentation` covers course transforms and wrap, marker count, representative proxy transforms, camera geometry and lack of input bindings, supported world types, label visibility, and the one-way presentation boundary.

## Fixtures and operator evidence

The simulator exposes `state_missing`, a deterministic ten-minute fixture retaining stroke rate while removing stroke state. Together with the existing fixtures, packaged-app confirmation is:

1. `-SimulatorDevice=random10min`: smooth cumulative motion and ordered proxy biomechanics.
2. `-SimulatorDevice=state_missing`: visible `Estimated stroke motion` during active rowing.
3. `-SimulatorDevice=packet_loss`: no more than the bounded 250 ms coast, then stopped boat and 500 ms return to catch.
4. `-SimulatorDevice=race2000m`: route wraps seamlessly while the HUD continues to show cumulative distance.

Real-PM5 visual proof remains part of the combined Phase 1 exit gate. It does not block this simulator-driven milestone.

## Verification

Required automated gates:

```sh
make test
make format-check
make doctor
make unreal-native-app
make unreal-smoke
```

The headless Automation filter is `VirtualRowing.CoursePresentation`. Packaging and visual evidence use:

```sh
make unreal-shipping
make unreal-package-verify
```

Implementation-session results:

- `make format-check`, `make unreal-native-app`, `git diff --check`, and the documentation file inventory passed.
- `course_runtime_tests` passed. CTest passed 26/26 when the unrelated Keychain-backed `local_data_mac_tests` was excluded. A complete `make test` had passed 27/27 earlier in the session, then its final rerun was blocked only by `local_data_mac_tests` receiving Keychain `OSStatus 100001`; no Milestone 8 test failed.
- `make doctor` failed because the Xcode Metal Toolchain component is absent. Consequently `make unreal-smoke` stopped at its doctor prerequisite. A direct UBT compile attempt also could not bypass that gate in this environment: UE 5.8's UBA process was denied shared-memory creation by the execution sandbox before compilation.
- The headless `VirtualRowing.CoursePresentation` run, `make unreal-shipping`, `make unreal-package-verify`, and the four packaged visual observations were therefore not run. They remain required; compilation or native tests are not substituted for them.

Follow-up (2026-09-20): the owner launched the packaged app with `-SimulatorDevice=random10min`; the HUD metrics updated, but the course presentation was not human-visible. There were several compounding causes. The HUD root border filled the viewport with an opaque dark brush; it is now fully transparent, while a smaller left-aligned metrics panel uses a 38% alpha fill and text shadows. The original spline was only a transform guide; the actor now creates visible spline-mesh edges. `UCourseSubsystem` was a game-instance subsystem whose world lookup could be unavailable during startup; it is now a world subsystem that owns the rendered world's actor and camera, and HUD creation activates it before entering the viewport. Runtime-loaded primitive assets could also be absent from a Shipping cook, so meshes and material are hard actor references. Finally, `/Engine/Maps/Entry` contains no authored light, leaving the lit primitive material black, and an attempted full-course overview made the 4.8 m boat and stroke motion only a few pixels. The actor now supplies a movable directional light and a close elevated follow camera that shows the moving route, boat, and stroke. Automation coverage asserts root/panel alpha, illumination, visible edge segments, and follow-camera framing. The relevant Unreal translation units and module compile and link using the generated response files. Full UBT and packaging attempts were blocked before compilation because this agent sandbox denies Unreal access to its global trace/XML-cache files under `~/Library/Application Support`; a freshly packaged build and the four visual observations remain required before the simulator evidence can pass.

Owner-run evidence update (2026-09-20): `make unreal-shipping` and `make unreal-package-verify` **passed** at revision `c8d78d720daf` on the pinned host. The four packaged simulator visual observations above remain pending and are not implied by the packaging pass.

Owner-run finding (2026-09-20): during normal real-PM5 recovery, the oars looked unsmooth. `CourseRuntime` already derives a continuous recovery phase, but `AGrayBoxCourseActor` directly assigned the resulting hand position and oar yaw to mesh components, making short telemetry/frame cadence irregularities visible at the oar. The actor now applies bounded presentation-only interpolation to the oar handle position and yaw, snapping only the initial pose; arms, PM telemetry, session state, journal values, and authoritative distance remain unchanged. A rebuilt packaged real-PM5 visual check remains required.

## Deferred

Route selection, authored production assets, content manifests, collision/gameplay physics, wake/VFX, audio, remote boats, user cameras, and performance certification are later work. QA-001 certification remains deferred to Phase 4 under ADR-0010; this milestone guards functional behavior and obvious game-thread regressions only.
