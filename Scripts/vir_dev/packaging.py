"""Shipping .app staging inspection and package verification."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

from vir_dev import common


BLUETOOTH_USAGE_DESCRIPTION = "Virtual Rowing uses Bluetooth to find and connect to your Concept2 PM5 rowing monitor and read your rowing data. It only scans after you choose to connect."
CONCEPT2PM_MODULE_NAME = "Concept2PMUnreal"


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
		# Shipping builds remain local and reproducible on clean builders.
		'-ubtargs=-NoUBA',
		"-cook",
		"-pak",
		"-iostore",
		"-stage",
		"-package",
		"-archive",
		f"-archivedirectory={archive_dir}",
		"-specifiedarchitecture=arm64",
		"-CookCultures=en",
		"-I18NPreset=English",
	]


HAN_MAP = "/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour"
HAN_CONTENT_RELATIVE_DIR = Path("Content") / "Phase2" / "HanRiver"
HAN_PAK_CHUNK = "pakchunk1001"
# Written to Config/GeneratedPakFileRules.ini and GeneratedGame.ini (UAT's build-machine-only, git-ignored
# layer) for the duration of one Han cook so ordinary Shipping packages are unaffected.
HAN_PAK_FILE_RULES = f"""[HanRiverExternal]
OverridePaks={HAN_PAK_CHUNK}
+Files=".../Content/Phase2/HanRiver/..."
"""
# Pak file rules only run on UAT's chunk-manifest staging path.
HAN_GAME_OVERRIDES = """[/Script/UnrealEd.ProjectPackagingSettings]
bGenerateChunks=True
"""


def han_cook_command(ue_root: Path, project: Path, stage_dir: Path) -> list[str]:
	"""BuildCookRun that cooks only the Han map and stages Han content as its own IoStore chunk."""
	return [
		str(ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"),
		"BuildCookRun",
		f"-project={project}",
		"-noP4",
		"-platform=Mac",
		"-targetplatform=Mac",
		"-clientconfig=Shipping",
		"-build",
		'-ubtargs=-NoUBA',
		"-cook",
		"-manifests",
		f"-map={HAN_MAP}",
		# The map does not reference every reviewed Han asset (e.g. meshes); cook the whole directory.
		f"-cookdir={project.parent / HAN_CONTENT_RELATIVE_DIR}",
		"-pak",
		"-iostore",
		"-stage",
		f"-stagingdirectory={stage_dir}",
		"-specifiedarchitecture=arm64",
		"-CookCultures=en",
		"-I18NPreset=English",
	]


def staged_app(archive_dir: Path) -> Path | None:
	apps = sorted(archive_dir.rglob("*.app")) if archive_dir.is_dir() else []
	return apps[0] if len(apps) == 1 else None


def toolchain_fingerprint(versions: dict) -> str:
	return ";".join((
		f"ue-{versions['unreal']['version']}.{versions['unreal']['approved_patch']}",
		f"xcode-{versions['xcode']['version']}",
		f"macos-{versions['platform']['minimum_version']}",
		versions["platform"]["architecture"],
	))


def write_shipping_provenance(versions: dict) -> None:
	common.UNREAL_SHIPPING_DIR.mkdir(parents=True, exist_ok=True)
	common.UNREAL_PROVENANCE_PATH.write_text(json.dumps({
		"schema_version": 1,
		"source_revision": common.source_revision(),
		"toolchain_fingerprint": toolchain_fingerprint(versions),
		"build_versions_sha256": hashlib.sha256(common.VERSIONS_PATH.read_bytes()).hexdigest(),
	}, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def package_binary_paths(app: Path) -> list[Path]:
	return [app / "Contents" / "MacOS" / app.stem]


HOMEBREW_LOAD_PREFIXES = ("/opt/homebrew/", "/usr/local/")


def homebrew_load_commands(binary: Path) -> list[str]:
	"""Dylib install names in `otool -L` that point into Homebrew. An unreadable binary reports none."""
	output = common.capture(["otool", "-L", str(binary)]) or ""
	found: list[str] = []
	for line in output.splitlines()[1:]:
		install_name = line.strip().split(" (", 1)[0]
		if install_name.startswith(HOMEBREW_LOAD_PREFIXES):
			found.append(install_name)
	return found


def non_system_swift_load_commands(binary: Path) -> list[str]:
	"""Swift runtime install names outside /usr/lib/swift (the OS copy). The Keychain cipher's Swift shim
	must load the system runtime, never a toolchain or @rpath copy the packaged app would have to ship."""
	output = common.capture(["otool", "-L", str(binary)]) or ""
	found: list[str] = []
	for line in output.splitlines()[1:]:
		install_name = line.strip().split(" (", 1)[0]
		if "libswift" in install_name and not install_name.startswith("/usr/lib/swift/"):
			found.append(install_name)
	return found


def plugin_module_linked(main: Path, module_name: str) -> bool:
	symbols = common.capture(["nm", str(main)]) or ""
	return module_name in symbols


def binary_architectures(binary: Path) -> set[str] | None:
	output = common.capture(["lipo", "-archs", str(binary)])
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
	main = binaries[0]
	if not main.is_file():
		failures.append(f"missing expected executable: Contents/MacOS/{app.stem}")
	elif not plugin_module_linked(main, CONCEPT2PM_MODULE_NAME):
		failures.append(f"Concept2PM plug-in module ({CONCEPT2PM_MODULE_NAME}) is not linked into the Shipping executable")
	for binary in binaries:
		if binary.is_file():
			homebrew = homebrew_load_commands(binary)
			if homebrew:
				failures.append(f"{binary.name} is not self-contained: it loads Homebrew libraries {homebrew}")
			swift_runtime = non_system_swift_load_commands(binary)
			if swift_runtime:
				failures.append(f"{binary.name} is not self-contained: it loads a non-system Swift runtime {swift_runtime}")
			arches = binary_architectures(binary)
			if arches != {versions["platform"]["architecture"]}:
				failures.append(f"wrong architecture for {binary.name}: {sorted(arches) if arches else 'unreadable'}")
	return failures
