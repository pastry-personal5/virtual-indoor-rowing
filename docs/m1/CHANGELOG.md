# Milestone 1 changelog

This file records accepted delivery-history changes for Milestone 1. Follow the [milestone changelog rules](../README.md#milestone-changelogs).

## Unreleased

### Changed

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
