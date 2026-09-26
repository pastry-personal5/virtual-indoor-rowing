#!/usr/bin/env python3
"""Tests for deterministic Han area registration and OSM mesh generation."""

from __future__ import annotations

import json
from io import BytesIO
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs
import xml.etree.ElementTree as ET


SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import han_area
import osm_han_area
import unreal_osm_area
import add_han_5k_legacy_banks


class HanAreaTests(unittest.TestCase):
	def test_full_5k_legacy_banks_cover_every_route_segment_and_keep_channel_clear(self) -> None:
		review = {
			"origin_lon_lat": [126.993885, 37.51381],
			"controls": [
				{"id": "banpo-bridge-reference", "lon_lat": [126.996000, 37.511500], "level_xy_cm": [18675.891, 25714.592]},
				{"id": "sebit-reference", "lon_lat": [126.990000, 37.510500], "level_xy_cm": [-34305.820, 36846.043]},
				{"id": "east-bank-reference", "lon_lat": [126.982200, 37.511200], "level_xy_cm": [-103181.403, 29047.980]},
			],
		}
		route_path = SCRIPTS.parent / "Content/Phase2/HanRiver/han-river-5k.geojson"
		transforms = add_han_5k_legacy_banks._bank_transforms(add_han_5k_legacy_banks._route_points(str(route_path), review))
		self.assertEqual(set(transforms), set(add_han_5k_legacy_banks.BANK_LABELS))
		self.assertEqual({label: len(instances) for label, instances in transforms.items()}, {label: 8 for label in add_han_5k_legacy_banks.BANK_LABELS})
		for port, starboard in zip(transforms[add_han_5k_legacy_banks.BANK_LABELS[0]], transforms[add_han_5k_legacy_banks.BANK_LABELS[1]]):
			self.assertAlmostEqual(math.dist(port[0][:2], starboard[0][:2]), 2 * add_han_5k_legacy_banks.BANK_OFFSET_CM, places=5)
			self.assertEqual(port[2][1], add_han_5k_legacy_banks.BANK_HALF_WIDTH_CM / 100.0)
		self.assertEqual(add_han_5k_legacy_banks.BRIDGE_BEATS, ("banpo-start", "dongjak-span", "hangang-bridge", "wonhyo-finish"))
	def test_projection_round_trip_and_true_north(self) -> None:
		origin = (126.993885, 37.51381)
		coordinate = (126.998, 37.514)
		self.assertEqual(tuple(round(value, 8) for value in han_area.unproject(han_area.project(coordinate, origin), origin)), coordinate)
		north = han_area.true_north(origin, origin)
		self.assertAlmostEqual(north["blender_xy_unit"][0], 0.0, places=7)
		self.assertAlmostEqual(north["blender_xy_unit"][1], 1.0, places=7)

	def test_registration_rejects_stretch_and_recovers_rigid_transform(self) -> None:
		origin = (126.993885, 37.51381)
		controls = []
		for identifier, point in (("a", (0, 0)), ("b", (100, 0)), ("c", (0, 100))):
			lon_lat = han_area.unproject(point, origin)
			controls.append({"id": identifier, "lon_lat": lon_lat, "level_xy_cm": [point[0] * 100 + 400, -point[1] * 100 - 900]})
		fit = han_area.register(controls, origin)
		self.assertAlmostEqual(fit["scale"], 1.0, places=8)
		self.assertAlmostEqual(fit["yaw_deg"], 0.0, places=8)
		self.assertEqual([round(value) for value in fit["translation_cm"]], [400, -900])
		controls[2]["level_xy_cm"][0] += 50
		with self.assertRaisesRegex(ValueError, r"scale distortion \(fitted scale="):
			han_area.register(controls, origin)

	def test_documented_bootstrap_controls_fit_without_scale_distortion(self) -> None:
		origin = (126.993885, 37.51381)
		controls = [
			{"id": "banpo-bridge-reference", "lon_lat": [126.996000, 37.511500], "level_xy_cm": [18675.891, 25714.592]},
			{"id": "sebit-reference", "lon_lat": [126.990000, 37.510500], "level_xy_cm": [-34305.820, 36846.043]},
			{"id": "east-bank-reference", "lon_lat": [126.982200, 37.511200], "level_xy_cm": [-103181.403, 29047.980]},
		]
		fit = han_area.register(controls, origin)
		self.assertAlmostEqual(fit["scale"], 1.0, places=8)
		self.assertAlmostEqual(fit["yaw_deg"], 0.0, places=6)
		self.assertLess(fit["rms_cm"], 0.01)

	def test_osm_obj_generation_and_unreal_preflight(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			osm = root / "fixture.osm"
			osm.write_text("""<?xml version=\"1.0\"?><osm version=\"0.6\"><node id=\"1\" lat=\"37.51380\" lon=\"126.99380\"/><node id=\"2\" lat=\"37.51380\" lon=\"126.99390\"/><node id=\"3\" lat=\"37.51390\" lon=\"126.99390\"/><node id=\"4\" lat=\"37.51390\" lon=\"126.99380\"/><node id=\"5\" lat=\"37.51370\" lon=\"126.99370\"/><node id=\"6\" lat=\"37.51370\" lon=\"126.99410\"/><node id=\"7\" lat=\"37.51400\" lon=\"126.99410\"/><node id=\"8\" lat=\"37.51400\" lon=\"126.99370\"/><way id=\"10\"><nd ref=\"1\"/><nd ref=\"2\"/><nd ref=\"3\"/><nd ref=\"4\"/><nd ref=\"1\"/><tag k=\"building\" v=\"yes\"/><tag k=\"min_height\" v=\"3\"/><tag k=\"height\" v=\"15\"/></way><way id=\"11\"><nd ref=\"1\"/><nd ref=\"2\"/><nd ref=\"3\"/><nd ref=\"4\"/><nd ref=\"1\"/><tag k=\"building\" v=\"roof\"/></way><way id=\"12\"><nd ref=\"5\"/><nd ref=\"6\"/><nd ref=\"7\"/><nd ref=\"8\"/><nd ref=\"5\"/><tag k=\"natural\" v=\"water\"/></way></osm>""")
			config = {"schema_version": 1, "area_id": han_area.AREA_ID, "origin_lon_lat": [126.993885, 37.51381], "tile_size_m": 100, "default_level_height_m": 3, "osm_path": str(osm), "osm_sha256": han_area.sha256(osm), "source_review": "TEST-1", "license_review": "TEST-2", "georeference_review": "TEST-3", "representative_export_review": "TEST-4"}
			config_path = root / "config.json"
			config_path.write_text(json.dumps(config))
			output = root / "output"
			manifest = osm_han_area.generate(config_path, output)
			self.assertGreaterEqual(len(manifest["tiles"]), 2)
			building_tile = next(tile for tile in manifest["tiles"] if tile["kind"] == "building")
			water_tile = next(tile for tile in manifest["tiles"] if tile["kind"] == "water")
			self.assertIn("f ", (output / building_tile["file"]).read_text())
			self.assertIn("300.000000", (output / building_tile["file"]).read_text())
			self.assertIn("1500.000000", (output / building_tile["file"]).read_text())
			self.assertIn("v ", (output / water_tile["file"]).read_text())
			self.assertNotIn("1500.000000", (output / water_tile["file"]).read_text())
			self.assertEqual(manifest["generation"]["skipped"]["roof_only"], 1)
			self.assertIn("sha256", manifest["generator"])
			controls = []
			for identifier, point in (("a", (0, 0)), ("b", (100, 0)), ("c", (0, 100))):
				controls.append({"id": identifier, "lon_lat": han_area.unproject(point, config["origin_lon_lat"]), "level_xy_cm": [point[0] * 100, -point[1] * 100]})
			manifest_path = output / "manifest.json"
			review = {"manifest_sha256": han_area.sha256(manifest_path), "origin_lon_lat": config["origin_lon_lat"], "controls": controls, "vertical_offset_cm": 0, "representative_scene_review": "TEST-5", "license_review": "TEST-6", "source_georeference_review": "TEST-7", "vertical_datum_review": "TEST-8", "budget_review": "TEST-9", "materials": {"building": "/Game/Phase2/HanRiver/Materials/M_Han_Concrete", "water": "/Game/Phase2/HanRiver/Materials/M_Han_Water"}}
			plan = unreal_osm_area.prepare(manifest_path, review)
			self.assertEqual(len(plan["assets"]), len(manifest["tiles"]))
			building_asset = next(asset for asset in plan["assets"] if asset["kind"] == "building")
			water_asset = next(asset for asset in plan["assets"] if asset["kind"] == "water")
			self.assertAlmostEqual(building_asset["scale"][0], 1.0, places=9)
			self.assertAlmostEqual(building_asset["scale"][1], 1.0, places=9)
			self.assertEqual(building_asset["scale"][2], 1.0)
			self.assertEqual(water_asset["material"], "/Game/Phase2/HanRiver/Materials/M_Han_Water")
			manifest["source"]["license"] = "unknown"
			manifest_path.write_text(json.dumps(manifest))
			review["manifest_sha256"] = han_area.sha256(manifest_path)
			with self.assertRaisesRegex(ValueError, "ODbL attribution"):
				unreal_osm_area.prepare(manifest_path, review)

	def test_multipolygon_split_outers_holes_and_member_deduplication(self) -> None:
		origin = (126.993885, 37.51381)
		osm = ET.Element("osm", version="0.6")
		coordinates = {
			1: (0, 0), 2: (20, 0), 3: (20, 20), 4: (0, 20),
			5: (5, 5), 6: (15, 5), 7: (15, 15), 8: (5, 15),
			9: (40, 0), 10: (60, 0), 11: (60, 20), 12: (40, 20),
			13: (45, 5), 14: (55, 5), 15: (55, 15), 16: (45, 15),
			17: (70, 0), 18: (90, 0), 19: (90, 20), 20: (70, 20),
			21: (100, 0), 22: (120, 0), 23: (120, 20), 24: (100, 20),
			25: (105, 5), 26: (115, 5), 27: (115, 15), 28: (105, 15),
		}
		for identifier, point in coordinates.items():
			lon, lat = han_area.unproject(point, origin)
			ET.SubElement(osm, "node", id=str(identifier), lon=f"{lon:.12f}", lat=f"{lat:.12f}")

		def add_way(identifier, references, tag=None):
			way = ET.SubElement(osm, "way", id=str(identifier))
			for reference in references:
				ET.SubElement(way, "nd", ref=str(reference))
			if tag:
				ET.SubElement(way, "tag", k=tag[0], v=tag[1])

		add_way(100, (1, 2, 3), ("natural", "water"))
		add_way(101, (3, 4, 1))
		add_way(102, (5, 6, 7))
		add_way(103, (7, 8, 5))
		add_way(200, (9, 10, 11, 12, 9), ("building", "yes"))
		add_way(201, (13, 14, 15, 16, 13))
		add_way(202, (17, 18, 19, 20, 17))
		add_way(300, (21, 22, 23, 24, 21), ("natural", "water"))
		add_way(301, (25, 26, 27, 28, 25))

		def add_relation(identifier, kind, members):
			relation = ET.SubElement(osm, "relation", id=str(identifier))
			for reference, role in members:
				ET.SubElement(relation, "member", type="way", ref=str(reference), role=role)
			ET.SubElement(relation, "tag", k="type", v="multipolygon")
			if kind:
				ET.SubElement(relation, "tag", k=kind[0], v=kind[1])
			return relation

		add_relation(900, ("natural", "water"), ((101, "outer"), (100, "outer"), (103, "inner"), (102, "inner")))
		add_relation(901, ("building", "yes"), ((202, "outer"), (200, "outer"), (201, "inner")))
		add_relation(903, None, ((300, "outer"), (301, "inner")))

		def surface_area_m2(contents, elevation_cm):
			vertices = []
			area_cm2 = 0.0
			for line in contents.splitlines():
				if line.startswith("v "):
					vertices.append(tuple(map(float, line.split()[1:])))
				if line.startswith("f "):
					first, second, third = (vertices[int(index) - 1] for index in line.split()[1:])
					if all(abs(point[2] - elevation_cm) < 1e-6 for point in (first, second, third)):
						area_cm2 += abs((second[0] - first[0]) * (third[1] - first[1]) - (second[1] - first[1]) * (third[0] - first[0])) / 2
			return area_cm2 / 10000

		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			path = root / "multipolygon.osm"
			ET.ElementTree(osm).write(path, encoding="unicode")
			features, skipped = osm_han_area.read_features(path, origin, 3)
			self.assertEqual([feature["id"] for feature in features], ["relation/900/outer/0", "relation/901/outer/0", "relation/901/outer/1", "relation/903/outer/0"])
			self.assertEqual(skipped["invalid_relation"], 0)
			self.assertEqual(skipped["relation_member"], 9)
			for feature in features:
				contents, vertices, triangles = osm_han_area.obj_lines([feature], (0, 0))
				expected = abs(osm_han_area.signed_area(feature["footprint_m"])) - sum(abs(osm_han_area.signed_area(hole)) for hole in feature["holes_m"])
				self.assertAlmostEqual(surface_area_m2(contents, 0 if feature["kind"] == "water" else 300), expected, places=3)
				self.assertGreater(vertices, 0)
				self.assertGreater(triangles, 0)

			class Response(BytesIO):
				status = 200

			requests = []

			def respond(request, timeout):
				requests.append((request, timeout))
				return Response(path.read_bytes())

			acquire_config = root / "acquire.json"
			acquire_config.write_text(json.dumps({"schema_version": 1, "area_id": han_area.AREA_ID, "source_mode": "overpass", "source_review": "TEST-12", "license_review": "TEST-13", "overpass": {"endpoint": osm_han_area.OVERPASS_ENDPOINT, "user_agent": "VIR-TestAgent", "query_review": "TEST-14"}}))
			snapshot = root / "snapshot"
			with patch.object(osm_han_area, "urlopen", side_effect=respond):
				metadata = osm_han_area.acquire(acquire_config, snapshot)
			self.assertEqual(metadata["mode"], "overpass")
			self.assertEqual(len(requests), 1)
			self.assertIn('relation["type"="multipolygon"]', parse_qs(requests[0][0].data.decode())["data"][0])
			generate_config = root / "generate.json"
			generate_config.write_text(json.dumps({"schema_version": 1, "area_id": han_area.AREA_ID, "origin_lon_lat": origin, "tile_size_m": 100, "default_level_height_m": 3, "osm_path": str(snapshot / "source.osm"), "osm_sha256": metadata["snapshot_sha256"], "source_review": "TEST-12", "license_review": "TEST-13", "georeference_review": "TEST-15", "representative_export_review": "TEST-16"}))
			manifest = osm_han_area.generate(generate_config, root / "output")
			generated_ids = [feature["id"] for tile in manifest["tiles"] for feature in tile["features"]]
			self.assertEqual(set(generated_ids), {feature["id"] for feature in features})
			self.assertGreater(len(generated_ids), len(set(generated_ids)))

			osm.remove(next(way for way in osm.findall("way") if way.attrib["id"] == "103"))
			add_way(104, (1, 2, 3, 4, 1), ("natural", "water"))
			add_relation(902, ("natural", "water"), ((104, "outer"), (102, "inner")))
			ET.ElementTree(osm).write(path, encoding="unicode")
			features, skipped = osm_han_area.read_features(path, origin, 3)
			self.assertEqual(skipped["invalid_relation"], 2)
			self.assertEqual(len(features), 3)

	def test_multipolygon_concave_surface_keeps_hole_empty(self) -> None:
		outer = [(0, 0), (30, 0), (30, 10), (20, 10), (20, 20), (30, 20), (30, 30), (0, 30)]
		hole = [(10, 5), (15, 10), (10, 15), (5, 10)]
		triangles = osm_han_area.triangulate_with_holes(outer, [hole])
		area = sum(abs(osm_han_area.cross(*triangle)) / 2 for triangle in triangles)
		self.assertAlmostEqual(area, abs(osm_han_area.signed_area(outer)) - abs(osm_han_area.signed_area(hole)))
		for triangle in triangles:
			center = (sum(point[0] for point in triangle) / 3, sum(point[1] for point in triangle) / 3)
			self.assertTrue(osm_han_area.point_in_ring(center, outer))
			self.assertFalse(osm_han_area.point_in_ring(center, hole))

	def test_tile_clipping_triangulates_a_polygon_with_boundary_vertices(self) -> None:
		footprint = [(-4714, 344), (-4698, 346), (-4690, 347), (-4686, 309), (-4669, 311), (-4669, 309), (-4663, 310), (-4659, 280), (-4679, 278), (-4677, 268), (-4691, 267), (-4691, 276), (-4695, 276), (-4695, 277), (-4696, 281), (-4715, 278), (-4716, 283), (-4690, 286), (-4691, 290), (-4707, 288), (-4708, 295), (-4692, 297), (-4693, 306), (-4709, 304), (-4710, 307), (-4710, 310), (-4694, 312), (-4695, 322), (-4711, 320), (-4712, 323), (-4712, 327), (-4696, 330), (-4697, 341), (-4714, 339)]
		feature = {"id": "way/test", "kind": "building", "footprint_m": footprint, "base_m": 0, "top_m": 3, "height_source": "test", "tags": {}}
		pieces = osm_han_area.tile_feature_pieces(feature, (-4800, 200, -4700, 300))
		self.assertGreater(len(pieces), 1)
		for piece in pieces:
			self.assertEqual(len(osm_han_area.triangulate(piece["footprint_m"])), len(piece["footprint_m"]) - 2)

	def test_offline_acquisition_and_fixed_live_query(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			source = root / "source.osm"
			source.write_text("<osm version=\"0.6\"><node id=\"1\" lat=\"37.5\" lon=\"126.9\"/></osm>")
			config = {"schema_version": 1, "area_id": han_area.AREA_ID, "source_mode": "offline", "source_review": "TEST-10", "license_review": "TEST-11", "offline": {"source_identifier": "owner fixture", "osm_path": str(source), "osm_sha256": han_area.sha256(source)}}
			config_path = root / "acquire.json"
			config_path.write_text(json.dumps(config))
			output = root / "snapshot"
			metadata = osm_han_area.acquire(config_path, output)
			self.assertEqual(metadata["mode"], "offline")
			self.assertEqual(metadata["snapshot_sha256"], han_area.sha256(output / "source.osm"))
			audit = han_area.audit(output)
			self.assertEqual(audit["source_layout"], "acquired_snapshot")
			self.assertEqual(audit["acquisition_mode"], "offline")
			query = osm_han_area.overpass_query()
			self.assertIn('way["building"](37.50627,126.97764,37.52135,127.01013)', query)
			self.assertIn('way["building:part"](37.50627,126.97764,37.52135,127.01013)', query)
			for selector in ('way["natural"="water"]', 'way["water"]', 'way["waterway"="riverbank"]', 'way["landuse"="basin"]', 'way["landuse"="reservoir"]'):
				self.assertIn(f"{selector}(37.50627,126.97764,37.52135,127.01013)", query)
			self.assertIn('relation["type"="multipolygon"](37.50627,126.97764,37.52135,127.01013)', query)


if __name__ == "__main__":
	unittest.main()
