"""Replace staged Route5K OSM geometry in the approved Unreal review level only."""

from __future__ import annotations

import json
from pathlib import Path

from unreal_osm_area import prepare, run


OSM_PREFIX = "SM_Han_5K_OSM_"


def replace(manifest_path: str, review_path: str) -> dict:
	"""Replace all prior Route5K OSM staging; never save or promote the level."""
	import unreal

	plan = prepare(Path(manifest_path), json.loads(Path(review_path).read_text()))
	if plan["area_id"] != "han-river-5k":
		raise ValueError("Replacement accepts only the approved 5 km OSM manifest")
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	current_map = world.get_path_name().split(".")[0]
	if current_map != plan["review_map"]:
		raise ValueError(f"Open the approved review map {plan['review_map']} through Unreal MCP first; current map is {current_map}")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	staged = [actor for actor in actors.get_all_level_actors() if str(actor.get_folder_path()).startswith(plan["folder"])]
	old_assets = [path for path in unreal.EditorAssetLibrary.list_assets(plan["destination"], recursive=True, include_folder=False) if path.rsplit("/", 1)[-1].startswith(OSM_PREFIX)]
	for actor in staged:
		actors.destroy_actor(actor)
	for path in old_assets:
		if not unreal.EditorAssetLibrary.delete_asset(path):
			raise RuntimeError(f"Could not delete obsolete staged Route5K OSM asset: {path}")
	result = run(manifest_path, review_path, apply=True)
	return {**result, "removed_staged_actors": len(staged), "removed_staged_assets": len(old_assets), "saved": False}
