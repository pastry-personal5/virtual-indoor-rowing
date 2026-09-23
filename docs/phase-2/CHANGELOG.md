# Phase 2 changelog

## Unreleased

- Extended the direct Area 01 OSM path on 2026-09-23 to fetch
  `type=multipolygon` relations with their way/node members, assemble split
  outer and inner rings, leave holes empty in building and water tiles, and
  avoid duplicate member-way output. Tags on older outer members are accepted
  when they agree on the feature class. Invalid relation topology is counted
  and excluded from generated geometry; deterministic fixture tests cover
  split rings, holes, multiple outers, and a broken relation. No live Overpass
  acquisition or Unreal import has been run for this change.

- Fixed the Area 01 live Overpass snapshot query on 2026-09-23 to request the
  same closed water-way tag categories accepted by the generator. The fixed
  query test now checks every water selector; the Phase 2 runbook describes
  the query scope. The index-linked runbooks are included in the Git index.

- Clarified the Han package mount failure on 2026-09-23. The course line now
  shows the content availability category, the Content panel identifies a
  failed mount, and the Shipping mount event captures Unreal's IoStore error
  category and system error without recording a local path. The packaging
  runbook now gives a packaged-app recovery path and records the local failed
  `1.0.0`/`1.0.1` canary evidence; no new signed release has been validated.

- Expanded the Han River packaging runbook on 2026-09-23 with clean-source and
  Git LFS checks, fresh cook transfer and hash matching, previous-catalog
  retrieval, release-file verification, a reproducible local Caddy smoke,
  remote digest checks, an immutable-catalog packaged canary before promotion,
  and a post-promotion catalog comparison. The origin upload remains the
  restricted operator's provider-specific procedure.

- Tightened the Han release path on 2026-09-23: the external cook now selects
  the runtime `L_HanRiver_BlueHour` map and the Han Materials/Meshes directories
  without sweeping in review or backup maps. `make han-source-verify` rejects
  missing, empty, LFS-pointer, or untracked map dependencies before a cook.
  The packaging runbook now identifies the 164 Area 01 OSM mesh packages and
  the Han-area scripts required in the release revision, and spells out the
  immutable upload, digest, catalog-pointer, and packaged-app checks.

- Added the [Han River packaging runbook](11-han-river-packaging-runbook.md) on
  2026-09-22. It documents the prerequisite checks, external IoStore cook,
  signed `HanRiver.vircontent` release, restricted-origin publication, and
  packaged Shipping verification without changing the content trust boundary.

- Split the planned Area 01 geospatial-source guidance into a detailed
  [OSM-to-Unreal runbook](09-han-area-01-osm-unreal-runbook.md) and a separate
  [OSM-to-Blender-to-Unreal runbook](10-han-area-01-osm-blender-unreal-runbook.md)
  on 2026-09-22. The OSM acquisition boundary explicitly selects either a
  reviewed offline source or one fixed, capped, HTTPS Overpass snapshot before
  it becomes a hash-pinned local input. The work records an Area 01
  registration contract, audits the local OSM source, and stages only unsaved
  review assets after explicit Unreal-MCP map inspection. It does not mutate
  the Han level, import a mesh, establish an OSM license/provenance approval,
  complete Milestone 4, or pass any Phase 2 gate.

- Corrected the Area 01 registration examples on 2026-09-22. The old
  illustrative review-level coordinates implied an invalid scale transform; the
  runbooks now use the tested scale-preserving bootstrap controls and explain
  the fitted-scale/residual diagnostics required when registering an existing
  hand-authored review map.

- Documented the Unreal Python module-path bootstrap on 2026-09-22. Unreal
  Editor does not automatically search the repository `Scripts/` directory,
  so the runbooks now add that path before importing the staging modules.

- Made the OSM Unreal staging map configurable, while restricting it to a
  `/Game/Phase2/HanRiver/Maps/*_Review` level, on 2026-09-22. The error now
  reports both the required and currently loaded map paths. Added exact Unreal
  console verification and retry guidance to both import runbooks.

