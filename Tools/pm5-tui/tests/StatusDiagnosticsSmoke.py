#!/usr/bin/env python3
"""Smoke-check explicit status output when no machine has been selected."""

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: StatusDiagnosticsSmoke.py <pm5-tui-executable>", file=sys.stderr)
        return 2

    help_result = subprocess.run(
        [sys.argv[1], "--help"],
        check=False,
        capture_output=True,
        text=True,
    )
    if help_result.returncode != 0 or "Interactive: run without arguments." not in help_result.stdout:
        print(help_result.stdout + help_result.stderr, file=sys.stderr)
        return 1

    invalid_result = subprocess.run(
        [sys.argv[1], "--not-an-option"],
        check=False,
        capture_output=True,
        text=True,
    )
    if invalid_result.returncode != 2 or "unknown option" not in invalid_result.stderr:
        print(invalid_result.stdout + invalid_result.stderr, file=sys.stderr)
        return 1

    private_marker = "unrecognized-private-input-7319"
    with tempfile.TemporaryDirectory(prefix="pm5-tui-status-smoke-") as temp_dir:
        result = subprocess.run(
            [
                sys.argv[1],
                "--script",
                "status",
                private_marker,
                "__test_identity",
                "__test_metric_correction",
                "__test_metric_sample",
                "__test_connection_state",
                "__test_telemetry_stale",
                "quit",
            ],
            check=False,
            capture_output=True,
            text=True,
            cwd=temp_dir,
        )
        log_path = Path(temp_dir) / "Logs" / "pm5-tui" / "pm5-tui.log"
        log_contents = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
        metrics_paths = list((Path(temp_dir) / "Metrics" / "pm5-tui").glob("*.jsonl"))
        metrics_records = (
            [json.loads(line) for line in metrics_paths[0].read_text(encoding="utf-8").splitlines()]
            if len(metrics_paths) == 1
            else []
        )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        print(output, file=sys.stderr)
        return result.returncode
    if private_marker in log_contents:
        print("raw user command leaked into the diagnostic log", file=sys.stderr)
        return 1
    if len(metrics_paths) != 1:
        print(f"expected one metrics file, got {len(metrics_paths)}", file=sys.stderr)
        return 1
    if not metrics_paths[0].name.startswith("pm5-tui-"):
        print("metrics file name does not use the pm5-tui run prefix", file=sys.stderr)
        return 1
    if [record["event"] for record in metrics_records] != [
        "run_started",
        "metric_corrected",
        "metric_sampled",
        "run_stopped",
    ]:
        print(f"unexpected metrics event sequence: {metrics_records}", file=sys.stderr)
        return 1
    started, correction_record, sample_record, stopped = metrics_records
    if started.get("schema_version") != 1 or not started.get("observed_at_utc"):
        print("metrics run-start record is missing schema/timestamp", file=sys.stderr)
        return 1
    for record, expected_sequence, expected_elapsed, expected_distance in (
        (correction_record, 42, 123456, 500000),
        (sample_record, 7, 7000, 12300),
    ):
        if (
            record.get("sequence") != expected_sequence
            or record.get("source_elapsed_ms") != expected_elapsed
            or record.get("distance_mm") != expected_distance
            or not record.get("observed_at_utc")
        ):
            print(f"metrics record is missing normalized telemetry: {record}", file=sys.stderr)
            return 1
    if correction_record.get("target_sample_sequence") != 42:
        print("metrics correction record is missing its target sequence", file=sys.stderr)
        return 1
    if not stopped.get("observed_at_utc"):
        print("metrics run-stop record is missing its timestamp", file=sys.stderr)
        return 1
    source_revision_pattern = r"event=TuiStarted source_revision=[0-9a-f]{12}(-dirty)?\b"
    if not re.search(source_revision_pattern, log_contents):
        print("startup log is missing the configured source revision", file=sys.stderr)
        return 1
    for expected_identity_field in (
        "event=MachineInfoObserved model=PM5",
        "hardware=test-hardware",
        "firmware=test-firmware",
        "machine_kind=IndoorRower",
        "support_state=Warn",
    ):
        if expected_identity_field not in log_contents:
            print(
                f"redacted identity log is missing {expected_identity_field!r}",
                file=sys.stderr,
            )
            return 1

    for expected_telemetry_field in (
        "event=MetricSampled telemetry sequence=7",
        "elapsed_ms=7000",
        "distance_mm=12300",
        "quality_flags=MissingField",
        "event=MetricCorrected target_sample_sequence=42",
        "telemetry sequence=42",
        "elapsed_ms=123456",
        "distance_mm=500000",
        "quality_flags=LateCorrection",
        "event=ConnectionStateChanged previous=Subscribing state=Ready reason=ReadinessConfirmed",
        "event=TelemetryStale last_sequence=7 age_ms=500",
        "event=TuiStopped samples=1 corrections=1 stale_events=1 faults=0",
        "duplicate_samples=0",
        "source_gap_samples=0",
        "time_regression_samples=0",
        "distance_regression_samples=0",
        "missing_field_samples=1",
        "last_sample_sequence=7",
    ):
        if expected_telemetry_field not in log_contents:
            print(
                f"diagnostic log is missing telemetry field {expected_telemetry_field!r}",
                file=sys.stderr,
            )
            return 1

    status_lines = [line for line in output.splitlines() if line.startswith("status:")]
    if len(status_lines) != 1:
        print(f"expected exactly one status line, got {len(status_lines)}", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    status = status_lines[0]
    for expected in (
        "last_transition_reason=None",
        "last_telemetry_age_ms=—",
        "event_queue=unavailable",
        "acquisition_queue=unavailable",
    ):
        if expected not in status:
            print(f"status line missing {expected!r}", file=sys.stderr)
            print(status, file=sys.stderr)
            return 1

    if re.search(r"state_age_ms=\d+", status):
        print("state age should be unavailable before any transition", file=sys.stderr)
        print(status, file=sys.stderr)
        return 1

    correction_lines = [
        line
        for line in output.splitlines()
        if line.startswith("CORRECTED METRIC SAMPLE ")
    ]
    if len(correction_lines) != 1:
        print(f"expected one explicitly marked correction, got {len(correction_lines)}", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    correction = correction_lines[0]
    for expected in (
        "target_sample_sequence=42",
        "corrected_sample_sequence=42",
        "elapsed_ms=123456",
        "distance_mm=500000",
        "heart_rate_bpm=—",
        "quality_flags=LateCorrection",
        "workout_state=Active",
        "rowing_state=Active",
        "stroke_state=Recovery",
        "last_telemetry_age_ms=—",
    ):
        if expected not in correction:
            print(f"correction line missing {expected!r}", file=sys.stderr)
            print(correction, file=sys.stderr)
            return 1

    metric_lines = [line for line in output.splitlines() if line.startswith("METRIC SAMPLE")]
    if len(metric_lines) != 1:
        print(f"expected one metric sample, got {len(metric_lines)}", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1
    metric = metric_lines[0]
    for expected in (
        "sequence=7",
        "speed_mm_per_s=—",
        "pace_ms_per_500m=—",
        "stroke_rate_deci_spm=—",
        "stroke_power_w=—",
        "average_power_w=—",
        "calories=—",
        "heart_rate_bpm=—",
        "drag_factor=—",
        "stroke_count=—",
        "quality_flags=MissingField",
        "workout_state=Active",
        "rowing_state=Active",
        "stroke_state=Drive",
    ):
        if expected not in metric:
            print(f"metric line missing {expected!r}", file=sys.stderr)
            print(metric, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
