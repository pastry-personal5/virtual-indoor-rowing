#!/usr/bin/env python3
"""Exercise manifest generation with checked-in, synthetic, and rejected inputs."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from generate_pm5_capabilities import ManifestError, generate, render_header, validate_manifest


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "Config" / "PM5Capabilities.json"


def valid_profile() -> dict:
	return {
		"monitor_model": "Synthetic PM5",
		"hardware_revision": "test-hw",
		"firmware_revision": "test-fw",
		"machine_kind": "IndoorRower",
		"support_state": "Allowed",
		"required_characteristics": [
			{
				"short_id": "0x0031",
				"required_properties": ["notify"],
				"allowed_packet_lengths": [19],
				"implemented_fields": ["source_elapsed", "distance", "drag_factor"],
			},
			{
				"short_id": "0x0032",
				"required_properties": ["indicate"],
				"allowed_packet_lengths": [17],
				"implemented_fields": ["source_elapsed", "speed", "heart_rate"],
			},
		],
		"optional_characteristics": [
			{
				"short_id": "0x0033",
				"required_properties": ["notify"],
				"allowed_packet_lengths": [20],
				"implemented_fields": ["average_power"],
			},
			{
				"short_id": "0x0034",
				"required_properties": ["read", "write"],
				"allowed_packet_lengths": [],
				"implemented_fields": [],
			},
		],
		"requested_status_period_ms": 100,
		"stale_after_ms": 500,
		"blocking_warning_after_ms": 1500,
		"source_spec_sha256": "a" * 64,
		"evidence_reference": "synthetic-fixture://manifest-generator-test",
		"approved_on": "2026-01-02",
	}


def valid_manifest() -> dict:
	return {
		"$schema": "./PM5Capabilities.schema.json",
		"schema_version": 1,
		"profile_version": 7,
		"profiles": [valid_profile()],
	}


def add_unknown_0036_layout(manifest: dict) -> None:
	manifest["profiles"][0]["optional_characteristics"].append(
		{
			"short_id": "0x0036",
			"required_properties": ["notify"],
			"allowed_packet_lengths": [16],
			"implemented_fields": ["stroke_power"],
		}
	)


class PM5CapabilityManifestTests(unittest.TestCase):
	def test_checked_in_manifest_generates_reviewed_pm5_allow_list(self) -> None:
		manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
		normalized = validate_manifest(manifest)
		self.assertEqual(normalized["profile_version"], 6)
		self.assertEqual(len(normalized["profiles"]), 1)
		profile = normalized["profiles"][0]
		self.assertEqual(profile["model"], "PM5")
		self.assertEqual(profile["hardware"], "634")
		self.assertEqual(profile["firmware"], "8200-000372-178.069")
		self.assertEqual(profile["support_state"], "Allowed")
		self.assertEqual(
			[(characteristic["id"], characteristic["lengths"]) for characteristic in profile["characteristics"]],
			[(0x0031, [19]), (0x0032, [17]), (0x0034, []), (0x0035, [20]), (0x0036, [15, 18])],
		)
		header = render_header(manifest)
		self.assertIn("GetGeneratedPM5CapabilityProfiles()", header)
		self.assertIn("Profiles.reserve(1);", header)
		self.assertIn('Profile.MonitorModel = "PM5";', header)
		self.assertIn("Profile.Version = 6U;", header)
		self.assertIn(
			"0x0034U, false, false, ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Read) | ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Write), {}",
			header,
		)
		self.assertIn("ERowingMetric::PeakDriveForce", header)
		self.assertIn("ERowingMetric::WorkPerStroke", header)
		self.assertIn("ERowingMetric::ProjectedWorkOther", header)

	def test_synthetic_profile_generates_exact_runtime_tuple_and_metrics(self) -> None:
		manifest = valid_manifest()
		normalized = validate_manifest(manifest)
		self.assertEqual(len(normalized["profiles"]), 1)
		with tempfile.TemporaryDirectory(prefix="pm5-capability-test-") as temp_dir:
			temporary = Path(temp_dir)
			input_path = temporary / "fixture.json"
			output_path = temporary / "generated" / "PM5CapabilityProfiles.generated.h"
			input_path.write_text(json.dumps(manifest), encoding="utf-8")
			generate(input_path, output_path)
			generated = output_path.read_text(encoding="utf-8")
			self.assertIn('Profile.MonitorModel = "Synthetic PM5";', generated)
			self.assertIn("Profile.Version = 7U;", generated)
			self.assertIn("Profile.RequestedStatusPeriodMs = 100U;", generated)
			self.assertIn("0x0031U, true, true, ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Notify), {19}", generated)
			self.assertIn("0x0033U, false, true, ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Notify), {20}", generated)
			self.assertIn("0x0034U, false, false, ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Read) | ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Write), {}", generated)
			self.assertIn("ERowingMetric::Distance", generated)
			self.assertIn("ERowingMetric::HeartRate", generated)

	def test_synthetic_generated_header_compiles(self) -> None:
		with tempfile.TemporaryDirectory(prefix="pm5-capability-compile-") as temp_dir:
			temporary = Path(temp_dir)
			output_path = temporary / "PM5CapabilityProfiles.generated.h"
			input_path = temporary / "fixture.json"
			input_path.write_text(json.dumps(valid_manifest()), encoding="utf-8")
			generate(input_path, output_path)
			source_path = temporary / "generated_profile_compile.cpp"
			source_path.write_text(
				"#include \"PM5CapabilityProfiles.generated.h\"\n"
				"int main() { return Concept2PM::GetGeneratedPM5CapabilityProfiles().empty(); }\n",
				encoding="utf-8",
			)
			result = subprocess.run(
				[
					"xcrun",
					"clang++",
					"-std=c++20",
					"-fsyntax-only",
					str(source_path),
					"-I",
					str(temporary),
					"-I",
					str(ROOT / "Plugins/Concept2PM/Source/Concept2PMCore/Public"),
					"-I",
					str(ROOT / "Source/RowingDevice/Public"),
					"-I",
					str(ROOT / "Source/RowingCore/Public"),
				],
				check=False,
				capture_output=True,
				text=True,
			)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_malformed_or_unimplemented_manifest_fails_closed(self) -> None:
		mutations = [
			("missing profile field", lambda value: value["profiles"][0].pop("evidence_reference")),
			("bad evidence digest", lambda value: value["profiles"][0].__setitem__("source_spec_sha256", "A" * 64)),
			("bad approval date", lambda value: value["profiles"][0].__setitem__("approved_on", "2026-02-30")),
			("unsupported machine enum", lambda value: value["profiles"][0].__setitem__("machine_kind", "SkiErg")),
			("unsupported property", lambda value: value["profiles"][0]["required_characteristics"][0].__setitem__("required_properties", ["read"])),
			("undersized packet", lambda value: value["profiles"][0]["required_characteristics"][0].__setitem__("allowed_packet_lengths", [18])),
			("unknown exact packet layout", lambda value: value["profiles"][0]["optional_characteristics"][0].__setitem__("allowed_packet_lengths", [21])),
			("unknown 0x0036 packet layout", add_unknown_0036_layout),
			("unsupported field", lambda value: value["profiles"][0]["required_characteristics"][0].__setitem__("implemented_fields", ["heart_rate"])),
			("missing required stream", lambda value: value["profiles"][0]["required_characteristics"].pop()),
			("unimplemented optional stream", lambda value: value["profiles"][0]["optional_characteristics"].append(copy.deepcopy(value["profiles"][0]["required_characteristics"][0]))),
		]
		for label, mutate in mutations:
			with self.subTest(label=label):
				manifest = valid_manifest()
				mutate(manifest)
				with self.assertRaises(ManifestError):
					validate_manifest(manifest)

	def test_duplicate_exact_identity_tuple_is_rejected(self) -> None:
		manifest = valid_manifest()
		manifest["profiles"].append(copy.deepcopy(manifest["profiles"][0]))
		with self.assertRaisesRegex(ManifestError, "duplicates an exact"):
			validate_manifest(manifest)

	def test_cli_does_not_write_output_for_malformed_manifest(self) -> None:
		manifest = valid_manifest()
		manifest["profiles"][0]["support_state"] = "Unknown"
		with tempfile.TemporaryDirectory(prefix="pm5-capability-invalid-") as temp_dir:
			temporary = Path(temp_dir)
			input_path = temporary / "malformed.json"
			output_path = temporary / "generated" / "PM5CapabilityProfiles.generated.h"
			input_path.write_text(json.dumps(manifest), encoding="utf-8")
			result = subprocess.run(
				[
					sys.executable,
					str(ROOT / "Scripts" / "generate_pm5_capabilities.py"),
					"--input",
					str(input_path),
					"--output",
					str(output_path),
				],
				check=False,
				capture_output=True,
				text=True,
			)
			self.assertEqual(result.returncode, 1)
			self.assertIn("must be Allowed, Warn, or Blocked", result.stderr)
			self.assertFalse(output_path.exists())


if __name__ == "__main__":
	unittest.main()
