# Delivery Phase 1 changelog

This document records changes to the milestones that contribute to delivery Phase 1. It does not assert completion of the delivery Phase 1 exit gate.

## Unreleased

### Added

- Opened delivery Phase 1 packet (`docs/phase-1/README.md`) per [ADR-0010](../adr/0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md), following delivery Phase 0's revised exit gate. No milestone scoped or started yet.
- Phase 1 Milestone 1 (build-order step 2): a real, non-diagnostic `RowingCore` session-continuity domain (`FRowingSessionId` UUIDv7, `ERowingSessionState`/`ERowingSessionDisposition`/`ERowingSessionStateReason`, `FRowingSessionStateMachine`) and its Protobuf contract seed (`Contracts/proto/rowing/v1/session.proto`, `SessionStateChangedEvent`). New behavioral tests: `rowing_session_state_machine_tests`, `rowing_session_contract_tests`. See [01-milestone-1-rowingcore-domain-contract.md](01-milestone-1-rowingcore-domain-contract.md).
- Pinned the Protobuf toolchain (`protobuf 36.1`) in `Config/BuildVersions.json` and `make doctor`, matching the existing `cmake`/`ninja`/`git_lfs` exact-version pattern. `protobuf 36.1` was installed via `brew install protobuf`, which also pulled in `abseil 20260817.0` as a transitive dependency; abseil itself is not independently pinned or checked by `make doctor`. New CMake target `RowingContracts` generates and builds the Protobuf contract code; nothing depends on it yet.

### Changed

- `CLAUDE.md` repository-layout table: `Contracts/proto/` moves from `planned` to `exists` (scoped to the one `rowing/v1/session.proto` file added this milestone).
- Reduced the Phase 1 exit gate's hardware-run duration from 60 minutes to 6 minutes, an explicit owner-directed scope reduction (2026-09-17), consistent with the same reduction already applied to the Phase 0 Milestone 4 Spike C visual-performance proxy run. Full 60-minute latency/durability evidence remains open as later follow-on. Updated in [the delivery plan](../architecture/10-delivery-plan.md) and this packet's README.
