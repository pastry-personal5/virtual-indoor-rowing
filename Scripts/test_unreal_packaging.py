#!/usr/bin/env python3
"""Behavioral tests for the unsigned Unreal Shipping package guardrails."""

from __future__ import annotations

import importlib.util
import json
import plistlib
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("dev", Path(__file__).with_name("dev.py"))
assert SPEC and SPEC.loader
dev = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dev)


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

	def make_app(self, name: str = "VirtualRowing.app") -> Path:
		app = self.root / name
		(app / "Contents" / "MacOS").mkdir(parents=True)
		(app / "Contents" / "Resources").mkdir(parents=True)
		(app / "Contents" / "MacOS" / app.stem).write_bytes(b"main")
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text(json.dumps(self.versions), encoding="utf-8")
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({"NSBluetoothAlwaysUsageDescription": dev.BLUETOOTH_USAGE_DESCRIPTION}))
		return app

	def test_uproject_declares_virtualrowing_as_a_default_runtime_module(self) -> None:
		# Without this entry, IProjectManager::LoadModulesForProject() (UE 5.8's
		# LaunchEngineLoop.cpp -> ProjectManager.cpp) never calls StartupModule() on the
		# primary game module: IMPLEMENT_PRIMARY_GAME_MODULE only statically registers the
		# module's factory in a monolithic build, it doesn't request that it be loaded.
		descriptor = json.loads((dev.ROOT / "VirtualRowing.uproject").read_text(encoding="utf-8"))
		modules = {module.get("Name"): module for module in descriptor.get("Modules", [])}
		self.assertIn("VirtualRowing", modules)
		self.assertEqual(modules["VirtualRowing"].get("Type"), "Runtime")
		self.assertEqual(modules["VirtualRowing"].get("LoadingPhase"), "Default")

	def test_unreal_shipping_command_requests_arm64_shipping_archive(self) -> None:
		command = dev.unreal_shipping_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/out"))
		self.assertIn("BuildCookRun", command)
		self.assertIn("-clientconfig=Shipping", command)
		self.assertIn("-specifiedarchitecture=arm64", command)
		self.assertIn("-archive", command)
		self.assertIn("-archivedirectory=/out", command)

	def test_unreal_shipping_command_requests_explicit_pak_iostore_and_culture(self) -> None:
		command = dev.unreal_shipping_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/out"))
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

		with patch.object(dev, "ROOT", self.root), \
			patch.object(dev, "UNREAL_ARCHIVE_DIR", archive_dir), \
			patch.object(dev, "load_versions", return_value=self.versions), \
			patch.object(dev, "find_unreal", return_value=ue_root), \
			patch.object(dev, "tool_env", return_value={}), \
			patch.object(dev, "source_revision", return_value="deadbeef"), \
			patch.object(dev, "write_shipping_provenance"), \
			patch.object(dev, "run", side_effect=fake_run):
			self.assertEqual(dev.unreal_shipping(), 0)

		embedded = app / "Contents" / "UE" / "VirtualRowing" / "VirtualRowing.uproject"
		self.assertTrue(embedded.is_file())
		self.assertEqual(embedded.read_text(encoding="utf-8"), project.read_text(encoding="utf-8"))

	def test_package_verifier_rejects_missing_app(self) -> None:
		self.assertEqual(dev.verify_package(self.root / "missing.app", self.versions), [f"missing staged app: {self.root / 'missing.app'}"])

	def test_package_verifier_rejects_wrong_architecture(self) -> None:
		app = self.make_app()
		with patch.object(dev, "binary_architectures", return_value={"x86_64"}), patch.object(dev, "plugin_module_linked", return_value=True):
			self.assertTrue(any("wrong architecture" in value for value in dev.verify_package(app, self.versions)))

	def test_package_verifier_rejects_missing_bluetooth_usage_description(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({}))
		with patch.object(dev, "binary_architectures", return_value={"arm64"}), patch.object(dev, "plugin_module_linked", return_value=True):
			self.assertIn("missing or incorrect NSBluetoothAlwaysUsageDescription", dev.verify_package(app, self.versions))

	def test_package_verifier_rejects_mismatched_build_metadata(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text("{}", encoding="utf-8")
		with patch.object(dev, "binary_architectures", return_value={"arm64"}), patch.object(dev, "plugin_module_linked", return_value=True):
			self.assertIn("staged BuildVersions.json does not match Config/BuildVersions.json", dev.verify_package(app, self.versions))

	def test_package_verifier_rejects_missing_expected_binary(self) -> None:
		app = self.make_app()
		(app / "Contents" / "MacOS" / "VirtualRowing").unlink()
		with patch.object(dev, "binary_architectures", return_value={"arm64"}):
			self.assertIn("missing expected executable: Contents/MacOS/VirtualRowing", dev.verify_package(app, self.versions))

	def test_package_verifier_rejects_unlinked_plugin_module(self) -> None:
		app = self.make_app()
		with patch.object(dev, "binary_architectures", return_value={"arm64"}), patch.object(dev, "plugin_module_linked", return_value=False):
			self.assertIn(
				f"Concept2PM plug-in module ({dev.CONCEPT2PM_MODULE_NAME}) is not linked into the Shipping executable",
				dev.verify_package(app, self.versions),
			)

	def test_plugin_module_linked_detects_symbol_in_nm_output(self) -> None:
		with patch.object(dev, "capture", return_value="0000000000000000 t __ZN23FConcept2PMUnrealModuleD1Ev"):
			self.assertTrue(dev.plugin_module_linked(self.root / "VirtualRowing", dev.CONCEPT2PM_MODULE_NAME))

	def test_plugin_module_linked_rejects_absent_symbol(self) -> None:
		with patch.object(dev, "capture", return_value="0000000000000000 t _main"):
			self.assertFalse(dev.plugin_module_linked(self.root / "VirtualRowing", dev.CONCEPT2PM_MODULE_NAME))

	def test_staged_app_finds_shipping_suffixed_bundle_name(self) -> None:
		archive_dir = self.root / "archive"
		app = archive_dir / "Mac" / "VirtualRowing-Mac-Shipping.app"
		(app / "Contents" / "MacOS").mkdir(parents=True)
		self.assertEqual(dev.staged_app(archive_dir), app)

	def test_package_verifier_accepts_shipping_suffixed_bundle_name(self) -> None:
		app = self.make_app(name="VirtualRowing-Mac-Shipping.app")
		with patch.object(dev, "binary_architectures", return_value={"arm64"}), patch.object(dev, "plugin_module_linked", return_value=True):
			self.assertEqual(dev.verify_package(app, self.versions), [])

	def test_app_bundle_hash_changes_when_bundle_content_changes(self) -> None:
		app = self.make_app()
		before = dev.app_bundle_sha256(app)
		(app / "Contents" / "MacOS" / "VirtualRowing").write_bytes(b"changed-main")
		self.assertNotEqual(before, dev.app_bundle_sha256(app))

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
		self.assertIsNone(dev.verify_bluetooth_probe_result(result))

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
		self.assertEqual(dev.verify_bluetooth_probe_result(result), "unexpected fields")

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
				"schema_version": dev.BLUETOOTH_PROBE_SCHEMA_VERSION,
				"source_revision": "0123456789ab",
				"toolchain_fingerprint": "ue-5.8.2;xcode-26.1.1;macos-26.6.2;arm64",
				"timestamp_utc": "2026-09-16T00:00:00Z",
				"result_state": "denied",
				"duration_ms": 2,
			}), encoding="utf-8")
			return SimpleNamespace(returncode=0, args=args)

		with patch.object(dev, "ROOT", self.root), \
			patch.object(dev, "UNREAL_ARCHIVE_DIR", archive_dir), \
			patch.object(dev, "load_versions", return_value=self.versions), \
			patch.object(dev, "tool_env", return_value={}), \
			patch.object(dev, "binary_architectures", return_value={"arm64"}), \
			patch.object(dev, "plugin_module_linked", return_value=True), \
			patch.object(dev, "run", side_effect=fake_run) as mock_run:
			self.assertEqual(dev.toolchain_bluetooth_probe(), 0)

		invoked_executable = mock_run.call_args[0][0][0]
		self.assertEqual(invoked_executable, str(app / "Contents" / "MacOS" / "VirtualRowing-Mac-Shipping"))

	def test_release_signature_rejects_missing_hardened_runtime(self) -> None:
		with patch.object(dev, "capture_combined", return_value="flags=0x10000\n"), patch.object(dev, "capture", return_value=""):
			self.assertEqual(
				dev.verify_release_signature(self.root / "VirtualRowing.app"),
				"Hardened Runtime is not enabled",
			)

	def test_release_signature_rejects_get_task_allow(self) -> None:
		with patch.object(dev, "capture_combined", return_value="flags=0x10000(runtime)\n"), patch.object(dev, "capture", return_value="<key>get-task-allow</key>"):
			self.assertEqual(
				dev.verify_release_signature(self.root / "VirtualRowing.app"),
				"get-task-allow must be absent from Shipping entitlements",
			)

	def test_release_signature_accepts_hardened_runtime_without_debug_entitlement(self) -> None:
		with patch.object(dev, "capture_combined", return_value="flags=0x10000(runtime)\n"), patch.object(dev, "capture", return_value="<dict/>"):
			self.assertIsNone(dev.verify_release_signature(self.root / "VirtualRowing.app"))


if __name__ == "__main__":
	unittest.main()
