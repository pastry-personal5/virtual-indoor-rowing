# Real-PM5 six-minute run evidence — 2026-09-20

Status: Complete owner-run evidence; checkpoint-fix rerun passed and Phase 1 exited.

## Scope

The owner completed a six-minute Just Row run against real PM5 hardware in the
packaged application on 2026-09-20. This note records redacted, local-journal
facts observed after the run. It does not establish PM5 monitor/display metric
agreement, a journal checkpoint bound, reconnection behavior, or process-kill
recovery.

## Redacted observation

| Item | Observation |
|---|---|
| Local durable outcome | `Completed` |
| Session start | 2026-09-20T06:22:30Z |
| Finalization time | 2026-09-20T06:29:16Z |
| Journal lifecycle events | `Started`, `CapabilityObserved`, `Completed` |
| Durable sample representation | 360 AES-GCM-sealed sample chunks, sequence range 107–3340 |
| Summary | Revision 1; quality flags value `1`; sealed 57-byte metrics payload |
| PM5 display ↔ Unreal HUD distance comparison | Owner-run: `SUCCESS` / values agreed within display resolution |
| Journal PM5 sample ↔ finalized summary distance | `SUCCESS` — 1,048,200 mm vs 1,048,200 mm; delta `0 mm` |
| Raw identifiers and telemetry | Not copied into this note or committed |

The session's approximately 6 minutes 46 seconds from recorded start to
finalization includes application lifecycle/finalization time. The owner
reports the rowing portion as six minutes; this note does not infer an exact
PM elapsed duration from the sealed payload.

## Evidence disposition

This is positive evidence that a real-device Just Row session reached durable
local completion and retained sealed samples. The owner subsequently accepted
the distance and summary/sample readback observations. Later inspection found
decoded chunks spanning more than one second, so checkpoint cadence and FR-007
remained pending a rebuilt owner run at that point.

The owner also performed the real-PM5 monitor-to-Unreal-HUD distance comparison
and recorded it as `SUCCESS`; the displayed distance agreed within display
resolution. This is owner-run FR-003 evidence. The journal remains the durable
local source artifact; no raw PM serial, private journal, or telemetry capture
is copied into the repository.

Owner-local journal readback independently confirms that the final decoded PM5
sample distance and the finalized session-summary distance are both
`1,048,200 mm`, with no journal-to-summary distance delta. The stored sample
distances are monotonic for this session.

This note itself does not establish no loss/duplication against the PM5 monitor,
or exercise a process-kill or reconnect scenario; those are separately tracked
Phase 1 gate rows.

Separately, the owner marked the real-PM5 HUD values plus simulator fixtures
successful for FR-003 on 2026-09-20. That FR-003 decision is recorded in the
Phase 1 evidence report; it does not add any of the FR-007 observations absent
from this note.

The owner-local journal remains the source artifact at
`~/Library/Application Support/dev.virtualrowing.app/journal/workout-journal.sqlite3`.
It is deliberately excluded from version control because it contains private
workout data.

## Checkpoint-fix rerun

The owner repeated the six-minute real-PM5 run on 2026-09-20 after the batching
checkpoint fix. Redacted journal inspection found 447 chunks containing 2,962
contiguous decoded samples (sequence 1–2962). The maximum source-time chunk span
was 900 ms and the maximum receipt-time span was approximately 900.080 ms; no
chunk exceeded 1,000 ms. Stored distance was monotonic, and the final sample and
summary both reported `1,126,600 mm` and `371,180 ms`.

The session-linked latency aggregate reported 2,942 samples, zero dropped,
p50 8.113 ms, p95 15.707 ms, p99 16.629 ms, and max 22.521 ms. The app-reported
source revision was `4c8e507835ad-dirty`. Raw identifiers and telemetry remain
outside the repository. This rerun verifies the outstanding checkpoint evidence
and completes the six-minute durability/latency gate.
