# Phase 2 changelog

## Unreleased

- Full 5 km OSM import preparation (2026-09-25): generalized the reviewed OSM
  acquisition/generation and Unreal staging preflight so `han-river-5k` uses a
  bounded, separately named source extent, `SM_Han_5K_OSM` tile names,
  `Route5K/OSM` assets, and `L_HanRiver_5K_Review` rather than being mislabeled
  as Area01. An owner-directed Overpass snapshot is stored only in ignored
  local evidence; it hashes to `30336ad2f4ece6eb026601ff00c63e7712f3c4eb411352e8d318844e408d6f74`
  and generated 631 source tiles / 328 Unreal import assets. The Editor is
  currently closed and Unreal MCP is unavailable, so no `.umap` or `.uasset`
  has been imported, promoted, or saved.

- Han Area01 OSM-only cleanup preparation (2026-09-25): added the
  Unreal-MCP-only cleanup utility for `L_HanRiver_BlueHour`. It removes the
  legacy primitive `Han/Instanced` actors, retains reviewed Area01 OSM actors,
  and replaces the old bridge instances with `SM_SM_Han_BridgeSpan`; the tool
  deliberately leaves the level unsaved for owner inspection. The required
  Unreal MCP server is not available in this session, so no editor-owned map
  asset has been mutated or verified yet. `make han-source-verify` and
  `make format-check` pass.

- Phase 2 Milestone 5 Shipping archive seal repair (2026-09-25): found that
  `make unreal-shipping` copied `BuildVersions.json` and the embedded
  `.uproject` after UAT's ad-hoc signing pass, invalidating the archive seal
  before macOS could start Unreal. The wrapper now re-seals and strictly
  verifies the final staged tree; package verification also rejects a stale
  existing seal. `make test` passed 33/33, `make format-check` passed, and a
  rebuilt archive passed `make unreal-package-verify` with SHA-256
  `319c732d1b4405259c7385144a9e888a74ffd0383bcd40b54a46a003802086fe`.
  This managed host's LaunchServices still aborts the direct archive launch
  before Unreal initializes, so the r12 mounted-Han canary, visual review, and
  thermal evidence remain open.

- Phase 2 Milestone 5 plan review (2026-09-25): added ordered source/editor,
  mounted-Han canary, comparison, and production-selection gates. The revised
  sequence prevents a Standard fallback or an unmounted external cook from
  being represented as Han visual/thermal evidence, and names the exact
  `APlanarReflection` / `UPlanarReflectionComponent` allowlist work required
  before any planar reflector can ship in signed Han content. No acceptance
  result changed; mounted Han and reference-Mac Shipping evidence remain open.

- Phase 2 Milestone 5 packaged Han diagnostic (2026-09-25): the owner-provided
  app Details views show failed v1.0.0/v1.0.1 installs with
  `content.mount_failed`. One launch verified catalog revision 11; a later
  launch used a different, older immutable catalog and was rejected as
  `revision_rollback`, leaving Download disabled. The packaging runbook now
  calls out this recovery path. Mounted-Han and thermal evidence remain open.

- Phase 2 Milestone 5 thermal gate (2026-09-25): owner reduced this
  milestone's packaged Shipping thermal measurement from 60 minutes to a warm
  six minutes per route. [ADR-0014](../adr/0014-six-minute-milestone-5-thermal-gate.md)
  records the narrow supersession of ADR-0013; the QA-001 product-level
  60-minute interval and Phase 4 certification remain open. Added a run
  worksheet for the reference Mac, including mounted Han verification,
  performance captures, thermal timeline, and matched water-cost trials.

- Phase 2 Milestone 5 fresh Han external cook (2026-09-25): after the owner
  closed the Editors, `make han-external-cook` passed with
  `VIR_HAN_IOSTORE_DIR=Build/han-cook/HanRiver-m5-20260925`. UE 5.8
  BuildCookRun completed in 55.61 seconds and the wrapper validated the
  nonempty `HanRiver.pak`, `.utoc`, and `.ucas` trio plus the IoStore TOC
  header. The default output's earlier trio was preserved. UnrealPak listed
  346 cooked files, including the reviewed `M_Han_Water` and runtime
  `L_HanRiver_BlueHour` packages and no `Trial/` path. Mounted packaged-app
  behavior and visual/performance acceptance remain open.

