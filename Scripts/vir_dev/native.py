"""Native CMake/Ninja configure, build, test, native-app, format-check, and clean."""

from __future__ import annotations

import shutil
import sys

from vir_dev import common


def ensure_build_tools(versions: dict) -> None:
	for binary, expected in (("cmake", versions["build_tools"]["cmake"]), ("ninja", versions["build_tools"]["ninja"])):
		observed = common.capture([binary, "--version"])
		if common.version_number(observed) != common.version_number(expected):
			print(f"ERROR: {binary} {expected} is required for native builds; observed {observed or 'missing'}", file=sys.stderr)
			raise SystemExit(1)


def cmake_base_args(versions: dict) -> list[str]:
	return [
		"cmake",
		"-S",
		str(common.ROOT),
		"-B",
		str(common.BUILD_DIR),
		"-G",
		"Ninja",
		"-DCMAKE_BUILD_TYPE=Debug",
		"-DCMAKE_OSX_ARCHITECTURES=arm64",
		f"-DVIR_APP_VERSION={versions['application_version']}",
		f"-DVIR_APP_BUILD={common.application_build_number(versions)}",
		f"-DVIR_SOURCE_REVISION={common.source_revision()}",
	]


def app_cmake_args(versions: dict) -> list[str]:
	"""Release, static-protobuf, libraries-only tree that the Unreal module links (Phase 1 Milestone 5)."""
	return [
		"cmake",
		"-S",
		str(common.ROOT),
		"-B",
		str(common.APP_BUILD_DIR),
		"-G",
		"Ninja",
		"-DCMAKE_BUILD_TYPE=Release",
		"-DCMAKE_OSX_ARCHITECTURES=arm64",
		f"-DCMAKE_OSX_DEPLOYMENT_TARGET={versions['static_dependencies']['app_deployment_target']}",
		"-DVIR_STATIC_PROTOBUF=ON",
		"-DVIR_APP_LIBS_ONLY=ON",
		"-DVIR_BUILD_TESTS=OFF",
		"-DVIR_BUILD_PM5_TUI=OFF",
		f"-DVIR_APP_VERSION={versions['application_version']}",
		f"-DVIR_APP_BUILD={common.application_build_number(versions)}",
		f"-DVIR_SOURCE_REVISION={common.source_revision()}",
	]


def native_app() -> int:
	"""Build the merged app-bound static archive; the first configure downloads hash-pinned protobuf/abseil sources."""
	versions = common.load_versions()
	ensure_build_tools(versions)
	env = common.tool_env(versions)
	result = common.run(app_cmake_args(versions), env=env).returncode
	if result:
		return result
	return common.run(["cmake", "--build", str(common.APP_BUILD_DIR), "--parallel"], env=env).returncode


def configure() -> int:
	versions = common.load_versions()
	ensure_build_tools(versions)
	return common.run(cmake_base_args(versions), env=common.tool_env(versions)).returncode


def build(target: str | None = None) -> int:
	result = configure()
	if result:
		return result
	args = ["cmake", "--build", str(common.BUILD_DIR), "--parallel"]
	if target:
		args.extend(["--target", target])
	return common.run(args, env=common.tool_env(common.load_versions())).returncode


def test() -> int:
	result = build()
	if result:
		return result
	return common.run(["ctest", "--test-dir", str(common.BUILD_DIR), "--output-on-failure", "--no-tests=error"], env=common.tool_env(common.load_versions())).returncode


def format_check() -> int:
	versions = common.load_versions()
	env = common.tool_env(versions)
	clang_format = shutil.which("clang-format", path=env.get("PATH"))
	if not clang_format:
		clang_format = common.capture(["xcrun", "--find", "clang-format"], env=env)
	if not clang_format:
		print("ERROR: clang-format is required; install the formatter shipped with the pinned Xcode", file=sys.stderr)
		return 1
	sources: list[str] = []
	for directory in ("Source", "Plugins/Concept2PM", "Tools", "Tests"):
		base = common.ROOT / directory
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
	result = common.run([clang_format, "--dry-run", "--Werror", *sources], env=env)
	return result.returncode


def clean() -> int:
	for directory in (common.BUILD_DIR, common.APP_BUILD_DIR):
		if directory.exists():
			shutil.rmtree(directory)
	return 0
