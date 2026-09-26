"""Refine Route5K legacy-bank continuity and add review-only bank dressing.

Uses the existing two HISM bank actors for continuous overlapping bank spans.
Trees and stepped quays use deliberately simple, original, non-colliding static
primitives until a dedicated instanced-dressing asset pass is approved.
"""

from __future__ import annotations

import json
from pathlib import Path

from add_han_5k_legacy_banks import BANK_LABELS, BANK_MATERIAL, BANK_FOLDER, _bank_transforms, _route_points, _stair_transforms, _tree_transforms
from add_han_5k_review_bridges import REVIEW_MAP


DRESSING_FOLDER = "Han/Route5K/BankDressing"


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def _actor(unreal, actors, label: str):
	matching = [actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == label]
	if len(matching) != 1:
		raise ValueError(f"Expected exactly one actor named {label}")
	return matching[0]


def _primitive(unreal, actors, created: list, label: str, location, yaw: float, scale, mesh_path: str, material_path: str) -> None:
	actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0))
	if actor is None:
		raise RuntimeError(f"Could not create bank dressing {label}")
	created.append(actor)
	actor.set_actor_label(label)
	actor.set_folder_path(DRESSING_FOLDER)
	actor.set_actor_scale3d(unreal.Vector(*scale))
	actor.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
	actor.static_mesh_component.set_material(0, unreal.load_asset(material_path))
	actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)


def run(review_path: str, route_path: str, apply: bool = False, replace: bool = False) -> dict:
	review = json.loads(Path(review_path).read_text())
	points = _route_points(route_path, review)
	banks, trees, stairs = _bank_transforms(points), _tree_transforms(points), _stair_transforms(points)
	result = {"bank_instances": {label: len(items) for label, items in banks.items()}, "tree_primitives": {label: len(items) for label, items in trees.items()}, "stair_primitives": {label: len(items) for label, items in stairs.items()}, "saved": False}
	if not apply:
		return result
	import unreal

	if _current_map(unreal) != REVIEW_MAP:
		raise ValueError(f"Open {REVIEW_MAP} through Unreal MCP first")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	existing = {actor.get_actor_label() for actor in actors.get_all_level_actors()}
	if any(label.startswith("Han_5K_BankTree_") or label.startswith("Han_5K_BankStair_") for label in existing) and not replace:
		raise ValueError("Existing bank dressing must be reviewed before replacement")
	if replace:
		for actor in list(actors.get_all_level_actors()):
			if str(actor.get_folder_path()) == DRESSING_FOLDER:
				actors.destroy_actor(actor)
	for label, transforms in banks.items():
		actor = _actor(unreal, actors, label)
		if str(actor.get_folder_path()) != BANK_FOLDER:
			raise ValueError(f"Bank actor {label} is outside {BANK_FOLDER}")
		components = actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
		if len(components) != 1:
			raise ValueError(f"Bank actor {label} has no unique HISM component")
		component = components[0]
		component.clear_instances()
		component.add_instances([unreal.Transform(unreal.Vector(*location), unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0), unreal.Vector(*scale)) for location, yaw, scale in transforms], False)
	created = []
	try:
		for label, transforms in trees.items():
			side = "Port" if label == BANK_LABELS[0] else "Starboard"
			for index, (location, yaw, scale) in enumerate(transforms, 1):
				_primitive(unreal, actors, created, f"Han_5K_BankTree_{side}_{index:02d}", location, yaw, scale, "/Engine/BasicShapes/Sphere", "/Game/Phase2/HanRiver/Materials/M_Han_Canopy")
		for label, transforms in stairs.items():
			side = "Port" if label == BANK_LABELS[0] else "Starboard"
			for index, (location, yaw, scale) in enumerate(transforms, 1):
				_primitive(unreal, actors, created, f"Han_5K_BankStair_{side}_{index:02d}", location, yaw, scale, "/Engine/BasicShapes/Cube", "/Game/Phase2/HanRiver/Materials/M_Han_Concrete")
	except Exception:
		for actor in reversed(created):
			actors.destroy_actor(actor)
		raise
	return {**result, "created_dressing": len(created), "saved": False}