- Phase 2 Milestone 5 fresh Editor automation and Han water reference repair
  (2026-09-25): Unreal MCP became available after the fresh native build.
  The full `VirtualRowing.CoursePresentation` suite passed 18/18 cases on
  the selected base module; several test-world cleanup warnings remain.
  Editor dependency inspection proved the tracked Han map actively used
  `/Game/Water/Trial/MI_Han_Water_RoughnessTrial`. Its water HISM override
  was restored to the reviewed `M_Han_Water` through Unreal MCP, saved,
  unloaded/reloaded, and read back clean. The map now reports 179 direct
  dependencies with no external `/Game` path, and `make han-source-verify`
  passes without a warning. `make water-source-check` passed with 32 native
  tests and one skip. Post-repair `make han-external-cook` attempts stopped at
  the process-inspection preflight (`/bin/ps` denied) before cooking. An
  Editor chase-height still shows broad pale reflection bands, so visual
  selection and Shipping acceptance remain open.

- Phase 2 Milestone 5 fresh Unreal build and Shipping package (2026-09-25):
  after the Editors were closed, `make unreal-smoke` succeeded and selected
  the unsuffixed `libUnrealEditor-VirtualRowing.dylib`. `make unreal-shipping`
  completed BuildCookRun, and `make unreal-package-verify` passed for the new
  unsigned app (`e9fc85252e718060c38a8f18c380bb8aad917994e1ec3ec118c174a9c0603f6a`).
  This verifies the package build and structure, not chase-view quality or
  frame time. A subsequent Han external-cook attempt stopped at its process
  preflight because `/bin/ps` access was denied in this session; it did not
  start a Han cook.

- Phase 2 Milestone 5 bitmap comparison color audit (2026-09-25):
  confirmed Standard's course actor sets `DeepColor=(0.03, 0.20, 0.35)` at
  runtime. The optional one- and two-sample trial defaults retain that value
  for actor-free comparisons; the procedural base asset keeps its darker
  fallback default. The graph test asserts the runtime-matched trial color.
  `make water-source-check` passed with 32 native tests and one skip. Saved
  Editor trials still require rebuilding and chase-view review.

- Phase 2 Milestone 5 Han cook guard (2026-09-25): the external cook now
  stops before native build or UAT when the runtime Han map or cook-directory
  assets contain unresolved serialized project paths outside the signed Han
  directory. The editor-open source check still warns so an Editor dependency
  query can distinguish a live reference from a stale serialized name. A
  regression test covers the cook preflight. `make water-source-check` passed
  with 32 native tests and one skip; the current map still names the trial
  roughness material, and `make unreal-smoke` stopped at four live Editors.

- Phase 2 Milestone 5 Han external-path diagnostic (2026-09-25): the Editor-open
  source check now warns when the runtime map or Han cook-directory assets
  contain serialized project paths outside the Han package. Its test covers
  map and material cases while excluding review maps. The current map produces
  a trial-material warning; an Editor dependency query is still required to
  determine whether that path is active. `make water-source-check` passed with
  32 native tests and one skip.

- Phase 2 Milestone 5 Han dependency audit (2026-09-25): found one serialized
  `/Game/Water/Trial/MI_Han_Water_RoughnessTrial` name in the modified tracked
  Han runtime map. The source verifier currently cannot establish whether this
  external path is an active dependency. Editor dependency and water-slot
  inspection are required before the external cook; no binary map edit was made.

- Phase 2 Milestone 5 bitmap graph source verification (2026-09-25): the
  one- and two-sample recipe test now traces both normal samples to the Normal
  output and the trial slope-variance parameter to Roughness. The Editor-open
  `make water-source-check` passed with 32 native tests and one skip; shader
  compilation and scene review still require Unreal Editor.

