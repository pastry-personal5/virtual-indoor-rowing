# Han Area 01 — OSM to Unreal runbook

Status: Planned — not started
Owner: Client/content, technical art
Last reviewed: 2026-09-22

## Purpose

This is the direct, review-gated route for Area 01:

```text
OSM source (offline snapshot or one-shot Overpass acquisition)
  -> immutable local source.osm + acquisition.json
  -> deterministic OBJ building/water tiles + manifest.json
  -> Unreal review-map staging assets
  -> human/editor review -> explicit save -> provenance/cook
```

It is an editor-content workflow only. It does not make Unreal a live map
client, add a runtime network dependency, replace `route.han-river.5k`, derive
workout distance, affect PM5 facts, add collision, or make a navigation/safety
claim. The route's authored 5,000 m virtual waterline remains authoritative.

The fixed Area 01 bounding box is
`126.97764,37.50627,127.01013,37.52135`; the representative scene is
Banpo-to-Sebit. The present runtime Han spline is authored rather than WGS84
registered, so source geography must be fitted to the **Area 01 review-level
local frame**. A guessed true-north yaw is not acceptable.

This runbook supplements [Milestone 4](08-milestone-4-blender-building-import.md).
It does not complete that milestone or pass a Phase 2 gate.

## Supported geometry and boundaries

The generator accepts closed OSM `way` footprints and `type=multipolygon`
relations made from way members. It stitches split `outer` and `inner` members
into closed rings, assigns each inner ring to exactly one outer, and leaves
those inner areas empty in the OBJ surface. A relation with multiple disjoint
outers produces one manifest feature per outer under the relation ID. Tagged
member ways are not emitted again as standalone tiles. The feature tag may be
on the relation or, for older mapping, on an outer member. Outer members must
agree on whether the feature is a building or water.

| Category | Accepted tags | Unreal result |
|---|---|---|
| Buildings | `building=*` or `building:part=*` on a way or multipolygon relation, except `roof` | Static wall + flat-roof OBJ tile; inner rings make open courtyards |
| Water | `natural=water`, `water=*`, `waterway=riverbank`, `landuse=basin`, `landuse=reservoir` on a way or multipolygon relation | Separate, flat, visual water OBJ tile; inner rings remain dry |

Building vertical extent uses `min_height` or `building:min_level`, then
explicit `height`, then `building:levels × reviewed default level height`, then
the recorded fallback. Roof-only ways are rejected rather than placed at ground
level.

The relation path requires complete way and node members, unambiguous closed
rings, non-overlapping outers and inners, and no more than 5,000 ring vertices
per relation. Invalid relations are counted as `invalid_relation` and their
members are not emitted as standalone geometry, so a broken hole cannot become
a filled water or roof tile. Nested relation members, roads, bridges, terrain,
elevation/DEM, interiors, water flow/current, waves, wake, depth, shoreline
physics, collision, routing, and safety/navigation semantics remain excluded.
Those require separately scoped topology, datum, visual, and product review.
The OSM water tiles use `M_Han_Water` but do not replace the existing
course-water behavior or authority.

## Prerequisites

1. Work from the repository root.
2. Keep source `.osm`, acquisition configuration, controls, review evidence,
   generated OBJ files, and manifests in owner-controlled storage. Do not
   commit them or private workstation paths.
3. Create the Area 01 review map through an approved Unreal-editor task before
   staging. The staging script refuses any other map.
4. Select at least three named, well-spaced, non-collinear controls visible in
   both the OSM source and review map. These are review controls, not survey
   accuracy claims.
5. Run the offline checks:

```sh
make han-area-test
git diff --check
```

## Step 1 — acquire a local OSM snapshot

Choose exactly one source mode. Both modes write a fresh `source.osm` and
`acquisition.json`. Geometry generation only reads the resulting local snapshot.

### Option A: pre-downloaded/offline OSM

Create `/owner-controlled/area01-acquire.json`:

