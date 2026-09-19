#!/usr/bin/env python3
"""Read-only consistency checks for the Phase 1 packet."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKET = ROOT / "docs" / "phase-1"
ALLOWED_EVIDENCE_STATUSES = {"Verified", "Pending owner run", "Blocked by environment", "Deferred to Phase 4"}


def main() -> int:
	readme = (PACKET / "README.md").read_text(encoding="utf-8")
	failures: list[str] = []
	if "Milestones 1–8" in readme or "Milestones 1-8" in readme:
		failures.append("phase README still contains stale Milestones 1–8 wording")
	if "Milestone 10" not in readme:
		failures.append("phase README does not list Milestone 10")

	ids: list[int] = []
	for path in sorted(PACKET.glob("[0-9][0-9]-*.md")):
		match = re.search(r"^# Phase 1 Milestone (\d+):", path.read_text(encoding="utf-8"), re.MULTILINE)
		if match:
			ids.append(int(match.group(1)))
	if len(ids) != len(set(ids)):
		failures.append("duplicate Phase 1 milestone number")
	if ids != sorted(ids):
		failures.append("milestone numbering is not ordered")

	report = PACKET / "04-evidence-report.md"
	report_text = report.read_text(encoding="utf-8")
	for line in report_text.splitlines():
		if not line.startswith("|") or line.startswith("|---"):
			continue
		columns = [column.strip() for column in line.strip("|").split("|")]
		if len(columns) < 3 or columns[0] == "Requirement / gate":
			continue
		status = columns[2]
		if status not in ALLOWED_EVIDENCE_STATUSES:
			failures.append(f"unknown evidence status: {status}")

	for path in sorted(PACKET.glob("*.md")):
		for target in re.findall(r"\]\(([^)#]+)(?:#[^)]+)?\)", path.read_text(encoding="utf-8")):
			if target.startswith(("http://", "https://")):
				continue
			if not (path.parent / target).exists():
				failures.append(f"{path.relative_to(ROOT)} links to missing {target}")

	if failures:
		for failure in failures:
			print(f"FAIL: {failure}")
		return 1
	print(f"OK Phase 1 packet: milestones={','.join(map(str, ids))}; evidence statuses valid")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