- Phase 2 Milestone 5 bitmap roughness trial source (2026-09-25): measured the
  Water 002 normal source's decoded XY mean-square magnitude at 0.0578 and
  added a trial-only control that moves faded bitmap slope energy into
  roughness for one- and two-sample variants. `make water-source-check` passed
  with 32 native tests and one skip. The saved trial assets have not been
  rebuilt or judged in a moving or packaged chase view.

- Phase 2 Milestone 5 two-sample bitmap trial source (2026-09-25): extended the
  water recipe with a separate two-sample mode using different world scales,
  rotated UV/slope axes, ambient pan directions, and footprint fades. The
  source test exercises one and two samples; `make water-source-check` passed
  with 32 native tests and one skip. No two-sample Editor asset or visual
  comparison has been produced yet.

- Phase 2 Milestone 5 bitmap trial source correction (2026-09-25): changed the
  optional normal-map feature fade to remove sampled detail smoothly between
  0.25 and 0.5 cycles per pixel; the former linear fade retained half its
  amplitude at 0.5. Added a test that executes the optional recipe branch.
  `make water-source-check` passed. Saved Editor trial materials and screenshots
  still use the earlier graph until rebuilt and compared in motion.

- Phase 2 Milestone 5 editor-open source loop (2026-09-25): added
  `make water-source-check` for formatting, native build/tests, and Han source
  validation without a closed-Editor preflight. Updated developer guidance to
  continue independent source work while Editor is open. The target passed with
  32 native tests, one skipped, and valid tracked Han dependencies; a following
  Unreal smoke preflight found four open Editor processes. Extended the Unreal
  presentation case to check that benchmark hiding preserves a multi-slot mesh
  with both Han water and non-water materials. Unreal compilation, editor
  automation, cook, and packaged acceptance remain separate.

- Phase 2 Milestone 5 saved-capture review (2026-09-25): a second inspection
  of the paused same-camera Editor comparisons found that the subtle Water 002
  trial adds broken detail but leaves the broad middle-distance bright bands.
  The recorded Han SSR-on still shows large distorted foreground highlights
  absent in its SSR-off pair. Production bitmap and reflection choices remain
  open pending moving chase views, packaged GPU evidence, and owner review.

- Phase 2 Milestone 5 Han water selector hardening (2026-09-25): benchmark
  hiding now identifies the authored Han water by full base-material asset path
  instead of its leaf name. The course presentation case uses the actual Han
  material and checks reduced-motion freezing, water hiding, and preservation
  of a same-named material outside the Han package. Native build/tests, Han
  source verification, formatting, and diff checks passed; Unreal compilation
  and fresh Editor automation remain pending.

- Phase 2 Milestone 5 GPU comparison instrumentation (2026-09-25): added
  diagnostic Shipping launch flags to hide only water interaction effects or
  both water surfaces and effects, including the streamed Han material. The
  same-build three-launch comparison can separate interaction and surface GPU
  cost without hiding the surrounding route. Source and acceptance procedure
  are recorded in the Milestone 5 packet; Unreal compilation, packaged visual
  checks, and timing remain unverified while live Editors block the required
  closed-Editor build and Unreal MCP access is denied in this environment.
  Follow-up source review narrowed streamed Han hiding to components whose
  every material slot is Han water, with an Unreal automation case for the
  water/non-water visibility boundary. That case has not run on a fresh module.

