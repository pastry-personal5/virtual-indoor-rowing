"""Call inspect_scene() through Blender MCP; export only an explicitly reviewed set."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re

from han_area import AREA_ID, sha256, true_north


def inspect_scene() -> dict:
	import bpy
	from mathutils import Vector

	scene = bpy.context.scene
	origin = [scene.get("lon"), scene.get("lat")]
	objects = []
	for obj in sorted(bpy.data.objects, key=lambda item: item.name):
		if not obj.name.startswith(("map_5.osm", "map_6.osm")):
			continue
		corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
		fingerprint = None
		if obj.type == "MESH":
			geometry = {"vertices": [list(obj.matrix_world @ vertex.co) for vertex in obj.data.vertices], "faces": [list(face.vertices) for face in obj.data.polygons]}
			fingerprint = hashlib.sha256(json.dumps(geometry, separators=(",", ":")).encode()).hexdigest()
		objects.append({
			"name": obj.name, "type": obj.type, "collections": [collection.name for collection in obj.users_collection],
			"matrix_world": [list(row) for row in obj.matrix_world],
			"bounds": [[min(corner[axis] for corner in corners) for axis in range(3)], [max(corner[axis] for corner in corners) for axis in range(3)]],
			"geometry_sha256": fingerprint, "modifiers": [modifier.type for modifier in obj.modifiers],
			"vertices": len(obj.data.vertices) if obj.type == "MESH" else None,
			"triangles": sum(len(face.vertices) - 2 for face in obj.data.polygons) if obj.type == "MESH" else None,
			"uv_layers": [layer.name for layer in obj.data.uv_layers] if obj.type == "MESH" else [],
			"materials": [material.name if material else None for material in obj.data.materials],
		})
	missing = [library.name for library in bpy.data.libraries if not Path(bpy.path.abspath(library.filepath)).exists()]
	projection_override = "bpyproj" in bpy.context.preferences.addons
	heading = scene.get("heading", 0.0)
	return {
		"source_file": Path(bpy.data.filepath).name,
		"source_sha256": sha256(Path(bpy.data.filepath)) if bpy.data.filepath else None,
		"is_dirty": bpy.data.is_dirty, "blender_version": bpy.app.version_string,
		"unit_scale": scene.unit_settings.scale_length, "unit_system": scene.unit_settings.system,
		"origin_lon_lat": origin, "bpyproj_enabled": projection_override, "heading": heading,
		"origin_true_north": true_north(origin, origin) if all(value is not None for value in origin) and not projection_override and heading == 0 else None,
		"missing_libraries": missing, "objects": objects,
	}


def export_reviewed(config: dict, output_directory: str) -> dict:
	import bpy
	from mathutils import Matrix, Vector

	report = inspect_scene()
	for field in ("source_review", "license_review", "representative_export_review"):
		if not isinstance(config.get(field), str) or not config[field].strip():
			raise ValueError(f"Missing review evidence: {field}")
	if report["is_dirty"] or report["source_sha256"] != config["source_sha256"]:
		raise ValueError("Save/review the exact source revision before export")
	if report["bpyproj_enabled"] or report["heading"] != 0 or report["origin_lon_lat"] != config["origin_lon_lat"]:
		raise ValueError("Unsupported projection override or unverified origin")
	if report["missing_libraries"] or report["unit_scale"] != 1.0:
		raise ValueError("Resolve missing libraries and verify one Blender unit = one meter")
	if config["collection"] not in ("map_5.osm", "map_6.osm"):
		raise ValueError("Select exactly one source batch")
	selected = config["objects"]
	if not selected or len({item["source"] for item in selected}) != len(selected) or len({item["asset"] for item in selected}) != len(selected):
		raise ValueError("An explicit, unique object and asset allowlist is required")
	original_scene = bpy.context.window.scene
	for item in selected:
		obj = original_scene.objects.get(item["source"])
		if obj is None or obj.type != "MESH" or obj.hide_get() or obj.hide_render:
			raise ValueError("Only visible approved mesh objects can be exported")
		if config["collection"] not in {collection.name for collection in obj.users_collection}:
			raise ValueError("Object is not in the chosen source batch")
		if not re.fullmatch(r"SM_Han_A01_[A-Za-z0-9_]+", item["asset"]):
			raise ValueError("Expected a stable SM_Han_A01_ asset name")
		if len(item["pivot_m"]) != 3 or not all(math.isfinite(value) for value in item["pivot_m"]):
			raise ValueError("Each asset requires an explicit finite three-dimensional pivot")
	output = Path(output_directory)
	output.mkdir(parents=True, exist_ok=False)
	manifest = {"schema_version": 1, "area_id": AREA_ID, "source_sha256": report["source_sha256"], "origin_lon_lat": report["origin_lon_lat"], "blender_version": report["blender_version"], "reviews": {key: config[key] for key in ("source_review", "license_review", "representative_export_review")}, "assets": []}
	depsgraph = bpy.context.evaluated_depsgraph_get()
	try:
		for item in selected:
			original = original_scene.objects[item["source"]]
			mesh = bpy.data.meshes.new_from_object(original.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph)
			staging = None
			staged = None
			try:
				matrix = Matrix.Translation(-Vector(item["pivot_m"])) @ original.matrix_world
				mesh.transform(matrix)
				if matrix.determinant() < 0:
					mesh.flip_normals()
				staging = bpy.data.scenes.new("VIR_Han_Export_Temporary")
				staging.unit_settings.system = "METRIC"
				staging.unit_settings.scale_length = 1.0
				bpy.context.window.scene = staging
				staged = bpy.data.objects.new(item["asset"], mesh)
				staging.collection.objects.link(staged)
				staged.select_set(True)
				bpy.context.view_layer.objects.active = staged
				filename = item["asset"] + ".fbx"
				settings = {"use_selection": True, "object_types": {"MESH"}, "global_scale": 1.0, "apply_unit_scale": True, "apply_scale_options": "FBX_SCALE_UNITS", "axis_forward": "-Y", "axis_up": "Z", "use_space_transform": True, "bake_space_transform": False, "use_mesh_modifiers": True, "bake_anim": False, "path_mode": "STRIP", "embed_textures": False}
				status = bpy.ops.export_scene.fbx(filepath=str(output / filename), **settings)
				if "FINISHED" not in status:
					raise RuntimeError("FBX export did not finish")
				manifest["assets"].append({"asset": item["asset"], "file": filename, "sha256": sha256(output / filename), "pivot_m": item["pivot_m"], "materials": [material.name if material else None for material in mesh.materials], "triangles": sum(len(face.vertices) - 2 for face in mesh.polygons)})
				manifest["export_settings"] = {key: sorted(value) if isinstance(value, set) else value for key, value in settings.items()}
			finally:
				bpy.context.window.scene = original_scene
				if staged is not None:
					bpy.data.objects.remove(staged, do_unlink=True)
				if staging is not None:
					bpy.data.scenes.remove(staging)
				bpy.data.meshes.remove(mesh)
	finally:
		bpy.context.window.scene = original_scene
	(output / "manifest.json").write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n")
	return manifest
