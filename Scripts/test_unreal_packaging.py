#!/usr/bin/env python3
"""Behavioral tests for the unsigned Unreal Shipping package guardrails."""

from __future__ import annotations

import json
import plistlib
import re
import runpy
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, patch


sys.path.insert(0, str(Path(__file__).resolve().parent))

from vir_dev import common, native, packaging, release, unreal  # noqa: E402


class UnrealShippingPackagingTests(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory()
		self.root = Path(self.temp.name)
		self.versions = {
			"platform": {"architecture": "arm64", "minimum_version": "26.6.2"},
			"unreal": {"version": "5.8", "approved_patch": 2},
			"xcode": {"version": "26.1.1"},
		}

	def tearDown(self) -> None:
		self.temp.cleanup()

	def test_explicit_engine_path_never_falls_back_after_a_move(self) -> None:
		with patch.dict(common.os.environ, {"UE_ROOT": "/missing/UE"}), \
			patch.object(Path, "is_file", lambda path: not str(path).startswith("/missing/")):
			self.assertIsNone(common.find_unreal(self.versions))
		with patch.dict(common.os.environ, {}, clear=True), \
			patch.object(Path, "is_file", lambda path: not str(path).startswith("/missing/")):
			versions = {"unreal": {"installation_root": "/missing/UE"}}
			self.assertIsNone(common.find_unreal(versions))

	def test_engine_alias_resolves_to_the_same_installation_and_relative_paths_fail(self) -> None:
		engine = self.installed_engine()
		(engine / "Engine/Build/Build.version").write_text("{}")
		alias = self.root / "UE alias"
		alias.symlink_to(engine, target_is_directory=True)
		with patch.dict(common.os.environ, {"UE_ROOT": str(alias)}):
			self.assertEqual(common.find_unreal(self.versions), engine.resolve())
		with patch.dict(common.os.environ, {"UE_ROOT": "relative/UE"}), patch.object(Path, "is_file", return_value=True):
			self.assertIsNone(common.find_unreal(self.versions))

	def test_editor_process_check_recognizes_aliases_spaces_and_commandlets(self) -> None:
		for executable in (
			"/Volumes/Engine Volume/UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor",
			"/Users/user1/ue-alias/Engine/Binaries/Mac/UnrealEditor-Cmd",
			"/UE/Engine/Binaries/Mac/UnrealEditor-Mac-DebugGame",
		):
			with self.subTest(executable=executable), patch.object(unreal.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout=f"  42 {executable}\n  43 /bin/ps\n")) as run:
				self.assertFalse(unreal.editor_closed_preflight())
				self.assertEqual(run.call_args.args[0], ["/bin/ps", "-axo", "pid=,comm="])
		with patch.object(unreal.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout="  43 /bin/ps\n  44 /tmp/UnrealEditorNotes\n")):
			self.assertTrue(unreal.editor_closed_preflight())

	def test_denied_empty_or_malformed_process_listing_is_never_treated_as_closed(self) -> None:
		for result in (
			SimpleNamespace(returncode=1, stdout=""),
			SimpleNamespace(returncode=0, stdout=""),
			SimpleNamespace(returncode=0, stdout="42\n"),
		):
			with patch.object(unreal.subprocess, "run", return_value=result):
				self.assertFalse(unreal.editor_closed_preflight())
		with patch.object(unreal.subprocess, "run", side_effect=PermissionError("denied")):
			self.assertFalse(unreal.editor_closed_preflight())

	def test_open_editor_blocks_all_native_builds_and_cleanup_before_mutation(self) -> None:
		for command in (unreal.unreal_smoke, unreal.unreal_shipping, unreal.han_external_cook, unreal.clean_unreal):
			with self.subTest(command=command.__name__), \
				patch.object(unreal, "editor_closed_preflight", return_value=False), \
				patch.object(common, "load_versions") as versions, \
				patch.object(native, "native_app") as native_app, \
				patch.object(common, "run") as run, \
				patch.object(unreal.shutil, "rmtree") as remove:
				self.assertEqual(command(), 1)
				versions.assert_not_called()
				native_app.assert_not_called()
				run.assert_not_called()
				remove.assert_not_called()

	def test_smoke_builds_project_target_without_hot_reload_and_checks_selected_module(self) -> None:
		engine = self.installed_engine()
		(engine / "Engine/Build/BatchFiles/Mac").mkdir()
		(engine / "Engine/Build/BatchFiles/Mac/Build.sh").touch()
		(self.root / "VirtualRowing.uproject").write_text("{}")
		with patch.object(common, "ROOT", self.root), \
			patch.object(common, "find_unreal", return_value=engine), \
			patch.object(common, "tool_env", return_value={}), \
			patch.object(unreal, "editor_closed_preflight", return_value=True), \
			patch.object(unreal, "unreal_build_preflight", return_value=True), \
			patch.object(unreal.doctor, "check_doctor", return_value=0), \
			patch.object(native, "native_app", return_value=0), \
			patch.object(common, "run", return_value=SimpleNamespace(returncode=0)) as run, \
			patch.object(unreal, "verify_editor_module", return_value=False) as verify:
			self.assertEqual(unreal.unreal_smoke(), 1)
			command = run.call_args.args[0]
			self.assertEqual(command[1:4], ["VirtualRowingEditor", "Mac", "Development"])
			self.assertIn("-NoHotReload", command)
			self.assertIn(f"-Log={self.root / 'Saved/Logs/UnrealBuildTool.log'}", command)
			self.assertNotIn("-NoLog", command)
			self.assertFalse(any(arg.startswith(("-Session=", "-XmlConfigCache=")) for arg in command))
			verify.assert_called_once()

	def test_smoke_verification_rejects_missing_or_stale_manifest_even_with_old_base_dylib(self) -> None:
		binaries = self.root / "Binaries/Mac"
		binaries.mkdir(parents=True)
		name = "libUnrealEditor-VirtualRowing.dylib"
		(binaries / name).write_bytes(b"old binary")
		manifest = binaries / "UnrealEditor.modules"
		with patch.object(common, "ROOT", self.root), patch.object(packaging, "homebrew_load_commands", return_value=[]):
			self.assertFalse(unreal.verify_editor_module())
			manifest.write_text(json.dumps({"Modules": {"VirtualRowing": "libUnrealEditor-VirtualRowing-0009.dylib"}}))
			self.assertFalse(unreal.verify_editor_module())
			manifest.write_text(json.dumps({"Modules": {"VirtualRowing": name}}))
			self.assertTrue(unreal.verify_editor_module())
			(binaries / name).unlink()
			self.assertFalse(unreal.verify_editor_module())

	def test_editor_opened_during_native_build_stops_before_ubt_or_uat(self) -> None:
		engine = self.installed_engine()
		(engine / "Engine/Build/BatchFiles/Mac").mkdir()
		(engine / "Engine/Build/BatchFiles/Mac/Build.sh").touch()
		(self.root / "VirtualRowing.uproject").write_text("{}")
		for command in (unreal.unreal_smoke, unreal.unreal_shipping, unreal.han_external_cook):
			with self.subTest(command=command.__name__), \
				patch.object(common, "ROOT", self.root), \
				patch.object(common, "find_unreal", return_value=engine), \
				patch.object(unreal, "han_source_verify", return_value=0), \
				patch.object(packaging, "han_external_reference_candidates", return_value=[]), \
				patch.object(unreal, "editor_closed_preflight", side_effect=[True, False]), \
				patch.object(unreal, "unreal_build_preflight", return_value=True), \
				patch.object(unreal.doctor, "check_doctor", return_value=0), \
				patch.object(native, "native_app", return_value=0), \
				patch.object(common, "run") as run, \
				patch.object(packaging, "write_shipping_provenance") as provenance:
				self.assertEqual(command(), 1)
				run.assert_not_called()
				provenance.assert_not_called()

	def test_cooks_disable_mcp_only_in_the_child_commandlet(self) -> None:
		for builder in (packaging.unreal_shipping_command, packaging.han_cook_command):
			with self.subTest(builder=builder.__name__):
				command = builder(Path("/UE"), Path("/repo with spaces/VirtualRowing.uproject"), Path("/out"))
				options = [arg.split("=", 1)[1] for arg in command if arg.startswith("-AdditionalCookerOptions=")]
				self.assertEqual(options, ["-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False"])
				self.assertNotIn("-ModelContextProtocolStartServer", " ".join(command))
				self.assertIn("-ubtargs=-NoUBA -NoHotReload", command)

	def installed_engine(self) -> Path:
		engine = self.root / "UE_5.8"
		(engine / "Engine/Build/BatchFiles").mkdir(parents=True)
		(engine / "Engine/Build/InstalledBuild.txt").touch()
		(engine / "Engine/Build/BatchFiles/RunUAT.sh").touch()
		return engine

	def test_uat_preflight_preserves_existing_cache_bytes(self) -> None:
		engine = self.installed_engine()
		settings = self.root / "Library/Application Support/Epic/UnrealEngine"
		settings.mkdir(parents=True)
		cache = settings / f"XmlConfigCache-{str(engine.resolve()).replace('/', '+')}.bin"
		cache.write_bytes(b"existing cache")
		before = cache.stat().st_mtime_ns
		with patch.object(unreal.sys, "platform", "darwin"), patch.object(Path, "home", return_value=self.root):
			self.assertTrue(unreal.unreal_build_preflight(engine))
		self.assertEqual(cache.read_bytes(), b"existing cache")
		self.assertEqual(cache.stat().st_mtime_ns, before)
		self.assertEqual(list(settings.rglob("vir-uat-preflight-*")), [])

	def test_denied_uat_cache_blocks_both_cooks_before_native_build_or_uat(self) -> None:
		engine = self.installed_engine()
		(self.root / "VirtualRowing.uproject").write_text("{}")
		for cook in (unreal.unreal_shipping, unreal.han_external_cook):
			with self.subTest(cook=cook.__name__), \
				patch.object(unreal, "editor_closed_preflight", return_value=True), \
				patch.object(unreal.sys, "platform", "darwin"), \
				patch.object(Path, "home", return_value=self.root), \
				patch.object(common, "ROOT", self.root), \
				patch.object(common, "find_unreal", return_value=engine), \
				patch.object(unreal, "han_source_verify", return_value=0), \
				patch.object(packaging, "han_external_reference_candidates", return_value=[]), \
				patch.object(unreal.tempfile, "TemporaryFile", side_effect=PermissionError("sandbox denied")), \
				patch.object(native, "native_app") as native_app, \
				patch.object(common, "run") as run:
				self.assertEqual(cook(), 1)
				native_app.assert_not_called()
				run.assert_not_called()

	def test_denied_trace_directory_blocks_smoke_before_ubt_can_abort(self) -> None:
		engine = self.installed_engine()
		(engine / "Engine/Build/BatchFiles/Mac").mkdir()
		(engine / "Engine/Build/BatchFiles/Mac/Build.sh").touch()
		(self.root / "VirtualRowing.uproject").write_text("{}")
		with patch.object(unreal.sys, "platform", "darwin"), \
			patch.object(unreal, "editor_closed_preflight", return_value=True), \
			patch.object(Path, "home", return_value=self.root), \
			patch.object(common, "ROOT", self.root), \
			patch.object(common, "find_unreal", return_value=engine), \
			patch.object(unreal.doctor, "check_doctor", return_value=0), \
			patch.object(unreal.tempfile, "TemporaryFile", side_effect=PermissionError("sandbox denied")), \
			patch.object(native, "native_app") as native_app, \
			patch.object(common, "run") as run:
			self.assertEqual(unreal.unreal_smoke(), 1)
			native_app.assert_not_called()
			run.assert_not_called()

	def test_water_recipes_can_rewire_existing_materials_without_deleting_rooted_nodes(self) -> None:
		# Emulate the observed engine assertion: any destructive expression edit
		# fails. Execute the actual recipes twice against an existing material.
		for recipe in ("build_water_material.py", "build_water_interaction_material.py"):
			with self.subTest(recipe=recipe):
				api = MagicMock()
				api.MaterialEditingLibrary.delete_all_material_expressions.side_effect = AssertionError("!IsRooted()")
				api.MaterialEditingLibrary.delete_material_expression.side_effect = AssertionError("!IsRooted()")
				with patch.dict(sys.modules, {"unreal": api}), patch.object(sys, "argv", [recipe]):
					for _ in range(2):
						runpy.run_path(str(common.ROOT / "Scripts" / recipe), run_name="__main__")
				self.assertEqual(api.EditorAssetLibrary.save_loaded_asset.call_count, 2)
			self.assertEqual(api.MaterialEditingLibrary.recompile_material.call_count, 2)
			self.assertTrue(api.MaterialEditingLibrary.connect_material_property.called)

	def test_water_bitmap_trial_recipe_builds_its_optional_detail_graph(self) -> None:
		class Texture2D:
			pass
		class Expression:
			def __init__(self, kind):
				self.kind = kind
				self.properties = {}

			def set_editor_property(self, name, value):
				self.properties[name] = value

		for count in (1, 2):
			with self.subTest(sample_count=count):
				api = MagicMock()
				api.Texture2D = Texture2D
				api.LinearColor.side_effect = lambda *channels: channels
				api.load_asset.side_effect = [MagicMock(), Texture2D()]
				expressions = []
				inputs = {}
				outputs = {}

				def create(_material, kind, _x, _y):
					expression = Expression(kind)
					expressions.append(expression)
					return expression

				def connect(source, _output, destination, _input):
					inputs.setdefault(destination, []).append(source)

				def connect_property(source, _output, property_name):
					outputs[property_name] = source

				def ancestors(expression):
					visited = set()
					pending = [expression]
					while pending:
						current = pending.pop()
						if current in visited:
							continue
						visited.add(current)
						pending.extend(inputs.get(current, []))
					return visited

				api.MaterialEditingLibrary.create_material_expression.side_effect = create
				api.MaterialEditingLibrary.connect_material_expressions.side_effect = connect
				api.MaterialEditingLibrary.connect_material_property.side_effect = connect_property
				with patch.dict(sys.modules, {"unreal": api}), patch.object(sys, "argv", [
					"build_water_material.py",
					"/Game/Water/Trial/M_CourseWater_DetailTrial",
					"/Game/Water/Trial/T_Water002_Normal_Trial",
					str(count),
				]):
					runpy.run_path(str(common.ROOT / "Scripts/build_water_material.py"), run_name="__main__")
				samples = [expression for expression in expressions
					if expression.kind is api.MaterialExpressionTextureSampleParameter2D]
				self.assertEqual(len(samples), count)
				variance = next(expression for expression in expressions
					if expression.properties.get("parameter_name") == "DetailSlopeVarianceTrial")
				self.assertIn(variance, ancestors(outputs[api.MaterialProperty.MP_ROUGHNESS]))
				deep_color = next(expression for expression in expressions
					if expression.properties.get("parameter_name") == "DeepColor")
				self.assertEqual(deep_color.properties["default_value"], (.03, .20, .35, 1))
				for sample in samples:
					self.assertIn(sample, ancestors(outputs[api.MaterialProperty.MP_NORMAL]))
				api.MaterialEditingLibrary.recompile_material.assert_called_once()
				api.EditorAssetLibrary.save_loaded_asset.assert_called_once()

	def make_app(self, name: str = "VirtualRowing.app") -> Path:
		app = self.root / name
		(app / "Contents" / "MacOS").mkdir(parents=True)
		(app / "Contents" / "Resources").mkdir(parents=True)
		(app / "Contents" / "MacOS" / app.stem).write_bytes(b"main")
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text(json.dumps(self.versions), encoding="utf-8")
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({"NSBluetoothAlwaysUsageDescription": packaging.BLUETOOTH_USAGE_DESCRIPTION}))
		return app

	def test_uproject_declares_virtualrowing_as_a_default_runtime_module(self) -> None:
		# Without this entry, IProjectManager::LoadModulesForProject() (UE 5.8's
		# LaunchEngineLoop.cpp -> ProjectManager.cpp) never calls StartupModule() on the
		# primary game module: IMPLEMENT_PRIMARY_GAME_MODULE only statically registers the
		# module's factory in a monolithic build, it doesn't request that it be loaded.
		descriptor = json.loads((common.ROOT / "VirtualRowing.uproject").read_text(encoding="utf-8"))
		modules = {module.get("Name"): module for module in descriptor.get("Modules", [])}
		self.assertIn("VirtualRowing", modules)
		self.assertEqual(modules["VirtualRowing"].get("Type"), "Runtime")
		self.assertEqual(modules["VirtualRowing"].get("LoadingPhase"), "Default")

	def test_han_cook_command_cooks_only_the_han_map_into_an_iostore_stage(self) -> None:
		command = packaging.han_cook_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/stage"))
		self.assertIn(f"-map={packaging.HAN_MAP}", command)
		for flag in ("-cook", "-pak", "-iostore", "-stage", "-stagingdirectory=/stage"):
			self.assertIn(flag, command)
		self.assertIn("-cookdir=/repo/Content/Phase2/HanRiver/Materials+/repo/Content/Phase2/HanRiver/Meshes+/repo/Content/Phase2/HanRiver/Textures", command)
		self.assertNotIn("-cookdir=/repo/Content/Phase2/HanRiver", command)
		self.assertNotIn("-archive", command)

	def test_han_runtime_map_dependencies_are_present(self) -> None:
		self.assertEqual(packaging.han_source_failures(common.ROOT / "VirtualRowing.uproject"), [])
		level = common.ROOT / packaging.HAN_MAP_RELATIVE_PATH
		osm_references = set(re.findall(rb"/Game/Phase2/HanRiver/Meshes/Area01/OSM/[A-Za-z0-9_+-]+", level.read_bytes()))
		self.assertEqual(len(osm_references), 164)

	def test_han_source_preflight_reports_missing_osm_mesh(self) -> None:
		level = self.root / packaging.HAN_MAP_RELATIVE_PATH
		level.parent.mkdir(parents=True)
		level.write_bytes(bytes.fromhex("c1832a9e") + b"/Game/Phase2/HanRiver/Meshes/Area01/OSM/SM_Han_A01_OSM_BuildingTile_E+0000_N+0000")
		failures = packaging.han_source_failures(self.root / "VirtualRowing.uproject")
		self.assertEqual(len(failures), 1)
		self.assertIn("missing Han source package", failures[0])

	def test_han_source_preflight_follows_material_texture_dependencies(self) -> None:
		level = self.root / packaging.HAN_MAP_RELATIVE_PATH
		level.parent.mkdir(parents=True)
		level.write_bytes(bytes.fromhex("c1832a9e") +
			b"/Game/Phase2/HanRiver/Meshes/Area01/OSM/SM_Tile " +
			b"/Game/Phase2/HanRiver/Materials/M_Han_Water")
		mesh = self.root / "Content/Phase2/HanRiver/Meshes/Area01/OSM/SM_Tile.uasset"
		mesh.parent.mkdir(parents=True)
		mesh.write_bytes(bytes.fromhex("c1832a9e") + b"mesh")
		material = self.root / "Content/Phase2/HanRiver/Materials/M_Han_Water.uasset"
		material.parent.mkdir(parents=True)
		material.write_bytes(bytes.fromhex("c1832a9e") +
			b"/Game/Phase2/HanRiver/Textures/T_WaterNormal")
		failures = packaging.han_source_failures(self.root / "VirtualRowing.uproject")
		self.assertEqual(failures, [f"missing Han source package: {self.root / 'Content/Phase2/HanRiver/Textures/T_WaterNormal.uasset'}"])
		texture = self.root / "Content/Phase2/HanRiver/Textures/T_WaterNormal.uasset"
		texture.parent.mkdir(parents=True)
		texture.write_bytes(bytes.fromhex("c1832a9e") + b"texture")
		self.assertEqual(packaging.han_source_failures(self.root / "VirtualRowing.uproject"), [])
		tracked = b"\0".join(path.as_posix().encode("utf-8") for path in (
			packaging.HAN_MAP_RELATIVE_PATH,
			mesh.relative_to(self.root),
			material.relative_to(self.root),
		)) + b"\0"
		with patch.object(packaging.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout=tracked)):
			failures = packaging.han_source_failures(self.root / "VirtualRowing.uproject", require_tracked=True)
		self.assertEqual(failures, [f"Han source package is not tracked by Git: {texture.relative_to(self.root)}"])

	def test_han_external_path_diagnostic_covers_runtime_map_and_cook_directories(self) -> None:
		level = self.root / packaging.HAN_MAP_RELATIVE_PATH
		level.parent.mkdir(parents=True)
		level.write_bytes(bytes.fromhex("c1832a9e") +
			b"/Game/Phase2/HanRiver/Materials/M_Han_Water " +
			b"/Game/Water/Trial/MI_Han_Water_RoughnessTrial")
		material = self.root / "Content/Phase2/HanRiver/Materials/M_Han_Water.uasset"
		material.parent.mkdir(parents=True)
		material.write_bytes(bytes.fromhex("c1832a9e") +
			b"/Game/Water/Trial/T_Water002_Normal_Trial")
		review = level.with_name("L_HanRiver_Area01_Review.umap")
		review.write_bytes(bytes.fromhex("c1832a9e") + b"/Game/Water/Trial/ReviewOnly")
		self.assertEqual(packaging.han_external_reference_candidates(self.root / "VirtualRowing.uproject"), [
			f"{level.relative_to(self.root)}: /Game/Water/Trial/MI_Han_Water_RoughnessTrial",
			f"{material.relative_to(self.root)}: /Game/Water/Trial/T_Water002_Normal_Trial",
		])

	def test_han_cook_stops_before_build_when_external_project_paths_need_editor_review(self) -> None:
		candidate = "Content/Phase2/HanRiver/Maps/L_HanRiver_BlueHour.umap: /Game/Water/Trial/MI_Han_Water_RoughnessTrial"
		with patch.object(unreal, "editor_closed_preflight", return_value=True), \
			patch.object(unreal, "han_source_verify", return_value=0), \
			patch.object(packaging, "han_external_reference_candidates", return_value=[candidate]), \
			patch.object(common, "load_versions") as versions, \
			patch.object(native, "native_app") as native_app, \
			patch.object(common, "run") as run:
			self.assertEqual(unreal.han_external_cook(), 1)
			versions.assert_not_called()
			native_app.assert_not_called()
			run.assert_not_called()

	def test_han_source_preflight_rejects_untracked_mesh(self) -> None:
		level = self.root / packaging.HAN_MAP_RELATIVE_PATH
		level.parent.mkdir(parents=True)
		level.write_bytes(bytes.fromhex("c1832a9e") + b"/Game/Phase2/HanRiver/Meshes/Area01/OSM/SM_Han_A01_OSM_BuildingTile_E+0000_N+0000")
		mesh = self.root / "Content/Phase2/HanRiver/Meshes/Area01/OSM/SM_Han_A01_OSM_BuildingTile_E+0000_N+0000.uasset"
		mesh.parent.mkdir(parents=True)
		mesh.write_bytes(bytes.fromhex("c1832a9e") + b"mesh")
		with patch.object(packaging.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout=b"Content/Phase2/HanRiver/Maps/L_HanRiver_BlueHour.umap\0")):
			failures = packaging.han_source_failures(self.root / "VirtualRowing.uproject", require_tracked=True)
		self.assertEqual(failures, [f"Han source package is not tracked by Git: {mesh.relative_to(self.root)}"])

	def test_han_river_runtime_map_is_the_bluehour_level_not_area01(self) -> None:
		# The binary is shipped separately from the cook; both must name the same
		# level or the cooked map never resolves at runtime.
		self.assertEqual(packaging.HAN_MAP, "/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour")
		route_table = (common.ROOT / "Source" / "ContentRuntime" / "Private" / "CourseLevel.cpp").read_text(encoding="utf-8")
		self.assertIn(f'return std::string("{packaging.HAN_MAP}")', route_table)
		self.assertNotIn("Area01_BlueHour", route_table)

	def test_han_pak_rules_route_han_content_to_a_dedicated_chunk(self) -> None:
		self.assertIn(f"OverridePaks={packaging.HAN_PAK_CHUNK}", packaging.HAN_PAK_FILE_RULES)
		self.assertIn("Content/Phase2/HanRiver/", packaging.HAN_PAK_FILE_RULES)

	def test_han_chunk_files_requires_exactly_one_of_each_extension(self) -> None:
		for extension in ("pak", "utoc"):
			(self.root / f"{packaging.HAN_PAK_CHUNK}-Mac.{extension}").write_bytes(b"x")
		self.assertIsNone(unreal._han_chunk_files(self.root))
		(self.root / f"{packaging.HAN_PAK_CHUNK}-Mac.ucas").write_bytes(b"x")
		self.assertEqual(set(unreal._han_chunk_files(self.root)), {"pak", "utoc", "ucas"})
		app_paks = self.root / "V.app" / "Contents" / "Paks"
		app_paks.mkdir(parents=True)
		(app_paks / f"{packaging.HAN_PAK_CHUNK}-Mac.pak").write_bytes(b"x")
		self.assertEqual(set(unreal._han_chunk_files(self.root)), {"pak", "utoc", "ucas"})

	def test_unreal_shipping_command_requests_arm64_shipping_archive(self) -> None:
		command = packaging.unreal_shipping_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/out"))
		self.assertIn("BuildCookRun", command)
		self.assertIn("-clientconfig=Shipping", command)
		self.assertIn("-specifiedarchitecture=arm64", command)
		self.assertIn("-archive", command)
		self.assertIn("-archivedirectory=/out", command)

	def test_unreal_shipping_command_requests_explicit_pak_iostore_and_culture(self) -> None:
		command = packaging.unreal_shipping_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/out"))
		self.assertIn("-pak", command)
		self.assertIn("-iostore", command)
		self.assertIn("-CookCultures=en", command)
		self.assertIn("-I18NPreset=English", command)

	def test_unreal_shipping_embeds_uproject_file_for_staged_project_dir_resolution(self) -> None:
		# UAT's Mac stage/package step embeds Binaries/ and Content/ under
		# Contents/UE/<ProjectName>/ but never copies the .uproject file itself there, which
		# silently prevents IProjectManager::LoadProjectFile() from ever succeeding (see
		# FGenericPlatformMisc::ProjectDir()'s relative fallback search) and StartupModule()
		# from ever running on the primary game module.
		project = self.root / "VirtualRowing.uproject"
		project.write_text(json.dumps({"FileVersion": 3}), encoding="utf-8")
		ue_root = self.root / "UE_5.8"
		(ue_root / "Engine" / "Build" / "BatchFiles").mkdir(parents=True)
		(ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh").write_bytes(b"")

		archive_dir = self.root / "archive"
		app = archive_dir / "Mac" / "VirtualRowing-Mac-Shipping.app"
		(app / "Contents" / "MacOS").mkdir(parents=True)

		def fake_run(args, **_kwargs):
			return SimpleNamespace(returncode=0, args=args)

		with patch.object(common, "ROOT", self.root), \
			patch.object(unreal, "editor_closed_preflight", return_value=True), \
			patch.object(unreal.doctor, "check_doctor", return_value=0), \
			patch.object(common, "UNREAL_ARCHIVE_DIR", archive_dir), \
			patch.object(common, "load_versions", return_value=self.versions), \
			patch.object(common, "find_unreal", return_value=ue_root), \
			patch.object(common, "tool_env", return_value={}), \
			patch.object(common, "source_revision", return_value="deadbeef"), \
			patch.object(packaging, "write_shipping_provenance"), \
			patch.object(native, "native_app", return_value=0) as native_app, \
			patch.object(common, "run", side_effect=fake_run):
			self.assertEqual(unreal.unreal_shipping(), 0)
			native_app.assert_called_once()

		embedded = app / "Contents" / "UE" / "VirtualRowing" / "VirtualRowing.uproject"
		self.assertTrue(embedded.is_file())
		self.assertEqual(embedded.read_text(encoding="utf-8"), project.read_text(encoding="utf-8"))

	def test_package_verifier_rejects_missing_app(self) -> None:
		self.assertEqual(packaging.verify_package(self.root / "missing.app", self.versions), [f"missing staged app: {self.root / 'missing.app'}"])

	def test_package_verifier_rejects_wrong_architecture(self) -> None:
		app = self.make_app()
		with patch.object(packaging, "binary_architectures", return_value={"x86_64"}), patch.object(packaging, "plugin_module_linked", return_value=True):
			self.assertTrue(any("wrong architecture" in value for value in packaging.verify_package(app, self.versions)))

	def test_package_verifier_rejects_missing_bluetooth_usage_description(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({}))
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), patch.object(packaging, "plugin_module_linked", return_value=True):
			self.assertIn("missing or incorrect NSBluetoothAlwaysUsageDescription", packaging.verify_package(app, self.versions))

	def test_package_verifier_rejects_mismatched_build_metadata(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text("{}", encoding="utf-8")
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), patch.object(packaging, "plugin_module_linked", return_value=True):
			self.assertIn("staged BuildVersions.json does not match Config/BuildVersions.json", packaging.verify_package(app, self.versions))

	def test_package_verifier_rejects_missing_expected_binary(self) -> None:
		app = self.make_app()
		(app / "Contents" / "MacOS" / "VirtualRowing").unlink()
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}):
			self.assertIn("missing expected executable: Contents/MacOS/VirtualRowing", packaging.verify_package(app, self.versions))

	def test_package_verifier_rejects_unlinked_plugin_module(self) -> None:
		app = self.make_app()
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), patch.object(packaging, "plugin_module_linked", return_value=False):
			self.assertIn(
				f"Concept2PM plug-in module ({packaging.CONCEPT2PM_MODULE_NAME}) is not linked into the Shipping executable",
				packaging.verify_package(app, self.versions),
			)

	def test_homebrew_load_commands_reports_only_homebrew_install_names(self) -> None:
		otool_output = "\n".join([
			"/x/libVirtualRowing.dylib:",
			"\t/usr/lib/libsqlite3.dylib (compatibility version 9.0.0, current version 377.0.0)",
			"\t/opt/homebrew/opt/protobuf/lib/libprotobuf.36.1.0.dylib (compatibility version 36.0.0, current version 36.1.0)",
			"\t/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation (compatibility version 300.0.0, current version 4109.1.255)",
		])
		with patch.object(common, "capture", return_value=otool_output):
			self.assertEqual(packaging.homebrew_load_commands(self.root / "x"), ["/opt/homebrew/opt/protobuf/lib/libprotobuf.36.1.0.dylib"])
		with patch.object(common, "capture", return_value=None):
			self.assertEqual(packaging.homebrew_load_commands(self.root / "x"), [])

	def test_package_verifier_rejects_homebrew_load_commands(self) -> None:
		app = self.make_app()
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), \
			patch.object(packaging, "plugin_module_linked", return_value=True), \
			patch.object(packaging, "homebrew_load_commands", return_value=["/opt/homebrew/lib/libabsl_base.2601.0.0.dylib"]):
			self.assertTrue(any("not self-contained" in value for value in packaging.verify_package(app, self.versions)))

	def test_non_system_swift_load_commands_allows_only_the_os_runtime(self) -> None:
		otool_output = "\n".join([
			"/x/libVirtualRowing.dylib:",
			"\t/usr/lib/swift/libswiftCore.dylib (compatibility version 1.0.0, current version 1.0.0)",
			"\t@rpath/libswiftCore.dylib (compatibility version 1.0.0, current version 1.0.0)",
			"\t/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx/libswiftCompatibility56.dylib (compatibility version 1.0.0, current version 1.0.0)",
			"\t/System/Library/Frameworks/Security.framework/Versions/A/Security (compatibility version 1.0.0, current version 61040.0.0)",
		])
		with patch.object(common, "capture", return_value=otool_output):
			self.assertEqual(packaging.non_system_swift_load_commands(self.root / "x"), [
				"@rpath/libswiftCore.dylib",
				"/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx/libswiftCompatibility56.dylib",
			])
		with patch.object(common, "capture", return_value=None):
			self.assertEqual(packaging.non_system_swift_load_commands(self.root / "x"), [])

	def test_package_verifier_rejects_a_bundled_swift_runtime(self) -> None:
		app = self.make_app()
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), \
			patch.object(packaging, "plugin_module_linked", return_value=True), \
			patch.object(packaging, "homebrew_load_commands", return_value=[]), \
			patch.object(packaging, "non_system_swift_load_commands", return_value=["@rpath/libswiftCore.dylib"]):
			self.assertTrue(any("non-system Swift runtime" in value for value in packaging.verify_package(app, self.versions)))

	def test_plugin_module_linked_detects_symbol_in_nm_output(self) -> None:
		with patch.object(common, "capture", return_value="0000000000000000 t __ZN23FConcept2PMUnrealModuleD1Ev"):
			self.assertTrue(packaging.plugin_module_linked(self.root / "VirtualRowing", packaging.CONCEPT2PM_MODULE_NAME))

	def test_plugin_module_linked_rejects_absent_symbol(self) -> None:
		with patch.object(common, "capture", return_value="0000000000000000 t _main"):
			self.assertFalse(packaging.plugin_module_linked(self.root / "VirtualRowing", packaging.CONCEPT2PM_MODULE_NAME))

	def test_staged_app_finds_shipping_suffixed_bundle_name(self) -> None:
		archive_dir = self.root / "archive"
		app = archive_dir / "Mac" / "VirtualRowing-Mac-Shipping.app"
		(app / "Contents" / "MacOS").mkdir(parents=True)
		self.assertEqual(packaging.staged_app(archive_dir), app)

	def test_package_verifier_accepts_shipping_suffixed_bundle_name(self) -> None:
		app = self.make_app(name="VirtualRowing-Mac-Shipping.app")
		with patch.object(packaging, "binary_architectures", return_value={"arm64"}), patch.object(packaging, "plugin_module_linked", return_value=True):
			self.assertEqual(packaging.verify_package(app, self.versions), [])

	def test_app_bundle_hash_changes_when_bundle_content_changes(self) -> None:
		app = self.make_app()
		before = packaging.app_bundle_sha256(app)
		(app / "Contents" / "MacOS" / "VirtualRowing").write_bytes(b"changed-main")
		self.assertNotEqual(before, packaging.app_bundle_sha256(app))

	def test_bluetooth_probe_result_accepts_exact_redacted_schema(self) -> None:
		result = self.root / "toolchain-bluetooth-probe.json"
		result.write_text(json.dumps({
			"schema_version": 1,
			"source_revision": "0123456789ab",
			"toolchain_fingerprint": "ue-5.8.2;xcode-26.1.1;macos-26.6.2;arm64",
			"timestamp_utc": "2026-09-16T00:00:00Z",
			"result_state": "denied",
			"duration_ms": 2,
		}), encoding="utf-8")
		self.assertIsNone(release.verify_bluetooth_probe_result(result))

	def test_bluetooth_probe_result_rejects_unredacted_fields(self) -> None:
		result = self.root / "toolchain-bluetooth-probe.json"
		result.write_text(json.dumps({
			"schema_version": 1,
			"source_revision": "0123456789ab",
			"toolchain_fingerprint": "fingerprint",
			"timestamp_utc": "2026-09-16T00:00:00Z",
			"result_state": "denied",
			"duration_ms": 2,
			"peripheral_identifier": "must-not-appear",
		}), encoding="utf-8")
		self.assertEqual(release.verify_bluetooth_probe_result(result), "unexpected fields")

	def test_toolchain_bluetooth_probe_invokes_shipping_suffixed_executable(self) -> None:
		archive_dir = self.root / "Build" / "unreal-shipping" / "archive"
		app = self.make_app(name="VirtualRowing-Mac-Shipping.app")
		(archive_dir / "Mac").mkdir(parents=True)
		app.rename(archive_dir / "Mac" / "VirtualRowing-Mac-Shipping.app")
		app = archive_dir / "Mac" / "VirtualRowing-Mac-Shipping.app"
		probe_directory = self.root / "Saved" / "Logs"
		probe_directory.mkdir(parents=True)

		def fake_run(args, **_kwargs):
			result_path = probe_directory / "toolchain-bluetooth-probe-test.json"
			result_path.write_text(json.dumps({
				"schema_version": release.BLUETOOTH_PROBE_SCHEMA_VERSION,
				"source_revision": "0123456789ab",
				"toolchain_fingerprint": "ue-5.8.2;xcode-26.1.1;macos-26.6.2;arm64",
				"timestamp_utc": "2026-09-16T00:00:00Z",
				"result_state": "denied",
				"duration_ms": 2,
			}), encoding="utf-8")
			return SimpleNamespace(returncode=0, args=args)

		with patch.object(common, "ROOT", self.root), \
			patch.object(common, "UNREAL_ARCHIVE_DIR", archive_dir), \
			patch.object(common, "load_versions", return_value=self.versions), \
			patch.object(common, "tool_env", return_value={}), \
			patch.object(packaging, "binary_architectures", return_value={"arm64"}), \
			patch.object(packaging, "plugin_module_linked", return_value=True), \
			patch.object(common, "run", side_effect=fake_run) as mock_run:
			self.assertEqual(release.toolchain_bluetooth_probe(), 0)

		invoked_executable = mock_run.call_args[0][0][0]
		self.assertEqual(invoked_executable, str(app / "Contents" / "MacOS" / "VirtualRowing-Mac-Shipping"))

	def test_release_signature_rejects_missing_hardened_runtime(self) -> None:
		with patch.object(common, "capture_combined", return_value="flags=0x10000\n"), patch.object(common, "capture", return_value=""):
			self.assertEqual(
				release.verify_release_signature(self.root / "VirtualRowing.app"),
				"Hardened Runtime is not enabled",
			)

	def test_release_signature_rejects_get_task_allow(self) -> None:
		with patch.object(common, "capture_combined", return_value="flags=0x10000(runtime)\n"), patch.object(common, "capture", return_value="<key>get-task-allow</key>"):
			self.assertEqual(
				release.verify_release_signature(self.root / "VirtualRowing.app"),
				"get-task-allow must be absent from Shipping entitlements",
			)

	def test_release_signature_accepts_hardened_runtime_without_debug_entitlement(self) -> None:
		with patch.object(common, "capture_combined", return_value="flags=0x10000(runtime)\n"), patch.object(common, "capture", return_value="<dict/>"):
			self.assertIsNone(release.verify_release_signature(self.root / "VirtualRowing.app"))


if __name__ == "__main__":
	unittest.main()
