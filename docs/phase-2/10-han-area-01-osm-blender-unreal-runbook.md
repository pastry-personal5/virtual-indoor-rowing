# Han Area 01 — OSM to Blender to Unreal runbook

Status: Planned — not started
Owner: Client/content, technical art
Last reviewed: 2026-09-22

## Purpose

This is the Blender-authored alternative for Area 01:

```text
reviewed OSM source -> Blender OSM scene -> approved authored mesh selection
  -> deterministic FBX export + manifest -> Unreal review-map staging
  -> human/editor review -> explicit save -> provenance/cook
```

Use this path when the desired result depends on Blender-authored modeling,
material separation, rooftop treatment, vegetation, bank dressing, or manual
composition that the narrow direct OSM-to-Unreal generator does not represent.
It supplements [Milestone 4](08-milestone-4-blender-building-import.md) and is
separate from the direct [`OSM to Unreal`](09-han-area-01-osm-unreal-runbook.md)
path.

It is presentation-only: no boat collision, route-distance derivation, PM5
fact mutation, journal mutation, rank mutation, runtime geospatial lookup, or
navigational/safety claim.

## Current Area 01 source findings

- The inspected source is `coord2.blend`, containing `map_5.osm` and
  `map_6.osm` collections. The two main OSM batches, and likewise their
  `*_extra` batches, have equal semantic geometry/tags; select one source batch
  rather than stacking both.
- The building meshes use `wall` and `roof` material slots and are approximately
  `2987.25 m × 1949.10 m × 196 m` in the inspected Blender scene.
- The source has a missing linked `way_profiles.blend` library. Resolve that
  before any export. A Blender source with missing libraries is not exportable.
- The current runtime Han spline is authored rather than WGS84 registered. Fit
  source geometry to the Area 01 review-level-local frame with reviewed
  controls; do not use a guessed north yaw.

## Coordinate and source contract

- OSM/GeoJSON use CRS84 `[longitude, latitude]` coordinate order.
- The local source projection is `X=east`, `Y=north`, `Z=up`, meters.
- With no `bpyproj` override and `heading == 0`, Blender `+Y` is true north.
- The deterministic FBX interchange uses `-Y` forward, `Z` up, meters; Unreal
  imports and converts to centimeters.
- The target is the Area 01 review-level local frame, never the procedural
  runtime world directly.
- At least three well-spaced, non-collinear named controls fit a similarity
  transform. Scale must remain `[0.999, 1.001]`; maximum residual is `200 cm`.

Use the shared local registration helper:

```sh
HAN_AREA_REVIEW=/owner-controlled/area01-registration.json \
make han-area-register
cat Saved/HanArea/banpo-sevit-01-register.json
```

The registration JSON uses the same `origin_lon_lat` and reviewed controls as
shown in the direct OSM runbook. Its documented controls are a scale-preserving
bootstrap for a review level using the projection's local east/south-centimeter
frame, not coordinates for an arbitrary hand-authored map. A failed fit reports
the fitted scale or maximum/RMS residual; it is a source/control/review-level
problem, not permission to scale or hand-rotate the asset.

## Step 1 — inspect and approve Blender source

First use Blender MCP read-only inspection. In a Blender Python execution:

```python
import blender_han_area
report = blender_han_area.inspect_scene()
print(report)
```

Record the source SHA-256, Blender version, origin, scene heading, projection
override state, missing library list, source object geometry hashes, bounds,
UVs, modifiers, and material slots. Verify all of the following before export:

1. The blend is saved and unmodified after its reviewed SHA-256 is recorded.
2. `way_profiles.blend` and every other linked library resolves.
3. Units are metric with one Blender unit equal to one meter.
4. `bpyproj` is disabled and heading is zero, or an explicitly reviewed
   equivalent projection/basis is documented before changing this workflow.
5. The chosen source is exactly one collection: `map_5.osm` or `map_6.osm`.
6. Meshes are an explicit artist-approved allowlist; do not export cameras,
   lights, curves, hidden helpers, OSM water, roads, or full collections by
   default.

## Step 2 — create and run the reviewed FBX export

Create the owner-local export configuration. Pivots are source-local meters
chosen by the content owner and must be recorded per asset:

```python
reviewed_config = {
    "source_sha256": "<coord2.blend sha256>",
    "origin_lon_lat": [126.993885, 37.51381],
    "collection": "map_5.osm",
    "source_review": "AREA01-BLEND-SRC-001",
    "license_review": "AREA01-LIC-001",
    "representative_export_review": "AREA01-BLEND-REP-001",
    "objects": [
        {"source": "map_5.osm_buildings", "asset": "SM_Han_A01_BanpoBuildings", "pivot_m": [0.0, 0.0, 0.0]}
    ]
}
```

In Blender, export to a fresh owner-controlled directory:

```python
import blender_han_area
manifest = blender_han_area.export_reviewed(
    reviewed_config,
    "/owner-controlled/Area01-FBX"
)
print(manifest)
```

