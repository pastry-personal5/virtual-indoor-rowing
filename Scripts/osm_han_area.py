"""Generate reviewed, local OSM building and water tiles for one Unreal course area.

This program can acquire a bounded OSM snapshot, but never invokes Unreal. It turns an
explicitly reviewed .osm snapshot into deterministic OBJ tiles and a manifest that the
Unreal editor-side staging script can import. It supports closed ways and building/water
multipolygon relations with stitched outer and inner way rings. Terrain, bridges, road
meshes, interiors, and unverified elevation remain outside this flow.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
from datetime import datetime, timezone
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET

from han_area import AREA_ID, BBOX, finite_values, project, sha256, unproject


OBJ_SCHEMA_VERSION = 1
EPSILON = 1e-8
OVERPASS_ENDPOINT = "https://overpass-api.de/api/interpreter"
MAX_LIVE_SNAPSHOT_BYTES = 50 * 1024 * 1024
MAX_RELATION_VERTICES = 5000


def tags(element: ET.Element) -> dict[str, str]:
	return {tag.attrib["k"]: tag.attrib["v"] for tag in element.findall("tag")}


def parse_meters(value: str | None) -> float | None:
	if value is None:
		return None
	match = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(?:m|metres?|meters?)?\s*", value, re.IGNORECASE)
	if not match:
		return None
	meters = float(match.group(1))
	return meters if meters > 0 else None


def building_vertical_extent(properties: dict[str, str], default_level_height_m: float) -> tuple[float, float, str]:
	base = parse_meters(properties.get("min_height"))
	base_source = "min_height"
	if base is None:
		min_level = parse_meters(properties.get("building:min_level"))
		base = min_level * default_level_height_m if min_level is not None else 0.0
		base_source = "building:min_level" if min_level is not None else "ground"
	top = parse_meters(properties.get("height"))
	if top is not None:
		if top <= base:
			raise ValueError("Building height is not above its base elevation")
		return base, top, f"{base_source}+height"
	levels = parse_meters(properties.get("building:levels"))
	if levels is not None:
		return base, base + levels * default_level_height_m, f"{base_source}+building:levels"
	return base, base + default_level_height_m, f"{base_source}+reviewed_default_level"


def signed_area(points: list[tuple[float, float]]) -> float:
	return sum(start[0] * end[1] - end[0] * start[1] for start, end in zip(points, points[1:] + points[:1])) / 2


def cross(origin, first, second) -> float:
	return (first[0] - origin[0]) * (second[1] - origin[1]) - (first[1] - origin[1]) * (second[0] - origin[0])


def point_in_triangle(point, first, second, third) -> bool:
	first_cross = cross(first, second, point)
	second_cross = cross(second, third, point)
	third_cross = cross(third, first, point)
	return (first_cross >= -EPSILON and second_cross >= -EPSILON and third_cross >= -EPSILON) or (first_cross <= EPSILON and second_cross <= EPSILON and third_cross <= EPSILON)


def triangulate(points: list[tuple[float, float]]) -> list[tuple[int, int, int]]:
	if len(points) < 3 or abs(signed_area(points)) < EPSILON:
		raise ValueError("Footprint is degenerate")
	indices = list(range(len(points)))
	if signed_area(points) < 0:
		indices.reverse()
	triangles = []
	while len(indices) > 3:
		for index, current in enumerate(indices):
			previous = indices[index - 1]
			next_index = indices[(index + 1) % len(indices)]
			if cross(points[previous], points[current], points[next_index]) <= EPSILON:
				continue
			if any(point_in_triangle(points[candidate], points[previous], points[current], points[next_index]) for candidate in indices if candidate not in (previous, current, next_index)):
				continue
			triangles.append((previous, current, next_index))
			del indices[index]
			break
		else:
			raise ValueError("Footprint is self-intersecting or cannot be triangulated")
	triangles.append(tuple(indices))
	return triangles


def tile_key(point: tuple[float, float], size_m: float) -> tuple[int, int]:
	return (math.floor(point[0] / size_m), math.floor(point[1] / size_m))


def water_polygon(properties: dict[str, str]) -> bool:
	return properties.get("natural") == "water" or properties.get("waterway") == "riverbank" or properties.get("water") not in (None, "no") or properties.get("landuse") in ("basin", "reservoir")


def feature_kind(properties: dict[str, str]) -> str | None:
	if any(properties.get(key, "no") != "no" for key in ("building", "building:part")):
		return "roof" if properties.get("building") == "roof" or properties.get("building:part") == "roof" else "building"
	return "water" if water_polygon(properties) else None


def ring_edges(points):
	return list(zip(points, points[1:] + points[:1]))


def point_on_segment(point, start, end) -> bool:
	return abs(cross(start, end, point)) <= EPSILON and min(start[0], end[0]) - EPSILON <= point[0] <= max(start[0], end[0]) + EPSILON and min(start[1], end[1]) - EPSILON <= point[1] <= max(start[1], end[1]) + EPSILON


def segments_intersect(first, second, third, fourth) -> bool:
	if max(first[0], second[0]) + EPSILON < min(third[0], fourth[0]) or max(third[0], fourth[0]) + EPSILON < min(first[0], second[0]):
		return False
	if max(first[1], second[1]) + EPSILON < min(third[1], fourth[1]) or max(third[1], fourth[1]) + EPSILON < min(first[1], second[1]):
		return False
	a, b = cross(first, second, third), cross(first, second, fourth)
	c, d = cross(third, fourth, first), cross(third, fourth, second)
	if (a > EPSILON and b < -EPSILON or a < -EPSILON and b > EPSILON) and (c > EPSILON and d < -EPSILON or c < -EPSILON and d > EPSILON):
		return True
	return point_on_segment(third, first, second) or point_on_segment(fourth, first, second) or point_on_segment(first, third, fourth) or point_on_segment(second, third, fourth)


def validate_ring(points: list[tuple[float, float]]) -> None:
	if len(points) < 3 or abs(signed_area(points)) < EPSILON:
		raise ValueError("Relation ring is degenerate")
	edges = ring_edges(points)
	if any(start == end for start, end in edges):
		raise ValueError("Relation ring has a zero-length edge")
	for index, (start, end) in enumerate(edges):
		for other_index in range(index + 1, len(edges)):
			if other_index == index + 1 or index == 0 and other_index == len(edges) - 1:
				continue
			if segments_intersect(start, end, *edges[other_index]):
				raise ValueError("Relation ring is self-intersecting")


def point_in_ring(point, ring) -> bool:
	inside = False
	for start, end in ring_edges(ring):
		if point_on_segment(point, start, end):
			return False
		if (start[1] > point[1]) != (end[1] > point[1]):
			x_crossing = start[0] + (point[1] - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
			if x_crossing > point[0]:
				inside = not inside
	return inside


def rings_intersect(first, second) -> bool:
	return any(segments_intersect(a, b, c, d) for a, b in ring_edges(first) for c, d in ring_edges(second))


def stitch_rings(members: list[tuple[str, list[str]]]) -> list[list[str]]:
	remaining = {identifier: references for identifier, references in members}
	if len(remaining) != len(members):
		raise ValueError("Relation repeats a way member")
	rings = []
	while remaining:
		identifier = min(remaining)
		ring = remaining.pop(identifier).copy()
		if len(ring) < 2:
			raise ValueError("Relation member way has too few nodes")
		while ring[0] != ring[-1]:
			matches = [(candidate, references) for candidate, references in remaining.items() if references and ring[-1] in (references[0], references[-1])]
			if len(matches) != 1:
				raise ValueError("Relation ring has a gap or branch")
			candidate, references = matches[0]
			if references[-1] == ring[-1]:
				references = list(reversed(references))
			ring.extend(references[1:])
			del remaining[candidate]
		if len(ring) < 4:
			raise ValueError("Relation ring has too few nodes")
		rings.append(ring)
	return rings


def triangulate_with_holes(outer, holes) -> list[tuple[tuple[float, float], tuple[float, float], tuple[float, float]]]:
	"""Tessellate horizontal slabs by even/odd crossings, preserving inner voids."""
	rings = [outer, *holes]
	edges = [edge for ring in rings for edge in ring_edges(ring)]
	x_values = sorted({point[0] for ring in rings for point in ring})
	triangles = []
	for left, right in zip(x_values, x_values[1:]):
		middle = (left + right) / 2
		crossings = []
		for start, end in edges:
			if min(start[0], end[0]) < middle < max(start[0], end[0]):
				def y_at(x):
					return start[1] + (x - start[0]) * (end[1] - start[1]) / (end[0] - start[0])
				crossings.append((y_at(middle), y_at(left), y_at(right)))
		crossings.sort()
		if len(crossings) % 2:
			raise ValueError("Relation has unpaired polygon crossings")
		for lower, upper in zip(crossings[::2], crossings[1::2]):
			quad = ((left, lower[1]), (right, lower[2]), (right, upper[2]), (left, upper[1]))
			for triangle in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
				if abs(cross(*triangle)) > EPSILON:
					triangles.append(triangle)
	expected = abs(signed_area(outer)) - sum(abs(signed_area(hole)) for hole in holes)
	actual = sum(abs(cross(*triangle)) / 2 for triangle in triangles)
	if expected <= EPSILON or not math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-4):
		raise ValueError("Relation tessellation does not preserve polygon area")
	return triangles


def relation_polygons(relation: ET.Element, ways: dict[str, ET.Element], nodes: dict, origin_lon_lat) -> list[tuple[list[tuple[float, float]], list[list[tuple[float, float]]]]]:
	member_ids = {"outer": [], "inner": []}
	for member in relation.findall("member"):
		if member.attrib.get("type") == "relation":
			raise ValueError("Nested relation members are unsupported")
		if member.attrib.get("type") != "way":
			continue
		role = member.attrib.get("role", "") or "outer"
		if role not in member_ids:
			raise ValueError("Relation has an unsupported way role")
		identifier = member.attrib["ref"]
		if identifier not in ways:
			raise ValueError("Relation member way is missing")
		member_ids[role].append((identifier, [node.attrib["ref"] for node in ways[identifier].findall("nd")]))
	if not member_ids["outer"]:
		raise ValueError("Relation has no outer ring")
	if sum(len(references) for members in member_ids.values() for _, references in members) > 2 * MAX_RELATION_VERTICES:
		raise ValueError("Relation exceeds the geometry budget")
	reference_rings = {role: stitch_rings(members) for role, members in member_ids.items()}
	if sum(len(ring) - 1 for rings in reference_rings.values() for ring in rings) > MAX_RELATION_VERTICES:
		raise ValueError("Relation exceeds the geometry budget")
	projected = {}
	for role, rings in reference_rings.items():
		projected[role] = []
		for ring in rings:
			if any(reference not in nodes for reference in ring):
				raise ValueError("Relation member node is missing")
			points = [project(nodes[reference], origin_lon_lat) for reference in ring[:-1]]
			validate_ring(points)
			projected[role].append(points)
	outers, inners = projected["outer"], projected["inner"]
	for index, outer in enumerate(outers):
		for other in outers[index + 1:]:
			if rings_intersect(outer, other) or point_in_ring(outer[0], other) or point_in_ring(other[0], outer):
				raise ValueError("Relation outer rings overlap")
	for index, inner in enumerate(inners):
		for other in inners[index + 1:]:
			if rings_intersect(inner, other) or point_in_ring(inner[0], other) or point_in_ring(other[0], inner):
				raise ValueError("Relation inner rings overlap")
	assignments = [[] for _ in outers]
	for inner in inners:
		owners = [index for index, outer in enumerate(outers) if not rings_intersect(inner, outer) and point_in_ring(inner[0], outer)]
		if len(owners) != 1:
			raise ValueError("Relation inner ring is not inside exactly one outer ring")
		assignments[owners[0]].append(inner)
	polygons = list(zip(outers, assignments))
	for outer, holes in polygons:
		triangulate_with_holes(outer, holes)
	return polygons


def read_features(osm_path: Path, origin_lon_lat, default_level_height_m: float) -> tuple[list[dict], dict]:
	root = ET.parse(osm_path).getroot()
	nodes = {node.attrib["id"]: (float(node.attrib["lon"]), float(node.attrib["lat"])) for node in root.findall("node")}
	ways = {way.attrib["id"]: way for way in root.findall("way")}
	features = []
	skipped = {"open": 0, "missing_node": 0, "invalid_polygon": 0, "invalid_vertical_extent": 0, "roof_only": 0, "nonfeature": 0, "invalid_relation": 0, "relation_member": 0}

	def make_feature(identifier, kind, properties, footprint, holes=None):
		if kind == "building":
			base_m, top_m, height_source = building_vertical_extent(properties, default_level_height_m)
			feature_tags = ("building", "building:part", "height", "min_height", "building:levels", "building:min_level", "name", "name:en")
		else:
			base_m = top_m = 0.0
			height_source = "static_water_surface"
			feature_tags = ("natural", "water", "waterway", "landuse", "name", "name:en")
		centroid = (sum(point[0] for point in footprint) / len(footprint), sum(point[1] for point in footprint) / len(footprint))
		feature = {"id": identifier, "kind": kind, "footprint_m": footprint, "base_m": base_m, "top_m": top_m, "height_source": height_source, "tags": {key: properties[key] for key in feature_tags if key in properties}, "centroid_m": centroid}
		if holes is not None:
			feature["holes_m"] = holes
		return feature

	consumed_ways = set()
	for relation in root.findall("relation"):
		properties = tags(relation)
		if properties.get("type") != "multipolygon":
			continue
		kind = feature_kind(properties)
		member_references = {member.attrib["ref"] for member in relation.findall("member") if member.attrib.get("type") == "way"}
		# Older OSM multipolygons sometimes put feature tags on an outer
		# member rather than the relation. All present classes must agree.
		tagged_outers = []
		for member in relation.findall("member"):
			identifier = member.attrib.get("ref")
			if member.attrib.get("type") != "way" or member.attrib.get("role", "") not in ("", "outer") or identifier not in ways:
				continue
			outer_tags = tags(ways[identifier])
			if feature_kind(outer_tags) is not None:
				tagged_outers.append((identifier, outer_tags))
		kinds = {feature_kind(outer_tags) for _, outer_tags in tagged_outers}
		if len(kinds) > 1 or (kind is not None and any(candidate != kind for candidate in kinds)):
			consumed_ways.update(member_references)
			skipped["invalid_relation"] += 1
			continue
		if kind is None and kinds:
			properties = {**min(tagged_outers, key=lambda item: item[0])[1], **properties}
			kind = feature_kind(properties)
		if kind is None:
			continue
		# Never turn a broken relation's tagged outer member into a filled
		# standalone way: that would silently paint over its intended holes.
		consumed_ways.update(member_references)
		if kind == "roof":
			skipped["roof_only"] += 1
			continue
		try:
			polygons = relation_polygons(relation, ways, nodes, origin_lon_lat)
			relation_features = [make_feature(f"relation/{relation.attrib['id']}/outer/{index}", kind, properties, outer, holes) for index, (outer, holes) in enumerate(polygons)]
		except ValueError:
			skipped["invalid_relation"] += 1
			continue
		features.extend(relation_features)

	for way in ways.values():
		identifier = way.attrib["id"]
		if identifier in consumed_ways:
			skipped["relation_member"] += 1
			continue
		properties = tags(way)
		kind = feature_kind(properties)
		if kind is None:
			skipped["nonfeature"] += 1
			continue
		if kind == "roof":
			skipped["roof_only"] += 1
			continue
		references = [node.attrib["ref"] for node in way.findall("nd")]
		if len(references) < 4 or references[0] != references[-1]:
			skipped["open"] += 1
			continue
		if any(reference not in nodes for reference in references):
			skipped["missing_node"] += 1
			continue
		try:
			footprint = [project(nodes[reference], origin_lon_lat) for reference in references[:-1]]
			triangulate(footprint)
		except ValueError:
			skipped["invalid_polygon"] += 1
			continue
		try:
			features.append(make_feature(f"way/{identifier}", kind, properties, footprint))
		except ValueError:
			skipped["invalid_vertical_extent"] += 1
	return features, skipped


def overpass_query() -> str:
	west, south, east, north = BBOX
	return f"""[out:xml][timeout:60];