- Owner decision on 2026-09-22: the runtime Han map is
  `/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour`, not
  `L_HanRiver_Area01_BlueHour`. The repaired level (13 HISM actors plus the
  staged OSM tiles) was saved at the `L_HanRiver_BlueHour` path, so the client
  route table, the cook `-map`, the editor-asset authoring script, and the
  loose-level dev path all name that one level. `L_HanRiver_Area01_BlueHour`
  and `L_HanRiver_Area01_Review` stay unsaved-state review assets only; the
  dated `L_HanRiver_BlueHour_20260922` map remains the rollback backup.

- Owner decision on 2026-09-22: the narrow direct OSM path now includes closed
  river/water polygons as separate static visual tiles with `M_Han_Water` and
  no collision. It does not add water relations/multipolygons, terrain, flow,
  wake, depth, safety, routing, or workout-fact semantics.

- Completed Phase 2 Milestone 3 by owner decision on 2026-09-22. The
  texture-free, math-only material (`/Game/Water/M_CourseWater`, five analytic
  directional waves with deep-water dispersion, distance-faded detail, capped
  Fresnel sky tint) was built and recompiled through Unreal MCP, and the kit
  water now uses it with a reduced-motion override. `M_Han_Water` in the Han
  package was rebuilt in place with the same graph after the owner authorized
  free Han asset edits (saved; the level still references it). Completion is
  incremental: `make unreal-smoke`, `CoursePresentationSpec`, a re-cook and
  content canary, owner visual review, and GPU-cost measurement remain deferred
  or unverified; `Scripts/build_water_material.py` is syntax-checked only.

- Han content diagnostics and update path (development aids, presentation only).
  A new right-hand `UContentPanelWidget` now holds the Han level state, course
  selection, package build time, Download, Content Licenses / Credits, and a
  scrollable DETAILS view (content status with a plain-language reading, catalog
  and package HTTP results, installed/offered versions, mount result, and a
  timestamped event log for both the content pipeline and the level stream,
  including the class the allowlist refused). The HUD keeps only the workout
  metrics and its fonts are slightly smaller. `Download Han River` also appears
  when a newer catalog than the mounted package is verified. Fixed a same-version
  reinstall that reported `malformed` and left the retained install's files
  overwritten: the release version is now `1.0.1`, and the client refuses a
  retained version before extracting. `make format-check`, `make content-canary`
  and `make unreal-smoke` pass; the packaged-app result is not yet observed.
  Root cause of the Han level never appearing (owner-confirmed fixed in the
  packaged app 2026-09-21): `UCourseSubsystem` spawned the course actor with
  `SpawnActor`, whose immediate `BeginPlay` built the Standard kit before
  `ConfigureRoute` ran; it now spawns deferred. The DETAILS view is behind a
  "Show details" toggle, the device panel no longer carries a duplicate course
  selector (the content panel sits above it), and the retained 1.0.0 install was
  re-extracted from the revision-2 package after an earlier same-version
  overwrite. `CoursePresentationSpec` (including the new begun-play case) has
  not been run.

- Renumbered the planned development and content-iteration loop packet from
  Phase 2 Milestone 4 to Phase 2 Milestone 10 (2026-09-22); scope and status
  remain unchanged:
  [06-milestone-10-development-loop.md](06-milestone-10-development-loop.md)
  scopes shorter code and content iteration loops (editor-safe Shipping build,
  uncooked Development run, loose-level course reload, one-command local Han
  publish). No code, command, or trust-boundary change; the cause of the
  editor/Shipping conflict is still unconfirmed and is its first spike.

- Milestone 2 spike 2 (conversion applied): `L_HanRiver_BlueHour` now holds 13
  HISM actors instead of 877 `StaticMeshActor`s (same instance transforms,
  materials, no collision). `make build && make test`, `make content-canary`, and
  `make format-check` pass. Not verified: a re-cook (blocked in-session by the
  live editor holding MCP port 8000), `CoursePresentationSpec`, and frame time.

- Milestone 2 spike 3 (partial): rejecting or losing the streamed Han level now
  restores the built-in kit (`course.level_lost` for a level that vanishes after
  being shown). `CoursePresentationSpec` gains a boat/camera-unmoved handover
  case. `make format-check`, `make build && make test`, and `make unreal-smoke`
  pass; the spec was not run and no hitch measurement exists.

