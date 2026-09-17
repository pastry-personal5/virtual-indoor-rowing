#!/usr/bin/env python3
"""Phase 0 Milestone 4 Spike B acceptance test.

Drives the durability-spike binary as an external process, kills it at
each of the three boundary classes ADR-0004 names (mid-chunk write,
between chunks, during the final-summary transaction), then reopens the
database and asserts recovery loses at most the uncommitted second and
explicitly marks recovered_after_unclean_exit. Modeled on
Tools/pm5-tui/tests/StatusDiagnosticsSmoke.py's process-driving pattern.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

SAMPLE_COUNT = 50
CHUNK_SIZE = 10
BOUNDARY_CHUNK_INDEX = 3


def parse_report(output: str) -> dict:
    report = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if value in ("true", "false"):
            report[key] = value == "true"
        else:
            try:
                report[key] = int(value)
            except ValueError:
                report[key] = value
    return report


def run_kill_scenario(executable: str, db_path: Path, kill_at: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [executable, "--db", str(db_path), "--run", "--kill-at", kill_at,
         "--sample-count", str(SAMPLE_COUNT)],
        check=False,
        capture_output=True,
        text=True,
    )


def run_recover(executable: str, db_path: Path) -> dict:
    result = subprocess.run(
        [executable, "--db", str(db_path), "--recover"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"--recover exited {result.returncode}: {result.stdout}{result.stderr}"
        )
    return parse_report(result.stdout)


def assert_killed_non_gracefully(kill_at: str, result: subprocess.CompletedProcess) -> None:
    if result.returncode != -9:
        raise AssertionError(
            f"{kill_at}: expected the process to die from SIGKILL (returncode -9), "
            f"got {result.returncode}: {result.stdout}{result.stderr}"
        )
    if "completed_gracefully" in result.stdout:
        raise AssertionError(f"{kill_at}: process reported graceful completion despite being killed")


def assert_report(kill_at: str, report: dict, expected_highest_sequence: int) -> None:
    if report.get("highest_verified_sequence") != expected_highest_sequence:
        raise AssertionError(
            f"{kill_at}: expected highest_verified_sequence="
            f"{expected_highest_sequence}, got {report}"
        )
    if report.get("truncated_chunk_count") != 0:
        raise AssertionError(f"{kill_at}: unexpected truncated chunks: {report}")
    if report.get("duplicate_chunk_count") != 0:
        raise AssertionError(f"{kill_at}: unexpected duplicate chunks: {report}")
    if report.get("recovered_after_unclean_exit") is not True:
        raise AssertionError(
            f"{kill_at}: expected recovered_after_unclean_exit=true, got {report}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: durability_spike_kill_recover.py <durability-spike-executable>", file=sys.stderr)
        return 2
    executable = sys.argv[1]

    last_committed_before_boundary = BOUNDARY_CHUNK_INDEX * CHUNK_SIZE - 1
    last_committed_all_chunks = SAMPLE_COUNT - 1

    scenarios = (
        ("mid-chunk", last_committed_before_boundary),
        ("between-chunks", last_committed_before_boundary),
        ("final-summary", last_committed_all_chunks),
    )

    try:
        for kill_at, expected_highest_sequence in scenarios:
            with tempfile.TemporaryDirectory(prefix="durability-spike-") as temp_dir:
                db_path = Path(temp_dir) / "journal.sqlite3"

                kill_result = run_kill_scenario(executable, db_path, kill_at)
                assert_killed_non_gracefully(kill_at, kill_result)

                report = run_recover(executable, db_path)
                assert_report(kill_at, report, expected_highest_sequence)

                # Recovery must be idempotent: recovering again changes nothing.
                second_report = run_recover(executable, db_path)
                assert_report(kill_at, second_report, expected_highest_sequence)
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print("durability-spike kill/recover scenarios passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
