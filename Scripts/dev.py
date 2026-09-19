#!/usr/bin/env python3
"""Reproducible local build entry points for the M1 diagnostic client."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vir_dev import doctor, native, release, tui, unreal  # noqa: E402


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("command", choices=("doctor", "configure", "build", "test", "format-check", "pm5-tui", "unreal-smoke", "unreal-shipping", "unreal-package-verify", "toolchain-bluetooth-probe", "release-sign-notarize", "hil-pm5", "pm5-tui-journal", "native-app", "clean", "clean-unreal"))
	args = parser.parse_args()
	if args.command == "doctor":
		return doctor.check_doctor()
	if args.command == "configure":
		return native.configure()
	if args.command == "build":
		return native.build()
	if args.command == "test":
		return native.test()
	if args.command == "native-app":
		return native.native_app()
	if args.command == "format-check":
		return native.format_check()
	if args.command == "pm5-tui":
		return tui.run_tui()
	if args.command == "hil-pm5":
		return tui.run_tui(hardware_probe=True)
	if args.command == "pm5-tui-journal":
		return tui.run_tui(journal=True)
	if args.command == "unreal-smoke":
		return unreal.unreal_smoke()
	if args.command == "unreal-shipping":
		return unreal.unreal_shipping()
	if args.command == "unreal-package-verify":
		return unreal.unreal_package_verify()
	if args.command == "toolchain-bluetooth-probe":
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
