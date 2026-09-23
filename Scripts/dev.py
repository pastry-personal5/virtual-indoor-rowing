#!/usr/bin/env python3
"""Reproducible local build entry points for the M1 diagnostic client."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vir_dev import content, doctor, native, release, tui, unreal  # noqa: E402


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("command", choices=("doctor", "configure", "build", "test", "format-check", "format", "pm5-tui", "unreal-smoke", "unreal-shipping", "han-source-verify", "han-external-cook", "unreal-package-verify", "unreal-bluetooth-probe", "release-sign-notarize", "pm5-tui-hil", "pm5-tui-journal", "unreal-native-app", "content-canary", "content-fixture", "content-release-package", "han-area-audit", "han-area-register", "han-area-osm-acquire", "han-area-osm-generate", "han-area-test", "clean", "clean-unreal"))
	args = parser.parse_args()
	if args.command in ("han-area-audit", "han-area-register"):
		import han_area
		try:
			return han_area.command(args.command.removeprefix("han-area-"))
		except (KeyError, OSError, ValueError) as error:
			print(f"ERROR: {error}", file=sys.stderr)
			return 2
	if args.command == "han-area-test":
		import unittest
		scripts = Path(__file__).resolve().parent
		if not (scripts / "test_han_area.py").is_file():
			print("ERROR: Scripts/test_han_area.py is missing", file=sys.stderr)
			return 1
		suite = unittest.defaultTestLoader.discover(str(scripts), pattern="test_han_area.py")
		if suite.countTestCases() == 0:
			print("ERROR: no Han area tests were discovered", file=sys.stderr)
			return 1
		return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
	if args.command == "han-area-osm-generate":
		import osm_han_area
		import os
		print(json.dumps(osm_han_area.generate(Path(os.environ["HAN_OSM_GENERATE_CONFIG"]), Path(os.environ["HAN_OSM_GENERATE_OUTPUT"])), ensure_ascii=False, indent=2))
		return 0
	if args.command == "han-area-osm-acquire":
		import osm_han_area
		import os
		print(json.dumps(osm_han_area.acquire(Path(os.environ["HAN_OSM_ACQUIRE_CONFIG"]), Path(os.environ["HAN_OSM_ACQUIRE_OUTPUT"])), ensure_ascii=False, indent=2))
		return 0
	if args.command == "doctor":
		return doctor.check_doctor()
	if args.command == "configure":
		return native.configure()
	if args.command == "build":
		return native.build()
	if args.command == "test":
		return native.test()
	if args.command == "unreal-native-app":
		return native.native_app()
	if args.command == "format-check":
		return native.format_check()
	if args.command == "format":
		return native.format_sources()
	if args.command == "pm5-tui":
		return tui.run_tui()
	if args.command == "pm5-tui-hil":
		return tui.run_tui(hardware_probe=True)
	if args.command == "pm5-tui-journal":
		return tui.run_tui(journal=True)
	if args.command == "unreal-smoke":
		return unreal.unreal_smoke()
	if args.command == "unreal-shipping":
		return unreal.unreal_shipping()
	if args.command == "han-source-verify":
		return unreal.han_source_verify()
	if args.command == "han-external-cook":
		return unreal.han_external_cook()
	if args.command == "unreal-package-verify":
		return unreal.unreal_package_verify()
	if args.command == "content-canary":
		return content.command("canary")
	if args.command == "content-fixture":
		return content.command("fixture")
	if args.command == "content-release-package":
		return content.command("release-package")
	if args.command == "unreal-bluetooth-probe":
		return release.toolchain_bluetooth_probe()
	if args.command == "release-sign-notarize":
		return release.release_sign_notarize()
	if args.command == "clean":
		return native.clean()
	if args.command == "clean-unreal":
		return unreal.clean_unreal()
	return 2


if __name__ == "__main__":
	raise SystemExit(main())
