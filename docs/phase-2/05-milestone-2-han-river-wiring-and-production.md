# Phase 2 Milestone 2: wire and enhance the Han River course assets

Status: In progress — wiring compiled; spike 1 passed (owner-observed); scene instanced (recook pending); production art (step 5) not started
Owner: Client/content, technical art, release/platform
Last reviewed: 2026-09-21

## Purpose

Milestone 1 delivered the signed download, mount, and next-launch activation of
`route.han-river.5k`, but its completion record states the gap this milestone
closes: the downloaded map, materials, and meshes are mounted and **no code loads
them**, and switching routes after startup does not rebuild the course scene.
The Han look today comes from the built-in C++ presentation kit in
`GrayBoxCourseActor`.

This milestone (1) makes the mounted content the real Han presentation, (2)
lets the athlete switch courses while idle without restarting, and (3) raises the
whole 5 km route to production-environment quality, with an optional audio bed.
It passes no Phase 2 exit gate.

## Owner-confirmed decisions (2026-09-21)

| Topic | Decision |
|---|---|
| Runtime wiring | Load the mounted Han map by route ID; the built-in C++ kit remains the fallback if the load fails. |
| Quality bar | Full-route production quality. This is the "separately scoped milestone" that [03-han-river-course-content-plan.md](03-han-river-course-content-plan.md) reserved for raising the whole route beyond the six-beat vertical slice. |
| Art source | Mix of original work and reviewed CC0 / CC BY-SA packs. |
| Audio | In scope: optional environmental bed, with provenance review. |
| Carry-over | Mid-session route switch rebuilds the scene. |
| Package size | **Compressed Han package cap is 100 GiB** (was 32 GiB in Milestone 1). Asset size is **not measured and not a gate** in this milestone; it is ignored for now. The client's uncompressed bound stays 4x the cap and the 100 GiB free-storage headroom is unchanged. |
| Frame-time cap | **Measure first; set the cap at milestone close.** Not a gate until then. |
| Acceptance | Owner route review of the full 5 km on the reference Mac. |

The other Milestone 1 gaps (packaged failure-case canary, beat captures, Shipping
verification of the WAL/Download fix) were not selected. They stay listed as open
in the [Milestone 1 evidence record](04-milestone-1-evidence.md); this milestone
does not close them.

## Fixed constraints (unchanged)

- The course is presentation only: no write path to distance, pace, workout state,
  or the journal. PM5 facts stay authoritative.
- Downloaded content is data only. Selection is by stable route ID, never a raw
  package path. Standard 2 km stays selectable offline and on any Han failure.
- No content operation, mount, or scene change while a workout is active.
- Only `content-current` signs releases; Milestone 1's signature, hash, size, path,
  and inventory validation is not relaxed.
- Unreal UI/world code stays on the game thread and consumes snapshots only.

## Design

### Implementation note (2026-09-21)

The authored level from `Scripts/build_han_editor_assets.py` is a 500 m scene
(authored x -70 m to +460 m), while the route is 5 km. Wiring therefore treats
it as an overlay on the kit baseline; it becomes the whole route only when the
production-art step extends it. Steps 2-4 of the work order are implemented and
compile. Spike 1 (does the streamed level resolve from the runtime mount on a
Shipping build, and does the allowlist accept the real cooked level) passed as
an owner-run check; see the spike list. A rejection still falls back safely.

### 1. Loading the mounted map

`AGENTS.md` and the content plan forbid raw-package-path selection, so the client
holds a compiled-in table `route ID -> fixed soft object path` for the level. The
signed inventory declares the level entry only as a logical role, and the client
rejects a mismatch.

Recommendation: stream the Han level as a **dynamic sublevel** (`ULevelStreamingDynamic`)
into the persistent level rather than `OpenLevel`. The boat, camera, HUD, and
workout actors stay alive and never see a level swap. `GrayBoxCourseActor` stays
the owner of the spline, boat transform, and the fallback kit. The kit is
disabled only after the streamed level reports loaded and visible.

