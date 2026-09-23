# Phase 2 Milestone 5: photorealistic water refinement

Status: In progress — boat and rower meshes applied; water interaction and acceptance evidence remain open  
Owner: Client/content and technical art  
Last reviewed: 2026-09-24

## Purpose and decision

Phase 2 Milestone 5 refines the calm blue-hour water delivered in
[Milestone 3](07-milestone-3-enhanced-water.md), which remains owner-complete.
The existing five-wave material works in the editor, but its Shipping
appearance, distant shimmer, reduced-motion
behavior on mounted Han content, and GPU cost are not yet established by the
required evidence.

The target is a photorealistic, calm river seen from the rowing chase camera:
fine nearby ripples, low broken bridge and sky highlights, a stable distant
surface, and restrained hull and oar interaction near the boat. Preserve the
blue-hour, after-rain art direction. Investigate actual scene reflections
first, including a bounded planar-reflection prototype,
before choosing any new water shading model. Approved bitmap detail textures
are now a candidate for the near water surface. The current opaque, Default
Lit, texture-free material remains the comparison baseline on the built-in
course and authored Han level. This milestone revisits Milestone 3's
texture-free choice under the owner's new direction; imported assets still
follow the Phase 2 provenance and content-validation rules.

No water state may affect PM5 distance, pace, power, course progress, workout
completion, or the local journal. Keep ambient water motion independent of
rowing speed. The material remains data-only in the signed Han package:
reviewed `Texture2D` assets may be included, but no Custom HLSL, Blueprint,
script, fluid simulation, or runtime package code. River current, broad foam
fields, challenge treatment, and underwater views are outside this milestone.
A small hull wake and oar-contact ripples or splashes are local cosmetic effects
owned by cooked-in RowingWorld presentation. This supersedes Milestone 3's
no-wake presentation choice for Milestone 5 only. Han water animates by default
at the material's
`MotionScale = 0.6`; reduced motion is disabled by default. Preserve the
existing explicit `-ReduceMotion` launch-argument behavior for both water
assets. Do not plan an automatic Han reduced-motion mode, a new setting, or a
change to that default without a separate owner decision.

## Research and technical choice

