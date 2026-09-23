"""Import reviewed OSM OBJ building tiles into the currently open Unreal review map."""

from __future__ import annotations

import json
from pathlib import Path
import re

from han_area import AREA_ID, apply_registration, finite_values, register, sha256
from unreal_han_area import REVIEW_MAP


DESTINATION = "/Game/Phase2/HanRiver/Meshes/Area01/OSM"
IMPORT_SETTINGS = {
	"convert_scene": False, "convert_scene_unit": False, "force_front_x_axis": False,
	"import_uniform_scale": 1.0, "combine_meshes": True, "auto_generate_collision": False,
	"transform_vertex_to_absolute": True, "bake_pivot_in_vertex": False,
}


def approved_review_map(review: dict) -> str:
	path = review.get("review_map", REVIEW_MAP)
	if not isinstance(path, str) or not re.fullmatch(r"/Game/Phase2/HanRiver/Maps/[A-Za-z0-9_]+_Review", path):
		raise ValueError("review_map must be an approved /Game/Phase2/HanRiver/Maps/*_Review level")
	return path


def prepare(manifest_path: Path, review: dict) -> dict:
	manifest = json.loads(manifest_path.read_text())
	review_map = approved_review_map(review)
	if manifest.get("schema_version") != 1 or manifest.get("area_id") != AREA_ID or manifest.get("format") != "obj":
		raise ValueError("Wrong OSM OBJ manifest")
	source = manifest.get("source")
	if not isinstance(source, dict) or source.get("license") != "ODbL-1.0" or not isinstance(source.get("attribution"), str) or not source["attribution"].strip():
		raise ValueError("OSM manifest is missing its reviewed ODbL attribution")
	if not isinstance(manifest.get("generator", {}).get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", manifest["generator"]["sha256"]):
		raise ValueError("OSM manifest is missing the generator hash")
	if sha256(manifest_path) != review["manifest_sha256"]:
		raise ValueError("OSM OBJ manifest changed since review")
	for evidence in ("representative_scene_review", "license_review", "source_georeference_review", "vertical_datum_review", "budget_review"):
		if not isinstance(review.get(evidence), str) or not review[evidence].strip():
			raise ValueError(f"Missing review evidence: {evidence}")
	if review["origin_lon_lat"] != manifest["origin_lon_lat"]:
		raise ValueError("OSM and target registration origins differ")
	fit = register(review["controls"], review["origin_lon_lat"])
	vertical_offset = finite_values([review["vertical_offset_cm"]], 1)[0]
	materials = review.get("materials")
	if not isinstance(materials, dict):
		raise ValueError("Expected Han-owned material mappings for building and water tiles")
	assets = []
	for tile in manifest["tiles"]:
		name = tile["name"]
		kind = tile.get("kind")
		if kind not in ("building", "water") or not re.fullmatch(rf"SM_Han_A01_OSM_{kind.title()}Tile_E[+-][0-9]{{4}}_N[+-][0-9]{{4}}", name):
			raise ValueError("Invalid stable OSM tile name")
		material = materials.get(kind)
		if not isinstance(material, str) or not re.fullmatch(r"/Game/Phase2/HanRiver/Materials/[A-Za-z0-9_]+", material):
			raise ValueError(f"Missing Han-owned material mapping for {kind} tile")
		file = manifest_path.parent / tile["file"]
		if file.resolve().parent != manifest_path.parent.resolve() or file.suffix != ".obj" or sha256(file) != tile["sha256"]:
			raise ValueError("OBJ is outside the reviewed directory or its hash changed")
		if not isinstance(tile.get("triangles"), int) or tile["triangles"] <= 0 or not isinstance(tile.get("vertices"), int) or tile["vertices"] <= 0:
			raise ValueError("OSM tile is missing valid mesh metrics")
		easting, northing = finite_values(tile["pivot_east_north_m"], 2)
		location = apply_registration((100 * easting, -100 * northing), fit)
		assets.append({"name": name, "kind": kind, "source": str(file.resolve()), "path": DESTINATION + "/" + name, "location_cm": [*location, vertical_offset], "yaw_deg": fit["yaw_deg"], "scale": [fit["scale"], fit["scale"], 1.0], "material": material, "triangles": tile["triangles"]})
	if not assets:
		raise ValueError("No OSM tile assets were approved")
	return {"area_id": AREA_ID, "review_map": review_map, "registration": fit, "assets": assets, "source_attribution": source["attribution"]}


def run(manifest_path: str, review_path: str, apply: bool = False) -> dict:
	plan = prepare(Path(manifest_path), json.loads(Path(review_path).read_text()))
	if not apply:
		return plan
	import unreal

	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	current_map = world.get_path_name().split(".")[0]
	if current_map != plan["review_map"]:
		raise ValueError(f"Open the approved review map {plan['review_map']} through Unreal MCP first; current map is {current_map}")
	for material in {item["material"] for item in plan["assets"]}:
		if unreal.load_asset(material) is None:
			raise ValueError("Approved Han-owned material is unavailable")
	for item in plan["assets"]:
		if unreal.EditorAssetLibrary.does_asset_exist(item["path"]):
			raise ValueError("First-import tool refuses overwrite; reimport needs a fresh review")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
	created_assets = []
	created_actors = []
	try:
		for item in plan["assets"]:
			options = unreal.FbxImportUI()
			for key, value in {"import_mesh": True, "import_as_skeletal": False, "import_animations": False, "import_materials": False, "import_textures": False, "automated_import_should_detect_type": False, "override_full_name": True, "mesh_type_to_import": unreal.FBXImportType.FBXIT_STATIC_MESH}.items():
				options.set_editor_property(key, value)
			options.set_editor_property("is_obj_import", True)
			for key, value in IMPORT_SETTINGS.items():
				options.static_mesh_import_data.set_editor_property(key, value)
			task = unreal.AssetImportTask()
			for key, value in {"filename": item["source"], "destination_path": DESTINATION, "destination_name": item["name"], "automated": True, "replace_existing": False, "save": False, "options": options, "factory": unreal.FbxFactory()}.items():
				task.set_editor_property(key, value)
			unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
			imported = task.get_editor_property("imported_object_paths")
			if len(imported) != 1 or imported[0].split(".")[0] != item["path"]:
				raise RuntimeError("OBJ importer naming/path differed")
			created_assets.append(item["path"])
			mesh = unreal.load_asset(item["path"])
			if not isinstance(mesh, unreal.StaticMesh):
				raise RuntimeError("OBJ import did not produce one StaticMesh")
			mesh_editor.remove_collisions(mesh)
			actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*item["location_cm"]), unreal.Rotator(0, item["yaw_deg"], 0))
			if actor is None:
				raise RuntimeError("Could not create OSM review actor")
			created_actors.append(actor)
			actor.set_actor_label(item["name"])
			actor.set_folder_path(f"Han/Area01/OSMReview/{item['kind'].title()}")
			actor.set_actor_scale3d(unreal.Vector(*item["scale"]))
			actor.static_mesh_component.set_static_mesh(mesh)
			actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
			actor.static_mesh_component.set_material(0, unreal.load_asset(item["material"]))
	except Exception:
		for actor in reversed(created_actors):
			actors.destroy_actor(actor)
		for path in reversed(created_assets):
			if unreal.EditorAssetLibrary.does_asset_exist(path):
				unreal.EditorAssetLibrary.delete_asset(path)
		raise
	return {**plan, "saved": False, "warning": "Review OSM geometry, attribution, bounds, slots, LODs and clear rowing channel before explicit MCP save; not a cooked release."}