Loading a downloaded `.umap` is the highest-risk step in the milestone because a
level can carry a level Blueprint and actor classes. The validator rules from
Milestone 1 apply, extended so that before cook it must reject:

- any level Blueprint or Blueprint-generated class;
- any actor or component class outside an explicit allowlist (static mesh,
  instanced/hierarchical static mesh, lights, sky/atmosphere, height fog, post
  process volume, audio, spline mesh);
- redirectors and any reference to an asset outside the declared inventory.

The client does the same allowlist check on the loaded level before making it
visible. That check needs a cooked-content mechanism: confirm in spike 1 that the
class check can run on the loaded package rather than only in the editor.

### 2. Mid-session route switch

`UContentSubsystem::SelectRoute` already gates on `CanSelectRoute`. Add a scene
rebuild step behind it: unload the streamed Han level, or load it; tear down and
rebuild the fallback kit; reposition the boat at the route start. The rebuild runs
only while idle. A failed Han load leaves the fallback kit visible and reports a
user-safe reason on the tile. Tests use the existing `...ForTesting` seams and
`CoursePresentationSpec`.

### 3. Full-route production quality

Extend the six beats and the transitions between them:

| Segment | Work |
|---|---|
| 0 m Banpo | Production bridge, park bank, distant housing lights |
| 0–650 m | Bank continuity, path lights, reeds, skyline layers |
| 650 m Some Sevit | Detailed original island volumes, warm reflections |
| 650–1,450 m | Transition to Dongjak, real parallax on the near bank |
| 1,450 m Dongjak | Paired bridge kit with practical lights |
| 1,450–3,000 m | Long bank stretch, riverside paths, mid-distance skyline |
| 3,000 m Nodeulseom | Island canopy, venue glow, layered far-bank lights |
| 3,000–3,650 m | Approach to the Hangang Bridge |
| 3,650 m Hangang Bridge | Long-deck approach as the crescendo |
| 3,650–5,000 m | Wide downstream vista, receding lights, Wonhyo finish |

Technical approach:

- Modular kits (bridge, bank, river, city atmosphere) with LODs and HLOD, plus
  instancing for repeated elements. The current 881-actor scene needs component
  merging or instancing; `Scripts/build_han_editor_assets.py` says merge
  optimization was unavailable in UE 5.8 for the transient assemblies, so spike 2
  covers this.
- Water: a single non-interactive surface with subtle normal variation and
  restrained reflections. No current, wake, or challenge treatment. The water
  surface and its animation are refined in
  [Milestone 3](07-milestone-3-enhanced-water.md).
- Lighting: one calibrated blue-hour preset, constrained exposure, warm
  practicals. Verified with the HUD at normal and large text and high contrast.
- Third-party art is admitted per asset. CC0 and original assets are cooked-eligible.
  CC BY-SA requires the exact license/version, creator, canonical URL, source SHA-256,
  modification description, and a `licenses/NOTICE.txt` entry, as in the content plan.
  Anything else needs the existing legal/provenance review first.
- Audio: a low-fatigue bed (light water, distant traffic, subtle city ambience),
  ducking under workout cues, honoring global audio controls, with no rhythmic or
  competitive cue. Recordings need provenance approval before import.
- Unreal MCP handles editor authoring and inspection. Confirm liveness first
  per `AGENTS.md`. If the server is down, editor work is reported blocked, not
  replaced by direct `.uasset` edits. Repository pipeline keeps provenance,
  validator rules, and release artifacts.

### 4. Measure, then cap

Record on the reference Mac: download time, mount time, scene load time,
GPU/CPU frame time at start, each beat, and the endpoint, plus memory. Package
and asset size are deliberately not recorded (owner decision: the cap is fixed at
100 GiB and size is ignored for now). Propose a frame budget at milestone close
for the owner to accept. QA-001 certification stays deferred under ADR-0010; regressions are
reported, not hidden.

## Spikes before build (each bounded and reversible)

