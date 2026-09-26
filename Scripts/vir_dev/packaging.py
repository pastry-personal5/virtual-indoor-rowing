"""Shipping .app staging inspection and package verification."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from vir_dev import common


BLUETOOTH_USAGE_DESCRIPTION = "Virtual Rowing uses Bluetooth to find and connect to your Concept2 PM5 rowing monitor and read your rowing data. It only scans after you choose to connect."
CONCEPT2PM_MODULE_NAME = "Concept2PMUnreal"
# UE's MCP plugin auto-starts in cook commandlets too. Override only the child
# process so a cook cannot contend with the interactive editor's MCP listener.
COOKER_MCP_OVERRIDE = "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False"


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
		'-ubtargs=-NoUBA -NoHotReload',
		"-cook",
		f"-AdditionalCookerOptions={COOKER_MCP_OVERRIDE}",
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
HAN_MAP_RELATIVE_PATH = HAN_CONTENT_RELATIVE_DIR / "Maps" / "L_HanRiver_BlueHour.umap"
HAN_ASSET_REFERENCE = re.compile(rb"/Game/Phase2/HanRiver/(?:Maps|Materials|Meshes|Textures)/[A-Za-z0-9_+/-]+")
PROJECT_ASSET_REFERENCE = re.compile(rb"/Game/[A-Za-z0-9_+/-]+")
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
	"""Cook the runtime map and reviewed asset directories into one IoStore chunk."""
	return [
		str(ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"),
		"BuildCookRun",
		f"-project={project}",
		"-noP4",
		"-platform=Mac",
		"-targetplatform=Mac",
		"-clientconfig=Shipping",
		"-build",
		'-ubtargs=-NoUBA -NoHotReload',
		"-cook",
		f"-AdditionalCookerOptions={COOKER_MCP_OVERRIDE}",
		"-manifests",
		f"-map={HAN_MAP}",
		# Keep review and dated backup maps out of the release cook.
		f"-cookdir={project.parent / HAN_CONTENT_RELATIVE_DIR / 'Materials'}+{project.parent / HAN_CONTENT_RELATIVE_DIR / 'Meshes'}+{project.parent / HAN_CONTENT_RELATIVE_DIR / 'Textures'}",
		"-pak",
		"-iostore",
		"-stage",
		f"-stagingdirectory={stage_dir}",
		"-specifiedarchitecture=arm64",
		"-CookCultures=en",
		"-I18NPreset=English",
	]


def han_source_failures(project: Path, *, require_tracked: bool = False) -> list[str]:
	"""Check the runtime map's transitive Han package references before cooking."""
	root = project.parent
	level = root / HAN_MAP_RELATIVE_PATH
	failures: list[str] = []
	required: set[Path] = set()
	pending = [HAN_MAP_RELATIVE_PATH]
	osm_references: set[str] = set()
	while pending:
		relative = pending.pop()
		if relative in required:
			continue
		required.add(relative)
		asset = root / relative
		try:
			data = asset.read_bytes()
		except OSError:
			failures.append(f"missing Han source package: {asset}")
			continue
		if not data or not data.startswith(bytes.fromhex("c1832a9e")):
			failures.append(f"Han source package is empty, not an Unreal package, or an LFS pointer: {asset}")
			continue
		for match in HAN_ASSET_REFERENCE.findall(data):
			reference = match.decode("ascii")
			if "/Meshes/Area01/OSM/" in reference and relative == HAN_MAP_RELATIVE_PATH:
				osm_references.add(reference)
			dependency = Path("Content") / reference.removeprefix("/Game/")
			dependency = dependency.with_suffix(".umap" if "/Maps/" in reference else ".uasset")
			if dependency not in required:
				pending.append(dependency)
	if not osm_references:
		failures.append("runtime Han map has no Area 01 OSM mesh references")
	if require_tracked:
		result = subprocess.run(["git", "ls-files", "-z", "--", *(path.as_posix() for path in sorted(required))], cwd=root, capture_output=True, check=False)
		if result.returncode:
			failures.append("cannot check Git index for Han source packages")
		else:
			tracked = {path.decode("utf-8") for path in result.stdout.split(b"\0") if path}
			for relative in sorted(required - {Path(path) for path in tracked}):
				failures.append(f"Han source package is not tracked by Git: {relative}")
	return failures


def han_external_reference_candidates(project: Path) -> list[str]:
	"""Report serialized project paths outside Han; only Editor can prove live dependencies."""
	root = project.parent
	content = root / HAN_CONTENT_RELATIVE_DIR
	packages = [root / HAN_MAP_RELATIVE_PATH]
	for directory in ("Materials", "Meshes", "Textures"):
		packages.extend(sorted((content / directory).rglob("*.uasset")))
	candidates: set[str] = set()
	for package in packages:
		try:
			data = package.read_bytes()
		except OSError:
			continue  # Missing packages are reported by han_source_failures.
		for match in PROJECT_ASSET_REFERENCE.findall(data):
			path = match.decode("ascii")
			if not path.startswith("/Game/Phase2/HanRiver/"):
				candidates.add(f"{package.relative_to(root)}: {path}")
	return sorted(candidates)


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


def ad_hoc_sign_bundle(app: Path) -> bool:
	"""Seal an internal Phase 0–3 app after local staging adds runtime files.

	UAT signs the bundle it packages, but the diagnostic provenance and embedded
	project descriptor are deliberately added afterwards.  Re-seal only after all
	post-stage files are present so macOS does not reject the otherwise unsigned,
	ad-hoc internal bundle before its game module can start.
	"""
	return common.run(["codesign", "--force", "--sign", "-", str(app)]).returncode == 0


def has_valid_code_signature(app: Path) -> bool:
	"""Return whether an existing bundle seal still covers all staged files."""
	if not (app / "Contents" / "_CodeSignature").is_dir():
		return False
	try:
		result = subprocess.run(
			["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)],
			cwd=common.ROOT,
			check=False,
			text=True,
			capture_output=True,
		)
	except OSError:
		return False
	return result.returncode == 0


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
	# Fixture bundles used by the source-level verifier intentionally have no
	# signature. A real UAT archive does, and its seal must still be valid after
	# the wrapper stages diagnostic runtime files.
	if (app / "Contents" / "_CodeSignature").exists() and not has_valid_code_signature(app):
		failures.append("staged app code signature is invalid")
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
