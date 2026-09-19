"""Shared paths, pinned-version loading, and subprocess helpers."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSIONS_PATH = ROOT / "Config" / "BuildVersions.json"
BUILD_DIR = ROOT / "Build" / "native"
APP_BUILD_DIR = ROOT / "Build" / "native-app"
APP_MERGED_ARCHIVE = APP_BUILD_DIR / "lib" / "libVirRowingApp.a"
UNREAL_SHIPPING_DIR = ROOT / "Build" / "unreal-shipping"
UNREAL_ARCHIVE_DIR = UNREAL_SHIPPING_DIR / "archive"
UNREAL_PROVENANCE_PATH = UNREAL_SHIPPING_DIR / "provenance.json"


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
