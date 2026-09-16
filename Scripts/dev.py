#!/usr/bin/env python3
"""Reproducible local build entry points for the M1 diagnostic client."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSIONS_PATH = ROOT / "Config" / "BuildVersions.json"
BUILD_DIR = ROOT / "Build" / "native"
UNREAL_SHIPPING_DIR = ROOT / "Build" / "unreal-shipping"
UNREAL_ARCHIVE_DIR = UNREAL_SHIPPING_DIR / "archive"
UNREAL_PROVENANCE_PATH = UNREAL_SHIPPING_DIR / "provenance.json"
BLUETOOTH_USAGE_DESCRIPTION = "Virtual Rowing uses Bluetooth only when you run the explicit toolchain Bluetooth diagnostic."
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


def load_versions() -> dict:
	try:
		return json.loads(VERSIONS_PATH.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as exc:
		print(f"ERROR: cannot read {VERSIONS_PATH.relative_to(ROOT)}: {exc}", file=sys.stderr)
		raise SystemExit(2)


def run(args: list[str], *, env: dict[str, str] | None = None, cwd: Path = ROOT) -> subprocess.CompletedProcess:
	return subprocess.run(args, cwd=cwd, env=env, check=False, text=True)


def capture(args: list[str], *, env: dict[str, str] | None = None) -> str | None:
	try:
		result = subprocess.run(args, cwd=ROOT, env=env, check=False, text=True, capture_output=True)
	except OSError:
		return None
	if result.returncode != 0:
		return None
	return result.stdout.strip()


def capture_combined(args: list[str], *, env: dict[str, str] | None = None) -> str | None:
	try:
		result = subprocess.run(args, cwd=ROOT, env=env, check=False, text=True, capture_output=True)
	except OSError:
		return None
	if result.returncode != 0:
		return None
	return (result.stdout + result.stderr).strip()


def version_number(value: str | None) -> tuple[int, ...] | None:
	if not value:
		return None
	match = re.search(r"\d+(?:\.\d+)+", value)
	return tuple(int(part) for part in match.group(0).split(".")) if match else None


def exact_version(label: str, observed: str | None, expected: str) -> bool:
	observed_num = version_number(observed)
	expected_num = version_number(expected)
	passed = observed_num == expected_num
	print(f"{'OK' if passed else 'FAIL'} {label}: required {expected}; observed {observed or 'missing'}")
	return passed


def tool_env(versions: dict) -> dict[str, str]:
	env = os.environ.copy()
	pinned_dev_dir = Path(versions["xcode"]["developer_directory"])
	clt_dir = Path("/Library/Developer/CommandLineTools")
	if pinned_dev_dir.is_dir():
		env["DEVELOPER_DIR"] = str(pinned_dev_dir)
	elif clt_dir.is_dir():
		env["DEVELOPER_DIR"] = str(clt_dir)
	return env


def find_unreal(versions: dict) -> Path | None:
	candidates: list[Path] = []
	configured = os.environ.get("UE_ROOT")
	if configured:
		candidates.append(Path(configured).expanduser())
	configured = versions["unreal"].get("installation_root")
	if configured:
		candidates.append(Path(configured).expanduser())
	candidates.extend([
		Path("/Users/Shared/Epic Games/UE_5.8"),
		Path("/Applications/Epic Games/UE_5.8"),
		Path("/Volumes/Unreal_Engine_Volume/work/UE_5.8"),
		Path("/Volumes/Work_Volume/UnrealEngine/UE_5.8"),
	])
	for candidate in candidates:
		if (candidate / "Engine" / "Build" / "Build.version").is_file():
			return candidate.resolve()
	return None


def check_doctor() -> int:
	versions = load_versions()
	failed = False
	if sys.platform != "darwin":
		print(f"FAIL host OS: required macOS {versions['platform']['minimum_version']} or later; observed {platform.system()}")
		failed = True
	else:
		observed_os = capture(["sw_vers", "-productVersion"]) or "unknown"
		current = version_number(observed_os)
		minimum = version_number(versions["platform"]["minimum_version"])
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
		env = tool_env(versions)
		xcode_info = capture([str(xcodebuild), "-version"], env=env) or ""
		observed_xcode = next(
			(line.removeprefix("Xcode ").strip() for line in xcode_info.splitlines() if line.startswith("Xcode ")),
			None,
		)
		xcode_ok = exact_version("Xcode", observed_xcode, versions["xcode"]["version"])
		failed |= not xcode_ok
		observed_build = next((line.split("Build version ", 1)[1] for line in xcode_info.splitlines() if line.startswith("Build version ")), None)
		if not versions["xcode"].get("build") or observed_build != versions["xcode"]["build"]:
			print(f"FAIL Xcode build: required {versions['xcode'].get('build') or 'recorded approved build'}; observed {observed_build or 'unknown'}")
			failed = True
		sdk = capture(["xcrun", "--sdk", "macosx", "--show-sdk-version"], env=env)
		if not versions["xcode"].get("sdk_version") or sdk != versions["xcode"]["sdk_version"]:
			print(f"FAIL macOS SDK: required {versions['xcode'].get('sdk_version') or 'recorded approved SDK'}; observed {sdk or 'unknown'}")
			failed = True
		compiler = capture(["xcrun", "clang", "--version"], env=env) or ""
		compiler_version = next((line for line in compiler.splitlines() if line.startswith("Apple clang version ")), None)
		if not versions["xcode"].get("compiler_version") or compiler_version != versions["xcode"]["compiler_version"]:
			print(f"FAIL Apple clang: required {versions['xcode'].get('compiler_version') or 'recorded approved compiler'}; observed {compiler_version or 'unknown'}")
			failed = True

	for binary, expected in (("cmake", versions["build_tools"]["cmake"]), ("ninja", versions["build_tools"]["ninja"])):
		observed = capture([binary, "--version"])
		failed |= not exact_version(binary, observed, expected)

	lfs = capture(["git", "lfs", "version"])
	if not lfs:
		print("FAIL Git LFS: required; install Git LFS and run `git lfs install`")
		failed = True
	else:
		failed |= not exact_version("Git LFS", lfs, versions["build_tools"]["git_lfs"])

	ue_root = find_unreal(versions)
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


def ensure_build_tools(versions: dict) -> None:
	for binary, expected in (("cmake", versions["build_tools"]["cmake"]), ("ninja", versions["build_tools"]["ninja"])):
		observed = capture([binary, "--version"])
		if version_number(observed) != version_number(expected):
			print(f"ERROR: {binary} {expected} is required for native builds; observed {observed or 'missing'}", file=sys.stderr)
			raise SystemExit(1)


def source_revision() -> str:
	revision = capture(["git", "rev-parse", "--short=12", "HEAD"]) or "unknown"
	worktree_changes = capture(["git", "status", "--porcelain"]) or ""
	return revision + ("-dirty" if worktree_changes else "")


def application_build_number(versions: dict) -> int:
	build_number = versions.get("application_build")
	if type(build_number) is not int or build_number < 1:
		print("ERROR: application_build must be a positive integer", file=sys.stderr)
		raise SystemExit(2)
	return build_number


def cmake_base_args(versions: dict) -> list[str]:
	return [
		"cmake",
		"-S",
		str(ROOT),
		"-B",
		str(BUILD_DIR),
		"-G",
		"Ninja",
		"-DCMAKE_BUILD_TYPE=Debug",
		"-DCMAKE_OSX_ARCHITECTURES=arm64",
		f"-DVIR_APP_VERSION={versions['application_version']}",
		f"-DVIR_APP_BUILD={application_build_number(versions)}",
		f"-DVIR_SOURCE_REVISION={source_revision()}",
	]


def configure() -> int:
	versions = load_versions()
	ensure_build_tools(versions)
	return run(cmake_base_args(versions), env=tool_env(versions)).returncode


def build(target: str | None = None) -> int:
	result = configure()
	if result:
		return result
	args = ["cmake", "--build", str(BUILD_DIR), "--parallel"]
	if target:
		args.extend(["--target", target])
	return run(args, env=tool_env(load_versions())).returncode


def test() -> int:
	result = build()
	if result:
		return result
	return run(["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure", "--no-tests=error"], env=tool_env(load_versions())).returncode


def launch_tui_in_magnified_wave(executable: Path, arguments: list[str]) -> bool:
	if os.environ.get("TERM_PROGRAM") != "waveterm":
		return False
	try:
		if not sys.stdout.isatty():
			return False
	except OSError:
		return False

	wsh = shutil.which("wsh")
	if not wsh:
		return False
	try:
		result = subprocess.run(
			[
				wsh,
				"run",
				"--magnified",
				"--cwd",
				str(ROOT),
				"--",
				str(executable),
				*arguments,
			],
			cwd=ROOT,
			env=os.environ.copy(),
			check=False,
			capture_output=True,
			text=True,
		)
	except OSError:
		return False
	if result.returncode == 0:
		print("PM5 TUI opened in a magnified Wave block.")
		return True
	return False


def resize_apple_terminal_window_for_tui() -> None:
	if sys.platform != "darwin" or os.environ.get("TERM_PROGRAM") != "Apple_Terminal":
		return
	try:
		if not sys.stdout.isatty():
			return
		tty_path = os.ttyname(sys.stdout.fileno())
	except OSError:
		return

	osascript = shutil.which("osascript")
	if not osascript:
		return
	script = '''
on run argv
	set targetTTY to item 1 of argv
	tell application "Finder" to set screenBounds to bounds of window of desktop
	set screenLeft to item 1 of screenBounds
	set screenTop to item 2 of screenBounds
	set screenRight to item 3 of screenBounds
	set screenBottom to item 4 of screenBounds
	set marginX to (screenRight - screenLeft) * 2 div 100
	set marginY to (screenBottom - screenTop) * 2 div 100
	if marginX < 16 then set marginX to 16
	if marginY < 16 then set marginY to 16
	tell application "Terminal"
		repeat with terminalWindow in windows
			repeat with terminalTab in tabs of terminalWindow
				if (tty of terminalTab) is targetTTY then
					set bounds of terminalWindow to {screenLeft + marginX, screenTop + marginY, screenRight - marginX, screenBottom - marginY}
					return "resized"
				end if
			end repeat
		end repeat
	end tell
	return "not-found"
end run
'''
	try:
		result = subprocess.run(
			[osascript, "-e", script, tty_path],
			cwd=ROOT,
			check=False,
			capture_output=True,
			text=True,
		)
	except OSError:
		return
	if result.returncode != 0:
		print("NOTE: Terminal window resize was unavailable; continuing at current size.", file=sys.stderr)


def run_tui(hardware_probe: bool = False) -> int:
	result = build("pm5-tui")
	if result:
		return result
	bundle_executable = BUILD_DIR / "pm5-tui.app" / "Contents" / "MacOS" / "pm5-tui"
	executable = bundle_executable if bundle_executable.is_file() else BUILD_DIR / "pm5-tui"
	if not executable.is_file():
		print("ERROR: pm5-tui target is not present; check that Tools/pm5-tui/src has its entry point", file=sys.stderr)
		return 2
	arguments = ["--hardware-probe"] if hardware_probe else []
	if launch_tui_in_magnified_wave(executable, arguments):
		return 0
	resize_apple_terminal_window_for_tui()
	# The adapter only scans after the TUI's explicit `scan` command. The HIL
	# entry point uses the same diagnostic UI and leaves selection user-driven.
	return run([str(executable), *arguments], env=tool_env(load_versions())).returncode


def format_check() -> int:
	versions = load_versions()
	env = tool_env(versions)
	clang_format = shutil.which("clang-format", path=env.get("PATH"))
	if not clang_format:
		clang_format = capture(["xcrun", "--find", "clang-format"], env=env)
	if not clang_format:
		print("ERROR: clang-format is required; install the formatter shipped with the pinned Xcode", file=sys.stderr)
		return 1
	sources: list[str] = []
	for directory in ("Source", "Plugins/Concept2PM", "Tools", "Tests"):
		base = ROOT / directory
		if base.is_dir():
			for suffix in ("*.h", "*.hpp", "*.cpp", "*.cc", "*.cxx", "*.mm", "*.m"):
				for path in base.rglob(suffix):
					if any(
						part in {"Binaries", "DerivedDataCache", "Intermediate", "Saved"}
						for part in path.parts
					):
						continue
					sources.append(str(path))
	if not sources:
		print("ERROR: no C++ sources found for format check", file=sys.stderr)
		return 1
	result = run([clang_format, "--dry-run", "--Werror", *sources], env=env)
	return result.returncode


def unreal_smoke() -> int:
	versions = load_versions()
	ue_root = find_unreal(versions)
	if not ue_root:
		print("ERROR: Unreal Engine 5.8 was not found; install the approved patch and set UE_ROOT", file=sys.stderr)
		return 1
	if check_doctor() != 0:
		print("ERROR: Unreal smoke is blocked until `make doctor` passes", file=sys.stderr)
		return 1
	ubt = ue_root / "Engine" / "Build" / "BatchFiles" / "Mac" / "Build.sh"
	if not ubt.is_file():
		print(f"ERROR: UnrealBuildTool wrapper missing at {ubt}", file=sys.stderr)
		return 1
	project = ROOT / "VirtualRowing.uproject"
	if not project.is_file():
		print(f"ERROR: Unreal smoke host is missing: {project}", file=sys.stderr)
		return 1
	return run([
		str(ubt),
		"UnrealEditor",
		"Mac",
		"Development",
		f"-Project={project}",
		"-WaitMutex",
	], env=tool_env(versions)).returncode


def unreal_shipping_command(ue_root: Path, project: Path, archive_dir: Path) -> list[str]:
	return [
		str(ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"),
		"BuildCookRun",
		f"-project={project}",
		"-noP4",
		"-platform=Mac",
		"-targetplatform=Mac",
		"-clientconfig=Shipping",
		"-build",
		"-cook",
		"-stage",
		"-package",
		"-archive",
		f"-archivedirectory={archive_dir}",
		"-specifiedarchitecture=arm64",
	]


def staged_app(archive_dir: Path) -> Path | None:
	apps = sorted(archive_dir.rglob("VirtualRowing.app")) if archive_dir.is_dir() else []
	return apps[0] if len(apps) == 1 else None


def toolchain_fingerprint(versions: dict) -> str:
	return ";".join((
		f"ue-{versions['unreal']['version']}.{versions['unreal']['approved_patch']}",
		f"xcode-{versions['xcode']['version']}",
		f"macos-{versions['platform']['minimum_version']}",
		versions["platform"]["architecture"],
	))


def write_shipping_provenance(versions: dict) -> None:
	UNREAL_SHIPPING_DIR.mkdir(parents=True, exist_ok=True)
	UNREAL_PROVENANCE_PATH.write_text(json.dumps({
		"schema_version": 1,
		"source_revision": source_revision(),
		"toolchain_fingerprint": toolchain_fingerprint(versions),
		"build_versions_sha256": hashlib.sha256(VERSIONS_PATH.read_bytes()).hexdigest(),
	}, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def unreal_shipping() -> int:
	versions = load_versions()
	ue_root = find_unreal(versions)
	if not ue_root:
		print("ERROR: Unreal Engine 5.8 was not found; install the approved patch and set UE_ROOT", file=sys.stderr)
		return 1
	project = ROOT / "VirtualRowing.uproject"
	uat = ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
	if not project.is_file() or not uat.is_file():
		print("ERROR: Unreal project or RunUAT.sh is missing", file=sys.stderr)
		return 1
	write_shipping_provenance(versions)
	env = tool_env(versions)
	env["VIR_SOURCE_REVISION"] = source_revision()
	result = run(unreal_shipping_command(ue_root, project, UNREAL_ARCHIVE_DIR), env=env)
	if result.returncode:
		return result.returncode
	app = staged_app(UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: BuildCookRun succeeded but exactly one VirtualRowing.app was not archived", file=sys.stderr)
		return 1
	metadata = app / "Contents" / "Resources" / "BuildVersions.json"
	metadata.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(VERSIONS_PATH, metadata)
	print(app)
	return 0


def package_binary_paths(app: Path) -> list[Path]:
	main = app / "Contents" / "MacOS" / "VirtualRowing"
	plugin_binaries = list(app.rglob("*Concept2PMUnreal*.dylib"))
	return [main, *plugin_binaries]


def binary_architectures(binary: Path) -> set[str] | None:
	output = capture(["lipo", "-archs", str(binary)])
	return set(output.split()) if output else None


def app_bundle_sha256(app: Path) -> str:
	digest = hashlib.sha256()
	for path in sorted(app.rglob("*"), key=lambda item: item.relative_to(app).as_posix()):
		relative_path = path.relative_to(app).as_posix().encode("utf-8")
		if path.is_symlink():
			digest.update(relative_path)
			digest.update(b"\0")
			digest.update(b"symlink\0")
			digest.update(os.readlink(path).encode("utf-8"))
			continue
		if path.is_dir():
			continue
		digest.update(relative_path)
		digest.update(b"\0")
		digest.update(b"file\0")
		with path.open("rb") as source:
			for chunk in iter(lambda: source.read(1024 * 1024), b""):
				digest.update(chunk)
	return digest.hexdigest()


def verify_package(app: Path, versions: dict) -> list[str]:
	failures: list[str] = []
	if not app.is_dir():
		return [f"missing staged app: {app}"]
	plist = app / "Contents" / "Info.plist"
	if not plist.is_file():
		failures.append("missing Info.plist")
	else:
		try:
			import plistlib
			usage = plistlib.loads(plist.read_bytes()).get("NSBluetoothAlwaysUsageDescription")
			if usage != BLUETOOTH_USAGE_DESCRIPTION:
				failures.append("missing or incorrect NSBluetoothAlwaysUsageDescription")
		except (OSError, ValueError):
			failures.append("unreadable Info.plist")
	metadata = app / "Contents" / "Resources" / "BuildVersions.json"
	try:
		if json.loads(metadata.read_text(encoding="utf-8")) != versions:
			failures.append("staged BuildVersions.json does not match Config/BuildVersions.json")
	except (OSError, json.JSONDecodeError):
		failures.append("missing or unreadable staged BuildVersions.json")
	binaries = package_binary_paths(app)
	if not binaries[0].is_file():
		failures.append("missing expected executable: Contents/MacOS/VirtualRowing")
	if len(binaries) < 2:
		failures.append("missing expected Concept2PM plug-in binary")
	for binary in binaries:
		if binary.is_file():
			arches = binary_architectures(binary)
			if arches != {versions["platform"]["architecture"]}:
				failures.append(f"wrong architecture for {binary.name}: {sorted(arches) if arches else 'unreadable'}")
	return failures


def unreal_package_verify() -> int:
	app = staged_app(UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: missing or ambiguous staged VirtualRowing.app; run `make unreal-shipping` first", file=sys.stderr)
		return 1
	failures = verify_package(app, load_versions())
	if failures:
		for failure in failures:
			print(f"FAIL {failure}", file=sys.stderr)
		return 1
	print(f"OK unsigned Shipping package: app_sha256={app_bundle_sha256(app)}")
	return 0


def toolchain_bluetooth_probe() -> int:
	app = staged_app(UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: missing staged VirtualRowing.app; run `make unreal-shipping` first", file=sys.stderr)
		return 1
	if verify_package(app, load_versions()):
		print("ERROR: package verification failed; refusing to launch probe", file=sys.stderr)
		return 1
	executable = app / "Contents" / "MacOS" / "VirtualRowing"
	probe_directory = ROOT / "Saved" / "Logs"
	probe_directory.mkdir(parents=True, exist_ok=True)
	def probe_results() -> set[Path]:
		return set(probe_directory.glob("toolchain-bluetooth-probe-*.json"))

	before = probe_results()
	result = run([
		str(executable),
		"-ToolchainBluetoothProbe",
		f"-ToolchainBluetoothProbeResultDir={probe_directory}",
	], env=tool_env(load_versions()))
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
	options = capture_combined(["codesign", "-d", "--options", ":-", str(app)]) or ""
	if "runtime" not in options.lower():
		return "Hardened Runtime is not enabled"
	entitlement_text = capture(["codesign", "-d", "--entitlements", ":-", str(app)]) or ""
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
	app = staged_app(UNREAL_ARCHIVE_DIR)
	if not app or verify_package(app, load_versions()):
		print("ERROR: a verified unsigned Shipping package is required", file=sys.stderr)
		return 1
	entitlements = ROOT / "Build" / "VirtualRowing.entitlements"
	if not entitlements.is_file():
		print("ERROR: hardened-runtime entitlements file is missing", file=sys.stderr)
		return 1
	code_paths = []
	for path in app.rglob("*"):
		if not path.is_file():
			continue
		file_kind = capture(["file", "-b", str(path)]) or ""
		if "Mach-O" in file_kind:
			code_paths.append(path)
	code_paths.sort(key=lambda path: len(path.parts), reverse=True)
	for path in code_paths:
		if run(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", str(path)]).returncode:
			return 1
	if run(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", "--entitlements", str(entitlements), str(app)]).returncode:
		return 1
	if run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)]).returncode:
		return 1
	signature_failure = verify_release_signature(app)
	if signature_failure:
		print(f"ERROR: {signature_failure}", file=sys.stderr)
		return 1
	dmg = UNREAL_SHIPPING_DIR / "VirtualRowing.dmg"
	if dmg.exists():
		dmg.unlink()
	if run(["hdiutil", "create", "-volname", "VirtualRowing", "-srcfolder", str(app), "-ov", "-format", "UDZO", str(dmg)]).returncode:
		return 1
	if run(["xcrun", "notarytool", "submit", str(dmg), "--keychain-profile", notary_profile, "--wait"]).returncode:
		return 1
	if run(["xcrun", "stapler", "staple", str(dmg)]).returncode:
		return 1
	return run(["spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=4", str(dmg)]).returncode


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("command", choices=("doctor", "configure", "build", "test", "format-check", "pm5-tui", "unreal-smoke", "unreal-shipping", "unreal-package-verify", "toolchain-bluetooth-probe", "release-sign-notarize", "hil-pm5", "clean"))
	args = parser.parse_args()
	if args.command == "doctor":
		return check_doctor()
	if args.command == "configure":
		return configure()
	if args.command == "build":
		return build()
	if args.command == "test":
		return test()
	if args.command == "format-check":
		return format_check()
	if args.command == "pm5-tui":
		return run_tui()
	if args.command == "hil-pm5":
		return run_tui(hardware_probe=True)
	if args.command == "unreal-smoke":
		return unreal_smoke()
	if args.command == "unreal-shipping":
		return unreal_shipping()
	if args.command == "unreal-package-verify":
		return unreal_package_verify()
	if args.command == "toolchain-bluetooth-probe":
		return toolchain_bluetooth_probe()
	if args.command == "release-sign-notarize":
		return release_sign_notarize()
	if args.command == "clean":
		if BUILD_DIR.exists():
			shutil.rmtree(BUILD_DIR)
		return 0
	return 2


if __name__ == "__main__":
	raise SystemExit(main())
