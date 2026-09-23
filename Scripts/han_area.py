"""Offline Han area research and registration; never modifies editor assets."""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
BBOX = (126.97764, 37.50627, 127.01013, 37.52135)
RADIUS_M = 6378137.0
AREA_ID = "banpo-sevit-01"


def sha256(path: Path) -> str:
	with path.open("rb") as stream:
		return hashlib.file_digest(stream, "sha256").hexdigest()


def finite_values(values, count: int) -> tuple[float, ...]:
	result = tuple(float(value) for value in values)
	if len(result) != count or not all(math.isfinite(value) for value in result):
		raise ValueError(f"Expected {count} finite numbers")
	return result


def project(lon_lat, origin_lon_lat) -> tuple[float, float]:
	longitude, latitude = finite_values(lon_lat, 2)
	origin_lon, origin_lat = finite_values(origin_lon_lat, 2)
	if not (-180 <= longitude <= 180 and -80 < latitude < 80 and -180 <= origin_lon <= 180 and -80 < origin_lat < 80):
		raise ValueError("Invalid geographic coordinate for local spherical TM")
	if abs(longitude - origin_lon) > 1 or abs(latitude - origin_lat) > 1:
		raise ValueError("Only local areas within one degree of the origin are supported")
	latitude_rad = math.radians(latitude)
	delta_lon = math.radians(longitude - origin_lon)
	easting = RADIUS_M * math.atanh(math.cos(latitude_rad) * math.sin(delta_lon))
	northing = RADIUS_M * (math.atan2(math.sin(latitude_rad), math.cos(latitude_rad) * math.cos(delta_lon)) - math.radians(origin_lat))
	return easting, northing


def unproject(east_north, origin_lon_lat) -> tuple[float, float]:
	easting, northing = finite_values(east_north, 2)
	origin_lon, origin_lat = finite_values(origin_lon_lat, 2)
	angle = northing / RADIUS_M + math.radians(origin_lat)
	longitude = origin_lon + math.degrees(math.atan2(math.sinh(easting / RADIUS_M), math.cos(angle)))
	latitude = math.degrees(math.asin(math.sin(angle) / math.cosh(easting / RADIUS_M)))
	project((longitude, latitude), (origin_lon, origin_lat))
	return longitude, latitude


def true_north(lon_lat, origin_lon_lat) -> dict:
	longitude, latitude = finite_values(lon_lat, 2)
	south = project((longitude, latitude - 0.00001), origin_lon_lat)
	north = project((longitude, latitude + 0.00001), origin_lon_lat)
	direction = (north[0] - south[0], north[1] - south[1])
	length = math.hypot(*direction)
	unit = [value / length for value in direction]
	return {"blender_xy_unit": unit, "clockwise_from_grid_y_deg": math.degrees(math.atan2(unit[0], unit[1]))}


def apply_registration(point, fit: dict) -> tuple[float, float]:
	easting, south = finite_values(point, 2)
	real, imaginary = fit["rotation_scale"]
	return (real * easting - imaginary * south + fit["translation_cm"][0], imaginary * easting + real * south + fit["translation_cm"][1])