1. **Streamed-level load from an external IoStore mount** on a Shipping build:
   does `ULevelStreamingDynamic` resolve a package from the runtime-mounted trio,
   and can the class allowlist run on the loaded package?
   **Result (2026-09-21, owner-run, Shipping): passed.** The level streamed from
   the runtime-mounted trio and the allowlist accepted the real cooked level.
   Owner-observed only; no capture or log is attached to this record, so the
   result is not independently reproducible from the repository.
2. **Component budget:** convert the 881-actor scene to instanced/merged form and
   measure cost on the reference Mac.
   **Partial result (2026-09-21, editor inventory via Unreal MCP, read-only):**
   `L_HanRiver_BlueHour` has 888 actors: 877 `StaticMeshActor`, plus the four
   lighting actors and seven system/editor actors. The 877 use only three
   meshes, all engine primitives (`/Engine/BasicShapes` Cube 577, Sphere 162,
   Cylinder 138), each with a per-actor override material, so grouping by
   (mesh, material) collapses the scene to a handful of instanced components.
   `PerInstanceSMData` on a `HierarchicalInstancedStaticMeshComponent` is
   settable through Unreal MCP, so the conversion is feasible in-editor.
   **Open design consequence:** an instanced-mesh actor is a plain
   `/Script/Engine.Actor`, which the client allowlist (`CourseLevel.cpp`) rejects
   because it checks actor classes only. The conversion needs a component-level
   check (allow `Actor` only when every component is on a component allowlist)
   before it can ship. That check is now implemented in the client
   (`IsCourseLevelComponentClassAllowed`; native tests pass, Unreal-side compile
   and `CoursePresentationSpec` coverage not yet run).
   **Conversion trial (2026-09-21, live editor, unsaved):** grouping by
   (mesh, material) collapsed the 877 `StaticMeshActor`s into 13 HISM actors
   (instance counts 1, 2, 19, 115, 11, 77, 74, 3, 3, 44, 88, 60, 380; sum 877).
   Union bounds of the originals and the instanced set match on X/Y and max Z;
   min Z differs (-50 vs -128), attributed to padded instanced-component bounds
   and not yet confirmed. The 877 originals were **not** removed, the level was
   not saved, and no frame-time or draw-call measurement was taken. The
   reference-Mac measurement is still owed.
   **Conversion applied (2026-09-21, live editor via Unreal MCP, saved):** the
   877 `StaticMeshActor`s in `L_HanRiver_BlueHour` were removed and replaced by
   13 `Actor`s (folder `Han/Instanced`), each a `DefaultSceneRoot` plus one
   `HierarchicalInstancedStaticMeshComponent` (Static mobility, no collision,
   per-(mesh, material) instances; sum 877). Union bounds match on X/Y and max Z;
   min Z is -128 vs -50 (padded instanced bounds, unconfirmed). Every component
   class is on the client component allowlist. Not yet done: a re-cook of the
   converted level, a `CoursePresentationSpec` run, and the reference-Mac
   frame-time measurement. **Recook (2026-09-21, owner-run, editor closed):**
   `make han-external-cook` and `make unreal-shipping` succeeded and
   `make unreal-package-verify` passed (app_sha256 90207902...903d). The new
   `HanRiver.ucas` is 931,120 bytes vs 996,656 for the pre-conversion cook
   (stale copy in `Build/han-cook/HanRiver`, 11:24; the new output went to
   `VIR_HAN_IOSTORE_DIR`). `make unreal-smoke` (editor compile only) passes. Not yet observed: the
   level loading in the packaged app, and frame time. `CoursePresentationSpec` ran in the editor (2026-09-21, via
   Unreal MCP): 11/11 passed, 0 errors. The spec's `BeforeEach` had called
   `InitializeNewWorld` on a world `CreateWorld` already initialized (fatal
   duplicate `WorldSettings`); fixed by passing the init values to
   `CreateWorld`. The run logs `AttachTo ... is not static` warnings per test
   (`GrayBoxCourseActor` root), not investigated. Build gotcha: building while an
   editor is running emits a numbered `-000N.dylib` the editor may not load;
   close the editor and rebuild.
   `Scripts/build_han_editor_assets.py` now emits the same 13 instanced actors
   (one HISM per mesh/material group) but that path has not been run: the
   script only runs into an empty destination and no editor Python entry point
   was available in-session, so it is syntax-checked only.
