#!/usr/bin/env python3
"""Validate PM5 capability profiles and generate their C++ allow-list."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path
from typing import Any


class ManifestError(ValueError):
	"""Raised when a manifest cannot be represented safely by the adapter."""


_TOP_LEVEL_KEYS = {"$schema", "schema_version", "profile_version", "profiles"}
_PROFILE_KEYS = {
	"monitor_model",
	"hardware_revision",
	"firmware_revision",
	"machine_kind",
	"support_state",
	"required_characteristics",
	"optional_characteristics",
	"requested_status_period_ms",
	"stale_after_ms",
	"blocking_warning_after_ms",
	"source_spec_sha256",
	"evidence_reference",
	"approved_on",
}
_CHARACTERISTIC_KEYS = {
	"short_id",
	"required_properties",
	"allowed_packet_lengths",
	"implemented_fields",
}
_PROPERTIES = {"read", "write", "write_without_response", "notify", "indicate"}
_SUPPORTED_PROPERTIES = {"notify", "indicate"}
_SUPPORT_STATES = {
	"Allowed": "Allowed",
	"Warn": "Warn",
	"Blocked": "Blocked",
}
_REQUIRED_CHARACTERISTICS = {0x0031, 0x0032}
_CHARACTERISTIC_FIELDS: dict[int, dict[str, str]] = {
	0x0031: {
		"source_elapsed": "SourceElapsed",
		"distance": "Distance",
		"workout_state": "WorkoutState",
		"rowing_state": "RowingState",
		"stroke_state": "StrokeState",
		"drag_factor": "DragFactor",
	},
	0x0032: {
		"source_elapsed": "SourceElapsed",
		"speed": "Speed",
		"pace": "Pace",
		"stroke_rate": "StrokeRate",
		"heart_rate": "HeartRate",
	},
	0x0033: {
		"source_elapsed": "SourceElapsed",
		"average_power": "AveragePower",
		"calories": "Calories",
	},
	0x0035: {
		"source_elapsed": "SourceElapsed",
		"stroke_count": "StrokeCount",
	},
	0x0036: {
		"source_elapsed": "SourceElapsed",
		"stroke_power": "StrokePower",
		"stroke_count": "StrokeCount",
	},
	0x0034: {},
}
_MINIMUM_PACKET_LENGTHS = {
	0x0031: 19,
	0x0032: 17,
	0x0033: 20,
	0x0035: 20,
	0x0036: 18,
}
_PROPERTY_ORDER = ("read", "write", "write_without_response", "notify", "indicate")


def _fail(path: str, detail: str) -> None:
	raise ManifestError(f"{path}: {detail}")


def _object(value: Any, path: str, required: set[str], allowed: set[str]) -> dict[str, Any]:
	if not isinstance(value, dict):
		_fail(path, "must be an object")
	missing = required - value.keys()
	unknown = value.keys() - allowed
	if missing:
		_fail(path, f"missing required field(s): {', '.join(sorted(missing))}")
	if unknown:
		_fail(path, f"unknown field(s): {', '.join(sorted(unknown))}")
	return value


def _positive_integer(value: Any, path: str, maximum: int) -> int:
	if type(value) is not int or value < 1 or value > maximum:
		_fail(path, f"must be an integer from 1 through {maximum}")
	return value


def _text(value: Any, path: str, maximum_bytes: int) -> str:
	if not isinstance(value, str) or not value:
		_fail(path, "must be a non-empty string")
	try:
		encoded = value.encode("utf-8")
	except UnicodeEncodeError:
		_fail(path, "must contain valid UTF-8 text")
	if len(encoded) > maximum_bytes:
		_fail(path, f"must be at most {maximum_bytes} UTF-8 bytes")
	if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
		_fail(path, "must not contain control characters")
	return value


def _validate_characteristic(value: Any, path: str, required: bool) -> dict[str, Any]:
	characteristic = _object(value, path, _CHARACTERISTIC_KEYS, _CHARACTERISTIC_KEYS)
	short_id = characteristic["short_id"]
	if not isinstance(short_id, str) or re.fullmatch(r"0x[0-9A-Fa-f]{4}", short_id) is None:
		_fail(f"{path}.short_id", "must be a 16-bit hexadecimal string such as 0x0031")
	characteristic_id = int(short_id[2:], 16)
	if characteristic_id not in _CHARACTERISTIC_FIELDS:
		_fail(f"{path}.short_id", f"characteristic {short_id} has no implemented packet decoder")
	if required and characteristic_id not in _REQUIRED_CHARACTERISTICS:
		_fail(f"{path}.short_id", "this adapter only supports required 0x0031 and 0x0032 streams")
	if not required and characteristic_id not in {0x0033, 0x0034, 0x0035, 0x0036}:
		_fail(f"{path}.short_id", "optional profiles are implemented only for 0x0033, 0x0034, 0x0035, and 0x0036")

	properties = characteristic["required_properties"]
	if not isinstance(properties, list) or not properties:
		_fail(f"{path}.required_properties", "must be a non-empty array")
	if any(not isinstance(prop, str) for prop in properties):
		_fail(f"{path}.required_properties", "must contain only string values")
	if len(set(properties)) != len(properties):
		_fail(f"{path}.required_properties", "must not contain duplicates")
	for prop in properties:
		if prop not in _PROPERTIES:
			_fail(f"{path}.required_properties", f"unsupported property value {prop!r}")
	if characteristic_id == 0x0034:
		if required:
			_fail(path, "the status-rate control characteristic must be optional")
		if set(properties) != {"read", "write"}:
			_fail(f"{path}.required_properties", "0x0034 status-rate control requires exactly read and write")
	else:
		if not set(properties) & _SUPPORTED_PROPERTIES:
			_fail(f"{path}.required_properties", "telemetry characteristics must require notify or indicate")

	lengths = characteristic["allowed_packet_lengths"]
	if not isinstance(lengths, list):
		_fail(f"{path}.allowed_packet_lengths", "must be an array")
	if any(type(length) is not int for length in lengths):
		_fail(f"{path}.allowed_packet_lengths", "must contain only integer values")
	if len(set(lengths)) != len(lengths):
		_fail(f"{path}.allowed_packet_lengths", "must not contain duplicates")
	if characteristic_id == 0x0034:
		if lengths:
			_fail(f"{path}.allowed_packet_lengths", "0x0034 is a write-only control profile and must have no packet lengths")
	else:
		if not lengths:
			_fail(f"{path}.allowed_packet_lengths", "must be non-empty for a decoded notification characteristic")
		minimum = _MINIMUM_PACKET_LENGTHS[characteristic_id]
		for length in lengths:
			if type(length) is not int or length < minimum or length > 512:
				_fail(
					f"{path}.allowed_packet_lengths",
					f"each exact decoder length must be an integer from {minimum} through 512",
				)

	fields = characteristic["implemented_fields"]
	if not isinstance(fields, list) or any(
		not isinstance(field, str) or not field or len(field) > 64 for field in fields
	):
		_fail(f"{path}.implemented_fields", "must be an array of non-empty field names up to 64 characters")
	if len(set(fields)) != len(fields):
		_fail(f"{path}.implemented_fields", "must not contain duplicates")
	if characteristic_id == 0x0034 and fields:
		_fail(f"{path}.implemented_fields", "0x0034 is a control characteristic and must implement no telemetry fields")
	implemented = _CHARACTERISTIC_FIELDS[characteristic_id]
	for field in fields:
		if field not in implemented:
			_fail(
				f"{path}.implemented_fields",
				f"field {field!r} is not implemented for characteristic {short_id}",
			)

	return {
		"id": characteristic_id,
		"required": required,
		"properties": [property for property in _PROPERTY_ORDER if property in properties],
		"lengths": sorted(lengths),
		"metric_names": [implemented[field] for field in fields],
	}


def validate_manifest(manifest: Any) -> dict[str, Any]:
	"""Validate schema and runtime representability, returning normalized data."""
	root = _object(manifest, "manifest", {"schema_version", "profile_version", "profiles"}, _TOP_LEVEL_KEYS)
	if "$schema" in root and not isinstance(root["$schema"], str):
		_fail("manifest.$schema", "must be a string when present")
	if root["schema_version"] != 1 or type(root["schema_version"]) is not int:
		_fail("manifest.schema_version", "must equal 1")
	profile_version = _positive_integer(root["profile_version"], "manifest.profile_version", 0xFFFFFFFF)
	profiles = root["profiles"]
	if not isinstance(profiles, list):
		_fail("manifest.profiles", "must be an array")

	seen_tuples: set[tuple[str, str, str, str]] = set()
	normalized_profiles: list[dict[str, Any]] = []
	for index, value in enumerate(profiles):
		path = f"manifest.profiles[{index}]"
		profile = _object(value, path, _PROFILE_KEYS, _PROFILE_KEYS)
		model = _text(profile["monitor_model"], f"{path}.monitor_model", 64)
		hardware = _text(profile["hardware_revision"], f"{path}.hardware_revision", 64)
		firmware = _text(profile["firmware_revision"], f"{path}.firmware_revision", 64)
		if profile["machine_kind"] != "IndoorRower":
			_fail(f"{path}.machine_kind", "only the implemented IndoorRower enum is supported")
		support_state = profile["support_state"]
		if not isinstance(support_state, str) or support_state not in _SUPPORT_STATES:
			_fail(f"{path}.support_state", "must be Allowed, Warn, or Blocked")
		for name, expected in (
			("requested_status_period_ms", 100),
			("stale_after_ms", 500),
			("blocking_warning_after_ms", 1500),
		):
			if type(profile[name]) is not int or profile[name] != expected:
				_fail(f"{path}.{name}", f"must equal the implemented value {expected}")
		source_hash = profile["source_spec_sha256"]
		if not isinstance(source_hash, str) or re.fullmatch(r"[0-9a-f]{64}", source_hash) is None:
			_fail(f"{path}.source_spec_sha256", "must be a lowercase SHA-256 hex digest")
		_text(profile["evidence_reference"], f"{path}.evidence_reference", 256)
		approved_on = profile["approved_on"]
		if not isinstance(approved_on, str):
			_fail(f"{path}.approved_on", "must be an ISO 8601 calendar date")
		try:
			if dt.date.fromisoformat(approved_on).isoformat() != approved_on:
				_fail(f"{path}.approved_on", "must use YYYY-MM-DD format")
		except ValueError:
			_fail(f"{path}.approved_on", "must be a valid YYYY-MM-DD date")

		required = profile["required_characteristics"]
		optional = profile["optional_characteristics"]
		if not isinstance(required, list) or not required:
			_fail(f"{path}.required_characteristics", "must be a non-empty array")
		if not isinstance(optional, list):
			_fail(f"{path}.optional_characteristics", "must be an array")
		normalized_required_chars = [
			_validate_characteristic(item, f"{path}.required_characteristics[{char_index}]", True)
			for char_index, item in enumerate(required)
		]
		normalized_optional_chars = [
			_validate_characteristic(item, f"{path}.optional_characteristics[{char_index}]", False)
			for char_index, item in enumerate(optional)
		]
		normalized_chars = normalized_required_chars + normalized_optional_chars
		ids = [characteristic["id"] for characteristic in normalized_chars]
		if len(set(ids)) != len(ids):
			_fail(path, "must not repeat characteristic IDs across required and optional characteristics")
		if {characteristic["id"] for characteristic in normalized_required_chars} != _REQUIRED_CHARACTERISTICS:
			_fail(
				f"{path}.required_characteristics",
				"must require both 0x0031 GeneralStatus and 0x0032 AdditionalStatus1",
			)

		identity = (model, hardware, firmware, profile["machine_kind"])
		if identity in seen_tuples:
			_fail(path, "duplicates an exact monitor/hardware/firmware/machine tuple")
		seen_tuples.add(identity)
		normalized_profiles.append(
			{
				"model": model,
				"hardware": hardware,
				"firmware": firmware,
				"machine_kind": "IndoorRower",
				"support_state": _SUPPORT_STATES[support_state],
				"requested_status_period_ms": profile["requested_status_period_ms"],
				"characteristics": sorted(normalized_chars, key=lambda item: item["id"]),
			}
		)
	return {"profile_version": profile_version, "profiles": normalized_profiles}


def _cpp_string(value: str) -> str:
	return json.dumps(value, ensure_ascii=False)


def render_header(manifest: Any) -> str:
	validated = validate_manifest(manifest)
	lines = [
		"// Generated from Config/PM5Capabilities.json. Do not edit.",
		"#pragma once",
		"",
		'#include "Concept2PMCore/Concept2PMProtocol.h"',
		"#include <utility>",
		"#include <vector>",
		"",
		"namespace Concept2PM",
		"{",
		"\tinline std::vector<FPM5CapabilityProfile>",
		"\tGetGeneratedPM5CapabilityProfiles()",
		"\t{",
		"\t\tstd::vector<FPM5CapabilityProfile> Profiles;",
		f"\t\tProfiles.reserve({len(validated['profiles'])});",
	]
	for profile in validated["profiles"]:
		lines.extend(
			[
			"\t\t{",
			"\t\t\tFPM5CapabilityProfile Profile;",
			f"\t\t\tProfile.Version = {validated['profile_version']}U;",
			f"\t\t\tProfile.MonitorModel = {_cpp_string(profile['model'])};",
			f"\t\t\tProfile.HardwareRevision = {_cpp_string(profile['hardware'])};",
			f"\t\t\tProfile.FirmwareRevision = {_cpp_string(profile['firmware'])};",
			"\t\t\tProfile.MachineKind = ERowingMachineKind::IndoorRower;",
			f"\t\t\tProfile.SupportState = ERowingMachineSupportState::{profile['support_state']};",
			"\t\t\tProfile.DiagnosticOnly = false;",
			f"\t\t\tProfile.RequestedStatusPeriodMs = {profile['requested_status_period_ms']}U;",
			]
		)
		for characteristic in profile["characteristics"]:
			properties = " | ".join(
				f"ToPM5CharacteristicProperties(EPM5CharacteristicProperty::{property.title().replace('_', '')})"
				for property in characteristic["properties"]
			)
			metrics = characteristic["metric_names"]
			if metrics:
				metric_expression = " | ".join(
					f"ToRowingMetricSet(ERowingMetric::{metric})" for metric in metrics
				)
			else:
				metric_expression = "ToRowingMetricSet(ERowingMetric::None)"
			lengths = ", ".join(str(length) for length in characteristic["lengths"])
			lines.append(
				"\t\t\tProfile.Characteristics.push_back({"
				f"0x{characteristic['id']:04X}U, {'true' if characteristic['required'] else 'false'}, "
				f"{'true' if set(characteristic['properties']) & _SUPPORTED_PROPERTIES else 'false'}, "
				f"{properties}, {{{lengths}}}, "
				f"{metric_expression}}});"
			)
		lines.extend(["\t\t\tProfiles.push_back(std::move(Profile));", "\t\t}"])
	lines.extend(["\t\treturn Profiles;", "\t}", "} // namespace Concept2PM", ""])
	return "\n".join(lines)


def generate(input_path: Path, output_path: Path) -> None:
	try:
		manifest = json.loads(input_path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as exc:
		raise ManifestError(f"cannot read {input_path}: {exc}") from exc
	header = render_header(manifest)
	output_path.parent.mkdir(parents=True, exist_ok=True)
	if not output_path.exists() or output_path.read_text(encoding="utf-8") != header:
		output_path.write_text(header, encoding="utf-8")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--input", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	args = parser.parse_args()
	try:
		generate(args.input, args.output)
	except ManifestError as exc:
		print(f"PM5 capability manifest error: {exc}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
