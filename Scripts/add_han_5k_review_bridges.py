"""Place the authored bridge span at each Han 5K bridge beat in the review map."""

from __future__ import annotations

import json
import math
from pathlib import Path

from han_area import apply_registration, project, register


REVIEW_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_5K_Review"
BRIDGE_MESH = "/Game/Phase2/HanRiver/Meshes/SM_SM_Han_BridgeSpan"
BRIDGE_FOLDER = "Han/Bridge"
BRIDGE_BEATS = ("banpo-start", "dongjak-span", "hangang-bridge", "wonhyo-finish")


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def _route_yaw_deg(route_coordinates: list[list[float]], index: int, origin: list[float]) -> float:
	"""Aim the mesh's local Y span across the local course tangent."""
	previous = route_coordinates[max(0, index - 1)]
	next_point = route_coordinates[min(len(route_coordinates) - 1, index + 1)]
	previous_east, previous_north = project(previous, origin)
	next_east, next_north = project(next_point, origin)
	return math.degrees(math.atan2(-(next_north - previous_north), next_east - previous_east))


def place(review_path: str, beats_path: str, route_path: str) -> dict:
	"""Add any missing named bridge-beat actors; leave the review level unsaved."""
	import unreal

	if _current_map(unreal) != REVIEW_MAP:
		raise ValueError(f"Open {REVIEW_MAP} through Unreal MCP first")
	review = json.loads(Path(review_path).read_text())
	beats = json.loads(Path(beats_path).read_text())["beats"]
	route_coordinates = json.loads(Path(route_path).read_text())["features"][0]["geometry"]["coordinates"]
	fit = register(review["controls"], review["origin_lon_lat"])
	mesh = unreal.load_asset(BRIDGE_MESH)
	if not isinstance(mesh, unreal.StaticMesh):
		raise RuntimeError(f"Required bridge mesh is unavailable: {BRIDGE_MESH}")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	existing_labels = {actor.get_actor_label() for actor in actors.get_all_level_actors()}
	created = []
	for beat in beats:
		if beat["id"] not in BRIDGE_BEATS:
			continue
		label = f"Han_5K_Bridge_{beat['id']}"
		if label in existing_labels:
			continue
		easting, northing = project(beat["coordinate_wgs84"], review["origin_lon_lat"])
		location = apply_registration((100 * easting, -100 * northing), fit)
		route_index = min(range(len(route_coordinates)), key=lambda index: sum((route_coordinates[index][axis] - beat["coordinate_wgs84"][axis]) ** 2 for axis in range(2)))
		yaw_deg = _route_yaw_deg(route_coordinates, route_index, review["origin_lon_lat"])
		actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(location[0], location[1], 0), unreal.Rotator(0, yaw_deg, 0))
		if actor is None:
			for created_actor in reversed(created):
				actors.destroy_actor(created_actor)
			raise RuntimeError(f"Could not create bridge actor for {beat['id']}")
		created.append(actor)
		actor.set_actor_label(label)
		actor.set_folder_path(BRIDGE_FOLDER)
		actor.static_mesh_component.set_static_mesh(mesh)
		actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	return {"created_bridge_actors": len(created), "bridge_beats": list(BRIDGE_BEATS), "saved": False}