def register(controls: list[dict], origin_lon_lat, tolerance_cm: float = 200.0) -> dict:
	if len(controls) < 3:
		raise ValueError("At least three non-collinear surveyed/identified controls are required")
	if not math.isfinite(tolerance_cm) or not 0 < tolerance_cm <= 200:
		raise ValueError("Review residual tolerance must be in (0, 200] cm")
	sources = []
	targets = []
	identifiers = set()
	for control in controls:
		identifier = control["id"]
		if not isinstance(identifier, str) or not identifier.strip() or identifier in identifiers:
			raise ValueError("Control IDs must be nonempty and unique")
		identifiers.add(identifier)
		easting, northing = project(control["lon_lat"], origin_lon_lat)
		sources.append(complex(easting * 100, -northing * 100))
		targets.append(complex(*finite_values(control["level_xy_cm"], 2)))
	source_mean = sum(sources) / len(sources)
	target_mean = sum(targets) / len(targets)
	centered = [point - source_mean for point in sources]
	variance = sum(abs(point) ** 2 for point in centered)
	spread_x = sum(point.real ** 2 for point in centered)
	spread_y = sum(point.imag ** 2 for point in centered)
	cross = sum(point.real * point.imag for point in centered)
	if variance < 10000 or (spread_x * spread_y - cross * cross) / (variance * variance) < 0.0001:
		raise ValueError("Controls are coincident, too close, or nearly collinear")
	coefficient = sum(point.conjugate() * (target - target_mean) for point, target in zip(centered, targets)) / variance
	translation = target_mean - coefficient * source_mean
	residuals = [abs(coefficient * source + translation - target) for source, target in zip(sources, targets)]
	scale = abs(coefficient)
	rms = math.sqrt(sum(value * value for value in residuals) / len(residuals))
	maximum_residual = max(residuals)
	if not 0.999 <= scale <= 1.001:
		raise ValueError(f"Registration implies scale distortion (fitted scale={scale:.9f}; expected 0.999..1.001). Measure the same controls in source and review-level centimeters; do not stretch geographic assets to fit the blockout")
	if maximum_residual > tolerance_cm:
		raise ValueError(f"Control residual exceeds tolerance (max={maximum_residual:.3f} cm, rms={rms:.3f} cm, limit={tolerance_cm:.3f} cm); no single rigid registration fits this scene")
	return {
		"rotation_scale": [coefficient.real, coefficient.imag],
		"scale": scale,
		"yaw_deg": math.degrees(math.atan2(coefficient.imag, coefficient.real)),
		"translation_cm": [translation.real, translation.imag],
		"rms_cm": rms,
		"residual_cm": dict(zip([control["id"] for control in controls], residuals)),
		"source_axes": "X=east,Y=south,Z=up; centimeters",
		"target_frame": "Han authored level local; NOT runtime world",
	}


