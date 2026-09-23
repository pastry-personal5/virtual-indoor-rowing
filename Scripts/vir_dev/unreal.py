"""Unreal Editor smoke build and unsigned Shipping BuildCookRun."""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

from vir_dev import common, doctor, native, packaging


def unreal_smoke() -> int:
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
	# The VirtualRowing module links the prebuilt CMake archive; build it first so
	# a missing or stale archive fails here, not inside UBT.
	native_result = native.native_app()
	if native_result:
		print("ERROR: `make unreal-native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	result = common.run([
		str(ubt),
		"UnrealEditor",
		"Mac",
		"Development",
		f"-Project={project}",
		"-WaitMutex",
		# The managed runner may deny UnrealBuildTool's default per-user log
		# rotation. Keep this compile smoke test read-only outside the worktree.
		"-NoLog",
		# An explicit XmlConfigCache is load-only in UBT. Omitting it lets UBT
		# generate and maintain the project-local cache under Intermediate/.
	], env=common.tool_env(versions)).returncode
	if result:
		return result
	# Phase 1 Milestone 5 closing bar: the UBT-built module must not load Homebrew libraries.
	module = common.ROOT / "Binaries" / "Mac" / "libUnrealEditor-VirtualRowing.dylib"
	homebrew = packaging.homebrew_load_commands(module) if module.is_file() else []
	if homebrew:
		print(f"ERROR: {module.name} is not self-contained; it loads Homebrew libraries: {homebrew}", file=sys.stderr)
		return 1
	return 0


def unreal_shipping() -> int:
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
	# The Shipping module links the prebuilt CMake archive; rebuild it so a stale
	# archive can never be packaged (UBT does not notice archive changes by itself).
	native_result = native.native_app()
	if native_result:
		print("ERROR: `make unreal-native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
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
	if han_source_verify():
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
	for directory in UNREAL_INTERMEDIATE_DIRS:
		if directory.exists():
			shutil.rmtree(directory)
	return 0
