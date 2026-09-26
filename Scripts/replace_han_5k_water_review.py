"""Replace only staged Route5K OSM water in the approved Unreal review map.

This is deliberately review-only: it does not save a level, promote content, or
touch buildings, bridges, or legacy runtime primitives.
"""

from __future__ import annotations

import json
from pathlib import Path

from unreal_osm_area import prepare, run


WATER_PREFIX = "SM_Han_5K_OSM_WaterTile_"


def replace(manifest_path: str, review_path: str) -> dict:
	"""Delete the prior staged water set, then import its reviewed replacement."""
	import unreal

	plan = prepare(Path(manifest_path), json.loads(Path(review_path).read_text()))
	if plan["area_id"] != "han-river-5k":
		raise ValueError("Water replacement accepts only the approved 5 km OSM manifest")
	if not any(item["kind"] == "water" for item in plan["assets"]):
		raise ValueError("Reviewed 5 km manifest has no water assets")
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	current_map = world.get_path_name().split(".")[0]
	if current_map != plan["review_map"]:
		raise ValueError(f"Open the approved review map {plan['review_map']} through Unreal MCP first; current map is {current_map}")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	staged_water = [actor for actor in actors.get_all_level_actors() if str(actor.get_folder_path()).startswith(f"{plan['folder']}/Water")]
	old_water_assets = [path for path in unreal.EditorAssetLibrary.list_assets(plan["destination"], recursive=True, include_folder=False) if path.rsplit("/", 1)[-1].startswith(WATER_PREFIX)]
	for actor in staged_water:
		actors.destroy_actor(actor)
	for path in old_water_assets:
		if not unreal.EditorAssetLibrary.delete_asset(path):
			raise RuntimeError(f"Could not delete obsolete staged water asset: {path}")
	result = run(manifest_path, review_path, apply=True, kinds={"water"})
	return {**result, "removed_water_actors": len(staged_water), "removed_water_assets": len(old_water_assets), "saved": False}
