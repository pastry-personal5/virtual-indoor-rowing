"""Unreal Editor smoke build and unsigned Shipping BuildCookRun."""

from __future__ import annotations

import shutil
import sys

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
		print("ERROR: `make native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	result = common.run([
		str(ubt),
		"UnrealEditor",
		"Mac",
		"Development",
		f"-Project={project}",
		"-WaitMutex",
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
		print("ERROR: `make native-app` failed; the Unreal module cannot link without it", file=sys.stderr)
		return native_result
	packaging.write_shipping_provenance(versions)
	env = common.tool_env(versions)
	env["VIR_SOURCE_REVISION"] = common.source_revision()
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
