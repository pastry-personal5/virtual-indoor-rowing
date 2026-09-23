# Phase 2 Milestone 4: import Blender buildings into the Han River course

Status: Complete — owner decision
Owner: Client/content, technical art
Last reviewed: 2026-09-23

## Purpose

Import the existing Blender-authored environment and building meshes into the
Han River course and replace the corresponding Unreal blockout geometry. This
milestone establishes a repeatable Blender-to-Unreal static-mesh handoff,
integrates the approved meshes into the six-beat route, and carries them through
the existing data-only Han River content pipeline.

This is presentation-only content work. It does not change
`route.han-river.5k`, workout facts, boat movement, course distance, journal
behavior, or the Phase 2 exit gate. The cooked-in Standard route remains the
safe fallback whenever Han content is unavailable or rejected.

## Completion disposition

The owner marked Milestone 4 complete on 2026-09-23. Importing several
additional OSM maps remains follow-up Han content work, to be independently
scoped and reviewed under the existing OSM acquisition, provenance, staging,
and import controls. That follow-up neither reopens this milestone nor passes
the Phase 2 exit gate.

## Bounded outcome

Milestone 4 is complete when:

- the existing Blender scene has an inventoried and reproducible static-mesh
  export;
- approved building and environment meshes are imported beneath
  `Content/Phase2/HanRiver/` with stable names and complete provenance;
- the Han level uses those meshes in place of the corresponding skyline,
  building, bank, or environment blockouts at the six existing route beats;
- imported geometry remains presentation-only and does not obstruct the rowing
  channel or HUD;
- content validation, external Han cooking, and reference-Mac visual review
  pass for the imported assets; and
- the imported content can be included in a signed Han package without code,
  scripts, plug-ins, or Blueprint bytecode.

Completing this milestone records incremental Phase 2 progress only. It does
not complete Milestone 2, pass the Phase 2 exit gate, or satisfy the deferred
Phase 4 visual-performance certification.

## Fixed constraints

- **Existing art is authoritative.** This milestone imports the environment and
  building meshes already authored in Blender. It does not commission a new
  skyline kit or substitute marketplace assets.
- **Static presentation only.** Imported buildings have no gameplay behavior,
  physics response, workout dependency, PM5 dependency, or official-fact write
  path.
- **Stable route contract.** Route selection continues through
  `route.han-river.5k`; no runtime code selects content by raw package path.
- **Data-only Han package.** Blender source files, exporter scripts, editor
  automation, and executable content are not shipped. Only validated Unreal
  data assets and approved license/provenance records enter the external
  IoStore package.
- **Self-contained external content.** Imported Han assets may not depend on
  uncooked editor-only assets or unrelated project content that is absent from
  the external Han chunk.
- **Open rowing line.** Buildings, banks, bridges, props, and their bounds stay
  outside the virtual channel and do not create a navigational or physical-Han
  safety claim.
- **Restrained presentation.** Imported content must preserve the blue-hour,
  after-rain direction, controlled highlights, quiet skyline rhythm, and HUD
  legibility defined by the Han course-content plan.
- **No automatic collision.** Building and environment meshes are non-collision
  by default. Any exceptional collision must be justified, reviewed, and shown
  not to affect the presentation boat or workout runtime.

## Source and export handoff

Before export, record the Blender source location, source owner, Blender
version, source file SHA-256, license, and reviewer. The source `.blend` file
remains in owner-controlled art storage unless the content owner separately
approves it for source control. Repository provenance must still identify the
exact source revision used for every import.

Use FBX as the canonical static-mesh interchange for this milestone. The export
procedure must be repeatable and record its settings. It shall:

- export only approved mesh objects, excluding cameras, lights, helpers, hidden
  work objects, and Blender-only scene controls;
- use metric units with scale and rotation applied so Unreal imports at the
  intended centimeter size and orientation;
- preserve stable object names, pivots, UV sets, vertex normals, material-slot
  names, and authored LOD naming where present;
- separate reusable buildings and environment modules from intentionally
  grouped compositions;
- use deterministic file names and export grouping so a re-export updates the
  intended Unreal assets rather than creating duplicates; and
- produce a SHA-256 inventory for the exported FBX files and any approved
  texture inputs.

Blender shader graphs are not runtime dependencies. Material slots map to
Han-owned Unreal materials. Texture files may be imported only when they are
required by the approved art, have complete provenance, fit the content budget,
and can be redistributed in the signed Han package.

If the Blender source does not contain usable LODs, the technical artist must
create simplified near, mid, and distant variants before full-route placement.
Automatic Unreal reduction may be used only after a visual review confirms that
it preserves silhouettes and does not introduce unstable shimmer.

## Unreal import and placement

Use Unreal MCP for live editor inspection, import, asset configuration, level
placement, save, and viewport evidence. Inspect the existing Han assets and
level before mutation; do not edit `.uasset` or `.umap` files directly.

The import flow is:

1. Inventory the Blender source objects and map each approved object or group
   to a stable VIR asset ID and intended Han beat or transition use.
2. Export and hash the representative Banpo-to-Sebit building/environment set.
3. Import the representative set beneath the existing Han content root, assign
   Han-owned materials, remove default collision, configure LODs, and verify
   scale, orientation, pivots, bounds, normals, UVs, and material slots.
4. Replace only the matching blockout actors in the representative 500 m scene.
   Review chase-camera composition, channel clearance, lighting, HUD
   readability, silhouette quality, and cost before full-route import.
