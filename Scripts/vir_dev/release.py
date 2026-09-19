"""Bluetooth probe verification and the protected sign/notarize procedure."""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

from vir_dev import common, packaging


BLUETOOTH_PROBE_SCHEMA_VERSION = 1
BLUETOOTH_PROBE_RESULT_STATES = frozenset((
	"authorized_powered_on",
	"denied",
	"restricted",
	"unsupported",
	"powered_off",
	"timeout",
	"corebluetooth_error",
))


def toolchain_bluetooth_probe() -> int:
	app = packaging.staged_app(common.UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: missing staged .app; run `make unreal-shipping` first", file=sys.stderr)
		return 1
	if packaging.verify_package(app, common.load_versions()):
		print("ERROR: package verification failed; refusing to launch probe", file=sys.stderr)
		return 1
	executable = packaging.package_binary_paths(app)[0]
	probe_directory = common.ROOT / "Saved" / "Logs"
	probe_directory.mkdir(parents=True, exist_ok=True)
	def probe_results() -> set[Path]:
		return set(probe_directory.glob("toolchain-bluetooth-probe-*.json"))

	before = probe_results()
	result = common.run([
		str(executable),
		"-ToolchainBluetoothProbe",
		f"-ToolchainBluetoothProbeResultDir={probe_directory}",
	], env=common.tool_env(common.load_versions()))
	if result.returncode:
		return result.returncode
	after = probe_results()
	created = sorted(after - before)
	if len(created) != 1:
		print("ERROR: expected exactly one redacted Bluetooth probe result", file=sys.stderr)
		return 1
	probe_failure = verify_bluetooth_probe_result(created[0])
	if probe_failure:
		print(f"ERROR: invalid Bluetooth probe result: {probe_failure}", file=sys.stderr)
		return 1
	print(created[0])
	return 0


def verify_bluetooth_probe_result(path: Path) -> str | None:
	try:
		payload = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		return "unreadable JSON"
	expected_fields = {
		"schema_version",
		"source_revision",
		"toolchain_fingerprint",
		"timestamp_utc",
		"result_state",
		"duration_ms",
	}
	if set(payload) != expected_fields:
		return "unexpected fields"
	if payload["schema_version"] != BLUETOOTH_PROBE_SCHEMA_VERSION:
		return "unsupported schema version"
	if payload["result_state"] not in BLUETOOTH_PROBE_RESULT_STATES:
		return "invalid result state"
	if not isinstance(payload["duration_ms"], int) or payload["duration_ms"] < 0 or payload["duration_ms"] > 16_000:
		return "invalid duration"
	if any(not isinstance(payload[field], str) or not payload[field] for field in (
		"source_revision", "toolchain_fingerprint", "timestamp_utc", "result_state",
	)):
		return "missing required string"
	return None


def verify_release_signature(app: Path) -> str | None:
	options = common.capture_combined(["codesign", "-d", "--options", ":-", str(app)]) or ""
	if "runtime" not in options.lower():
		return "Hardened Runtime is not enabled"
	entitlement_text = common.capture(["codesign", "-d", "--entitlements", ":-", str(app)]) or ""
	if "get-task-allow" in entitlement_text:
		return "get-task-allow must be absent from Shipping entitlements"
	return None


def release_sign_notarize() -> int:
	identity = os.environ.get("VIR_DEVELOPER_ID_IDENTITY")
	notary_profile = os.environ.get("VIR_NOTARY_KEYCHAIN_PROFILE")
	if not identity or not notary_profile:
		print("ERROR: protected command requires preconfigured VIR_DEVELOPER_ID_IDENTITY and VIR_NOTARY_KEYCHAIN_PROFILE names", file=sys.stderr)
		return 2
	if identity == "-" or not re.fullmatch(r"Developer ID Application: .+", identity) or not re.fullmatch(r"[A-Za-z0-9._-]+", notary_profile):
		print("ERROR: only a Developer ID identity and a Keychain profile name are accepted", file=sys.stderr)
		return 2
	app = packaging.staged_app(common.UNREAL_ARCHIVE_DIR)
	if not app or packaging.verify_package(app, common.load_versions()):
		print("ERROR: a verified unsigned Shipping package is required", file=sys.stderr)
		return 1
	entitlements = common.ROOT / "Build" / "VirtualRowing.entitlements"
	if not entitlements.is_file():
		print("ERROR: hardened-runtime entitlements file is missing", file=sys.stderr)
		return 1
	code_paths = []
	for path in app.rglob("*"):
		if not path.is_file():
			continue
		file_kind = common.capture(["file", "-b", str(path)]) or ""
		if "Mach-O" in file_kind:
			code_paths.append(path)
	code_paths.sort(key=lambda path: len(path.parts), reverse=True)
	for path in code_paths:
		if common.run(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", str(path)]).returncode:
			return 1
	if common.run(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", "--entitlements", str(entitlements), str(app)]).returncode:
		return 1
	if common.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)]).returncode:
		return 1
	signature_failure = verify_release_signature(app)
	if signature_failure:
		print(f"ERROR: {signature_failure}", file=sys.stderr)
		return 1
	dmg = common.UNREAL_SHIPPING_DIR / "VirtualRowing.dmg"
	if dmg.exists():
		dmg.unlink()
	if common.run(["hdiutil", "create", "-volname", "VirtualRowing", "-srcfolder", str(app), "-ov", "-format", "UDZO", str(dmg)]).returncode:
		return 1
	if common.run(["xcrun", "notarytool", "submit", str(dmg), "--keychain-profile", notary_profile, "--wait"]).returncode:
		return 1
	if common.run(["xcrun", "stapler", "staple", str(dmg)]).returncode:
		return 1
	return common.run(["spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=4", str(dmg)]).returncode