(
  way[\"building\"]({south},{west},{north},{east});
  way[\"building:part\"]({south},{west},{north},{east});
  way[\"natural\"=\"water\"]({south},{west},{north},{east});
  way[\"water\"]({south},{west},{north},{east});
  way[\"waterway\"=\"riverbank\"]({south},{west},{north},{east});
  way[\"landuse\"=\"basin\"]({south},{west},{north},{east});
  way[\"landuse\"=\"reservoir\"]({south},{west},{north},{east});
  relation[\"type\"=\"multipolygon\"]({south},{west},{north},{east});
);
(._;>;);
out body;"""


def read_limited(response, max_bytes: int) -> bytes:
	chunks = []
	total = 0
	while True:
		chunk = response.read(min(1024 * 1024, max_bytes - total + 1))
		if not chunk:
			break
		total += len(chunk)
		if total > max_bytes:
			raise ValueError("Live OSM snapshot exceeds the configured size limit")
		chunks.append(chunk)
	return b"".join(chunks)


def acquire(config_path: Path, output_directory: Path) -> dict:
	config = json.loads(config_path.read_text())
	if config.get("schema_version") != OBJ_SCHEMA_VERSION or config.get("area_id") != AREA_ID:
		raise ValueError("Wrong OSM acquisition configuration")
	mode = config.get("source_mode")
	if mode not in ("offline", "overpass"):
		raise ValueError("source_mode must be offline or overpass")
	for evidence in ("source_review", "license_review"):
		if not isinstance(config.get(evidence), str) or not config[evidence].strip():
			raise ValueError(f"Missing review evidence: {evidence}")
	if output_directory.exists():
		raise ValueError("Acquisition output directory already exists")
	query = None
	if mode == "offline":
		source = config.get("offline")
		if not isinstance(source, dict) or not isinstance(source.get("source_identifier"), str) or not source["source_identifier"].strip():
			raise ValueError("Offline acquisition requires a nonempty source_identifier")
		source_path = Path(source.get("osm_path", "")).expanduser().resolve()
		if not source_path.is_file() or sha256(source_path) != source.get("osm_sha256"):
			raise ValueError("Offline OSM snapshot does not match its reviewed hash")
		payload = source_path.read_bytes()
		metadata = {"mode": mode, "source_identifier": source["source_identifier"], "source_sha256": sha256(source_path)}
	else:
		source = config.get("overpass")
		if not isinstance(source, dict) or source.get("endpoint", OVERPASS_ENDPOINT) != OVERPASS_ENDPOINT:
			raise ValueError("Live acquisition permits only the reviewed HTTPS Overpass endpoint")
		user_agent = source.get("user_agent")
		if not isinstance(user_agent, str) or len(user_agent.strip()) < 8 or "\r" in user_agent or "\n" in user_agent:
			raise ValueError("Live acquisition requires a safe, identifying user_agent")
		if not isinstance(source.get("query_review"), str) or not source["query_review"].strip():
			raise ValueError("Live acquisition requires a reviewed fixed-query reference")
		query = overpass_query()
		request = Request(OVERPASS_ENDPOINT, data=urlencode({"data": query}).encode(), method="POST", headers={"Accept": "application/xml", "Content-Type": "application/x-www-form-urlencoded", "User-Agent": user_agent.strip()})
		with urlopen(request, timeout=90) as response:
			if response.status != 200:
				raise ValueError(f"Overpass returned HTTP {response.status}")
			payload = read_limited(response, MAX_LIVE_SNAPSHOT_BYTES)
		metadata = {"mode": mode, "endpoint": OVERPASS_ENDPOINT, "query": query, "query_review": source["query_review"], "downloaded_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")}
	try:
		ET.fromstring(payload)
	except ET.ParseError as error:
		raise ValueError("Acquired OSM response is not valid XML") from error
	output_directory.mkdir(parents=True)
	snapshot = output_directory / "source.osm"
	snapshot.write_bytes(payload)
	metadata.update({"area_id": AREA_ID, "bbox_wsen": BBOX, "snapshot_file": snapshot.name, "snapshot_sha256": sha256(snapshot), "license": "ODbL-1.0", "attribution": "© OpenStreetMap contributors", "reviews": {key: config[key] for key in ("source_review", "license_review")}})
	(output_directory / "acquisition.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
	return metadata


def obj_lines(features: list[dict], pivot_m: tuple[float, float]) -> tuple[str, int, int]:
	kind = features[0]["kind"]
	lines = ["# Generated by Scripts/osm_han_area.py", "# X=east cm, Y=south cm, Z=up cm", f"o OSM_{kind.title()}Tile"]
	vertex_count = 0
	triangle_count = 0
	for feature in features:
		footprint = [(100 * (easting - pivot_m[0]), -100 * (northing - pivot_m[1])) for easting, northing in feature["footprint_m"]]
		base_cm = 100 * feature["base_m"]
		top_cm = 100 * feature["top_m"]
		base = vertex_count + 1
		lines.append("g " + feature["id"].replace("/", "_"))
		if "holes_m" in feature:
			holes = [[(100 * (easting - pivot_m[0]), -100 * (northing - pivot_m[1])) for easting, northing in ring] for ring in feature["holes_m"]]
			vertices = {}

			def vertex(point, elevation):
				nonlocal vertex_count
				key = (point[0], point[1], elevation)
				if key not in vertices:
					vertex_count += 1
					vertices[key] = vertex_count
					lines.append(f"v {point[0]:.6f} {point[1]:.6f} {elevation:.6f}")
				return vertices[key]

			for triangle in triangulate_with_holes(footprint, holes):
				first, second, third = (vertex(point, top_cm) for point in triangle)
				lines.append(f"f {first} {second} {third}")
				triangle_count += 1
			if kind == "building":
				for index, ring in enumerate([footprint, *holes]):
					oriented = ring if signed_area(ring) * (1 if index == 0 else -1) > 0 else list(reversed(ring))
					for start, end in ring_edges(oriented):
						first = vertex(start, base_cm)
						second = vertex(end, base_cm)
						third = vertex(end, top_cm)
						fourth = vertex(start, top_cm)
						lines.append(f"f {first} {second} {third}")
						lines.append(f"f {first} {third} {fourth}")
						triangle_count += 2
			continue
		if kind == "water":
			for easting, south in footprint:
				lines.append(f"v {easting:.6f} {south:.6f} 0.000000")
			for first, second, third in triangulate(footprint):
				lines.append(f"f {base + first} {base + second} {base + third}")
				triangle_count += 1
			vertex_count += len(footprint)
			continue
		for easting, south in footprint:
			lines.append(f"v {easting:.6f} {south:.6f} {base_cm:.6f}")
		for easting, south in footprint:
			lines.append(f"v {easting:.6f} {south:.6f} {top_cm:.6f}")
		count = len(footprint)
		for first, second, third in triangulate(footprint):
			lines.append(f"f {base + count + first} {base + count + second} {base + count + third}")
			triangle_count += 1
		for index in range(count):
			next_index = (index + 1) % count
			lines.append(f"f {base + index} {base + next_index} {base + count + next_index}")
			lines.append(f"f {base + index} {base + count + next_index} {base + count + index}")
			triangle_count += 2
		vertex_count += 2 * count
	return "\n".join(lines) + "\n", vertex_count, triangle_count


def generate(config_path: Path, output_directory: Path) -> dict:
	config = json.loads(config_path.read_text())
	if config.get("schema_version") != OBJ_SCHEMA_VERSION or config.get("area_id") != AREA_ID:
		raise ValueError("Wrong OSM generation configuration")
	for evidence in ("source_review", "license_review", "georeference_review", "representative_export_review"):
		if not isinstance(config.get(evidence), str) or not config[evidence].strip():
			raise ValueError(f"Missing review evidence: {evidence}")
	origin = finite_values(config["origin_lon_lat"], 2)
	size_m = float(config["tile_size_m"])
	default_level_height_m = float(config["default_level_height_m"])
	if not 25 <= size_m <= 500 or not 2 <= default_level_height_m <= 6:
		raise ValueError("Tile size must be 25..500 m and reviewed default level height must be 2..6 m")
	osm_path = Path(config["osm_path"]).expanduser().resolve()
	if not osm_path.is_file() or sha256(osm_path) != config["osm_sha256"]:
		raise ValueError("OSM snapshot does not match its reviewed hash")
	if output_directory.exists():
		raise ValueError("Output directory already exists")
	features, skipped = read_features(osm_path, origin, default_level_height_m)
	if not features:
		raise ValueError("No usable closed building or water ways in the reviewed snapshot")
	tiles: dict[tuple[str, int, int], list[dict]] = {}
	for feature in features:
		tile_east, tile_north = tile_key(feature["centroid_m"], size_m)
		tiles.setdefault((feature["kind"], tile_east, tile_north), []).append(feature)
	output_directory.mkdir(parents=True)
	manifest = {"schema_version": OBJ_SCHEMA_VERSION, "area_id": AREA_ID, "format": "obj", "coordinate_system": "source X=east, Y=south, Z=up, units=centimeters; per-tile pivots are WGS84 origins", "source": {"osm_file": osm_path.name, "osm_sha256": sha256(osm_path), "license": "ODbL-1.0", "attribution": "© OpenStreetMap contributors"}, "generator": {"script": Path(__file__).name, "sha256": sha256(Path(__file__))}, "origin_lon_lat": origin, "generation": {"tile_size_m": size_m, "default_level_height_m": default_level_height_m, "skipped": skipped}, "reviews": {key: config[key] for key in ("source_review", "license_review", "georeference_review", "representative_export_review")}, "tiles": []}
	for (kind, tile_east, tile_north), tile_features in sorted(tiles.items()):
		pivot = ((tile_east + 0.5) * size_m, (tile_north + 0.5) * size_m)
		name = f"SM_Han_A01_OSM_{kind.title()}Tile_E{tile_east:+05d}_N{tile_north:+05d}"
		contents, vertex_count, triangle_count = obj_lines(tile_features, pivot)
		path = output_directory / f"{name}.obj"
		path.write_text(contents)
		manifest["tiles"].append({"name": name, "kind": kind, "file": path.name, "sha256": sha256(path), "pivot_east_north_m": pivot, "pivot_lon_lat": unproject(pivot, origin), "features": [{key: feature[key] for key in ("id", "kind", "base_m", "top_m", "height_source", "tags")} for feature in tile_features], "vertices": vertex_count, "triangles": triangle_count})
	(output_directory / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n")
	return manifest
