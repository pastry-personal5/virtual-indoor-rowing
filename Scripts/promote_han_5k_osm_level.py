"""Promote reviewed 5 km OSM actors into the Han runtime level.

Run only through Unreal MCP with the runtime map open. The module validates all
reviewed assets before mutation. It deliberately leaves the level unsaved for
inspection after it replaces the legacy instanced dressing.
"""

from __future__ import annotations

from pathlib import Path

from cleanup_han_osm_level import BRIDGE_LABEL, BRIDGE_MESH, LEGACY_FOLDER, _is_bridge_actor, _is_legacy_instanced_actor
from unreal_osm_area import prepare


RUNTIME_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour"
OSM_FOLDER = "Han/Route5K/OSM"


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def plan(manifest_path: str, review_path: str) -> dict:
	import unreal

	if _current_map(unreal) != RUNTIME_MAP:
		raise ValueError(f"Open {RUNTIME_MAP} through Unreal MCP before promotion")
	prepared = prepare(Path(manifest_path), __import__("json").loads(Path(review_path).read_text()))
	if prepared["area_id"] != "han-river-5k":
		raise ValueError("Promotion accepts only the approved 5 km OSM manifest")
	if not isinstance(unreal.load_asset(BRIDGE_MESH), unreal.StaticMesh):
		raise RuntimeError(f"Required bridge mesh is unavailable: {BRIDGE_MESH}")
	for item in prepared["assets"]:
		if not isinstance(unreal.load_asset(item["path"]), unreal.StaticMesh):
			raise RuntimeError(f"Reviewed OSM mesh is unavailable: {item['path']}")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
	legacy = [actor for actor in actors if _is_legacy_instanced_actor(unreal, actor)]
	existing = [actor for actor in actors if str(actor.get_folder_path()).startswith(OSM_FOLDER)]
	bridge_actors = [actor for actor in actors if _is_bridge_actor(unreal, actor)]
	if existing:
		raise ValueError("Runtime map already has Route5K OSM actors; promotion refuses overwrite")
	return {
		"runtime_map": RUNTIME_MAP,
		"review_map": prepared["review_map"],
		"promote_osm_actors": len(prepared["assets"]),
		"remove_legacy_instanced_actors": len(legacy),
		"existing_bridge_actors": len(bridge_actors),
		"will_create_bridge": not bridge_actors,
		"saved": False,
	}


def run(manifest_path: str, review_path: str, apply: bool = False) -> dict:
	result = plan(manifest_path, review_path)
	if not apply:
		return result
	import unreal

	prepared = prepare(Path(manifest_path), __import__("json").loads(Path(review_path).read_text()))
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	created = []
	try:
		for item in prepared["assets"]:
			actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*item["location_cm"]), unreal.Rotator(0, item["yaw_deg"], 0))
			if actor is None:
				raise RuntimeError("Could not create Route5K OSM runtime actor")
			created.append(actor)
			actor.set_actor_label(item["name"])
			actor.set_folder_path(f"{OSM_FOLDER}/{item['kind'].title()}")
			actor.set_actor_scale3d(unreal.Vector(*item["scale"]))
			actor.static_mesh_component.set_static_mesh(unreal.load_asset(item["path"]))
			actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
			actor.static_mesh_component.set_material(0, unreal.load_asset(item["material"]))
		if not any(_is_bridge_actor(unreal, actor) for actor in actors.get_all_level_actors()):
			bridge = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
			if bridge is None:
				raise RuntimeError("Could not create the preserved Han bridge actor")
			created.append(bridge)
			bridge.set_actor_label(BRIDGE_LABEL)
			bridge.set_folder_path("Han/Bridge")
			bridge.static_mesh_component.set_static_mesh(unreal.load_asset(BRIDGE_MESH))
			bridge.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	except Exception:
		for actor in reversed(created):
			actors.destroy_actor(actor)
		raise
	legacy = [actor for actor in actors.get_all_level_actors() if _is_legacy_instanced_actor(unreal, actor)]
	for actor in legacy:
		actors.destroy_actor(actor)
	return {**result, "promoted_osm_actors": len(prepared["assets"]), "removed_legacy_instanced_actors": len(legacy), "saved": False}
