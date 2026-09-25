"""Unreal Editor smoke build and unsigned Shipping BuildCookRun."""

from __future__ import annotations

import os
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from vir_dev import common, doctor, native, packaging


def editor_closed_preflight() -> bool:
	"""Native builds/cleanup require closed Editors, including commandlets.

	Query executable names only, never process arguments (which may contain
	credentials). Treat denied/incomplete process inspection as unknown, not
	proof that no Editor is running. MCP availability is not a process check.
	"""
	try:
		result = subprocess.run(["/bin/ps", "-axo", "pid=,comm="], capture_output=True, text=True, check=False, timeout=10)
		if result.returncode or not result.stdout.strip():
			raise RuntimeError("process listing unavailable")
		editors = []
		for line in result.stdout.splitlines():
			parts = line.strip().split(None, 1)
			if len(parts) != 2 or not parts[0].isdigit():
				raise RuntimeError("incomplete process listing")
			name = Path(parts[1]).name
			if name == "UnrealEditor" or name.startswith("UnrealEditor-"):
				editors.append(f"{name} (PID {parts[0]})")
	except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
		print(f"ERROR: cannot establish that Unreal Editor is closed: {exc}. Build/cleanup stopped before changing native binaries. Use an owner terminal with process-inspection access; do not infer Editor state from a port or stale PID file.", file=sys.stderr)
		return False
	if editors:
		print(f"ERROR: close Unreal Editor/commandlets before native build or cleanup: {', '.join(editors)}. Save your work and quit normally, then rerun this target. Native hot reload can retain obsolete automation classes.", file=sys.stderr)
		return False
	return True


def verify_editor_module() -> bool:
	"""Verify the module the next Editor will load, not a leftover base dylib."""
	binaries = common.ROOT / "Binaries" / "Mac"
	name = "libUnrealEditor-VirtualRowing.dylib"
	try:
		manifest = json.loads((binaries / "UnrealEditor.modules").read_text(encoding="utf-8"))
		if manifest["Modules"]["VirtualRowing"] != name:
			raise ValueError("manifest still selects a hot-reload module; a closed-Editor build is required")
		module = binaries / name
		if not module.is_file():
			raise ValueError(f"missing compiled module {name}")
		if packaging.homebrew_load_commands(module):
			raise ValueError(f"{name} loads Homebrew libraries")
	except (OSError, ValueError, KeyError, TypeError) as exc:
		print(f"ERROR: Unreal Editor module verification failed: {exc}", file=sys.stderr)
		return False
	return True


def unreal_build_preflight(ue_root: Path, *, automation_tool: bool = True) -> bool:
	"""Check stock macOS UAT's mandatory per-user writes without changing caches.

	PlatformExports.Initialize calls ReadConfigFiles(null, null) before any
	BuildCookRun command: neither -project nor -ubtargs redirects that cache.
	"""
	if sys.platform != "darwin":
		return True
	if not (ue_root / "Engine" / "Build" / "InstalledBuild.txt").is_file():
		return True
	epic = Path.home() / "Library" / "Application Support" / "Epic"
	settings = epic / ("UnrealEngine" if automation_tool else "UnrealBuildTool")
	cache = settings / f"XmlConfigCache-{str(ue_root.resolve()).replace(':', '').replace('/', '+')}.bin"
	directories = (settings, settings / "Intermediate" / "Build") if automation_tool else (settings,)
	files = (cache, settings / "Intermediate" / "Build" / "UnrealBuildTool.Env.BuildConfiguration.xml") if automation_tool else (settings / "Trace.uba", *settings.glob("Trace-backup-*.uba"))
	try:
		for directory in directories:
			directory.mkdir(parents=True, exist_ok=True)
			# os.access does not reliably detect managed-runner sandbox denials.
			with tempfile.TemporaryFile(prefix="vir-uat-preflight-", dir=directory):
				pass
		for path in files:
			if path.exists():
				# No truncation, writes or timestamp changes to existing user files.
				with path.open("r+b"):
					pass
	except OSError as exc:
		print(f"ERROR: Unreal build tools require writable per-user build state at {settings}: {exc}", file=sys.stderr)
		print("This blocks the build before compilation/cooking; it is not an Unreal Editor crash. Run this make target from an owner terminal with write access to that directory. Do not delete caches or change engine files to work around the denial.", file=sys.stderr)
		if automation_tool:
			print("-XmlConfigCache is load-only in UBT and does not redirect UAT startup.", file=sys.stderr)
		else:
			print("UE 5.8 rotates Trace.uba before processing -NoLog/-NoUBA; those flags cannot prevent this denial.", file=sys.stderr)
		return False
	return True


