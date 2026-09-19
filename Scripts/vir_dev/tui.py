"""Launch the PM5 diagnostic TUI, including terminal window handling."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from vir_dev import common, native


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
				str(common.ROOT),
				"--",
				str(executable),
				*arguments,
			],
			cwd=common.ROOT,
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
			cwd=common.ROOT,
			check=False,
			capture_output=True,
			text=True,
		)
	except OSError:
		return
	if result.returncode != 0:
		print("NOTE: Terminal window resize was unavailable; continuing at current size.", file=sys.stderr)


def run_tui(hardware_probe: bool = False, journal: bool = False) -> int:
	result = native.build("pm5-tui")
	if result:
		return result
	bundle_executable = common.BUILD_DIR / "pm5-tui.app" / "Contents" / "MacOS" / "pm5-tui"
	executable = bundle_executable if bundle_executable.is_file() else common.BUILD_DIR / "pm5-tui"
	if not executable.is_file():
		print("ERROR: pm5-tui target is not present; check that Tools/pm5-tui/src has its entry point", file=sys.stderr)
		return 2
	arguments = (["--hardware-probe"] if hardware_probe else []) + (["--journal"] if journal else [])
	if launch_tui_in_magnified_wave(executable, arguments):
		return 0
	resize_apple_terminal_window_for_tui()
	# The adapter only scans after the TUI's explicit `scan` command. The HIL
	# entry point uses the same diagnostic UI and leaves selection user-driven.
	return common.run([str(executable), *arguments], env=common.tool_env(common.load_versions())).returncode