- Owner decision (2026-09-21): the compressed Han package cap is raised from
  32 GiB to 100 GiB (`MaximumCompressedPackageBytes`, the release packager cap).
  Asset and package size are not measured and not gated in Milestone 2. The
  4x uncompressed bound and the 100 GiB free-storage headroom are unchanged.

- Milestone 2 spike 2 (partial): the 877 static-mesh actors in the Han level
  use three engine primitive meshes and collapse into 13 instanced-mesh
  actors in an unsaved editor trial. The client allowlist now admits a plain
  `Actor` only when every component is a native scene, static-mesh, instanced,
  or hierarchical-instanced mesh component (`content_runtime_tests`,
  `make build && make test`, `make format-check` pass). Unreal compile,
  `CoursePresentationSpec` coverage, and reference-Mac measurement remain open.

- Recorded Milestone 2 spike 1 as passed (owner-run on a Shipping build,
  2026-09-21): the Han level streams from the runtime-mounted trio and the
  class allowlist accepted the cooked level. Owner-observed with no attached
  evidence; the streamed-sublevel design stands.

- Milestone 2 wiring (compiled, not yet run in a mounted package). The client
  now owns a route-ID-to-level table (`ContentRuntime/CourseLevel.h`) and an
  actor-class allowlist. `UCourseSubsystem` streams the mounted Han level as a
  hidden dynamic sublevel, checks every actor is a native allowlisted class, and
  only then shows it; any failure keeps the built-in kit and records a redacted
  category. A route-selection change now destroys and rebuilds the course scene
  (selection is already idle-only), so no restart is needed. The authored level
  is 500 m, so it overlays the kit's start area instead of replacing the route
  baseline; `AGrayBoxCourseActor::SetAuthoredLevelActive` is reversible.
  Covered by `content_runtime_tests` and `CoursePresentationSpec`
  (`make build && make test`, `make format-check`, `make unreal-smoke` pass).

- Marked Phase 2 Milestone 1 completed by owner decision on 2026-09-21. The
  evidence record lists what was demonstrated (external cook, signed release,
  download, next-launch activation, mount) and the packaged-canary checks that
  remain uncaptured. This does not pass the Phase 2 exit gate.
- Fixed `content.storage_insufficient` being reported on a disk with ample free
  space. `HasStorageAdmission` measured the staging directory itself, which is
  created only after admission, and `std::filesystem::space` fails on a missing
  path. It now measures the nearest existing ancestor. Regression assertion in
  `ContentRuntimeTests`.
- Fixed the packaged app's `Download Han River` button staying disabled. Root
  cause: Unreal's statically linked `SQLiteCore` replaces the system SQLite in
  the app, and its file layer has no shared-memory support, so it cannot open
  `rowing.sqlite3` once native tools have left it in WAL mode
  (`unable to open database file`, code 14) and content init aborted. The
  connection now falls back to `locking_mode=EXCLUSIVE` and
  `journal_mode=DELETE`, which converts the file to a rollback journal; native
  tools switch it back to WAL when they next open it. Covered by
  `ContentRepositoryTests` with a no-shared-memory stand-in VFS. The panel now
  has a right-hand `Content status` box that reports failure detail (statement,
  SQLite code, file layer, cause). Not yet verified in a rebuilt Shipping app.
- `make content-release-package` now supports a first publication: with
  `VIR_CONTENT_PREVIOUS_CATALOG` unset it requires
  `VIR_CONTENT_CATALOG_REVISION=1`; otherwise the prior catalog is still
  verified and the revision must be strictly higher. Covered by
  `Scripts/test_content_release_packager.py`.
- Added `make han-external-cook`: a dedicated UE 5.8 BuildCookRun that cooks
  the Han map and directory into a separate IoStore chunk and emits
  `HanRiver.pak`, `HanRiver.utoc`, and `HanRiver.ucas`, validated as non-empty
  with a real IoStore TOC header. Temporary generated config keeps ordinary
  Shipping packaging unchanged. Covered by `Scripts/test_unreal_packaging.py`;
  mounting the result in a packaged app remains unverified.