def read_osm(path: Path) -> tuple[dict, dict]:
	root = ET.parse(path).getroot()
	entities = {}
	counts = {kind: 0 for kind in ("node", "way", "relation")}
	buildings = []
	landmarks = []
	nodes = {element.attrib["id"]: (float(element.attrib["lon"]), float(element.attrib["lat"])) for element in root.findall("node")}
	for element in root:
		if element.tag not in counts:
			continue
		counts[element.tag] += 1
		tags = {tag.attrib["k"]: tag.attrib["v"] for tag in element.findall("tag")}
		geometry = {
			"coordinate": [element.get("lon"), element.get("lat")],
			"nodes": [node.attrib["ref"] for node in element.findall("nd")],
			"members": [member.attrib for member in element.findall("member")],
			"tags": tags,
		}
		key = f"{element.tag}/{element.attrib['id']}"
		entities[key] = geometry
		if any(tag in tags and tags[tag] != "no" for tag in ("building", "building:part")):
			buildings.append(tags)
		name = tags.get("name:en", tags.get("name", ""))
		if any(term in name.lower() for term in ("banpo", "sevit", "sebit", "dongjak", "반포", "세빛", "가빛", "채빛", "솔빛", "동작")):
			coordinates = [nodes[reference] for reference in geometry["nodes"] if reference in nodes]
			if element.tag == "node":
				coordinates = [nodes[element.attrib["id"]]]
			landmarks.append({"id": key, "name": name, "mean_lon_lat_not_control": [sum(point[axis] for point in coordinates) / len(coordinates) for axis in range(2)] if coordinates else None})
	bounds = root.find("bounds")
	meta = root.find("meta")
	semantic_hash = hashlib.sha256(json.dumps(entities, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
	return {
		"file": path.name, "sha256": sha256(path), "semantic_sha256": semantic_hash,
		"osm_base": meta.get("osm_base") if meta is not None else None,
		"bounds": dict(bounds.attrib) if bounds is not None else None,
		"counts": counts, "building_tagged_elements": len(buildings),
		"building_elements_with_height": sum("height" in tags for tags in buildings),
		"building_elements_with_levels": sum("building:levels" in tags for tags in buildings),
		"landmarks": landmarks,
	}, entities


def audit(source: Path) -> dict:
	reports = []
	duplicates = {}
	if source.is_file():
		report, _ = read_osm(source)
		reports.append(report)
		layout = "single_snapshot"
	elif source.is_dir() and (source / "source.osm").is_file():
		report, _ = read_osm(source / "source.osm")
		reports.append(report)
		layout = "acquired_snapshot"
		acquisition = source / "acquisition.json"
		if acquisition.is_file():
			try:
				metadata = json.loads(acquisition.read_text())
			except json.JSONDecodeError as error:
				raise ValueError(f"Invalid acquisition metadata: {acquisition}") from error
			if metadata.get("snapshot_sha256") != report["sha256"]:
				raise ValueError("Acquisition metadata hash does not match source.osm")
			duplicates["acquisition_mode"] = metadata.get("mode")
	elif source.is_dir():
		names = ("map_5.osm", "map_5_extra.osm", "map_6.osm", "map_6_extra.osm")
		missing = [name for name in names if not (source / name).is_file()]
		if missing:
			raise ValueError("HAN_OSM_DIR must be an acquired source.osm snapshot, one .osm file, or a legacy Blender OSM directory containing " + ", ".join(names) + "; missing " + ", ".join(missing))
		entity_sets = []
		for name in names:
			report, entities = read_osm(source / name)
			reports.append(report)
			entity_sets.append(entities)
		layout = "legacy_blender_batches"
		duplicates = {"duplicate_main_geometry_and_tags": entity_sets[0] == entity_sets[2], "duplicate_extra_geometry_and_tags": entity_sets[1] == entity_sets[3]}
	else:
		raise ValueError(f"HAN_OSM_DIR does not exist: {source}")
	west, south, east, north = BBOX
	center = ((west + east) / 2, (south + north) / 2)
	southwest = project((west, south), center)
	northeast = project((east, north), center)
	waterline = json.loads((ROOT / "Content/Phase2/HanRiver/han-river-5k.geojson").read_text())["features"][0]["geometry"]["coordinates"]
	points = [project(point, center) for point in waterline]
	inside = [index for index, (longitude, latitude) in enumerate(waterline) if west <= longitude <= east and south <= latitude <= north]
	return {
		"area_id": AREA_ID, "bbox_wsen": BBOX, "bbox_center_lon_lat_NOT_verified_scene_origin": center,
		"source_layout": layout,
		"approximate_projected_extent_m": [northeast[axis] - southwest[axis] for axis in range(2)],
		"sources": reports,
		**duplicates,
		"geojson_vertices_inside_bbox": inside,
		"geojson_polyline_length_m_NOT_official_distance": sum(math.dist(start, end) for start, end in zip(points, points[1:])),
		"blocking": ["Live Blender projection origin and override not yet certified", "Source-to-mesh control residuals not yet measured", "FBX basis not yet calibrated", "OSM provenance review and representative-scene review required"],
	}


def command(mode: str) -> int:
	if mode == "audit":
		directory = os.environ.get("HAN_OSM_DIR")
		if not directory:
			raise ValueError("Set HAN_OSM_DIR to the owner-controlled OSM directory")
		report = audit(Path(directory))
	else:
		config = json.loads(Path(os.environ["HAN_AREA_REVIEW"]).read_text())
		report = register(config["controls"], config["origin_lon_lat"])
	output = ROOT / "Saved/HanArea" / f"{AREA_ID}-{mode}.json"
	output.parent.mkdir(parents=True, exist_ok=True)
	output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n")
	print(f"Han area {mode}: {output.relative_to(ROOT)}")
	return 0
