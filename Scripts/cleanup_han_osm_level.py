"""Remove legacy primitive dressing from the Han runtime level.

Run this only in Unreal Editor, with the runtime Han level open.  The cleanup
keeps imported Area01 OSM actors and replaces the old mixed HISM dressing set
with the separately authored bridge mesh.  It intentionally does not save;
inspect the result and save through the approved Editor workflow.
"""

from __future__ import annotations


RUNTIME_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour"
LEGACY_FOLDER = "Han/Instanced"
OSM_FOLDER = "Han/Area01/OSMReview"
BRIDGE_MESH = "/Game/Phase2/HanRiver/Meshes/SM_SM_Han_BridgeSpan"
BRIDGE_LABEL = "Han_OSM_Bridge"


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def _is_legacy_instanced_actor(unreal, actor) -> bool:
	if str(actor.get_folder_path()) != LEGACY_FOLDER:
		return False
	components = actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
	return bool(components)


def _is_osm_actor(actor) -> bool:
	return str(actor.get_folder_path()).startswith(OSM_FOLDER)


def _is_bridge_actor(unreal, actor) -> bool:
	if actor.get_actor_label() == BRIDGE_LABEL:
		return True
	for component in actor.get_components_by_class(unreal.StaticMeshComponent):
		mesh = component.get_editor_property("static_mesh")
		if mesh is not None and mesh.get_path_name() == BRIDGE_MESH:
			return True
	return False


def plan() -> dict:
	import unreal

	if _current_map(unreal) != RUNTIME_MAP:
		raise ValueError(f"Open {RUNTIME_MAP} through Unreal MCP before cleanup")
	bridge = unreal.load_asset(BRIDGE_MESH)
	if not isinstance(bridge, unreal.StaticMesh):
		raise RuntimeError(f"Required bridge mesh is unavailable: {BRIDGE_MESH}")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
	legacy = [actor for actor in actors if _is_legacy_instanced_actor(unreal, actor)]
	osm = [actor for actor in actors if _is_osm_actor(actor)]
	bridge_actors = [actor for actor in actors if _is_bridge_actor(unreal, actor)]
	return {
		"runtime_map": RUNTIME_MAP,
		"remove_legacy_instanced_actors": len(legacy),
		"preserve_osm_actors": len(osm),
		"existing_bridge_actors": len(bridge_actors),
		"will_create_bridge": not bridge_actors,
		"saved": False,
	}


def run(apply: bool = False) -> dict:
	result = plan()
	if not apply:
		return result

	import unreal

	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	level_actors = actors.get_all_level_actors()
	legacy = [actor for actor in level_actors if _is_legacy_instanced_actor(unreal, actor)]
	for actor in legacy:
		actors.destroy_actor(actor)

	if not any(_is_bridge_actor(unreal, actor) for actor in actors.get_all_level_actors()):
		bridge = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
		if bridge is None:
			raise RuntimeError("Could not create the preserved Han bridge actor")
		bridge.set_actor_label(BRIDGE_LABEL)
		bridge.set_folder_path("Han/Bridge")
		bridge.static_mesh_component.set_static_mesh(unreal.load_asset(BRIDGE_MESH))
		bridge.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

	return {**result, "removed_legacy_instanced_actors": len(legacy), "saved": False}