The exporter selects only the approved meshes, evaluates modifiers, preserves
stable names/UVs/normals/material slots, emits FBX with `-Y` forward and `Z`
up, excludes textures/animations, and writes hashes/settings to `manifest.json`.
It refuses dirty source, unresolved libraries, unit/projection mismatch,
unreviewed source hash, duplicate assets, or source objects outside the selected
collection.

Export the unchanged representative input twice and compare manifest/FBX hashes
before import. Any differing hash must be reviewed before continuing.

## Step 3 — calibrate FBX basis and stage in Unreal

Before importing actual art, export/import a reviewed one-meter ruler with the
same settings. Measure the resulting Unreal vectors in centimeters:

- `east_cm`: the source `+X` ruler endpoint (expected `[100, 0, 0]` after yaw);
- `north_cm`: the source `+Y` endpoint (expected `[0, -100, 0]` after yaw);
- `up_cm`: the source `+Z` endpoint (expected `[0, 0, 100]`).

Open the approved Area 01 review map through Unreal MCP. Create
`/owner-controlled/area01-fbx-unreal-review.json`:

```json
{
  "manifest_sha256": "<sha256 of Area01-FBX/manifest.json>",
  "origin_lon_lat": [126.993885, 37.51381],
  "controls": [
    {"id": "banpo-bridge-reference", "lon_lat": [126.996000, 37.511500], "level_xy_cm": [18675.891, 25714.592]},
    {"id": "sebit-reference", "lon_lat": [126.990000, 37.510500], "level_xy_cm": [-34305.820, 36846.043]},
    {"id": "east-bank-reference", "lon_lat": [126.982200, 37.511200], "level_xy_cm": [-103181.403, 29047.980]}
  ],
  "vertical_offset_cm": 0,
  "representative_scene_review": "AREA01-BLEND-REP-002",
  "license_review": "AREA01-LIC-001",
  "source_georeference_review": "AREA01-GEO-001",
  "vertical_datum_review": "AREA01-VERT-001",
  "budget_review": "AREA01-BUDGET-001",
  "fbx_calibration_review": "AREA01-FBX-CAL-001",
  "materials": {
    "wall": "/Game/Phase2/HanRiver/Materials/M_Han_Concrete",
    "roof": "/Game/Phase2/HanRiver/Materials/M_Han_Steel"
  },
  "calibration": {
    "import_settings": {
      "convert_scene": true,
      "convert_scene_unit": true,
      "force_front_x_axis": false,
      "import_uniform_scale": 1.0,
      "combine_meshes": true,
      "auto_generate_collision": false,
      "transform_vertex_to_absolute": true,
      "bake_pivot_in_vertex": false
    },
    "east_cm": [100, 0, 0],
    "north_cm": [0, -100, 0],
    "up_cm": [0, 0, 100]
  }
}
```

Use the same accepted registration-control values here. If the review map uses
a different local frame, replace every `level_xy_cm` value in this JSON and the
registration JSON with corresponding measured landmark coordinates.

Dry-run, then stage only after it is accepted:

```python
import sys

repo_root = "/Volumes/Unreal_Engine_Volume/work/virtual-indoor-rowing"
scripts_path = repo_root + "/Scripts"
if scripts_path not in sys.path:
    sys.path.insert(0, scripts_path)

import unreal_han_area

manifest = "/owner-controlled/Area01-FBX/manifest.json"
review = "/owner-controlled/area01-fbx-unreal-review.json"
plan = unreal_han_area.run(manifest, review)
print(plan)
result = unreal_han_area.run(manifest, review, apply=True)
print(result)
```

The script refuses the wrong review map or overwrite, imports only below
`/Game/Phase2/HanRiver/Meshes/Area01`, maps the declared Blender slots to
Han-owned materials, removes collision, and leaves all changes unsaved for
human inspection.

For the Blender path, use the same Area 01 review-level discipline: duplicate
the approved source map to
`/Game/Phase2/HanRiver/Maps/L_HanRiver_Area01_Review`, load that map, and verify
the current level before staging. The current FBX helper uses that explicit
Area 01 review-map contract; do not stage into `/Engine/Maps/Entry`, the runtime
Han map, or a saved production map.

## Step 4 — review and promote

Before saving, review source-aligned placement, control residuals, pivot/bounds,
scale, normals, UVs, material slots, lightmap/LOD plan, expected dependencies,
no collision, central-channel clearance, HUD readability, and blue-hour
composition. Do not retain duplicate full blockouts once imported art is
approved.

After explicit save through Unreal MCP, add complete asset/source/export hashes,
creator/owner/license/reviewer/modification records and required attribution to
`provenance.json` and `licenses/NOTICE.txt`. Then run the project pipeline:

```sh
make build && make test
make format-check
make unreal-native-app
make unreal-smoke
make han-external-cook
make unreal-shipping
make unreal-package-verify
```

These commands and an editor review do not substitute for the required packaged
canary or reference-Mac evidence.
