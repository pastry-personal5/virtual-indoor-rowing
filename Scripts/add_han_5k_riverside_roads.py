"""Stage near-bank OSM roads and lawn fallbacks in the approved 5 km review map.

Roads are visual-only, non-colliding cube strips.  A source road segment is
eligible only when its midpoint lies within 200 m of the *outer* edge of an
authored legacy bank.  For each bank-side route segment without an eligible
road, a flat lawn reaches from that bank edge toward the nearest source
building on the same side.  This is review dressing, not traffic, navigation,
terrain, collision, or a runtime promotion.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

from add_han_5k_legacy_banks import BANK_HALF_WIDTH_CM, BANK_OFFSET_CM, BANK_SEGMENT_OVERLAP_CM, BANK_LABELS, _route_points
from add_han_5k_review_bridges import REVIEW_MAP
from han_area import project


ROAD_FOLDER = "Han/Route5K/OSMRoads"
LAWN_FOLDER = "Han/Route5K/RiversideLawns"
ROAD_MESH = "/Engine/BasicShapes/Cube"
ROAD_MATERIAL = "/Game/Phase2/HanRiver/Materials/M_Han_Concrete"
LAWN_MATERIAL = "/Game/Phase2/HanRiver/Materials/M_Han_Canopy"
ROAD_WIDTH_CM = 1_100.0
ROAD_HEIGHT_CM = 12.0
LAWN_HEIGHT_CM = 8.0
BANK_OUTER_EDGE_CM = BANK_OFFSET_CM + BANK_HALF_WIDTH_CM
ROAD_BANK_DISTANCE_CM = 20_000.0
# Cosmetic lawn only bridges a genuine near-bank gap.  A distant building must
# not create a kilometre-scale ground slab across the review water scene.
NEAR_BUILDING_MAX_DISTANCE_CM = BANK_OUTER_EDGE_CM + 12_000.0
ROAD_HIGHWAYS = {"motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential", "living_street", "service"}


def _tags(element: ET.Element) -> dict[str, str]:
	return {tag.attrib["k"]: tag.attrib["v"] for tag in element.findall("tag")}


def _source(source_path: str, origin_lon_lat: list[float]) -> tuple[dict[str, tuple[float, float]], list[tuple[str, list[tuple[float, float]]]], list[tuple[float, float]]]:
	root = ET.parse(source_path).getroot()
	nodes = {node.attrib["id"]: project((float(node.attrib["lon"]), float(node.attrib["lat"])), origin_lon_lat) for node in root.findall("node")}
	roads, buildings = [], []
	for way in root.findall("way"):
		properties = _tags(way)
		references = [node.attrib["ref"] for node in way.findall("nd")]
		points = [nodes[reference] for reference in references if reference in nodes]
		if properties.get("highway") in ROAD_HIGHWAYS and len(points) >= 2:
			roads.append((way.attrib["id"], points))
		if properties.get("building", "no") != "no" and len(points) >= 3:
			buildings.append((sum(point[0] for point in points) / len(points), sum(point[1] for point in points) / len(points)))
	return nodes, roads, buildings


def _distance_to_segment(point: tuple[float, float], start: tuple[float, float], end: tuple[float, float]) -> tuple[float, float]:
	delta = (end[0] - start[0], end[1] - start[1])
	length_sq = delta[0] * delta[0] + delta[1] * delta[1]
	if length_sq <= 0:
		return math.dist(point, start), 0.0
	fraction = max(0.0, min(1.0, ((point[0] - start[0]) * delta[0] + (point[1] - start[1]) * delta[1]) / length_sq))
	closest = (start[0] + fraction * delta[0], start[1] + fraction * delta[1])
	return math.dist(point, closest), fraction


def _road_plan(review_path: str, route_path: str, source_path: str) -> tuple[list[dict], list[dict]]:
	review = json.loads(Path(review_path).read_text())
	route = _route_points(route_path, review)
	_, roads, buildings = _source(source_path, review["origin_lon_lat"])
	# The source coordinates are metres; route coordinates are registered cm.
	from han_area import apply_registration, register
	fit = register(review["controls"], review["origin_lon_lat"])
	registered_roads = [(identifier, [apply_registration((100 * east, -100 * north), fit) for east, north in points]) for identifier, points in roads]
	registered_buildings = [apply_registration((100 * east, -100 * north), fit) for east, north in buildings]
	road_segments, covered = [], {label: set() for label in BANK_LABELS}
	for identifier, points in registered_roads:
		for index, (start, end) in enumerate(zip(points, points[1:])):
			length = math.dist(start, end)
			if length < 200.0:
				continue
			midpoint = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
			nearest = min((_distance_to_segment(midpoint, route_start, route_end)[0], route_index, route_start, route_end) for route_index, (route_start, route_end) in enumerate(zip(route, route[1:])))
			distance, route_index, route_start, route_end = nearest
			if abs(distance - BANK_OUTER_EDGE_CM) > ROAD_BANK_DISTANCE_CM:
				continue
			delta = (route_end[0] - route_start[0], route_end[1] - route_start[1])
			normal = (-delta[1] / math.dist(route_start, route_end), delta[0] / math.dist(route_start, route_end))
			side = 1 if (midpoint[0] - route_start[0]) * normal[0] + (midpoint[1] - route_start[1]) * normal[1] >= 0 else -1
			label = BANK_LABELS[1] if side > 0 else BANK_LABELS[0]
			covered[label].add(route_index)
			road_segments.append({"id": f"{identifier}-{index}", "side": label, "location": (midpoint[0], midpoint[1], ROAD_HEIGHT_CM / 2), "yaw": math.degrees(math.atan2(end[1] - start[1], end[0] - start[0])), "scale": (length / 100.0, ROAD_WIDTH_CM / 100.0, ROAD_HEIGHT_CM / 100.0)})
	lawns = []
	for route_index, (start, end) in enumerate(zip(route, route[1:])):
		length = math.dist(start, end)
		delta = ((end[0] - start[0]) / length, (end[1] - start[1]) / length)
		normal = (-delta[1], delta[0])
		midpoint = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
		for label, side in zip(BANK_LABELS, (-1.0, 1.0)):
			if route_index in covered[label]:
				continue
			candidates = []
			for building in registered_buildings:
				along = (building[0] - start[0]) * delta[0] + (building[1] - start[1]) * delta[1]
				radius = side * ((building[0] - start[0]) * normal[0] + (building[1] - start[1]) * normal[1])
				if -5_000 <= along <= length + 5_000 and BANK_OUTER_EDGE_CM + 2_000 <= radius <= NEAR_BUILDING_MAX_DISTANCE_CM:
					candidates.append((radius, building))
			if not candidates:
				continue
			radius, _ = min(candidates)
			width = radius - BANK_OUTER_EDGE_CM
			lawn_center = BANK_OUTER_EDGE_CM + width / 2
			lawns.append({"id": f"{label.rsplit('_', 1)[-1]}-{route_index + 1:02d}", "location": (midpoint[0] + side * normal[0] * lawn_center, midpoint[1] + side * normal[1] * lawn_center, LAWN_HEIGHT_CM / 2), "yaw": math.degrees(math.atan2(delta[1], delta[0])), "scale": ((length + BANK_SEGMENT_OVERLAP_CM) / 100.0, width / 100.0, LAWN_HEIGHT_CM / 100.0)})
	return road_segments, lawns


def plan(review_path: str, route_path: str, source_path: str) -> dict:
	roads, lawns = _road_plan(review_path, route_path, source_path)
	return {"review_map": REVIEW_MAP, "road_segments": len(roads), "lawn_fields": len(lawns), "road_bank_distance_m": ROAD_BANK_DISTANCE_CM / 100.0, "saved": False}


def run(review_path: str, route_path: str, source_path: str, apply: bool = False) -> dict:
	result = plan(review_path, route_path, source_path)
	if not apply:
		return result
	import unreal
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	if world.get_path_name().split(".")[0] != REVIEW_MAP:
		raise ValueError(f"Open {REVIEW_MAP} through Unreal MCP first")
	roads, lawns = _road_plan(review_path, route_path, source_path)
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	for actor in list(actors.get_all_level_actors()):
		if str(actor.get_folder_path()) in (ROAD_FOLDER, LAWN_FOLDER):
			actors.destroy_actor(actor)
	mesh = unreal.load_asset(ROAD_MESH)
	road_material, lawn_material = unreal.load_asset(ROAD_MATERIAL), unreal.load_asset(LAWN_MATERIAL)
	if not isinstance(mesh, unreal.StaticMesh) or not isinstance(road_material, unreal.MaterialInterface) or not isinstance(lawn_material, unreal.MaterialInterface):
		raise RuntimeError("Required review primitive mesh/material is unavailable")
	created = []
	try:
		for kind, entries, folder, material, prefix in (("road", roads, ROAD_FOLDER, road_material, "Han_5K_OSMRoad"), ("lawn", lawns, LAWN_FOLDER, lawn_material, "Han_5K_RiversideLawn")):
			for entry in entries:
				actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*entry["location"]), unreal.Rotator(0, entry["yaw"], 0))
				if actor is None:
					raise RuntimeError(f"Could not create {kind} primitive")
				created.append(actor)
				actor.set_actor_label(f"{prefix}_{entry['id']}")
				actor.set_folder_path(folder)
				actor.set_actor_scale3d(unreal.Vector(*entry["scale"]))
				actor.static_mesh_component.set_static_mesh(mesh)
				actor.static_mesh_component.set_material(0, material)
				actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	except Exception:
		for actor in reversed(created):
			actors.destroy_actor(actor)
		raise
	return {**result, "created_roads": len(roads), "created_lawns": len(lawns), "saved": False}
