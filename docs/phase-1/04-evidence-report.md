# Phase 1 evidence report

Status: Complete — Pass (2026-09-20). All non-deferred Phase 1 exit-gate rows are verified; explicitly deferred evidence remains assigned to Phase 4 by ADRs 0009–0011.

This report is the Phase 1 evidence index. It distinguishes automated implementation evidence from owner-run product evidence and does not itself waive an exit-gate requirement.

## Evidence matrix

| Requirement / gate | Required evidence | Current status | Evidence owner or source |
|---|---|---|---|
| FR-002 — PM5 discovery, pairing, reconnect | Packaged app with a real supported Model D/PM5; redacted run note | Verified | Milestone 7 owner checklist; owner-run discovery, pairing, and reconnection passed on 2026-09-20 |
| FR-003 — live rowing HUD | Real PM5 HUD values plus simulator fixtures | Verified | Owner marked the real-PM5 HUD values and simulator fixtures successful on 2026-09-20. |
| FR-004 — offline Just Row and gray-box route | Packaged simulator route fixtures and a real-device row without account/network | Verified | Owner marked the packaged simulator route fixtures and real-device offline row successful on 2026-09-20. |
| FR-007 — durable local history | Six-minute row, journal checkpoint cadence, summary/sample readback | Verified | Checkpoint-fix rerun: 447 chunks and 2,962 contiguous samples; maximum decoded source-time span 900 ms; summary/sample distance delta 0 mm |
| Six-minute durability/latency | Redacted aggregate latency, no unexplained meter loss/duplication, ≤1 s checkpoint observation | Verified | Owner-run distance comparison plus checkpoint-fix rerun; session-linked latency row recorded with 2,942 samples and zero dropped |
| Process-kill recovery | Kill mid-row, relaunch, explicit interrupted recovery, intact journal | Verified | Owner-run real-PM5 force-kill and relaunch; latest journal contains `RecoveredAfterUncleanExit` and preserved sample chunks |
| Development sync contracts/persistence | Go tests, PostgreSQL/MinIO implementation, forward migration and worker tests | Verified | Milestones 9–11 |
| Packaged unsigned build | `make unreal-shipping` and `make unreal-package-verify` on current tree | Verified | Milestones 7–8; owner-run commands succeeded on 2026-09-20 at revision `c8d78d720daf` on arm64 macOS 26.6.2 with Xcode 26.1.1 and Unreal Engine 5.8.2 |
| Fresh-install/TCC matrix | Signed/notarized build scenarios | Deferred to Phase 4 | ADR-0009 |
| Managed-workout hardware acceptance | Distance, time, interval, reject/abort | Deferred to Phase 4 | ADR-0010 |
| Visual performance spike | Six-minute and 60-minute frame/thermal evidence | Deferred to Phase 4 | ADR-0010 |
| Realtime/race evidence | Gateway, room, replay, load, recovery | Deferred to Phase 4 | ADR-0010 |
| Design-partner setup cohort | Unassisted setup with categorized failures | Deferred to Phase 4 | ADR-0011 |

## Required environment record

The owner-run evidence must record the app build, Unreal version, Xcode/macOS tuple, PM5 firmware/hardware tuple, simulator fixture or run identifier, and command result. Raw PM serials, tokens, private journals, and raw telemetry are not committed.

## FR-003 decision

The owner has accepted the real-PM5 HUD values together with the simulator-fixture evidence as successful for FR-003. This verifies the live-HUD row and contributes to the completed combined Phase 1 exit gate.

## FR-004 decision

The owner has accepted the packaged simulator route fixtures together with the real-device row performed without an account or network as successful for FR-004. This verifies the offline Just Row and gray-box-route row and contributes to the completed combined Phase 1 exit gate.

## FR-007 decision