5. Export and import the remaining approved meshes with the same settings and
   replace the corresponding blockout actors at Banpo, Some Sevit, Dongjak,
   Nodeulseom, Hangang Bridge, Wonhyo, and economical transition areas.
6. Preserve the existing water, route centerline, checkpoint metadata, chase
   camera, endpoint hold, and course-runtime wiring unless a placement defect
   requires a separately reviewed correction.
7. Save the level and imported assets, update provenance and package inventory,
   validate the content, and produce a fresh external Han cook.

Do not retain both imported buildings and their full blockout equivalents in
the shipped level. Small blockout pieces may remain where they provide banks,
occlusion, or distant fill not supplied by the Blender source, but they must be
intentional and included in the visual and cost review.

## Naming, materials, and provenance

Imported static meshes use Unreal names beginning with `SM_Han_`; material and
material-instance assets use `M_Han_` and `MI_Han_`. Names must describe the
asset's purpose rather than its temporary Blender collection or export batch.
Reimports must target the same Unreal asset paths.

For each imported asset or approved group, `provenance.json` must record:

- stable asset ID and final Unreal package path;
- creator, owner, source identifier, license, and reviewer;
- Blender source SHA-256 and exported-file SHA-256;
- Blender and exporter versions plus the export-settings revision;
- modification and optimization description;
- material and texture dependencies; and
- required attribution or share-alike designation, when applicable.

Unknown ownership, incompatible redistribution terms, missing source hashes, or
unreviewed third-party inputs block import into the cooked Han package. Any
required attribution is added to the existing package `licenses/NOTICE.txt`.

## Content budgets and validation

The representative import establishes per-category budgets before full-route
placement. Record at least triangles per LOD, material-slot count, texture
dimensions and memory, mesh bounds, collision state, placed instance count,
and observed draw-call/frame impact on the reference Mac.

The Han content validator must reject:

- missing or duplicate stable asset IDs;
- assets outside the declared Han package inventory;
- unexpected editor-only, Blueprint, script, plug-in, or executable
  dependencies;
- invalid scale, orientation, pivots, bounds, normals, or UV data;
- missing required LODs or geometry/material/texture cost above the approved
  budget;
- unexpected collision or complex-as-simple collision;
- missing provenance, source hashes, reviewer, or license data;
- broken material or texture references, redirectors, and duplicate reimport
  assets; and
- dependencies outside the self-contained external Han content root unless
  explicitly approved and proven present in the shipped application.

Nanite is not a milestone requirement. It may be enabled only for a reviewed
asset where the reference-Mac comparison demonstrates a clear benefit without
adding a route-wide dependency or weakening conservative fallback behavior.

## Verification and evidence

### Export evidence

- Export the representative source twice with unchanged input and confirm
  stable file names, object membership, settings, and hashes.
- Verify intended dimensions and orientation against known Blender and Unreal
  measurements.
- Record source and export hashes without copying private workstation paths into
  logs or release artifacts.

### Editor evidence

- Verify every imported mesh's scale, pivot, bounds, normals, UVs, material
  slots, LODs, collision state, and dependency set.
- Capture the representative 500 m scene before full-route replacement.
- Capture Banpo, Some Sevit, Dongjak, Nodeulseom, Hangang Bridge, Wonhyo, and at
  least two transition areas after placement.
- Review normal, large-text, high-contrast, and reduced-motion presentations.
- Confirm the central rowing line remains clear and no building silhouette,
  emissive surface, reflection, or window pattern obscures the HUD.

### Runtime and package evidence

- Unreal automation confirms route-ID selection and that imported presentation
  assets have no write path to official distance, pace, power, workout state,
  journal data, or ranking state.
- `make build && make test`, `make format-check`, `make unreal-native-app`, and
  `make unreal-smoke` pass for the candidate.
- `make han-external-cook` produces a fresh non-empty
  `HanRiver.{pak,utoc,ucas}` trio containing the declared imported assets.
- The release owner runs `make content-release-package` and verifies inventory,
  provenance, license notice, hashes, and the next immutable catalog revision.
- `make unreal-shipping` and `make unreal-package-verify` pass, followed by a
  packaged canary that downloads, activates, mounts, selects, and rows the Han
  course while Standard remains usable offline.
- The content canary continues to pass corruption, incompatibility, expiry,
  withdrawal, rollback, storage refusal, mount failure, and active-workout
  blocking cases.
- Reference-Mac observations record frame time and memory before and after the
  import. Regressions outside the approved content budget block completion even
  though QA-001 certification remains deferred to Phase 4.

## Completion gate

The milestone owner may mark Milestone 4 complete only when:

1. the representative import review passes before full-route placement;
2. all approved Blender buildings and environment meshes intended for this
   milestone are imported, validated, and used at their mapped route locations;
3. source ownership, hashes, export settings, licenses, and Unreal dependencies
   are complete and reviewable;
4. no imported asset affects PM authority, workout behavior, local durability,
   route identity, or Standard fallback;
5. the full route passes editor visual review and the documented content
   budgets on the reference Mac; and
6. a fresh external Han cook and packaged mount demonstrate that the imported
   assets are present in the signed, data-only content path.

Any mesh that fails ownership, import, visual, cost, or packaging review remains
excluded and is replaced by the existing safe blockout rather than weakening
the gate.

## Explicit exclusions

This milestone does not create new Blender models, recreate exact real-world
landmarks, add animated buildings, traffic, crowds, interiors, destructible or
interactive geometry, runtime mesh import, user-generated content, steering,
physical collision gameplay, new route mechanics, navigation claims, or a new
content-delivery system.
