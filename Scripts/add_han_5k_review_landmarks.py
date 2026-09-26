"""Place the two non-bridge landmark assemblies missing from the 5 km review map.

The four bridge beats are represented by the dedicated bridge actors.  This
script supplies only restrained, original primitive assemblies for Some Sevit
and Nodeulseom; it creates neither an exact landmark replica nor collision.
Run only through Unreal MCP with the approved review map open.
"""

from __future__ import annotations

import json
from pathlib import Path

from han_area import apply_registration, project, register


REVIEW_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_5K_Review"
LANDMARK_FOLDER = "Han/Route5K/Landmarks"
LANDMARK_LABELS = {
	"sebit-lookback": "Han_5K_Landmark_sebit-lookback",
	"nodeulseom": "Han_5K_Landmark_nodeulseom",
}
BRIDGE_LANDMARK_LABELS = (
	"Han_5K_Bridge_banpo-start",
	"Han_5K_Bridge_dongjak-span",
	"Han_5K_Bridge_hangang-bridge",
	"Han_5K_Bridge_wonhyo-finish",
)


def _current_map(unreal) -> str:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	return world.get_path_name().split(".")[0]


def plan(review_path: str, beats_path: str) -> dict:
	review = json.loads(Path(review_path).read_text())
	beats = {beat["id"]: beat for beat in json.loads(Path(beats_path).read_text())["beats"]}
	fit = register(review["controls"], review["origin_lon_lat"])
	locations = {}
	for beat_id in LANDMARK_LABELS:
		easting, northing = project(beats[beat_id]["coordinate_wgs84"], review["origin_lon_lat"])
		locations[beat_id] = [*apply_registration((100 * easting, -100 * northing), fit), 0.0]
	return {
		"review_map": REVIEW_MAP,
		"landmark_labels": LANDMARK_LABELS,
		"bridge_landmark_labels": list(BRIDGE_LANDMARK_LABELS),
		"locations_cm": locations,
		"saved": False,
	}


def _primitive(unreal, actors, created: list, label: str, location, scale, mesh_path: str, material_path: str) -> None:
	"""Create a supported static primitive without collision or runtime behavior."""
	actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator())
	if actor is None:
		raise RuntimeError(f"Could not create landmark primitive {label}")
	created.append(actor)
	actor.set_actor_label(label)
	actor.set_folder_path(LANDMARK_FOLDER)
	actor.set_actor_scale3d(unreal.Vector(*scale))
	actor.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
	actor.static_mesh_component.set_material(0, unreal.load_asset(material_path))
	actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)


def run(review_path: str, beats_path: str, apply: bool = False) -> dict:
	result = plan(review_path, beats_path)
	if not apply:
		return result
	import unreal

	if _current_map(unreal) != REVIEW_MAP:
		raise ValueError(f"Open {REVIEW_MAP} through Unreal MCP first")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	existing = {actor.get_actor_label() for actor in actors.get_all_level_actors()}
	if not set(BRIDGE_LANDMARK_LABELS) <= existing:
		raise ValueError("All four bridge landmark actors must exist before landmark completion")
	created = []
	try:
		for beat_id, label in LANDMARK_LABELS.items():
			if label in existing:
				continue
			anchor = result["locations_cm"][beat_id]
			if beat_id == "sebit-lookback":
				# Three original island-side volumes, intentionally not a replica.
				for index, (x, y) in enumerate(((-3300, -24000), (0, -25500), (3600, -24000))):
					primitive_label = label if index == 0 else f"{label}_pavilion_{index + 1}"
					_primitive(unreal, actors, created, primitive_label, (anchor[0] + x, anchor[1] + y, 820), (18, 14, 11), "/Engine/BasicShapes/Cylinder", "/Game/Phase2/HanRiver/Materials/M_Han_Glass")
			else:
				# Low island canopy and a subdued venue glow, clear of the waterline.
				for index, (x, y, z) in enumerate(((-7000, 24500, 950), (-3800, 26000, 1050), (0, 24700, 900), (3600, 26300, 1100), (7100, 25000, 980))):
					primitive_label = label if index == 0 else f"{label}_canopy_{index + 1}"
					_primitive(unreal, actors, created, primitive_label, (anchor[0] + x, anchor[1] + y, z), (8, 7, 9), "/Engine/BasicShapes/Sphere", "/Game/Phase2/HanRiver/Materials/M_Han_Canopy")
				_primitive(unreal, actors, created, f"{label}_venue", (anchor[0], anchor[1] + 25800, 520), (34, 12, 6), "/Engine/BasicShapes/Cube", "/Game/Phase2/HanRiver/Materials/M_Han_Concrete")
				_primitive(unreal, actors, created, f"{label}_glow", (anchor[0], anchor[1] + 24500, 1080), (20, 0.5, 0.35), "/Engine/BasicShapes/Cube", "/Game/Phase2/HanRiver/Materials/M_Han_WarmLight")
	except Exception:
		for actor in reversed(created):
			actors.destroy_actor(actor)
		raise
	return {**result, "created_landmark_actors": len(created), "saved": False}
