#!/usr/bin/env python3
"""Reject platform, vendor, Unreal, or terminal dependencies in public core headers."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC_ROOT = ROOT / "Source"
FORBIDDEN_INCLUDE = re.compile(
	r"^\s*#\s*include\s*[<\"]\s*(?:"
	r"Concept2PM(?:/|\b)|"
	r"(?:CoreBluetooth|Foundation|AppKit|UIKit|ObjectiveC|CoreFoundation)(?:/|\.h|\b)|"
	r"(?:CoreMinimal|Engine|HAL|UObject|Slate|UMG|Framework)(?:/|\.h|\b)|"
	r"(?:termios|curses|ncurses|readline)(?:/|\.h|\b)|"
	r"(?:unistd|pthread|dispatch|TargetConditionals|mach|sys)(?:/|\.h|\b)"
	r")"
)
FORBIDDEN_TYPES = re.compile(
	r"\b(?:CB[A-Z][A-Za-z0-9_]*|NSError|NSString|NSData|NSUUID|"
	r"UObject|AActor|UActorComponent|FString|FName|FText|TArray|TSharedPtr|"
	r"FTerminal[A-Za-z0-9_]*|ncurses|termios)\b"
)


def check_headers() -> list[str]:
	issues: list[str] = []
	for header in sorted(PUBLIC_ROOT.glob("**/Public/**/*.h")) + sorted(
		PUBLIC_ROOT.glob("**/Public/**/*.hpp")
	):
		try:
			lines = header.read_text(encoding="utf-8").splitlines()
		except (OSError, UnicodeError) as exc:
			issues.append(f"{header.relative_to(ROOT)}: cannot read header: {exc}")
			continue
		for line_number, line in enumerate(lines, start=1):
			if FORBIDDEN_INCLUDE.search(line):
				issues.append(
					f"{header.relative_to(ROOT)}:{line_number}: forbidden platform/vendor/engine/terminal include"
				)
			if FORBIDDEN_TYPES.search(line):
				issues.append(
					f"{header.relative_to(ROOT)}:{line_number}: forbidden platform/engine/terminal type"
				)
	return issues


def main() -> int:
	issues = check_headers()
	if issues:
		for issue in issues:
			print(issue, file=sys.stderr)
		return 1
	print("Public-header dependency boundary passed.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