```json
{
  "schema_version": 1,
  "area_id": "banpo-sevit-01",
  "source_mode": "offline",
  "source_review": "AREA01-SRC-001",
  "license_review": "AREA01-LIC-001",
  "offline": {
    "source_identifier": "Area 01 reviewed OSM extract",
    "osm_path": "/owner-controlled/area01.osm",
    "osm_sha256": "<sha256 of area01.osm>"
  }
}
```

Hash the source and acquire a new snapshot directory:

```sh
shasum -a 256 /owner-controlled/area01.osm
HAN_OSM_ACQUIRE_CONFIG=/owner-controlled/area01-acquire.json \
HAN_OSM_ACQUIRE_OUTPUT=/owner-controlled/Area01-OSM-Snapshot \
make han-area-osm-acquire
shasum -a 256 /owner-controlled/Area01-OSM-Snapshot/source.osm
```

### Option B: reviewed live Overpass snapshot

This mode sends one fixed HTTPS POST query for the Area 01 bounding box. The
query requests building and water ways and all `type=multipolygon`
relations, then recursively fetches their member ways and nodes. Relation
members may extend beyond the bounding box; the 50 MiB response cap still
applies. It requires an identifying user agent, a query-review ID, and a
second review against the downloaded snapshot hash. It never runs in Unreal,
during packaging, or during a workout.

Create `/owner-controlled/area01-live-acquire.json`:

```json
{
  "schema_version": 1,
  "area_id": "banpo-sevit-01",
  "source_mode": "overpass",
  "source_review": "AREA01-LIVE-SRC-001",
  "license_review": "AREA01-LIC-001",
  "overpass": {
    "endpoint": "https://overpass-api.de/api/interpreter",
    "user_agent": "VIR-HanArea/1.0 owner-contact",
    "query_review": "AREA01-LIVE-QUERY-001"
  }
}
```

Do not commit an identifying user agent or personal contact address. Acquire and
pin the returned snapshot:

```sh
HAN_OSM_ACQUIRE_CONFIG=/owner-controlled/area01-live-acquire.json \
HAN_OSM_ACQUIRE_OUTPUT=/owner-controlled/Area01-OSM-Snapshot-20260922 \
make han-area-osm-acquire
shasum -a 256 /owner-controlled/Area01-OSM-Snapshot-20260922/source.osm
cat /owner-controlled/Area01-OSM-Snapshot-20260922/acquisition.json
```

Record the returned snapshot hash, source date/query, licensing review, and
attribution before continuing. A downloaded OSM response is not auto-approved
merely because acquisition succeeded.

## Step 2 — audit, register, and generate OBJ tiles

Audit the selected snapshot directory; this writes ignored local evidence only.
`han-area-audit` accepts an acquired directory containing `source.osm` (the
normal direct-OSM case), a single `.osm` file, or the legacy Blender
`map_5/map_6` batch directory:

```sh
HAN_OSM_DIR=/owner-controlled/Area01-OSM-Snapshot make han-area-audit
```

Create `/owner-controlled/area01-registration.json`. The template below is a
**scale-preserving bootstrap** derived from this runbook's projection origin:
it intentionally gives scale `1`, yaw `0`, and translation `0`. It is useful
only when the Area 01 review level intentionally uses the same local
east/south-centimeter frame. It is not a registration to an existing hand-made
level layout.

```json
{
  "origin_lon_lat": [126.993885, 37.51381],
  "controls": [
    {"id": "banpo-bridge-reference", "lon_lat": [126.996000, 37.511500], "level_xy_cm": [18675.891, 25714.592]},
    {"id": "sebit-reference", "lon_lat": [126.990000, 37.510500], "level_xy_cm": [-34305.820, 36846.043]},
    {"id": "east-bank-reference", "lon_lat": [126.982200, 37.511200], "level_xy_cm": [-103181.403, 29047.980]}
  ]
}
```

For an existing Area 01 review-level layout, replace **all three**
`level_xy_cm` pairs with measured coordinates of the exact same named
landmarks. Do not mix this bootstrap's target coordinates with hand-measured
coordinates from a different level frame.