The initial six-minute real-device row successfully demonstrated distance
agreement and summary/sample readback but exposed decoded chunks spanning more
than one second. The owner then repeated the run on the checkpoint-fix build.
That rerun produced 447 chunks and 2,962 contiguous decoded samples, with a
maximum source-time chunk span of 900 ms and no chunk over 1,000 ms. The final
sample and finalized summary both report `1,126,600 mm`, with a `0 mm` delta.
FR-007 is verified.

## Process-kill recovery decision

The owner completed the real-PM5 force-kill and relaunch scenario. The
owner-local journal now contains a session whose lifecycle is `Started`, three
`CapabilityObserved` events, and `RecoveredAfterUncleanExit`; its `sessions`
row is `Ended` and its durable sample data remains present across 12 chunks,
covering 107 decoded samples (sequence range 52–158). This verifies the
process-kill recovery gate. The private journal remains outside version
control.

## Initial six-minute durability/latency observation

Redacted owner-run observation from the latest real-PM5 packaged-app session
(2026-09-20; private session identity omitted):

| Observation | Result |
|---|---|
| Journal lifecycle | `Started`, three `CapabilityObserved`, `Completed` |
| Journal lifecycle duration | Approximately 380.4 seconds |
| Durable sample chunks | 338 chunks; 3,015 decoded samples; sequence range 2–3025 |
| Final PM5 sample ↔ finalized summary distance | `1,016,500 mm` ↔ `1,016,500 mm`; delta `0 mm` |
| Stored-distance monotonicity | Passed |
| PM5 display ↔ Unreal HUD comparison | Owner reported successful within display resolution |
| HUD software latency | 2,926 samples; 0 dropped; p50 9.021 ms; p95 16.758 ms; p99 17.710 ms; max 23.104 ms |
| Checkpoint observation | Incomplete: maximum decoded chunk span approximately 1,140 ms; 112 chunks exceeded 1,000 ms |

This entry is redacted and excludes session identifiers, PM serials, raw
telemetry, and the private journal. The distance and latency portions are
positive evidence. Its failed checkpoint observation is retained as the defect
baseline superseded by the passing rerun below.

## Checkpoint-fix six-minute rerun

Redacted owner-run observation from the rebuilt real-PM5 packaged-app session
on 2026-09-20. The app reported source revision `4c8e507835ad-dirty`; private
session identity, PM serial, and raw telemetry remain omitted.

| Observation | Result |
|---|---|
| Journal lifecycle | `Started`, three `CapabilityObserved`, `Completed` |
| Journal lifecycle duration | Approximately 383.049 seconds |
| Durable sample chunks | 447 chunks; 2,962 decoded samples; sequence range 1–2962 |
| Sequence continuity | Passed; zero gaps or duplicates in stored sequence numbers |
| Checkpoint source-time span | Passed; maximum 900 ms; zero chunks exceeded 1,000 ms |
| Checkpoint receipt-time span | Passed; maximum approximately 900.080 ms; zero chunks exceeded 1,000 ms |
| Final PM5 sample ↔ finalized summary distance | `1,126,600 mm` ↔ `1,126,600 mm`; delta `0 mm` |
| Final PM5 sample ↔ finalized summary elapsed time | `371,180 ms` ↔ `371,180 ms`; delta `0 ms` |
| Stored-distance monotonicity | Passed; zero regressions |
| Finalized sample accounting | 2,962 accepted; 104 rejected; summary quality flags value `33` |
| Session-linked HUD software latency | 2,942 samples; 0 dropped; p50 8.113 ms; p95 15.707 ms; p99 16.629 ms; max 22.521 ms |

The rerun closes the checkpoint defect exposed by the initial run. Combined
with the previously accepted real-PM5 monitor-to-Unreal distance comparison,
summary/sample readback, and process-kill recovery evidence, it verifies the
six-minute durability/latency gate.

## Exit decision

**Pass.** Every non-deferred Phase 1 row is verified. The checkpoint-fix rerun
demonstrates the required ≤1-second observation, session-linked latency evidence
is present, and process-kill recovery is verified. Evidence explicitly deferred
by ADRs 0009–0011 remains a Phase 4 obligation and does not block this decision.