- Phase 2 Milestone 5 additional crash prevention (2026-09-25): traced the
  hot-reload failure to `Module Recompile VirtualRowing` loading module 6137
  while Unreal refused to replace both existing native automation registrations.
  Old course tests then referenced module 0009's obsolete actor class. Build and
  cleanup wrappers now require all Editors/commandlets closed and stop if
  process inspection is unavailable; builds recheck after native prerequisites.
  Smoke builds select the project's `VirtualRowingEditor` target, disable hot
  reload, retain a project-local UBT text log, and verify the module selected by
  `UnrealEditor.modules` rather than accepting a leftover base dylib. Both cooks
  disable hot reload and require the pinned doctor checks. Invalid explicit
  engine paths fail without selecting another installation. The current engine
  and project aliases resolve to their volume paths; no crash evidence shows
  those aliases caused the observed failures. Added workflow rules against using
  live recompilation or path/session changes to work around blocked builds.
  `make doctor`, `make test` (32 passed, one skipped), and `make format-check`
  passed, including regression cases for aliases, rejected process inspection,
  an Editor opened during prerequisites, and stale module manifests.
  `make unreal-smoke` and `make unreal-shipping` stopped before UBT/UAT because
  this runner cannot inspect processes. Actual closed-Editor compilation/cooking
  remains unverified; Unreal MCP confirmed the existing Editor responsive.