| Finding | Application here |
|---|---|
| [Bruneton, Neyret, and Holzschuch](https://morpho.inrialpes.fr/Publications/2010/BNH10/article.pdf) show that water detail below a pixel should transition into the normal distribution/BRDF rather than alias at the horizon. | Replace the current depth-only wave fade with a projected-footprint fade and transfer unresolved slope energy into roughness. This is an application of the paper's principle, not its ocean renderer. |
| [Epic's reflection environment guidance](https://dev.epicgames.com/documentation/unreal-engine/reflections-environment-in-unreal-engine) describes reflection captures plus screen-space reflections (SSR) as a practical combination. Captures approximate parallax and are static at runtime; SSR cannot reflect off-screen objects. | Compare existing reflections, local captures plus SSR, and planar reflection in the same chase views. Use the actual scene's bridge and sky light before increasing the material's emissive sky tint. |
| [Epic's planar-reflection guidance](https://dev.epicgames.com/documentation/unreal-engine/planar-reflections-in-unreal-engine) says the scene is rendered again, suggests bounded actors and capture-quality controls, and reports scene-dependent costs. [Project settings](https://dev.epicgames.com/documentation/unreal-engine/rendering-settings-in-the-unreal-engine-project-settings) say the global clip plane adds about 12% to Base Pass triangle cost even without an active planar reflection. | Prototype one bounded plane at a representative bridge. Measure clip-plane-only overhead separately from capture cost, then vary screen percentage, captured primitives, and distance. Do not extrapolate Epic's example timings to this Mac. |
| [Epic's Single Layer Water model](https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model-in-unreal-engine) adds a pass for scattering, absorption, and refraction. | Keep it as a later comparison only if scene reflections and normal filtering cannot meet the visual target. The chase view does not currently expose the river bed, so its extra volume features have no established benefit here. |
| [Cox and Munk](https://opg.optica.org/josa/abstract.cfm?uri=josa-44-11-838) relate water glitter to surface-slope statistics. [NVIDIA's water chapter](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) emphasizes that fine normal variation drives perceived water realism. | Judge highlight breakup and slope variation, not just color and a continuous Fresnel band. Do not apply ocean wind coefficients to the sheltered Han River without scene evidence. |
| [3DTextures Water 001](https://3dtextures.me/2017/12/28/water-001/) and [Water 002](https://3dtextures.me/2018/11/29/water-002/) offer seamless 1024×1024 normal and roughness bitmaps. The publisher [licenses its textures CC0](https://3dtextures.me/about/). [Epic's UE 5.8 import reference](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine) includes normal-map detection and green-channel inversion controls. | Trial the normal maps as close-range detail and the roughness maps only if they improve highlight breakup. Verify the downloaded file, its normal convention, mips, memory cost, and license record before cooking. |
| [Grift, Tummers, and Westerweel](https://repository.tudelft.nl/record/uuid:3bd50509-4291-43a0-a84f-eef15dc87f7e) measured blade vortices during on-water rowing. | Use the visible hull/blade motion to time restrained cosmetic disturbance. The PM5 does not measure blade-water forces, so do not portray the effect as a physical simulation or use it to infer workout facts. |
| [Epic's shader-complexity view](https://dev.epicgames.com/documentation/unreal-engine/viewport-modes-in-unreal-engine) is useful for finding expensive pixels but does not itself measure GPU time. [Apple's Metal tools](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app/) provide frame captures and counters. | Use material statistics and complexity for diagnosis, then decide on packaged-build GPU timing and thermal evidence. |

The project targets Unreal Engine 5.8 on the reference Apple-silicon Mac.
Keep conventional deferred shading. [ADR-0013](../adr/0013-increase-reference-gpu-frame-budget.md)
sets the whole-scene GPU budget to 16.5 ms p95 while QA-001's 16.7 ms p95
frame-time and thermal objectives remain unchanged. Prototype costs may exceed
the budget, but acceptance requires both the GPU and QA-001 gates. Lumen,
Nanite, hardware ray tracing, and preview rendering features are not water
dependencies.

## Reflection comparison and selection

1. Establish the current material, lighting, SSR configuration, and actual
   reflection sources in both scenes. Capture the same near-boat, bridge, and
   horizon views with water visible and hidden; inspect off-screen bridge
   reflections and screen-edge fades.
2. Compare local reflection captures with SSR on the existing Default Lit water.
   Build captures from reviewed level lighting; assess incorrect projection,
   doubled bridge shapes, exposure, and stale dynamic objects. Keep the
   `ReflectionMax` emissive sky fill only as a controlled fallback where scene
   reflection is absent, so it does not add a second bright reflection band.
3. Prototype one planar reflector over the visible bridge-water region. Compare
   clip plane off/on without an actor, then measure the actor with bounded
   coverage and controlled screen percentage, visible primitives, and normal
   distortion. Record whether the camera moving across the capture boundary
   causes a visible transition. The clip-plane switch is a project setting
   included in the application build, not a Han package-only change; account
   for its cost on Standard too. Reject a route-wide plane if its capture cost
   or visual transition is unacceptable.
4. If a planar actor is chosen for downloaded Han content, extend the source
   validator and the exact native actor/component allowlist before cooking.
   `ContentRuntime::IsCourseLevelActorClassAllowed` does not currently admit
   planar or reflection-capture actors. Preserve the signed, data-only package
   policy and negative validation cases; never bypass the allowlist to make a
   visually successful editor prototype load at runtime.
5. Select the least costly method that passes the owner photo/reference review
   and packaged Shipping checks. Only then decide whether Single Layer Water
   deserves a separate prototype; do not assume its refraction or scattering
   improves an opaque, dark chase-view river.

## Bitmap detail trial and content provenance

1. Inspect the source previews and downloaded maps for Water 001 and Water 002.
   Use the source normal as a near-field detail candidate; trial roughness only
   if it improves broken highlights. Do not copy color, ambient-occlusion, or
   displacement maps into the river material merely because they are included
   in the source set. Keep the existing blue-hour color and flat waterline.
2. Import the selected bitmaps through Unreal Editor as reviewed `Texture2D`
   assets. Confirm normal-map compression, linear rather than sRGB sampling for
   normal/roughness data, correct green-channel direction, generated mips, and
   streaming on the reference Metal build. Compare one and two world-anchored
   normal samples at different scales and directions against the procedural
   baseline; both pan using ambient `MotionScale`, so `-ReduceMotion` freezes
   them and boat speed never drives the UVs. Fade sampled detail before it
   aliases at the horizon and retain the roughness compensation below.
3. Keep Han's texture dependency inside its own signed package and a separate
   cooked-in copy for Standard. Record the exact creator, canonical asset page,
   CC0 1.0 license URL, downloaded source SHA-256, transformations, and
   reviewer in Han provenance. CC0 allows commercial use and redistribution
   under the publisher's stated terms. If those candidates fail visual review,
   present one stronger candidate with its exact asset license, price,
   attribution, modification, and redistribution terms for owner review and
   the existing legal/provenance review before import or publication.
4. Update the Han source-dependency check and external cook to cover the chosen
   texture packages, including references reached through `M_Han_Water`. The
   current map-reference scan only recognizes `Maps`, `Materials`, and `Meshes`,
   and the explicit cook directories are `Materials` and `Meshes`; they cannot
   by themselves prove a new `Textures` dependency is tracked and shipped.
   `Texture2D` is already an allowed signed-inventory asset class. Verify the
   cooked inventory contains the selected texture and that Standard still
   starts offline without the Han package.

## Hull and oar water interaction

1. Implement a bounded, cooked-in RowingWorld presentation effect shared by
   Standard and mounted Han. It consumes the existing course-presentation
   snapshot and rendered boat/oar transforms on the game thread; it does not
   read raw PM5 packets, write workout or route state, or require executable
   content in the signed Han package. Effects remain non-colliding visuals.
2. Add a narrow, short-lived hull trail that starts at the visible waterline
   and dissipates behind the boat. Its position follows only presented boat
   movement; no trail is backfilled across disconnects, reconnects, route
   switches, or a skipped distance. Prototype a pooled, world-anchored mesh
   ribbon with subtle normal breakup and opacity decay. Compare it with a
   local water-material normal perturbation if the ribbon shows an overlay
   edge or reflection mismatch. Bound lifetime, active instances, and
   translucent overdraw before scene review.
3. Add one small contact ripple/splash per visible oar entry or exit, using
   the rendered blade position and stroke animation. The new oar mesh has a
   distinct blade, but its current animation only sweeps in yaw. Add blade
   pitch/depth and a waterline crossing before emitting a splash. Suppress
   duplicate contacts and new pulses when the
   presentation input is stale; let existing effects expire without implying
   another measured stroke. Trial a pooled, low-opacity expanding ripple mesh
   and a very small splash only where the blade is visibly submerged; reject
   billboard halos or a repeated ring pattern in chase-view footage.
4. On explicit `-ReduceMotion`, freeze ambient surface motion and suppress
   animated hull/oar effects on both routes. Review normal and reduced-motion
   chase views, including idle, steady rowing, rest, disconnect/reconnect,
   and route switching. Measure the interaction GPU delta separately from the
   base water surface and reflection delta.

### Oar geometry applied on 2026-09-24

`Scripts/build_scull_oar_mesh.py` generates an original 314 cm sculling-oar
source mesh with tapered grip, shaft, collar, and cupped blade sections. Unreal
Editor imported `/Game/Boat/Meshes/SM_ScullOar` with four original-color
materials, no Nanite, and no collision. The cooked-in course actor now holds a
constructor reference to that mesh and uses it for port and starboard oars on
both routes, mirrored about the hull while retaining the existing smoothed
stroke sweep. A cube remains the asset-missing fallback. `make unreal-smoke`
compiled the revised actor; the mesh has not been visually checked in a
packaged Shipping app. Blade pitch/depth, waterline contact, ripples, and
splashes remain implementation work.

### Hull geometry applied on 2026-09-24

`Scripts/build_scull_hull_mesh.py` generates an original 840 cm single-scull
hull source mesh with a tapered bow and stern, shaped shell and sheer, recessed
cockpit, deck panels, and port/starboard riggers aligned with the existing
oarlocks. Unreal Editor imported `/Game/Boat/Meshes/SM_ScullHull` as a 434-triangle
static mesh with five original-color material slots, no Nanite, and no collision.
The cooked-in course actor uses it for both routes and keeps the old cube as an
asset-missing fallback. The hull is presentation-only; it does not change boat
position, PM-backed facts, or the water interaction design above. The editor
asset thumbnail was inspected, but the hull has not yet been viewed in a
packaged Shipping chase camera or measured on the reference Mac.

### Rower geometry applied on 2026-09-24

`Scripts/build_rower_character_meshes.py` generates original, multipolygon
torso/head, arm, leg, and shoe source meshes. Unreal Editor imported four static
meshes under `/Game/Boat/Character`, with original-color materials, no Nanite,
and no collision. The cooked-in course actor uses them on both routes. Its
existing seat, torso, and arm pose values position the modular parts; cosmetic
leg segments connect the moving hips and a fixed foot position. If any of the
four assets is unavailable, the actor keeps the earlier cube torso and arms.
This does not change telemetry, PM-backed facts, or course progress. Editor
asset thumbnails and bounds were inspected; an in-scene animation review,
packaged Shipping chase-camera review, and GPU measurement remain open.

## Material and scene design

1. Keep the current five world-space wave wavelengths, directions, dispersion,
   and small slopes as the starting look. In `Scripts/build_water_material.py`,
   calculate each wave's phase footprint from screen-space derivatives of its
   unwrapped world-space phase, before `Frac` or `Cosine` introduces
   discontinuities. Retain the wave below 0.25 cycles/pixel, smoothly fade it
   between 0.25 and 0.5 cycles/pixel, and remove it at or above 0.5. Avoid a
   hard cutoff during camera motion. The same fade must apply to both normal
   components of that wave.
2. Account for removed high-frequency slopes in roughness. If a wave has peak
   slope `s` and retained amplitude fraction `w`, use `s²(1-w²)/2` as its
   unresolved mean-square slope contribution. Combine those contributions in
   the microfacet roughness domain, then convert back to the material's
   perceptual `Roughness` control and clamp to the existing distant ceiling of
   0.30. As an initial isotropic approximation, evaluate
   `min(0.30, (Roughness^4 + sum(unresolved_variance))^0.25)` and verify its
   highlight width in Unreal. This is a tuning hypothesis, not an exact
   implementation of Bruneton's full normal-distribution model.
   Preserve the world-space normal convention and prevent normal inversion or
   sparkles at grazing angles. Do not introduce vertex displacement: the
   existing thin river mesh and low camera are designed for a stable waterline.
3. Keep `DeepColor`, `SkyTint`, `NormalStrength`, `MotionScale`,
   `ReflectionMax`, and `Roughness` as the material controls. Tune their
   defaults against the current concept art and source reference photos, with
   Han slightly darker than Standard. Reduce or disable the Fresnel sky-tint
   fill wherever scene reflections provide the same sky contribution. No
   bright continuous band may sit behind HUD text. If the bitmap trial wins,
   expose only the selected detail-normal strength and world scale needed for
   tuning; keep the procedural waves for broad motion.
4. Rebuild `/Game/Water/M_CourseWater` and
   `/Game/Phase2/HanRiver/Materials/M_Han_Water` from the same editor recipe,
   preserving Han's self-contained package dependency. Inspect the graph,
   material slot, and the tracked runtime map
   `/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour` before and after saving.
   Leave the untracked review and backup maps alone. Do not change route
   geometry or the water tiles' no-collision presentation role.
5. Leave Han `MotionScale` at its animated default when `-ReduceMotion` is
   absent. Preserve the current dynamic `MotionScale = 0` override only when
   that launch argument is explicitly present, on both the built-in kit and
   streamed Han level. A stationary camera must show ambient drift by default
   and a frozen pattern with the argument. Neither mode may depend on PM5
   telemetry or boat speed. Connecting a platform preference, adding an in-app
   toggle, or enabling reduced motion by default requires a future owner
   decision and is outside this plan.

Use the configured Unreal MCP for the initial editor inspection and bounded
material/map changes. If it is unavailable, stop editor-owned work and report
it unverified; do not directly edit `.uasset` or `.umap` bytes. The source
recipe may be reviewed and changed independently, but that alone does not
establish that either saved material matches it.

## Implementation sequence and evidence

1. Capture a baseline on the reference Mac from a packaged Shipping build:
   identical chase-camera views near the boat, under a bridge, and toward the
   horizon on Standard and mounted Han. Record normal and `-ReduceMotion`
   footage, material statistics, GPU p95, and a warm 60-minute thermal run.
   Verify that Han animates on an ordinary launch and freezes only on a
   separate explicit `-ReduceMotion` launch.
   Record the app/content versions and camera locations without private workout
   data. If Han is not mounted, diagnose that separately before claiming a Han
   comparison.
2. Run the reflection comparison and bitmap trial above. Edit the recipe for
   the selected reflection treatment, filtered normals, and approved textures;
   add the shared hull and oar presentation effect, then rebuild both material
   assets through Unreal Editor. Visually compare the same views. Review the
   HUD at normal and large text sizes and in high contrast. Check temporal
   stability during a stationary
   camera and steady rowing motion, especially horizon pixels, screen edges,
   bridge highlights, and reflection transitions.
3. Run `make format-check`, `make build && make test`, and
   `make unreal-smoke` for any changed source or test behavior. Run the Unreal
   course presentation spec for normal/reduced motion, one contact per visible
   oar transition, no new effect on stale input, and no reconnect backfill.
   Add a focused test or editor validation for the revised material parameters
   and Han reference. Visual quality itself requires the reference-Mac review.
4. Run `make han-source-verify`, `make han-external-cook`, and
   `make unreal-package-verify`. Confirm the external IoStore trio contains the
   revised Han material and tracked runtime map. Follow the
   [packaging runbook](11-han-river-packaging-runbook.md) for any signed test
   release and packaged-app canary; a source asset save alone does not update
   installed content. Do not advance `catalog-current.pb` without the normal
   publication checks.
5. Repeat the reference-Mac captures and 60-minute Shipping run. Record
   full-scene p95 frame/GPU time, thermal behavior, and the water-on versus
   water-hidden GPU delta, with the hull/oar effect cost identified. Accept only
   when the full scene stays at or below
   16.5 ms GPU p95 and passes QA-001: sustained 60 fps at 2560×1600 High,
   p95 frame time at most 16.7 ms, and no sustained thermal throttling.
   Require owner review for no visible tiling, horizon shimmer, distracting
   bright band, HUD-obscuring highlight, oversized wake, or mistimed oar
   splash. Record and explain any miss before
   retuning or selecting a cheaper reflection treatment.

Record results, unresolved defects, exact build/content identifiers, and the
owner's visual decision in the Milestone 5 evidence and Phase 2 changelog.
No simulator-only result substitutes for the reference-Mac visual and thermal
checks. This refinement does not by itself pass the Phase 2 exit gate.