- Replaced the embedded `content-current` public verification key in
  `ContentSubsystem.cpp` and `Scripts/vir_dev/content.py` and documented
  creation of the signing key under `${HOME}/work/vir-tools/han-river`. Only
  `content-current` is needed to publish; the C++ change is not yet rebuilt.
- Verified Unreal MCP asset creation/edit/save with `MI_Han_Concrete`, then
  rebuilt the Unreal module with `make unreal-smoke` and authored the saved
  Han representative scene through the editor Python console. The editor MCP
  reported `HAN_AUTHORING_COMPLETE` (881 actors); the saved level and materials
  are present under `Content/Phase2/HanRiver`, and the viewport capture was
  verified at `/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour`.
- Added the route-specific Han River presentation kit to the Unreal course
  actor: blue-hour lighting, full-length water and banks, abstract bridge and
  skyline rhythm, Some Sevit-inspired original volumes, and sparse off-line
  buoys. Han suppresses the generic yellow route edge and distance-marker
  labels; all meshes are non-collision presentation and have no official-fact
  write path.
- Removed the representative-scene review prerequisite for Han River
  Unreal-MCP asset mutation. Provenance, validation, and release-pipeline
  requirements remain in force.
- Added a CC0 Wonhyo Bridge/Yeouido finish-vista bitmap to the unshipped Han
  reference snapshot, with source and local hashes, creator, license, and
  validator coverage. It remains reference-only pending asset-import review.
- Phase 2 Milestone 1 implementation gates are now covered by engine-independent
  manifest, inventory, license-notice, package-validation, LocalData lifecycle,
  Standard-fallback, and Unreal integration code. The evidence record is in
  [04-milestone-1-evidence.md](04-milestone-1-evidence.md). A published Han
  artifact/catalog and packaged canary remain required before the milestone is
  marked complete.
- Added `make content-canary` and `make content-fixture`. The automated canary
  now records deterministic origin publication, interrupted resume, restart
  activation, fallback, withdrawal, and newer-revision rollback. Its marker
  artifact is intentionally not treated as cooked-IoStore evidence; a real UE
  5.8 external cook and packaged canary remain release-owner work.
- Added `make content-release-package`, a restricted release-owner operation
  that consumes the reviewed external Han IoStore trio, generates the
  canonical route/inventory/notice archive, verifies a signed previous
  catalog before accepting a higher revision, signs a seven-day manifest only
  with `content-current.pem`, and emits immutable hash-addressed package and
  catalog outputs without uploading them.
- Hardened the deterministic canary to verify its fixture signature and archive
  entry hashes, and to reject a corrupt artifact or altered signature. It uses
  an explicitly separate public RFC test key; client trust-key provisioning is
  still restricted release-owner work.
- Added runtime catalog refresh, user-initiated resumable download,
  storage-admission, validation/extraction orchestration, and next-launch
  activation. These remain subject to real cooked-IoStore packaged-canary
  evidence before a delivery claim.
- Hardened the persisted catalog ledger: an identical signed revision may be
  replayed, but a distinct manifest cannot replace an already accepted
  revision. The repository regression test covers both cases.
- Added the selected review-only Han River key-art candidate (`v2`), with a
  provenance hash and explicit restraint/legibility rationale. It is not a
  cooked route asset and still needs the required content review before import.
- `make content-canary` now validates the Han source candidate itself: its
  stable route identity, six fixed beats, virtual-only waterline boundary,
  provenance hashes, and reference-only license notice.
- Replaced the app's development content trust vectors with the release-owner
  current and next Ed25519 public verification keys. No private key is stored
  in the repository.
- Course selection is now exposed on the blocking launch/device surface as
  well as the workout HUD: Standard 2 km is always selectable while idle;
  Han River 5 km remains visibly disabled with a safe reason until validated
  content activates. This implementation evidence does not pass a Phase 2
  gate.
- Removed the speculative future-milestone sequence. Future milestones are
  scoped only when their planning begins.