Fit the source to the review level:

```sh
HAN_AREA_REVIEW=/owner-controlled/area01-registration.json \
make han-area-register
cat Saved/HanArea/banpo-sevit-01-register.json
```

The fit must use scale `[0.999, 1.001]` and maximum residual `≤ 200 cm`. A
rejection now reports its fitted scale, or maximum/RMS residual, so correct the
specific source/control/review-level mismatch. Do not stretch or hand-rotate
source geometry.

Create `/owner-controlled/area01-generate.json` with the acquired snapshot hash:

```json
{
  "schema_version": 1,
  "area_id": "banpo-sevit-01",
  "origin_lon_lat": [126.993885, 37.51381],
  "tile_size_m": 100,
  "default_level_height_m": 3,
  "osm_path": "/owner-controlled/Area01-OSM-Snapshot/source.osm",
  "osm_sha256": "<sha256 of acquired source.osm>",
  "source_review": "AREA01-SRC-002",
  "license_review": "AREA01-LIC-001",
  "georeference_review": "AREA01-GEO-001",
  "representative_export_review": "AREA01-REP-001"
}
```

Generate into a fresh directory and inspect the manifest before Unreal:

```sh
HAN_OSM_GENERATE_CONFIG=/owner-controlled/area01-generate.json \
HAN_OSM_GENERATE_OUTPUT=/owner-controlled/Area01-OSM-OBJ \
make han-area-osm-generate
shasum -a 256 /owner-controlled/Area01-OSM-OBJ/*.obj \
  /owner-controlled/Area01-OSM-OBJ/manifest.json
cat /owner-controlled/Area01-OSM-OBJ/manifest.json
```

The manifest records source, generator, OBJ hashes, OSM IDs, skipped geometry,
per-tile pivots, bounds-facing coordinates, triangle/vertex counts, and
building/water classification. Inspect `invalid_relation`, `relation_member`,
self-intersecting footprints, and unexpected cost before staging. A relation
member count is expected when valid relations suppress duplicate way output;
an invalid relation requires source review rather than manual edits to the
generated OBJ.

## Step 3 — stage the validated tiles in Unreal

Open the approved Area 01 review map through Unreal MCP. Do not substitute the
runtime Han level or `/Engine/Maps/Entry`.

If the review map does not exist, duplicate the approved Han review source
level in the Unreal Content Browser or through Unreal MCP to a path ending in
`_Review`, for example:

```text
/Game/Phase2/HanRiver/Maps/L_HanRiver_Area01_Review
```

Load that level and verify the editor reports that exact path before running
`apply=True`. The staging script refuses `/Engine/Maps/Entry`, the runtime
Han level, and any map outside `/Game/Phase2/HanRiver/Maps/*_Review`.

Create `/owner-controlled/area01-unreal-review.json`:

```json
{
  "review_map": "/Game/Phase2/HanRiver/Maps/L_HanRiver_Area01_Review",
  "manifest_sha256": "<sha256 of Area01-OSM-OBJ/manifest.json>",
  "origin_lon_lat": [126.993885, 37.51381],
  "controls": [
    {"id": "banpo-bridge-reference", "lon_lat": [126.996000, 37.511500], "level_xy_cm": [18675.891, 25714.592]},
    {"id": "sebit-reference", "lon_lat": [126.990000, 37.510500], "level_xy_cm": [-34305.820, 36846.043]},
    {"id": "east-bank-reference", "lon_lat": [126.982200, 37.511200], "level_xy_cm": [-103181.403, 29047.980]}
  ],
  "vertical_offset_cm": 0,
  "representative_scene_review": "AREA01-REP-002",
  "license_review": "AREA01-LIC-001",
  "source_georeference_review": "AREA01-GEO-001",
  "vertical_datum_review": "AREA01-VERT-001",
  "budget_review": "AREA01-BUDGET-001",
  "materials": {
    "building": "/Game/Phase2/HanRiver/Materials/M_Han_Concrete",
    "water": "/Game/Phase2/HanRiver/Materials/M_Han_Water"
  }
}
```

