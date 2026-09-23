"""Dry-run by default; first import into an already opened, reviewed staging map."""

from __future__ import annotations

import json
import math
from pathlib import Path
import re

from han_area import AREA_ID, apply_registration, finite_values, register, sha256


DESTINATION = "/Game/Phase2/HanRiver/Meshes/Area01"
REVIEW_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_Area01_Review"
IMPORT_SETTINGS = {
	"convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": False,
	"import_uniform_scale": 1.0, "combine_meshes": True, "auto_generate_collision": False,
	"transform_vertex_to_absolute": True, "bake_pivot_in_vertex": False,
}


def calibrated_yaw(calibration: dict) -> float:
	if calibration["import_settings"] != IMPORT_SETTINGS:
		raise ValueError("Ruler calibration used different importer settings")
	east = finite_values(calibration["east_cm"], 3)
	north = finite_values(calibration["north_cm"], 3)
	up = finite_values(calibration["up_cm"], 3)
	yaw = -math.atan2(east[1], east[0])
	for observed, expected in ((east, (100, 0, 0)), (north, (0, -100, 0)), (up, (0, 0, 100))):
		rotated = (math.cos(yaw) * observed[0] - math.sin(yaw) * observed[1], math.sin(yaw) * observed[0] + math.cos(yaw) * observed[1], observed[2])
		if math.dist(rotated, expected) > 0.1:
			raise ValueError("FBX ruler is mirrored, tilted, or not 100 cm per source meter")
	return math.degrees(yaw)


def prepare(manifest_path: Path, review: dict) -> dict:
	manifest = json.loads(manifest_path.read_text())
	if manifest.get("schema_version") != 1 or manifest.get("area_id") != AREA_ID:
		raise ValueError("Wrong area manifest")
	if sha256(manifest_path) != review["manifest_sha256"]:
		raise ValueError("Export changed since review")
	for evidence in ("representative_scene_review", "license_review", "source_georeference_review", "vertical_datum_review", "budget_review", "fbx_calibration_review"):
		if not isinstance(review.get(evidence), str) or not review[evidence].strip():
			raise ValueError(f"Missing review evidence: {evidence}")
	if review["origin_lon_lat"] != manifest["origin_lon_lat"]:
		raise ValueError("Source and target registration origins differ")
	fit = register(review["controls"], review["origin_lon_lat"])
	axis_yaw = calibrated_yaw(review["calibration"])
	vertical_offset = finite_values([review["vertical_offset_cm"]], 1)[0]
	assets = []
	names = set()
	for item in manifest["assets"]:
		name = item["asset"]
		if not re.fullmatch(r"SM_Han_A01_[A-Za-z0-9_]+", name) or name in names:
			raise ValueError("Invalid or duplicate stable asset name")
		names.add(name)
		if item["file"] != name + ".fbx":
			raise ValueError("FBX must be a flat file adjacent to its manifest")
		file = manifest_path.parent / item["file"]
		if file.resolve().parent != manifest_path.parent.resolve() or sha256(file) != item["sha256"]:
			raise ValueError("FBX is outside the export directory or its hash changed")
		easting, northing, height = finite_values(item["pivot_m"], 3)
		position = apply_registration((100 * easting, -100 * northing), fit)
		materials = []
		for slot in item["materials"]:
			material = review["materials"].get(slot)
			if not isinstance(material, str) or not re.fullmatch(r"/Game/Phase2/HanRiver/Materials/[A-Za-z0-9_]+", material):
				raise ValueError(f"Missing Han-owned material mapping for {slot}")
			materials.append(material)
		assets.append({"name": name, "source": str(file.resolve()), "path": DESTINATION + "/" + name, "location_cm": [*position, height * 100 + vertical_offset], "yaw_deg": fit["yaw_deg"] + axis_yaw, "scale": [fit["scale"], fit["scale"], 1.0], "materials": materials})
	if not assets:
		raise ValueError("No assets were approved")
	return {"area_id": AREA_ID, "review_map": REVIEW_MAP, "registration": fit, "assets": assets}


def run(manifest_path: str, review_path: str, apply: bool = False) -> dict:
	plan = prepare(Path(manifest_path), json.loads(Path(review_path).read_text()))
	if not apply:
		return plan
	import unreal as unreal

	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	if world.get_path_name().split(".")[0] != REVIEW_MAP:
		raise ValueError("Open the explicitly approved Area01 review map through Unreal MCP first")
	for item in plan["assets"]:
		if unreal.EditorAssetLibrary.does_asset_exist(item["path"]):
			raise ValueError("First-import tool refuses overwrite; reimport requires separate review")
		for material in item["materials"]:
			if not isinstance(unreal.load_asset(material), unreal.MaterialInterface):
				raise ValueError("Approved material is missing or is not a material")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
	for item in plan["assets"]:
		options = unreal.FbxImportUI()
		for key, value in {"import_mesh": True, "import_as_skeletal": False, "import_animations": False, "import_materials": False, "import_textures": False, "automated_import_should_detect_type": False, "override_full_name": True, "mesh_type_to_import": unreal.FBXImportType.FBXIT_STATIC_MESH}.items():
			options.set_editor_property(key, value)
		for key, value in IMPORT_SETTINGS.items():
			options.static_mesh_import_data.set_editor_property(key, value)
		options.static_mesh_import_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
		task = unreal.AssetImportTask()
		for key, value in {"filename": item["source"], "destination_path": DESTINATION, "destination_name": item["name"], "automated": True, "replace_existing": False, "save": False, "options": options, "factory": unreal.FbxFactory()}.items():
			task.set_editor_property(key, value)
		unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
		imported = task.get_editor_property("imported_object_paths")
		if len(imported) != 1 or imported[0].split(".")[0] != item["path"]:
			raise RuntimeError("Importer naming/path differed; stop and inspect unsaved staging assets")
		mesh = unreal.load_asset(item["path"])
		if not isinstance(mesh, unreal.StaticMesh) or len(mesh.get_editor_property("static_materials")) != len(item["materials"]):
			raise RuntimeError("Unexpected asset type or material-slot count")
		mesh_editor.remove_collisions(mesh)
		for index, material in enumerate(item["materials"]):
			mesh.set_material(index, unreal.load_asset(material))
		actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*item["location_cm"]), unreal.Rotator(0, item["yaw_deg"], 0))
		if actor is None:
			raise RuntimeError("Could not create review actor")
		actor.set_actor_label(item["name"])
		actor.set_folder_path("Han/Area01/Review")
		actor.set_actor_scale3d(unreal.Vector(*item["scale"]))
		actor.static_mesh_component.set_static_mesh(mesh)
		actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	return {**plan, "saved": False, "warning": "Review geometry, slots, LODs, bounds, dependencies and collision before explicit MCP save; this is NOT a cooked release."}
