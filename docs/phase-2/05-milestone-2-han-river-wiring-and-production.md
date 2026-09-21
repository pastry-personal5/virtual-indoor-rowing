# Phase 2 Milestone 2: wire and enhance the Han River course assets

Status: In progress — wiring compiled; runtime load unverified
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
| Package size / frame-time cap | **Measure first; set the cap at milestone close.** Not a gate until then. |
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
compile; spike 1 (does the streamed level resolve from the runtime mount on a
Shipping build, and does the allowlist accept the real cooked level) is still
open and owner-run. The allowlist may need one more class once a real cooked
level is inspected; a rejection falls back safely.

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
  restrained reflections. No current, wake, or challenge treatment.
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

Record on the reference Mac: compressed package size, download time, mount time,
scene load time, GPU/CPU frame time at start, each beat, and the endpoint, plus
memory. Propose a size cap and frame budget at milestone close for the owner to
accept. QA-001 certification stays deferred under ADR-0010; regressions are
reported, not hidden.

## Spikes before build (each bounded and reversible)

1. **Streamed-level load from an external IoStore mount** on a Shipping build:
   does `ULevelStreamingDynamic` resolve a package from the runtime-mounted trio,
   and can the class allowlist run on the loaded package?
2. **Component budget:** convert the 881-actor scene to instanced/merged form and
   measure cost on the reference Mac.
3. **Fallback swap:** prove the kit-to-streamed-level handover and the reverse
   without a boat or camera hitch.

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
| 8 | Measurements and proposed cap | Recorded in the evidence record |
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
- The download size will grow, and no cap exists until close-out.

## Out of scope

Additional routes, ranked racing, user steering, currents or waves that affect
workout facts, real-world navigation claims, executable content, automatic
downloads, signing/notarization, and Phase 2 exit-gate claims.
