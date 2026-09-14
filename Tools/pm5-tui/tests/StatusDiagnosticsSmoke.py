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
                "__test_discovered_candidate",
                "__test_identity",
                "__test_metric_correction",
                "__test_metric_sample",
                "__test_diagnostic_metric_sample",
                "__test_stroke_metrics",
                "__test_connection_state",
                "__test_connection_restored",
                "__test_telemetry_stale",
                "__test_fault",
                "status",
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
    metrics_text = json.dumps(metrics_records)
    if "private-candidate-name-must-not-be-captured" in metrics_text:
        print("candidate display label leaked into the metrics file", file=sys.stderr)
        return 1
    if not metrics_paths[0].name.startswith("pm5-tui-"):
        print("metrics file name does not use the pm5-tui run prefix", file=sys.stderr)
        return 1
    if [record["event"] for record in metrics_records] != [
        "run_started",
        "candidate_discovered",
        "machine_info_observed",
        "metric_corrected",
        "metric_sampled",
        "diagnostic_sampled",
        "stroke_metrics",
        "connection_state_changed",
        "connection_restored",
        "telemetry_stale",
        "fault_observed",
        "run_stopped",
    ]:
        print(f"unexpected metrics event sequence: {metrics_records}", file=sys.stderr)
        return 1
    started, candidate_record, identity_record, correction_record, sample_record, diagnostic_sample, stroke_record, state_record, restored_record, stale_record, fault_record, stopped = metrics_records
    if started.get("schema_version") != 4 or not started.get("observed_at_utc"):
        print("metrics run-start record is missing schema/timestamp", file=sys.stderr)
        return 1
    if started.get("hardware_probe", {}).get("raw_telemetry_capture") is not False:
        print("ordinary TUI unexpectedly enabled raw telemetry capture", file=sys.stderr)
        return 1
    if (
        identity_record.get("hardware_version") != "test-hardware"
        or identity_record.get("support_state") != "Warn"
        or identity_record.get("supported_metrics")
        != ["stroke_power", "peak_drive_force_deci_lb"]
        or identity_record.get("supported_metric_flags") != 262176
    ):
        print(f"metrics identity record is incomplete: {identity_record}", file=sys.stderr)
        return 1
    if candidate_record.get("signal_strength_dbm") != -54 or candidate_record.get("kind_hint") != "IndoorRower":
        print(f"redacted candidate record is incomplete: {candidate_record}", file=sys.stderr)
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
            or (record.get("event") == "metric_sampled" and record.get("diagnostic_only") is not False)
        ):
            print(f"metrics record is missing normalized telemetry: {record}", file=sys.stderr)
            return 1
    if correction_record.get("target_sample_sequence") != 42:
        print("metrics correction record is missing its target sequence", file=sys.stderr)
        return 1
    if (
        diagnostic_sample.get("sequence") != 99
        or diagnostic_sample.get("diagnostic_only") is not True
        or diagnostic_sample.get("distance_mm") != 99000
    ):
        print(f"diagnostic-only telemetry was not captured: {diagnostic_sample}", file=sys.stderr)
        return 1
    if (
        stroke_record.get("source_elapsed_ms") != 123450
        or stroke_record.get("stroke_count") != 1234
        or stroke_record.get("peak_drive_force_deci_lb") != 250
        or stroke_record.get("work_per_stroke_deci_joules") != 10000
        or not stroke_record.get("monotonic_timestamp_ns")
    ):
        print(f"per-stroke metrics were not timestamped/captured: {stroke_record}", file=sys.stderr)
        return 1
    if (
        state_record.get("previous_state") != "Subscribing"
        or state_record.get("state") != "Ready"
        or state_record.get("reason") != "ReadinessConfirmed"
    ):
        print(f"connection transition record is incomplete: {state_record}", file=sys.stderr)
        return 1
    if restored_record.get("gap_duration_ms") != 2750 or stale_record.get("age_ms") != 500:
        print("metrics reconnect/stale event records are incomplete", file=sys.stderr)
        return 1
    if (
        fault_record.get("code") != "Disconnected"
        or fault_record.get("operation") != "Reconnect"
        or fault_record.get("expected_value") != 18
        or fault_record.get("actual_value") != 15
        or "redacted diagnostic detail" in json.dumps(fault_record)
    ):
        print(f"metrics fault record is incomplete or unredacted: {fault_record}", file=sys.stderr)
        return 1
    if not stopped.get("observed_at_utc"):
        print("metrics run-stop record is missing its timestamp", file=sys.stderr)
        return 1
    if not isinstance(stopped.get("duration_ms"), int) or stopped.get("final_connection_state") != "Ready":
        print(f"metrics run-stop record lacks duration/final state: {stopped}", file=sys.stderr)
        return 1
    expected_summary = {
        "sample_count": 2,
        "diagnostic_sample_count": 1,
        "stroke_metrics_record_count": 1,
        "correction_count": 1,
        "stale_event_count": 1,
        "fault_count": 1,
        "duplicate_sample_count": 0,
        "source_gap_sample_count": 0,
        "time_regression_sample_count": 0,
        "distance_regression_sample_count": 0,
        "missing_field_sample_count": 2,
        "last_sample_sequence": 99,
        "reconnect_count": 1,
        "total_reconnect_gap_ms": 2750,
        "longest_reconnect_gap_ms": 2750,
        "acquisition_queue": None,
        "event_queue": None,
        "pm5_diagnostics": None,
    }
    if any(stopped.get(field) != expected for field, expected in expected_summary.items()):
        print(f"metrics run-stop record has an invalid aggregate: {stopped}", file=sys.stderr)
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
        "event=ConnectionRestored gap_ms=2750",
        "event=DiagnosticMetricSampled telemetry sequence=99",
        "event=StrokeMetricsObserved elapsed_ms=123450 stroke_count=1234",
        "event=FaultObserved code=Disconnected",
        "event=TuiStopped samples=2 corrections=1 stale_events=1 faults=1",
        "duplicate_samples=0",
        "source_gap_samples=0",
        "time_regression_samples=0",
        "distance_regression_samples=0",
        "missing_field_samples=2",
        "last_sample_sequence=99",
        "stroke_metrics_records=1",
    ):
        if expected_telemetry_field not in log_contents:
            print(
                f"diagnostic log is missing telemetry field {expected_telemetry_field!r}",
                file=sys.stderr,
            )
            return 1

    status_lines = [line for line in output.splitlines() if line.startswith("status:")]
    if len(status_lines) != 2:
        print(f"expected exactly two status lines, got {len(status_lines)}", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    initial_status, live_status = status_lines
    for expected in (
        "tone=Neutral",
        'headline="DISCONNECTED — PRESS SCAN TO FIND A PM5"',
        "connection_state=Idle",
        "last_transition_reason=None",
        "last_telemetry_age_ms=—",
        "event_queue=unavailable",
        "acquisition_queue=unavailable",
    ):
        if expected not in initial_status:
            print(f"status line missing {expected!r}", file=sys.stderr)
            print(initial_status, file=sys.stderr)
            return 1

    if re.search(r"state_age_ms=\d+", initial_status):
        print("state age should be unavailable before any transition", file=sys.stderr)
        print(initial_status, file=sys.stderr)
        return 1

    for expected in (
        "tone=Healthy",
        'headline="LIVE — PM5 READY"',
        "connection_state=Ready",
        "last_transition_reason=ReadinessConfirmed",
        "discovery candidates=0",
    ):
        if expected not in live_status:
            print(f"live status line missing {expected!r}", file=sys.stderr)
            print(live_status, file=sys.stderr)
            return 1
    if not re.search(r"last_telemetry_age_ms=\d+", live_status):
        print("live status line is missing telemetry age", file=sys.stderr)
        print(live_status, file=sys.stderr)
        return 1

    if "hardware_probe=disabled" not in initial_status or "hardware_probe=disabled" not in live_status:
        print("ordinary TUI status does not identify probe mode as disabled", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="pm5-hardware-probe-status-smoke-") as probe_temp_dir:
        probe_result = subprocess.run(
            [sys.argv[1], "--hardware-probe", "--script", "status", "quit"],
            check=False,
            capture_output=True,
            text=True,
            cwd=probe_temp_dir,
        )
        probe_metrics_paths = list((Path(probe_temp_dir) / "Metrics" / "pm5-tui").glob("*.jsonl"))
        probe_log_path = Path(probe_temp_dir) / "Logs" / "pm5-tui" / "pm5-tui.log"
        probe_log = probe_log_path.read_text(encoding="utf-8") if probe_log_path.exists() else ""
        probe_records = (
            [json.loads(line) for line in probe_metrics_paths[0].read_text(encoding="utf-8").splitlines()]
            if len(probe_metrics_paths) == 1
            else []
        )
    if probe_result.returncode != 0 or len(probe_records) != 2:
        print("hardware-probe scripted launch failed", file=sys.stderr)
        print(probe_result.stdout + probe_result.stderr, file=sys.stderr)
        return 1
    if probe_records[0].get("hardware_probe", {}).get("raw_telemetry_capture") is not True:
        print("hardware-probe metrics do not declare raw capture", file=sys.stderr)
        return 1
    if "hardware_probe=enabled" not in probe_result.stdout:
        print("hardware-probe status is not visibly marked", file=sys.stderr)
        return 1
    if "event=HardwareProbeStarted raw_telemetry_capture=true owner_only=true" not in probe_log:
        print("hardware-probe activation was not logged", file=sys.stderr)
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
