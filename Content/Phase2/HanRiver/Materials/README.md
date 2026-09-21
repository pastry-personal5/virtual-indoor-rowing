# Han editor assets

`MI_Han_Concrete.uasset` was created, assigned a Color override of
`(0.24, 0.29, 0.34, 1)`, and saved successfully through Unreal MCP on
2026-09-21. Its parent is `/Engine/BasicShapes/BasicShapeMaterial`.
This verifies editor asset creation, modification, and persistence.

Creator: Virtual Indoor Rowing via Codex. Original material parameter choices;
the parent remains Unreal Engine content under the project's engine license.
No third-party bitmap is incorporated. Reviewer: content-owner-pending.
Status: unshipped review candidate, not a signed-content inventory entry.

`Scripts/build_han_editor_assets.py` was executed in the open Unreal Editor
through the Slate/Python console after a clean `make unreal-smoke` rebuild.
It authored and saved `Maps/L_HanRiver_BlueHour.umap`, the three reusable mesh
assets under `Meshes/`, eight materials, and an 881-actor representative scene.
The editor MCP reported `HAN_AUTHORING_COMPLETE` and a viewport capture was
verified at the saved level. Merge optimization was unavailable in UE 5.8 for
these transient component assemblies, so the level retains the authored
component assemblies and the merge candidates remain review artifacts.
Status: unshipped review candidate, not a signed-content inventory entry.
