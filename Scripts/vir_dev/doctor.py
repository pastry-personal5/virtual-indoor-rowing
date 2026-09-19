"""`make doctor`: verify the host toolchain against Config/BuildVersions.json."""

from __future__ import annotations

import hashlib
import json
import platform
import re
import sys
from pathlib import Path

from vir_dev import common


def check_doctor() -> int:
	versions = common.load_versions()
	failed = False
	if sys.platform != "darwin":
		print(f"FAIL host OS: required macOS {versions['platform']['minimum_version']} or later; observed {platform.system()}")
		failed = True
	else:
		observed_os = common.capture(["sw_vers", "-productVersion"]) or "unknown"
		current = common.version_number(observed_os)
		minimum = common.version_number(versions["platform"]["minimum_version"])
		ok = current is not None and minimum is not None and current >= minimum
		print(f"{'OK' if ok else 'FAIL'} macOS: required {versions['platform']['minimum_version']} or later; observed {observed_os}")
		failed |= not ok

	arch = platform.machine()
	arch_ok = arch == versions["platform"]["architecture"]
	print(f"{'OK' if arch_ok else 'FAIL'} architecture: required {versions['platform']['architecture']}; observed {arch}")
	failed |= not arch_ok

	developer_dir = Path(versions["xcode"]["developer_directory"])
	xcodebuild = developer_dir / "usr" / "bin" / "xcodebuild"
	if not xcodebuild.is_file():
		print(f"FAIL Xcode: required {versions['xcode']['version']} at {developer_dir}; not installed")
		failed = True
	else:
		env = common.tool_env(versions)
		xcode_info = common.capture([str(xcodebuild), "-version"], env=env) or ""
		observed_xcode = next(
			(line.removeprefix("Xcode ").strip() for line in xcode_info.splitlines() if line.startswith("Xcode ")),
			None,
		)
		xcode_ok = common.exact_version("Xcode", observed_xcode, versions["xcode"]["version"])
		failed |= not xcode_ok
		observed_build = next((line.split("Build version ", 1)[1] for line in xcode_info.splitlines() if line.startswith("Build version ")), None)
		if not versions["xcode"].get("build") or observed_build != versions["xcode"]["build"]:
			print(f"FAIL Xcode build: required {versions['xcode'].get('build') or 'recorded approved build'}; observed {observed_build or 'unknown'}")
			failed = True
		sdk = common.capture(["xcrun", "--sdk", "macosx", "--show-sdk-version"], env=env)
		if not versions["xcode"].get("sdk_version") or sdk != versions["xcode"]["sdk_version"]:
			print(f"FAIL macOS SDK: required {versions['xcode'].get('sdk_version') or 'recorded approved SDK'}; observed {sdk or 'unknown'}")
			failed = True
		compiler = common.capture(["xcrun", "clang", "--version"], env=env) or ""
		compiler_version = next((line for line in compiler.splitlines() if line.startswith("Apple clang version ")), None)
		if not versions["xcode"].get("compiler_version") or compiler_version != versions["xcode"]["compiler_version"]:
			print(f"FAIL Apple clang: required {versions['xcode'].get('compiler_version') or 'recorded approved compiler'}; observed {compiler_version or 'unknown'}")
			failed = True
		metal_output = common.capture_combined(["xcrun", "metal", "--version"], env=env) or ""
		metal_ok = "Apple metal version" in metal_output
		print(f"{'OK' if metal_ok else 'FAIL'} Metal Toolchain: required for Shipping shader cook; {'installed' if metal_ok else 'missing — run `xcodebuild -downloadComponent MetalToolchain` (or retry after a few minutes; first use of `metal` can trigger an on-demand download) before `make unreal-shipping`'}")
		failed |= not metal_ok

	for binary, expected in (
		("cmake", versions["build_tools"]["cmake"]),
		("ninja", versions["build_tools"]["ninja"]),
		("protoc", versions["build_tools"]["protobuf"]),
	):
		observed = common.capture([binary, "--version"])
		failed |= not common.exact_version(binary, observed, expected)

	static = versions.get("static_dependencies", {})
	protobuf_pin = static.get("protobuf", {})
	if protobuf_pin.get("version") != versions["build_tools"]["protobuf"]:
		print(f"FAIL static protobuf: pinned runtime {protobuf_pin.get('version')} must equal the pinned protoc {versions['build_tools']['protobuf']} so generated code and runtime agree")
		failed = True
	for name in ("protobuf", "abseil"):
		pin = static.get(name, {})
		pin_ok = bool(pin.get("version")) and re.fullmatch(r"[0-9a-f]{64}", pin.get("sha256", "")) is not None and str(pin.get("url", "")).startswith("https://")
		print(f"{'OK' if pin_ok else 'FAIL'} static {name}: {'pinned ' + pin['version'] + ' (sha256 verified by CMake at fetch)' if pin_ok else 'version, https url, and sha256 must be recorded in Config/BuildVersions.json'}")
		failed |= not pin_ok

	lfs = common.capture(["git", "lfs", "version"])
	if not lfs:
		print("FAIL Git LFS: required; install Git LFS and run `git lfs install`")
		failed = True
	else:
		failed |= not common.exact_version("Git LFS", lfs, versions["build_tools"]["git_lfs"])

	ue_root = common.find_unreal(versions)
	if not ue_root:
		print("FAIL Unreal Engine: stock 5.8 installation not found; install Epic's approved patch and set UE_ROOT")
		failed = True
	else:
		try:
			build_version = json.loads((ue_root / "Engine" / "Build" / "Build.version").read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError):
			build_version = {}
		observed = f"{build_version.get('MajorVersion', '?')}.{build_version.get('MinorVersion', '?')} changelist {build_version.get('Changelist', '?')}"
		print(f"{'OK' if build_version.get('MajorVersion') == 5 and build_version.get('MinorVersion') == 8 else 'FAIL'} Unreal Engine: required 5.8; observed {observed}")
		fingerprint = hashlib.sha256((ue_root / "Engine" / "Build" / "Build.version").read_bytes()).hexdigest()
		approved = versions["unreal"].get("fingerprint_sha256")
		approved_patch = versions["unreal"].get("approved_patch")
		patch_ok = approved_patch is not None and build_version.get("PatchVersion") == approved_patch
		print(f"{'OK' if patch_ok else 'FAIL'} Unreal patch: required {approved_patch if approved_patch is not None else 'approved patch not recorded'}; observed {build_version.get('PatchVersion', 'unknown')}")
		print(f"{'OK' if approved and fingerprint == approved else 'FAIL'} Unreal fingerprint: {'matches approved manifest' if approved and fingerprint == approved else 'not approved/recorded'}")
		failed |= not (build_version.get("MajorVersion") == 5 and build_version.get("MinorVersion") == 8 and patch_ok and approved and fingerprint == approved)

	if failed:
		print("Doctor failed. Native configure/build/test may still run with pinned CMake/Ninja and available Apple command-line tools.")
		return 1
	print("Doctor passed: host matches the recorded M1 toolchain baseline.")
	return 0
