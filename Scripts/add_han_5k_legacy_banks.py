"""Place the full-length legacy bank HISM primitives and all bridge beats.

Run only through Unreal MCP with the approved 5 km review map open.  The
operation uses the reviewed similarity registration, keeps the rowing channel
open, adds no collision, and intentionally leaves the level unsaved for visual
inspection.  It is idempotent: a second run reports the existing bank groups
and bridge actors rather than duplicating them.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

from add_han_5k_review_bridges import BRIDGE_BEATS, REVIEW_MAP, place
from han_area import apply_registration, project, register


BANK_FOLDER = "Han/Route5K/LegacyBanks"
BANK_LABELS = ("Han_5K_LegacyBank_Port", "Han_5K_LegacyBank_Starboard")
BANK_MESH = "/Engine/BasicShapes/Cube"
BANK_MATERIAL = "/Game/Phase2/HanRiver/Materials/M_Han_Bank"
# Preserve the representative map's bank silhouette while keeping a 210 m
# visual rowing channel clear of every non-colliding bank primitive.
BANK_OFFSET_CM = 25_500.0
BANK_HALF_WIDTH_CM = 9_000.0
BANK_HEIGHT_CM = 200.0
BANK_CENTER_Z_CM = 90.0


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def _route_points(route_path: str, review: dict) -> list[tuple[float, float]]:
	route = json.loads(Path(route_path).read_text())["features"][0]["geometry"]["coordinates"]
	if len(route) < 2:
		raise ValueError("The Han 5 km waterline needs at least two route coordinates")
	fit = register(review["controls"], review["origin_lon_lat"])
	return [apply_registration((100 * east, -100 * north), fit) for east, north in (project(point, review["origin_lon_lat"]) for point in route)]


def _bank_transforms(route_points: list[tuple[float, float]]) -> dict[str, list[tuple[tuple[float, float, float], float, tuple[float, float, float]]]]:
	groups = {label: [] for label in BANK_LABELS}
	for start, end in zip(route_points, route_points[1:]):
		delta_x, delta_y = end[0] - start[0], end[1] - start[1]
		length_cm = math.hypot(delta_x, delta_y)
		if length_cm <= 0:
			raise ValueError("The Han 5 km waterline contains duplicate route coordinates")
		direction_x, direction_y = delta_x / length_cm, delta_y / length_cm
		normal_x, normal_y = -direction_y, direction_x
		mid_x, mid_y = (start[0] + end[0]) / 2, (start[1] + end[1]) / 2
		yaw_deg = math.degrees(math.atan2(delta_y, delta_x))
		for label, side in zip(BANK_LABELS, (-1.0, 1.0)):
			groups[label].append(((mid_x + side * normal_x * BANK_OFFSET_CM, mid_y + side * normal_y * BANK_OFFSET_CM, BANK_CENTER_Z_CM), yaw_deg, (length_cm / 100.0, BANK_HALF_WIDTH_CM / 100.0, BANK_HEIGHT_CM / 100.0)))
	return groups


def _legacy_bank_actor(unreal, actor, label: str) -> bool:
	if actor.get_actor_label() != label or str(actor.get_folder_path()) != BANK_FOLDER:
		return False
	components = actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
	return len(components) == 1


def plan(review_path: str, route_path: str) -> dict:
	import unreal

	if _current_map(unreal) != REVIEW_MAP:
		raise ValueError(f"Open {REVIEW_MAP} through Unreal MCP first")
	review = json.loads(Path(review_path).read_text())
	groups = _bank_transforms(_route_points(route_path, review))
	mesh = unreal.load_asset(BANK_MESH)
	material = unreal.load_asset(BANK_MATERIAL)
	if not isinstance(mesh, unreal.StaticMesh) or not isinstance(material, unreal.MaterialInterface):
		raise RuntimeError("The required legacy bank mesh or material is unavailable")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
	bank_actors = {label: [actor for actor in actors if _legacy_bank_actor(unreal, actor, label)] for label in BANK_LABELS}
	duplicates = [label for label, matching in bank_actors.items() if len(matching) > 1]
	if duplicates:
		raise ValueError("Duplicate legacy bank HISM actors: " + ", ".join(duplicates))
	bridge_labels = {actor.get_actor_label() for actor in actors}
	missing_bridges = [f"Han_5K_Bridge_{beat}" for beat in BRIDGE_BEATS if f"Han_5K_Bridge_{beat}" not in bridge_labels]
	return {
		"review_map": REVIEW_MAP,
		"bank_groups": {label: len(transforms) for label, transforms in groups.items()},
		"existing_bank_groups": [label for label, matching in bank_actors.items() if matching],
		"missing_bridge_actors": missing_bridges,
		"saved": False,
	}


def run(review_path: str, route_path: str, beats_path: str, apply: bool = False) -> dict:
	result = plan(review_path, route_path)
	if not apply:
		return result

	import unreal

	review = json.loads(Path(review_path).read_text())
	groups = _bank_transforms(_route_points(route_path, review))
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	mesh = unreal.load_asset(BANK_MESH)
	material = unreal.load_asset(BANK_MATERIAL)
	created = []
	try:
		for label, transforms in groups.items():
			if any(_legacy_bank_actor(unreal, actor, label) for actor in actors.get_all_level_actors()):
				continue
			actor = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
			if actor is None:
				raise RuntimeError(f"Could not create legacy bank group {label}")
			created.append(actor)
			actor.set_actor_label(label)
			actor.set_folder_path(BANK_FOLDER)
			component = actor.add_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent, False, unreal.Transform(), False)
			component.set_static_mesh(mesh)
			component.set_material(0, material)
			component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
			component.set_mobility(unreal.ComponentMobility.STATIC)
			component.add_instances([unreal.Transform(unreal.Vector(*location), unreal.Rotator(0, yaw, 0), unreal.Vector(*scale)) for location, yaw, scale in transforms], False)
		bridge_result = place(review_path, beats_path, route_path)
	except Exception:
		for actor in reversed(created):
			actors.destroy_actor(actor)
		raise
	return {**result, "created_bank_groups": len(created), "created_bridge_actors": bridge_result["created_bridge_actors"], "saved": False}