- Phase 2 Milestone 5 crash investigation (2026-09-25): confirmed the material
  `!IsRooted()` fatal in expression deletion, earlier duplicate `WorldSettings`
  initialization fatals, and a later hot-reload component-registration ensure.
  Preserved the existing material-rewiring and single-initialization fixes;
  added regression coverage for repeated water-recipe execution and fixed the
  recipe's `unary` input name colliding with the component-mask `a` property.
  CoursePresentation now rejects a replaced class before queuing automation.
  Both cooks disable MCP auto-start through a child-only INI override.
  Build wrappers preflight per-user write access: `XmlConfigCache` is a UAT
  startup permission failure, and UBT can separately abort rotating `Trace.uba`
  before `-NoLog` takes effect. Neither is repaired by deleting caches.
  See [development troubleshooting](../../DEVELOPMENT.md#editor-crash-prevention-and-build-failures).
  Verified `make doctor`, `make build`, `make test` (32 passed, one skipped),
  and `make format-check`. Unreal MCP health and all 17 CoursePresentation cases
  passed on the already-loaded module 6137, with 10 test-world cleanup warnings.
  This run predates the new C++ guard. `make unreal-smoke` and
  `make unreal-shipping` now stop at the confirmed managed-filesystem permission
  checks; compiling/executing the guard and verifying the cook override in a
  real cook remain blocked on owner-terminal access. No engine or binary asset
  changes were made by this investigation; no new Shipping package was produced.

- Corrected the tracked Han runtime map's water HISM from an identity cube at
  the origin to the 650 m by 480 m footprint specified by the Han scene recipe.
  Set its collision profile to `NoCollision` and disabled overlap events.
  Unreal MCP saved and reloaded the map; read-back confirmed the transform,
  one instance, `M_Han_Water` slot, clean state, and broad X/Y bounds.
  `make han-source-verify` passed. The newly visible surface still needs
  reflection tuning, mounted Shipping review, performance evidence, and owner
  acceptance; see the [Milestone 5 evidence log](13-milestone-5-evidence.md).

- Compared Han water at one fixed Editor camera with its ambient motion frozen
  and emissive `ReflectionMax` at 0.35 versus 0. The zero-fill trial darkened
  the surface but left the broad highlight bands visible, so further scene
  reflection and roughness comparison is needed. Saved a separate trial-only
  material instance; the tracked map still uses `M_Han_Water` and reloaded
  clean after the comparison.

- Ran a four-view Han Editor reflection comparison at one fixed camera:
  local sphere capture on/off crossed with SSR quality 0/3, with material
  motion and emissive sky fill disabled. The tested large sphere capture made
  the foreground reflection brighter and more smeared, so it was not selected.
  The temporary actor was removed, SSR restored to quality 3, and the tracked
  map reloaded clean. Moving-camera, planar, packaged GPU, and owner review
  remain open.

- Retried `make unreal-shipping` on 2026-09-25 with commandlet MCP auto-start
  disabled only during the run. The native prerequisite completed, but
  AutomationTool again could not write UnrealBuildTool's per-user
  `XmlConfigCache` in this managed filesystem and stopped before cooking.
  Restored the MCP setting; no new Shipping package was produced.

- Saved the Han water material through Unreal MCP with the same five
  footprint-filtered wave bands and roughness compensation as Standard.
  Inspected its reference from the tracked Han level and confirmed the saved
  material and map are clean. Imported the CC0 Water 002 normal into a
  separate Standard trial asset with source and license metadata. A separate
  one-sample trial material now uses it with world-anchored ambient motion and
  footprint fade; it recompiled, saved clean, and depends on the imported
  texture. In-scene visual and memory comparisons remain open. The full Editor
  course presentation spec ran 17 cases on module 0009: 15 passed, including the water effects and
  edge attachment assertion; two Han camera cases failed after an immediate
  presentation jump. A camera snap fix compiled into module 0010 but has not
  run in Editor. Scene reflection comparison, packaged Shipping visual and
  performance evidence, and owner review remain open; see the
  [Milestone 5 evidence log](13-milestone-5-evidence.md).

- Compared the Standard procedural water with a separate Water 002 one-sample
  normal-map trial in paused Editor Simulate at one fixed camera and matched
  runtime color. The bitmap adds broken highlights but is too busy toward the
  horizon for selection without further tuning and packaged review. Saved a
  trial-only baseline material instance and a second trial material with a
  lower 0.06 normal strength for the next comparison; no production water
  texture reference was added. The second variant recompiled, saved clean, and
  depends only on the Water 002 normal. The temporary preview actor was removed and the tracked Han map
  restored byte-for-byte to its repository version after the comparison.
  A live module recompile loaded the camera source fix, but Unreal retained
  the previous automation test registration and the camera rerun hit hot-reload
  component-registration errors. A fresh Editor run is required to verify the
  two camera cases.

- Captured a second paused Editor A/B for the 0.06 Water 002 normal trial at
  the matched Standard-water camera. It adds subtler broken highlights than
  the 0.16 trial. The Editor grid and a competing-light warning still limit
  visual conclusions, and Shipping, motion, GPU, and owner review remain open.
  The temporary preview actor was removed; the tracked Han map reloaded clean
  with its repository bytes unchanged.

- Saved the filtered five-band Standard water material and a cooked-in
  translucent interaction material through Unreal MCP. Both recompiled and
  reported clean after save. Two new Unreal course automation cases passed;
  their output exposed course-edge attachment warnings, so spline edges now
  use the movable mobility required by their parent. `make unreal-smoke`
  compiled that correction; an Editor restart reached only the project browser,
  so its automation rerun and chase-view inspection,
  Han water update, Shipping package, GPU and thermal evidence remain open.
  A follow-up assertion now checks that every course edge attaches to its root;
  `make unreal-smoke` compiled it, while macOS LaunchServices errors blocked
  the next Editor and command-line automation launches before project load.

- Owner decision on 2026-09-24: the representative-scene view review of the
  500 m Banpo-to-Sebit Han River section completed with **SUCCESS** against
  commit `b38ff54`. Performance-budget measurement, packaged Shipping
  evidence, and the full 5 km owner route review remain open.

- Added source-side Milestone 5 hull and oar water effects: a bounded pool of
  world-anchored, non-colliding planes, fresh-motion hull samples, one ripple
  per rendered blade crossing, expiry, and reduced-motion suppression. Added
  an Editor recipe for the cooked-in translucent material and extended the
  Unreal course presentation spec. `make unreal-smoke` compiled the revised
  actor and spec; `make build && make test` passed 32 native tests with one
  skip. The material asset has not been saved through Unreal Editor, so the
  visual effects and Unreal spec execution remain unverified.

- Advanced Milestone 5 source work on 2026-09-24: projected-footprint wave
  filtering and slope-variance roughness in the shared water recipe, fresh-drive
  oar blade pitch and contact-state suppression with a course presentation
  source test, and transitive Han package
  dependency checks that include texture assets and their cook directory.
  `make han-source-verify`, `make test` (latest: 32 passed, one skipped), and
  `make format-check` passed.
  The editor crashed while deleting rooted material expressions before saving a
  new water asset; the recipe now avoids that deletion. A subsequent
  `make unreal-smoke` succeeded with zero compile actions, while a new Shipping
  cook could not finish because Unreal MCP's editor listener occupied port 8000
  and, on a retry with auto-start temporarily disabled, AutomationTool could
  not write its per-user `XmlConfigCache` in this managed filesystem. The
  saved material, water effects, Shipping visual and
  performance evidence, and owner review remain open; see the
  [Milestone 5 evidence log](13-milestone-5-evidence.md).

- Added an original multipolygon rower as four modular Unreal static meshes:
  torso/head, arm, leg, and shoe. The course actor applies them on both routes
  using the existing presentation poses, with connected arm and leg segments
  and cube torso/arm fallback when the character assets are missing. Editor
  asset bounds and thumbnails were inspected; in-scene motion, packaged visual
  review, and GPU cost remain unverified.

- Replaced the course actor's cube hull with an original 840 cm multipolygon
  scull hull on both routes. The editor-imported mesh includes a shaped shell,
  recessed cockpit, deck, and riggers; it has five material slots, no collision,
  and no Nanite. The cube remains the missing-asset fallback. Editor asset
  inspection passed; packaged chase-camera review and GPU measurement remain
  open.

- Began Milestone 5 implementation on 2026-09-24 with an original, multi-face
  sculling-oar mesh. The editor-imported asset has shaped grip, shaft, collar,
  and blade sections and replaces the cube oars in the cooked-in course actor
  for both routes. Water-contact animation and effects are still pending;
  `make format-check` and `make unreal-smoke` passed; packaged visual review
  and performance evidence are open.

- Accepted [ADR-0013](../adr/0013-increase-reference-gpu-frame-budget.md) on
  2026-09-23: the reference whole-scene GPU p95 budget rises from 14.0 to
  16.5 ms, as confirmed by the owner. QA-001's 60 fps, 16.7 ms p95 frame-time,
  and thermal requirements remain unchanged. Milestone 5 now uses the revised
  budget; no Shipping performance result is claimed by this decision.

- Scoped Phase 2 Milestone 5 from the water-refinement research on 2026-09-23.
  New research adds a measured comparison of scene reflection captures, SSR,
  and a bounded planar prototype before material-shading changes; it also
  specifies slope-variance filtering and the Han content-allowlist consequence.
  The bitmap trial now includes two CC0 water-normal candidates, texture import
  checks, source provenance, and Han cook/dependency coverage. The owner also
  included restrained hull and oar water effects, and chose to review the
  exact license terms of any non-CC0 candidate before import if the CC0 trial
  fails. This is documentation only; no water asset, source shader, or
  acceptance evidence changed.

- Completed Phase 2 Milestone 4 by owner decision on 2026-09-23. The
  Blender-building import scope is closed. Importing several additional OSM
  maps remains separately scoped, review-gated Han content follow-up work; it
  does not reopen Milestone 4 or pass the Phase 2 exit gate.

- Added interval-rest camera cutscene #1 for every non-Standard route on
  2026-09-23. It is a route-local Unreal-only state machine driven by immutable workout snapshots:
  five seconds of continuously fresh PM-reported rest lead to a ten-second
  rear/starboard reveal; leaving rest returns to the chase view over four
  seconds. Reduce Motion keeps the chase view and exposes a keyboard-reachable
  static "View surroundings" action during a valid rest. The feature has no
  write path to PM facts, route selection, or the local journal. Automated
  source coverage was added; editor scene/HUD review and reference-Mac
  frame-time evidence remain required before acceptance.

- Initially documented the [realistic water refinement plan](12-realistic-water-refinement-plan.md)
  on 2026-09-23. It specifies projected-footprint wave filtering, restrained
  reflections, explicit reduced-motion checks, editor asset review, Han
  re-cook, and packaged reference-Mac visual and thermal evidence. Han water
  remains animated by default; automatic reduced motion or a changed default
  requires a separate owner decision. At that point it was a follow-up plan,
  before Milestone 5 was scoped. This is planning only;
  no water source or Unreal asset changed and no acceptance gate passed.

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