3. **Fallback swap:** prove the kit-to-streamed-level handover and the reverse
   without a boat or camera hitch.
   **Partial result (2026-09-21, code and compile only):** the handover only
   toggles kit visibility, light, and water height (`SetAuthoredLevelActive`);
   it never writes the boat or camera transform. `RejectAuthoredLevel` now also
   restores the kit, and a level that is lost after it was shown is rejected
   (`course.level_lost`) so the kit returns. A new `CoursePresentationSpec` case
   asserts the boat and camera transforms are identical across both handovers.
   `make unreal-smoke` compiles; the spec has **not been run** (Unreal MCP
   automation tools were not reachable) and no frame-time hitch measurement was
   taken. The hitch check on the reference Mac is still owed.

If spike 1 fails, stop and bring back the alternative (spawning mounted meshes
from `GrayBoxCourseActor` by fixed asset table). Do not proceed on the plan above.

## Work order

| Order | Deliverable | Gate |
|---:|---|---|
| 1 | Spikes 1-3 | Written result; owner picks approach if spike 1 fails |
| 2 | Level allowlist validator (editor and client) with negative tests | `make test`, `make content-canary` |
| 3 | Route-ID level table, streaming load, fallback handover | `make unreal-smoke`; new automation in `CoursePresentationSpec` |
| 4 | Mid-session switch rebuild | Automation for idle-only switching and failed-load fallback |
| 5 | Kits, then per-segment dressing, with provenance for each import | Editor validator plus provenance inventory pass before cook |
| 6 | Audio bed | Provenance approval; ducking and global-control tests |
| 7 | `make han-external-cook`, `make content-release-package` (release owner, `content-current`), publish revision | Signed catalog accepted by the app |
| 8 | Frame-time, load, and memory measurements and proposed frame budget (no size measurement) | Recorded in the evidence record |
| 9 | Owner full-route review, packaged Shipping, reference Mac | Owner sign-off recorded |

Step 7 signing and step 9 packaged runs are owner-run. Per the memory note,
`make unreal-shipping` and launching the packaged app are not run in-session
unless the owner overrides for that instance.

## Acceptance evidence

- Automation: class-allowlist rejection, route-ID-only level selection, idle-only
  switch, failed-load fallback to the kit, and no write path to official facts.
- `make build && make test`, `make format-check`, `make unreal-native-app`,
  `make unreal-smoke`, `make han-external-cook`, `make unreal-package-verify`.
- Provenance inventory and `NOTICE.txt` cover every third-party asset and audio file.
- A measurement table (section 4) from the reference Mac.
- Owner route review of the whole 5 km in the packaged app, with captures at each
  beat, each transition midpoint, and the endpoint hold, at normal and large text
  and high contrast.

## Open items and risks

- Class allowlist enforcement on a cooked level is unproven (spike 1).
- Scope is large: ten segments of production art. If the schedule slips, cut
  transition polish before cutting beats, and record what was cut.
- Third-party pack licenses may fall outside "CC0 / CC BY-SA"; those assets are
  blocked until legal review.
- The packaged failure cases from Milestone 1 stay unverified; content added here
  ships through an unproven fallback path.
- Package size is unmeasured and only bounded by the 100 GiB cap. The 100 GiB
  free-storage headroom was sized for a 32 GiB package and now sits at the cap;
  revisit it when size is next considered.

## Out of scope

Additional routes, ranked racing, user steering, currents or waves that affect
workout facts, real-world navigation claims, executable content, automatic
downloads, signing/notarization, and Phase 2 exit-gate claims.
