# Phase 2 Milestone 3: enhanced water surface and animation

Status: In progress — kit and Han materials rebuilt in the editor; C++ not compiled, no re-cook, nothing observed in a running game
Owner: Client/content, technical art
Last reviewed: 2026-09-21

## Purpose

Replace the flat, single-colour water plane with a calm, realistic, animated
surface on both the built-in kit (Standard route and the Han whole-route
baseline) and the authored Han level. Presentation only: it changes no workout
fact, requirement, or trust boundary, and passes no Phase 2 exit gate.

This is the only item in Milestone 3 so far. Other items are added individually,
before their work begins. It refines the water bullet in
[Milestone 2](05-milestone-2-han-river-wiring-and-production.md) ("subtle normal
variation and restrained reflections"); it does not change that bullet's limits.

## Fixed constraints

These come from the existing specifications and bound every option below.

- **No current, wake, or challenge treatment** ([content plan](03-han-river-course-content-plan.md)).
  Water must not move with rowing pace or distance. The animation is ambient and
  world-anchored: a static camera would see the same drift regardless of speed,
  and the apparent motion while rowing comes from the boat passing through it.
  It reads no PM5 fact, so it cannot synthesize distance or imply speed.
- **No gameplay decision reads render water** ([client architecture](../architecture/03-macos-unreal-client.md)).
- **Subdued specular, low broken reflections.** No highlight or reflection band may
  obscure the HUD at normal or large text or in high contrast.
- **Data-only content** ([content plan](03-han-river-course-content-plan.md)): the Han
  water must be plain material data. No Custom HLSL node, Blueprint, script, or
  imported third-party bitmap.
- **Reduced motion** is honored (`-ReduceMotion` today; the platform preference is
  still unwired).
- **Metal, no beta dependency.** No Nanite, Lumen, hardware ray tracing, or
  Single Layer Water requirement; conventional deferred shading only.
- Frame budget: GPU 14.0 ms p95 for the whole scene. The water must earn its cost;
  none is measured yet.

## Research summary

What realistic-water practice offers, and what this app takes from it.

| Technique | Finding | Decision |
|---|---|---|
| FFT ocean spectra (Tessendorf) | Physically grounded, produces sharp crests and flat troughs, but needs compute passes and displacement/foam textures. Built for open ocean. | Not used: overkill for a calm 420 m river seen from a chase camera, and a compute dependency on Metal. |
| Gerstner / trochoidal waves | Cheap sum of directional waves; good for large low-frequency swell. Needs a tessellated mesh to displace. | Vertex displacement not used: the water is a flat 0.1-thick cube, and swell would move the horizon against the low camera. Its *slope* math is used per pixel instead. |
| Two panning normal maps | The standard game recipe: sample a normal map twice, offset in opposing directions at different speeds. | The idea is kept (multiple crossing directions and speeds) but the textures are replaced by analytic waves, to avoid tiling and third-party art. |
| Fresnel-weighted reflection | Reflection strength rises at grazing angles; the chase camera looks along the water, so this matters most. | Kept, computed from the animated normal, tinted to the blue-hour sky and capped. |
| Single Layer Water shading | Epic's cost-effective absorption/scattering model; needs SSR and has a reduced path on lower-end platforms. | Not used: an opaque river bed is never visible, and it adds a platform-dependent path. |
| Distance filtering | Small waves alias into shimmer beyond the pixel footprint; the energy should become roughness. | Kept: each wave fades with pixel depth; lost detail raises roughness. |

## Design

One math-only material, built from five analytic directional waves. Each wave
contributes a normal-slope term `slope · cos(2π(k·x + f·t))` along its direction:

- **Realistic dispersion.** Wavelengths are 11, 6.3, 3.4, 1.7, and 0.9 m. Each
  frequency is `sqrt(g / 2πλ)`, the deep-water dispersion relation, so long waves
  move faster than ripples and the surface does not slide as one sheet. Speed is
  then scaled by a `MotionScale` parameter (0.6 default) because a calm river
  should not move like open water.
- **No visible tiling.** Five incommensurate wavelengths at spread directions
  (−35° to 105° from the river axis) never repeat within the course; there is no
  texture to tile.
- **World-anchored.** Phase comes from absolute world XY, so the pattern stays put
  while the boat and camera move.
- **Distance filtering.** Each wave fades out by pixel depth (about 45 wavelengths),
  and roughness rises from 0.08 to 0.30 by about 120 m.
- **Shading.** Opaque, default lit. Specular is 0.25 (F0 near 0.02), the normal is
  world-space, and a Fresnel term (exponent 5) tinted by `SkyTint` and capped by
  `ReflectionMax` (0.35) drives emissive as a stand-in reflection because the
  kit's map has no sky capture. Peak slopes are 0.018–0.028, subtle by design.
- **Reduced motion.** `MotionScale` is set to 0 for the kit through a dynamic
  material instance, and for any `M_Han_Water` slot in the authored level before it
  is shown, so the wave phase freezes.
- **Parameters** (group `Water`): `DeepColor`, `SkyTint`, `Roughness`,
  `NormalStrength`, `MotionScale`, `ReflectionMax`.

Rejected for now: foam and shoreline interaction, bow ripples and oar-blade
splashes (they would need presentation of rowing state and edge the "no wake"
rule), caustics, and refraction.

## Implementation

| Piece | State |
|---|---|
| `/Game/Water/M_CourseWater` (kit material, 127 expressions, recompiled and saved) | Done through Unreal MCP, 2026-09-21 |
| `AGrayBoxCourseActor`: kit water uses a dynamic instance of it, `DeepColor` per route, reduced-motion override | Written; `make unreal-smoke` not run |
| `UCourseSubsystem`: reduced motion applied to the level's `M_Han_Water` slots before the level is shown | Written; not compiled |
| `CoursePresentationSpec`: kit water animates by default and freezes for reduced motion | Written; not run |
| `Scripts/build_water_material.py`: the recipe as editor Python | Syntax-checked only; not run, so the MCP-built graph is the verified one |
| `M_Han_Water` (Han package) rebuilt in place with the same graph (127 expressions, default `DeepColor` kept Han-dark, saved; the level still references it) | Done through Unreal MCP, 2026-09-21, after the owner authorized free editing of Han assets. Not yet re-cooked |

Han's package must stay self-contained, so it carries its own copy of the graph
rather than referencing `/Game/Water`. A re-cook and re-release
(`make han-external-cook`, `make content-release-package`) are owner-run steps
and are needed before the packaged Han level shows the new water. The material
has no imported bitmap, so the existing `han-river-unreal-editor-review-kit`
provenance record still describes it; `provenance.json` is unchanged.

## Gate

- `make build && make test`, `make format-check`, and `make unreal-smoke` pass.
- `CoursePresentationSpec` passes, including the reduced-motion case.
- Han `M_Han_Water` carries the same graph and `make content-canary` passes.
- Owner-run, reference Mac: water reads as calm and realistic in the chase view at
  normal and large text and high contrast; no reflection or highlight obscures the
  HUD; no shimmer or visible tiling toward the horizon; `-ReduceMotion` freezes it.
- Water GPU cost recorded against the 14.0 ms budget. No target is asserted before
  it is measured.
- No PM5 fact, distance, or rank path reads or is read by the water.

## Open decisions for the owner

1. Whether the kit should also get a real sky capture so reflections are physical
   instead of a tinted Fresnel term (needs lighting work in the kit map).
2. Whether `MotionScale` 0.6 and the slope amplitudes suit the intended mood; they
   are tuned by judgement, not measurement.

## Sources

- [Tessendorf, Simulating Ocean Water / Normal Maps for Vast Ocean Scenes](https://jtessen.people.clemson.edu/reports/papers_files/normalmap_eg.pdf)
- [Trochoidal wave](https://en.wikipedia.org/wiki/Trochoidal_wave)
- [Gerstner-wave ocean shader overview](https://gameidea.org/2023/12/01/3d-ocean-shader-using-gerstner-waves/)
- [Single Layer Water Shading Model, Unreal Engine docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine)
- [Using Fresnel in Your Unreal Engine Materials](https://dev.epicgames.com/documentation/unreal-engine/using-fresnel-in-your-unreal-engine-materials)
- [The formula for a good water shader for games in Unreal 4](https://medium.com/@m.marier/the-formula-for-a-good-water-shader-for-games-in-unreal-4-ac72e5e86d49) (found in search; page not retrievable, so only its summary line was used)
