# Phase 1 evidence report

Status: Open — implementation milestones are recorded; the combined exit gate has not been evaluated.

This report is the Phase 1 evidence index. It distinguishes automated implementation evidence from owner-run product evidence and does not itself waive an exit-gate requirement.

## Evidence matrix

| Requirement / gate | Required evidence | Current status | Evidence owner or source |
|---|---|---|---|
| FR-002 — PM5 discovery, pairing, reconnect | Packaged app with a real supported Model D/PM5; redacted run note | Pending owner run | Milestone 7 owner checklist |
| FR-003 — live rowing HUD | Real PM5 HUD values plus simulator fixtures | Pending owner run | Milestones 5, 7 |
| FR-004 — offline Just Row and gray-box route | Packaged simulator route fixtures and a real-device row without account/network | Pending owner run | Milestone 8 + phase gate |
| FR-007 — durable local history | Six-minute row, journal checkpoint cadence, summary/sample readback | Pending owner run | Milestone 7 owner checklist |
| Six-minute durability/latency | Redacted aggregate latency, no unexplained meter loss/duplication, ≤1 s checkpoint observation | Pending owner run | Milestone 7 owner checklist |
| Process-kill recovery | Kill mid-row, relaunch, explicit interrupted recovery, intact journal | Pending owner run | Milestone 7 owner checklist |
| Development sync contracts/persistence | Go tests, PostgreSQL/MinIO implementation, forward migration and worker tests | Verified | Milestones 9–11 |
| Packaged unsigned build | `make unreal-shipping` and `make unreal-package-verify` on current tree | Pending owner run | Milestones 7–8 |
| Fresh-install/TCC matrix | Signed/notarized build scenarios | Deferred to Phase 4 | ADR-0009 |
| Managed-workout hardware acceptance | Distance, time, interval, reject/abort | Deferred to Phase 4 | ADR-0010 |
| Visual performance spike | Six-minute and 60-minute frame/thermal evidence | Deferred to Phase 4 | ADR-0010 |
| Realtime/race evidence | Gateway, room, replay, load, recovery | Deferred to Phase 4 | ADR-0010 |
| Design-partner setup cohort | Unassisted setup with categorized failures | Deferred to Phase 4 | ADR-0011 |

## Required environment record

The owner-run evidence must record the app build, Unreal version, Xcode/macOS tuple, PM5 firmware/hardware tuple, simulator fixture or run identifier, and command result. Raw PM serials, tokens, private journals, and raw telemetry are not committed.

## Exit decision

The Phase 1 decision remains open until every non-deferred row above has evidence and the owner records `Pass`, `Pass with named follow-up`, or `Does not pass` with rationale and next action.
