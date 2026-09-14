# Milestone 1 changelog

This file records accepted delivery-history changes for Milestone 1. Follow the [milestone changelog rules](../README.md#milestone-changelogs).

## Unreleased

### Changed

- Split ordinary TUI diagnostics from an explicit `make hil-pm5` hardware-probe
  mode. Metrics schema v4 records bounded raw rowing-service packets only in the
  visibly active, owner-only probe, with characteristic/global sequence,
  monotonic receive time, connection state, parser outcome, approved lengths,
  exact payload length/hex, and explicit duration/packet/queue loss counters.
  This supplies the missing byte-level evidence for real 15-byte `0x0036`
  variants without weakening the reviewed capability profile or capturing PM
  identity payloads.
- Transferred a remembered-PM5 relaunch connection from discovery into the
  TUI's normal single-machine ownership path, so it is explicitly disconnected
  and its actual final state, queues, and PM5 diagnostics are retained at stop.
- `SupportedMetrics` now begins empty and expands only after structurally valid
  notifications prove each source in the current connection; optional profile
  declarations no longer claim unavailable hardware metrics.
- Tightened capability generation to reject packet lengths outside the exact
  implemented layouts, and added per-characteristic approved lengths plus the
  last parser-error length/timestamp to redacted run aggregates.
- Added an always-visible, high-contrast PM5 TUI status footer that distinguishes
  live, connecting, stale, reconnecting, unsupported, permission, failed, and
  diagnostic-only states while continuously showing telemetry age, transition
  reason, support state, sample/reconnect counts, quality, faults, and queue health.
- Revised the exact PM5 development capability profile to v4: keep the allowed
  tuple pinned to the revision 0.34 18-byte `0x0036` BLE layout, retain decoder
  test coverage for the older published 15-byte layout, and treat unapproved or
  malformed optional stroke notifications as warnings instead of terminating
  otherwise valid required status telemetry. Decoder diagnostics now retain the
  characteristic, observed packet length, and approved lengths without payloads.
- Expanded per-run PM5 metrics to schema v3 with timestamped connection,
  identity, RSSI-only discovery, stale, reconnect, and fault records; capture
  diagnostic-only telemetry and notification-enable outcomes separately, and
  retain categorized CoreBluetooth error codes without IDs or raw error text.
- Expanded the PM5 TUI telemetry panel to fill nearly the whole app viewport;
  Wave launches use a magnified block, and Apple Terminal is resized when possible.
- Per-run metrics now include adapter-private characteristic properties,
  notification counts/cadence histograms, parser-error categories, status-rate
  write outcomes, and successful reconnect gap durations. These measurements
  improve future HIL captures; the previous six-minute run is not backfilled.
- Added sparse, independently timestamped stroke metrics from PM5 stroke-data
  notifications: drive/recovery timing, drive/stroke distance, peak/average
  force, work per stroke, stroke power, calories/hour, and projected-work
  fields. The undefined-unit projected-work value remains raw. Profile v4
  declares `0x0035`/`0x0036` optional for the exact PM5 tuple and subscribes
  only when notify is available; hardware acceptance is still pending.
- Recorded the 2026-09-14 short reconnecting launch with zero telemetry as a
  no-data observation, not as a reconnect failure or hardware acceptance case.
- The 2026-09-13 user-confirmed six-minute real-rowing capture is now recorded
  as performed evidence, with its PM completion state and known measurement
  limits; it is not called an acceptance pass while required aggregates and
  hardware scenarios remain unverified.
- Timestamped per-run metrics now end with normalized quality counters and
  generic acquisition/event queue high-water and overflow summaries, making
  future HIL captures self-describing without adding PM protocol details to the
  public TUI contract.
- Expanded PM5 TUI diagnostic logging to record normalized telemetry and late
  corrections, connection transitions, stale events, and a redacted aggregate
  stop summary with quality, fault, and queue counters. The Phase 1 evidence
  report now records the corresponding short HIL observation and passing native
  automated checks without treating either as acceptance evidence.
- Added a user-authorized, owner-only `Metrics/pm5-tui/` JSONL capture: every
  diagnostic launch creates one timestamped, Git-ignored normalized-metrics file
  for local AI analysis, without raw packets or device identifiers.
- Added the initial exact PM5/634/8200-000372-178.067/IndoorRower development capability profile. Profile version 2 additionally declares the documented `0x0034` `read`/`write` control so the adapter can request 100 ms status only after CoreBluetooth verifies both properties; an earlier 1 Hz run produced false 500 ms stale faults.
- Switched the normal PM5 TUI launch path from the unverified diagnostic-only factory to the reviewed capability-profile factory, so `make hil-pm5` can exercise the approved tuple.
- Replaced the PM5 TUI's deferred stdin command handling with an FTXUI event loop that continuously drains discovery and telemetry events; selecting a candidate now stops scanning before connection.
- Reduced the M1 Phase 1 real-hardware live-row acceptance run from 60 minutes to 6 minutes; the same redacted aggregate telemetry, gap, queue, reconnect, and parser-error evidence remains required.
- Added a separate unverified diagnostic-only PM5 telemetry path for the exact locally observed tuple; it remains `Warn`, never reaches `Ready`, emits a distinct diagnostic sample event, and performs no PM5 writes. Physical telemetry validation remains pending.
- Added DEBUG-default rotating file logging for the PM5 TUI under `Logs/pm5-tui/`, with bounded backups and redacted event content.
- Corrected PM5 BLE discovery to filter on Concept2's advertised discovery UUID `0x0000`, rather than the post-connection rowing GATT service `0x0030`; clarified the distinction in the PM5 integration specification.
- Pinned the M1 native toolchain to Git LFS 3.8.0 after the scoped dependency-standardization decision; repository initialization remains an explicit preflight check.
- Approved and pinned stock Unreal Engine 5.8.2 (changelist 56702186) by its `Build.version` digest; the Unreal smoke result remains pending.
- Approved and pinned Xcode 26.1.1 build 17B100, macOS SDK 26.1, and Apple clang 17.0.0 (`clang-1700.4.4.1`).
- Corrected `make doctor` Xcode-version parsing and resolve `clang-format` through the selected Xcode toolchain.
- Pinned the published Concept2 PM CSAFE Communication Definition revision 0.34 digest for the first capability-profile review.

### Added

- Added a deterministic merger regression for repeated stopped-row status
  notifications; unchanged PM elapsed time and distance are retained and marked
  `Duplicate` rather than treated as new progress.
- Implemented M1 Phase 1's automatic reconnection functionality: store last explicitly selected PM5 identifier locally, attempt reconnect on TUI launch without requiring a new scan, and show clear status messages about reconnection attempts.
- Planned M1 Phase 2's narrow, adapter-private remembered-PM5 relaunch reconnect flow for refinement and documentation, including fail-closed identity revalidation and a user-driven forget control.
- Integrated FTXUI library as external dependency for enhanced terminal user interface
- Enhanced PM5 TUI with modern UI components while maintaining all existing functionality

## 2026-09-13

### Changed

- Clarified Phase 1 preflight, capability-profile semantics, liveness recovery, evidence provenance, and exit-decision rules.

### Added

- Established the Milestone 1 documentation baseline for the toolchain and PM5 diagnostic foundation.