The review JSON must use the exact same control set and target frame as the
accepted registration JSON. For a hand-authored review map, replace all three
target-coordinate pairs in both files with the map measurements; do not retain
these bootstrap coordinates.

Unreal does not automatically add this repository's `Scripts/` directory to
its Python module search path. In the already-open Unreal Editor Python console
or a bounded Unreal-MCP Python execution, add the repository path first, then
dry-run the preflight:

```python
import sys

repo_root = "/Volumes/Unreal_Engine_Volume/work/virtual-indoor-rowing"
scripts_path = repo_root + "/Scripts"
if scripts_path not in sys.path:
    sys.path.insert(0, scripts_path)

import unreal_osm_area

root_path = "/Users/user1/work/t/osm"
manifest = root_path + "/Area01-OSM-OBJ/manifest.json"
review = root_path + "/area01-unreal-review.json"
plan = unreal_osm_area.run(manifest, review)
print(plan)
```

If the repository is mounted elsewhere, replace `repo_root` with the path that
contains `Scripts/unreal_osm_area.py`. The OSM output path and repository script
path are separate: `root_path` points to generated owner-controlled artifacts,
while `repo_root` points to the Python module source.

Confirm every target is new, maps only to
`/Game/Phase2/HanRiver/Meshes/Area01/OSM`, has a valid Han material, and has a
registration residual within the limit. Then stage unsaved review assets:

```python
result = unreal_osm_area.run(manifest, review, apply=True)
print(result)
```

The script imports OBJ static meshes, uses separate building/water material
mappings, disables collision, places actors under
`Han/Area01/OSMReview/Building` or `Han/Area01/OSMReview/Water`, refuses
overwrite/wrong-map staging, and rolls back actors/assets it created if any
tile fails. It does **not** save the map or assets.

### Troubleshooting the review-map error

If the console reports:

```text
ValueError: Open the approved review map ...; current map is ...
```

the source artifacts and registration may still be valid; only the editor
context is wrong. Check the current level from the Unreal Python console:

```python
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
print(editor.get_editor_world().get_path_name())
```

The result must match the `review_map` field exactly, without the object suffix
after the level name. If the level is missing, duplicate the approved source
map to a path such as
`/Game/Phase2/HanRiver/Maps/L_HanRiver_Area01_Review`, load it, and verify the
path again. Do not use `/Engine/Maps/Entry` or
`/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour` as the runtime context.

After correcting the editor context, rerun the `plan = ...run(...)` dry-run
before `apply=True`. If the error changes to an existing-asset refusal, the
previous staging attempt produced assets in the destination; inspect them in
the Content Browser and use a fresh review destination or a separately
reviewed reimport procedure. Do not set `replace_existing=True` in this path.

## Step 4 — review, save, and package

Before any explicit save, inspect each representative tile for:

- registration against all controls; source pivot/bounds/scale/orientation;
- correct `M_Han_Concrete` or `M_Han_Water` assignment, normals, UVs, LOD plan,
  draw cost, and no redirectors/unexpected dependencies;
- no collision, no boat obstruction, and a visually open central rowing channel;
- water having no current/wake/physical implication and no HUD-legibility loss;
- Banpo-to-Sebit blue-hour composition, exposure, reflection restraint, and
  transitions to retained authored scene geometry.

Only after the representative review approves, use Unreal MCP to save the
reviewed assets/map, add provenance and required OSM attribution to the package
inventory/`licenses/NOTICE.txt`, and continue through the existing validator and
cook path:

```sh
make build && make test
make format-check
make unreal-native-app
make unreal-smoke
make han-external-cook
make unreal-shipping
make unreal-package-verify
```

`make han-area-test`, acquisition, generation, or an editor screenshot is not
substitute evidence for source-license approval, map review, external content
validation, packaged canary, or reference-Mac evidence.
