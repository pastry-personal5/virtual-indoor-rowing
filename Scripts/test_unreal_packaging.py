#!/usr/bin/env python3
"""Behavioral tests for the unsigned Unreal Shipping package guardrails."""

from __future__ import annotations

import importlib.util
import json
import plistlib
import tempfile
import unittest
from pathlib import Path
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

	def make_app(self) -> Path:
		app = self.root / "VirtualRowing.app"
		(app / "Contents" / "MacOS").mkdir(parents=True)
		(app / "Contents" / "Resources").mkdir(parents=True)
		(app / "Contents" / "PlugIns").mkdir(parents=True)
		(app / "Contents" / "MacOS" / "VirtualRowing").write_bytes(b"main")
		(app / "Contents" / "PlugIns" / "libConcept2PMUnreal.dylib").write_bytes(b"plugin")
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text(json.dumps(self.versions), encoding="utf-8")
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({"NSBluetoothAlwaysUsageDescription": dev.BLUETOOTH_USAGE_DESCRIPTION}))
		return app

	def test_unreal_shipping_command_requests_arm64_shipping_archive(self) -> None:
		command = dev.unreal_shipping_command(Path("/UE"), Path("/repo/VirtualRowing.uproject"), Path("/out"))
		self.assertIn("BuildCookRun", command)
		self.assertIn("-clientconfig=Shipping", command)
		self.assertIn("-specifiedarchitecture=arm64", command)
		self.assertIn("-archive", command)
		self.assertIn("-archivedirectory=/out", command)

	def test_package_verifier_rejects_missing_app(self) -> None:
		self.assertEqual(dev.verify_package(self.root / "missing.app", self.versions), [f"missing staged app: {self.root / 'missing.app'}"])

	def test_package_verifier_rejects_wrong_architecture(self) -> None:
		app = self.make_app()
		with patch.object(dev, "binary_architectures", return_value={"x86_64"}):
			self.assertTrue(any("wrong architecture" in value for value in dev.verify_package(app, self.versions)))

	def test_package_verifier_rejects_missing_bluetooth_usage_description(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({}))
		with patch.object(dev, "binary_architectures", return_value={"arm64"}):
			self.assertIn("missing or incorrect NSBluetoothAlwaysUsageDescription", dev.verify_package(app, self.versions))

	def test_package_verifier_rejects_mismatched_build_metadata(self) -> None:
		app = self.make_app()
		(app / "Contents" / "Resources" / "BuildVersions.json").write_text("{}", encoding="utf-8")
		with patch.object(dev, "binary_architectures", return_value={"arm64"}):
			self.assertIn("staged BuildVersions.json does not match Config/BuildVersions.json", dev.verify_package(app, self.versions))

	def test_package_verifier_rejects_missing_expected_binary(self) -> None:
		app = self.make_app()
		(app / "Contents" / "MacOS" / "VirtualRowing").unlink()
		with patch.object(dev, "binary_architectures", return_value={"arm64"}):
			self.assertIn("missing expected executable: Contents/MacOS/VirtualRowing", dev.verify_package(app, self.versions))

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


if __name__ == "__main__":
	unittest.main()