def unreal_smoke() -> int:
	if not editor_closed_preflight():
		return 1
	versions = common.load_versions()
	ue_root = common.find_unreal(versions)
	if not ue_root:
		print("ERROR: Unreal Engine 5.8 was not found; install the approved patch and set UE_ROOT", file=sys.stderr)
		return 1
	if doctor.check_doctor() != 0:
		print("ERROR: Unreal smoke is blocked until `make doctor` passes", file=sys.stderr)
		return 1
	ubt = ue_root / "Engine" / "Build" / "BatchFiles" / "Mac" / "Build.sh"
	if not ubt.is_file():
		print(f"ERROR: UnrealBuildTool wrapper missing at {ubt}", file=sys.stderr)
		return 1
	project = common.ROOT / "VirtualRowing.uproject"
	if not project.is_file():
		print(f"ERROR: Unreal smoke host is missing: {project}", file=sys.stderr)
		return 1
	if not unreal_build_preflight(ue_root, automation_tool=False):
		return 1
	# The VirtualRowing module links the prebuilt CMake archive; build it first so
	# a missing or stale archive fails here, not inside UBT.
	native_result = native.native_app()
	if native_result:
		print("ERROR: `make unreal-native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	# Native dependencies can take minutes. Recheck before UBT can replace DLLs.
	if not editor_closed_preflight():
		return 1
	log = common.ROOT / "Saved" / "Logs" / "UnrealBuildTool.log"
	log.parent.mkdir(parents=True, exist_ok=True)
	result = common.run([
		str(ubt),
		"VirtualRowingEditor",
		"Mac",
		"Development",
		f"-Project={project}",
		"-WaitMutex",
		"-NoHotReload",
		# Preserve diagnostics at a supported project-local log path. This does
		# not redirect UBA's separate per-user trace or XML configuration cache.
		f"-Log={log}",
		# An explicit XmlConfigCache is load-only in UBT. Omitting it lets UBT
		# generate and maintain the project-local cache under Intermediate/.
	], env=common.tool_env(versions)).returncode
	if result:
		return result
	# Phase 1 Milestone 5 closing bar: the UBT-built module must not load Homebrew libraries.
	return 0 if verify_editor_module() else 1


def unreal_shipping() -> int:
	if not editor_closed_preflight():
		return 1
	versions = common.load_versions()
	ue_root = common.find_unreal(versions)
	if not ue_root:
		print("ERROR: Unreal Engine 5.8 was not found; install the approved patch and set UE_ROOT", file=sys.stderr)
		return 1
	project = common.ROOT / "VirtualRowing.uproject"
	uat = ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
	if not project.is_file() or not uat.is_file():
		print("ERROR: Unreal project or RunUAT.sh is missing", file=sys.stderr)
		return 1
	if not unreal_build_preflight(ue_root):
		return 1
	if doctor.check_doctor() != 0:
		print("ERROR: Unreal Shipping is blocked until `make doctor` passes", file=sys.stderr)
		return 1
	# The Shipping module links the prebuilt CMake archive; rebuild it so a stale
	# archive can never be packaged (UBT does not notice archive changes by itself).
	native_result = native.native_app()
	if native_result:
		print("ERROR: `make unreal-native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	if not editor_closed_preflight():
		return 1
	packaging.write_shipping_provenance(versions)
	env = common.tool_env(versions)
	env["VIR_SOURCE_REVISION"] = common.source_revision()
	# RunUAT defaults to ~/Library/Logs/Unreal Engine/LocalBuildLogs on macOS.
	# Keep generated build diagnostics inside the repository build area so the
	# wrapper works on restricted builders and does not mutate a developer's
	# unrelated global Unreal logs.
	env["uebp_LogFolder"] = str(common.UNREAL_SHIPPING_DIR / "logs")
	result = common.run(packaging.unreal_shipping_command(ue_root, project, common.UNREAL_ARCHIVE_DIR), env=env)
	if result.returncode:
		return result.returncode
	app = packaging.staged_app(common.UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: BuildCookRun succeeded but exactly one staged .app was not archived", file=sys.stderr)
		return 1
	metadata = app / "Contents" / "Resources" / "BuildVersions.json"
	metadata.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(common.VERSIONS_PATH, metadata)
	# UAT's Mac stage/package step embeds Binaries/ and Content/ under
	# Contents/UE/<ProjectName>/ but never copies the .uproject file itself there. Without
	# it, FGenericPlatformMisc::ProjectDir()'s fallback search (relative to that exact
	# embedded location) never finds a project file, IProjectManager::LoadProjectFile()
	# never succeeds, and the primary game module's StartupModule() never runs — silently,
	# since the engine's own "could not find a valid project file" log is Warning-level and
	# UE_LOG is a no-op in this Shipping config. Stage it ourselves.
	embedded_project = app / "Contents" / "UE" / project.stem / project.name
	embedded_project.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(project, embedded_project)
	print(app)
	return 0


HAN_GENERATED_CONFIG = {
	common.ROOT / "Config" / "GeneratedPakFileRules.ini": packaging.HAN_PAK_FILE_RULES,
	common.ROOT / "Config" / "GeneratedGame.ini": packaging.HAN_GAME_OVERRIDES,
}
HAN_IOSTORE_MAGIC = b"-==--==--==--==-"


def han_source_verify() -> int:
	project = common.ROOT / "VirtualRowing.uproject"
	failures = packaging.han_source_failures(project, require_tracked=True)
	for failure in failures[:10]:
		print(f"ERROR: {failure}", file=sys.stderr)
	if failures:
		if len(failures) > 10:
			print(f"ERROR: {len(failures) - 10} further Han source failures omitted", file=sys.stderr)
		return 1
	candidates = packaging.han_external_reference_candidates(project)
	for candidate in candidates[:10]:
		print(f"WARNING: serialized external project path needs Editor dependency review: {candidate}", file=sys.stderr)
	if len(candidates) > 10:
		print(f"WARNING: {len(candidates) - 10} further external-path candidates omitted", file=sys.stderr)
	print("OK Han runtime map and referenced source packages are present and tracked")
	return 0


def _han_chunk_files(stage_dir: Path) -> dict[str, Path] | None:
	"""Find the staged Han chunk trio, or None unless each extension resolves to exactly one file."""
	found = {}
	for extension in ("pak", "utoc", "ucas"):
		# UAT also copies the Paks into the staged .app; that duplicate is not a second cook.
		matches = sorted(path for path in stage_dir.rglob(f"{packaging.HAN_PAK_CHUNK}-*.{extension}") if not any(part.endswith(".app") for part in path.parts))
		if len(matches) != 1:
			return None
		found[extension] = matches[0]
	return found


def han_external_cook() -> int:
	"""Cook Han River into the external HanRiver.{pak,utoc,ucas} IoStore trio.

	Output goes to $VIR_HAN_IOSTORE_DIR (the same variable `content-release-package`
	consumes) or Build/han-cook/HanRiver.
	"""
	if not editor_closed_preflight():
		return 1
	if han_source_verify():
		return 1
	# Serialized names can be stale, so the editor-open source check reports
	# them as candidates. A release cook must wait for an Editor dependency
	# inspection and a clean saved runtime map instead of risking an external
	# trial asset in the signed Han package.
	external_candidates = packaging.han_external_reference_candidates(common.ROOT / "VirtualRowing.uproject")
	if external_candidates:
		for candidate in external_candidates[:10]:
			print(f"ERROR: Han cook has an unresolved external project path: {candidate}", file=sys.stderr)
		print("ERROR: inspect live dependencies in Unreal Editor, remove external references, and save before cooking", file=sys.stderr)
		return 1
	versions = common.load_versions()
	ue_root = common.find_unreal(versions)
	if not ue_root:
		print("ERROR: Unreal Engine 5.8 was not found; install the approved patch and set UE_ROOT", file=sys.stderr)
		return 1
	project = common.ROOT / "VirtualRowing.uproject"
	uat = ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
	if not project.is_file() or not uat.is_file():
		print("ERROR: Unreal project or RunUAT.sh is missing", file=sys.stderr)
		return 1
	if not unreal_build_preflight(ue_root):
		return 1
	if doctor.check_doctor() != 0:
		print("ERROR: Han cook is blocked until `make doctor` passes", file=sys.stderr)
		return 1
	for generated in HAN_GENERATED_CONFIG:
		if generated.exists():
			print(f"ERROR: {generated} already exists; remove the stale generated file first", file=sys.stderr)
			return 1
	output = Path(os.environ.get("VIR_HAN_IOSTORE_DIR") or common.HAN_COOK_DIR / "HanRiver").resolve()
	if any(output.joinpath(f"HanRiver.{ext}").exists() for ext in ("pak", "utoc", "ucas")):
		print(f"ERROR: {output} already holds a cook; choose a new VIR_HAN_IOSTORE_DIR or remove it", file=sys.stderr)
		return 1
	native_result = native.native_app()
	if native_result:
		print("ERROR: `make unreal-native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	if not editor_closed_preflight():
		return 1
	stage_dir = common.HAN_COOK_DIR / "stage"
	shutil.rmtree(stage_dir, ignore_errors=True)
	env = common.tool_env(versions)
	env["VIR_SOURCE_REVISION"] = common.source_revision()
	env["uebp_LogFolder"] = str(common.HAN_COOK_DIR / "logs")
	try:
		for generated, content in HAN_GENERATED_CONFIG.items():
			generated.write_text(content, encoding="utf-8")
		result = common.run(packaging.han_cook_command(ue_root, project, stage_dir), env=env)
	finally:
		for generated in HAN_GENERATED_CONFIG:
			generated.unlink(missing_ok=True)
	if result.returncode:
		return result.returncode
	staged = _han_chunk_files(stage_dir)
	if not staged:
		print(f"ERROR: BuildCookRun did not stage exactly one {packaging.HAN_PAK_CHUNK}-*.pak/.utoc/.ucas trio under {stage_dir}", file=sys.stderr)
		return 1
	for extension, path in staged.items():
		if path.stat().st_size == 0:
			print(f"ERROR: staged {path.name} is empty", file=sys.stderr)
			return 1
	with staged["utoc"].open("rb") as toc:
		if toc.read(len(HAN_IOSTORE_MAGIC)) != HAN_IOSTORE_MAGIC:
			print("ERROR: staged .utoc is not an Unreal IoStore TOC", file=sys.stderr)
			return 1
	output.mkdir(parents=True, exist_ok=True)
	for extension, path in staged.items():
		shutil.copy2(path, output / f"HanRiver.{extension}")
	print(output)
	return 0


def unreal_package_verify() -> int:
	app = packaging.staged_app(common.UNREAL_ARCHIVE_DIR)
	if not app:
		print("ERROR: missing or ambiguous staged .app; run `make unreal-shipping` first", file=sys.stderr)
		return 1
	failures = packaging.verify_package(app, common.load_versions())
	if failures:
		for failure in failures:
			print(f"FAIL {failure}", file=sys.stderr)
		return 1
	print(f"OK unsigned Shipping package: app_sha256={packaging.app_bundle_sha256(app)}")
	return 0


UNREAL_INTERMEDIATE_DIRS = (
	common.ROOT / "Saved",
	common.ROOT / "Intermediate",
	common.ROOT / "Binaries",
	common.ROOT / "DerivedDataCache",
	common.UNREAL_SHIPPING_DIR,
)


def clean_unreal() -> int:
	if not editor_closed_preflight():
		return 1
	for directory in UNREAL_INTERMEDIATE_DIRS:
		if directory.exists():
			shutil.rmtree(directory)
	return 0
