# Phase 2 changelog

## Unreleased

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
