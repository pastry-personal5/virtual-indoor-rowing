# Phase 1 Milestone 1: real RowingCore session domain + Protobuf contract seed

Status: Implemented (2026-09-17) — `make build`/`make test`/`make format-check`/`make doctor` all pass; no hardware evidence required for this milestone
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-17

## Relationship to the delivery plan

This is the first milestone scoped under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It covers build-order step 2 ("`RowingCore` types/units/states and Protobuf contract generation") only. Build-order step 1 (repo rules, pinned manifest, CI, stock engine, module boundaries) is treated as already substantially satisfied by Phase 0; this milestone adds the one missing piece of it (pinning the Protobuf toolchain) rather than re-litigating the rest.

## Owner-confirmed scoping decisions (2026-09-17)

- The Protobuf toolchain is pinned as part of this milestone, not deferred: `protoc`/`libprotobuf` join `Config/BuildVersions.json` and `make doctor` the same way `cmake`/`ninja`/`git_lfs` are pinned (exact observed version, not a range). `protobuf 36.1` (`abseil 20260817.0` dependency) was installed via `brew install protobuf` on this host and is the version to record.
- This milestone includes a session-lifecycle state machine, not just types/units — scoped narrowly to session continuity (the "Local workout continuity" truth), not device connection state (already owned by `RowingDevice`/`ERowingConnectionState`) and not workout *content* (plans/routes — Phase 2).
- New session-domain types are added **alongside** the existing Phase 0 `RowingTelemetry.h`/`RowingMachineTypes.h` types, not in place of them. `FRowingMetricSample` already matches the canonical units in [data model and protocols](../architecture/06-data-and-protocols.md) and is not diagnostic-only itself (only `FRowingDiagnosticSample`/`DiagnosticSampleObserved` carry that label) — but per that same header's own compatibility rule, "these in-process C++ types must not be copied directly into future Protobuf or database schemas without separate review." This milestone is that separate review: it defines new session types and a Protobuf contract without modifying `RowingTelemetry.h`/`RowingMachineTypes.h`.

## In scope

1. **`RowingCore` session domain** (`Source/RowingCore/Public/RowingCore/RowingSession.h`, header-only — the state machine has no internals worth hiding behind a `Private/` seam), engine-independent, no Protobuf/BLE/DB dependency:
   - `FRowingSessionId` — opaque 128-bit UUIDv7 per the canonical-identifiers rule ("Client-created session IDs may be UUIDv7 generated locally"); exposes raw 16 bytes (for the wire) and a canonical lowercase-hex string (for logs/JSON), never a parseable internal structure.
   - `ERowingSessionState`: `Created`, `Active`, `ConnectionLost`, `Ended`. (Deliberately not reusing `ERowingConnectionState` — that enum is BLE-adapter connection state owned by `RowingDevice`; this one is session-continuity state owned by `RowingCore`, matching the local journal's `SessionStateChanged`/`ConnectionLost`/`ConnectionRestored` events in the architecture spec.)
   - `ERowingSessionDisposition`: `Completed`, `Interrupted`, `Aborted` — reuses the exact vocabulary from the `Session` record in [data model and protocols](../architecture/06-data-and-protocols.md#session).
   - `ERowingSessionStateReason`: `None`, `SessionStarted`, `DeviceReady`, `LinkLost`, `LinkRestored`, `UserCompleted`, `UserAborted`, `RecoveredAfterUncleanExit` (this last one lines up with `LocalData`'s existing `FLocalDataRecoveryReport::RecoveredAfterUncleanExit`, so a recovered journal can drive a session straight to `Ended{Interrupted}` with a reason that says why).
   - `FRowingSessionStateChanged { PreviousState, NewState, Reason, std::optional<ERowingSessionDisposition> Disposition }` — same shape family as the existing `FRowingConnectionStateChanged`.
   - `FRowingSessionStateMachine` — a small pure state machine (no I/O) with `TryTransition(ERowingSessionStateReason) -> std::optional<FRowingSessionStateChanged>`, enforcing only these edges:
     - `Created -> Active` (`SessionStarted`)
     - `Created -> Ended{Aborted}` (`UserAborted`)
     - `Active -> ConnectionLost` (`LinkLost`)
     - `ConnectionLost -> Active` (`LinkRestored`)
     - `Active -> Ended{Completed}` (`UserCompleted`)
     - `Active -> Ended{Aborted}` (`UserAborted`)
     - `ConnectionLost -> Ended{Interrupted}` (`RecoveredAfterUncleanExit` or `UserAborted` while disconnected)
     - `Ended` is terminal — every transition out of it is rejected.
     Any other requested edge returns `std::nullopt`; it is not a fabricated success and not a thrown exception (matches the existing "commands report acceptance, not synthesized success" convention).
2. **Protobuf contract seed** (`Contracts/proto/rowing/v1/session.proto`), proto3, `package rowing.v1`, `lower_snake_case` fields, field numbers never reused once assigned:
   - `SessionState` enum, `SessionDisposition` enum, `SessionStateReason` enum — wire mirrors of the three `RowingCore` enums above, unknown values tolerated per design rule 8.
   - `SessionStateChangedEvent` message — wire mirror of `FRowingSessionStateChanged`, plus `session_id` (`bytes`, 16-byte UUIDv7) and `contract_version` (`uint32`).
   - No `MetricSample`/`MetricFrame` wire message yet — mapping `FRowingMetricSample` to the wire is explicitly deferred (see "Out of scope"), since that review is bigger than this milestone and nothing consumes it yet.
3. **Build wiring**:
   - `Config/BuildVersions.json` gains a `protobuf` entry (`protoc`/`libprotobuf` version) and an updated `observed_baseline`.
   - `Scripts/dev.py`'s `check_doctor()` gains an exact-version check for `protoc`, following the existing `cmake`/`ninja` pattern.
   - `CMakeLists.txt` gains `find_package(Protobuf CONFIG REQUIRED)`, a codegen step for `Contracts/proto/rowing/v1/session.proto`, and a new static library target `RowingContracts` (alias `rowing_contracts`) containing only the generated code. `RowingContracts` depends on the Protobuf runtime; **nothing depends on `RowingContracts` yet**, and `RowingCore` does not depend on it — keeping `RowingCore` itself free of the Protobuf/runtime dependency, consistent with its "must build and test without ... a network, or a database runtime" rule. (Protobuf is not literally one of those four excluded things, but keeping the domain module free of any serialization library dependency costs nothing here and avoids deciding that question under time pressure.)
   - `CLAUDE.md` repository-layout table: `Contracts/proto/` moves from `planned` to `exists` (scoped to this one `.proto` file, not the full contract set).
4. **Tests** (behavioral names, per the engineering rules):
   - `Source/RowingCore/Tests/RowingSessionStateMachineTests.cpp`: covers every edge above plus rejected edges, e.g. `rowing_session_starts_created`, `rowing_session_active_to_connection_lost_on_link_lost`, `rowing_session_connection_lost_recovers_to_active`, `rowing_session_ended_is_terminal`, `rowing_session_rejects_active_to_created`.
   - `Tests/Contract/RowingSessionContractTests.cpp`: a round-trip encode/decode test for `SessionStateChangedEvent` (proves the Protobuf toolchain/codegen pipeline works end-to-end) plus an unknown-field-tolerance test (design rule 8) — not a full domain-to-wire mapper, since no mapper is in scope yet.
5. **Docs**: this checkpoint doc; `docs/phase-1/README.md` updated from "no milestone scoped or started yet"; `docs/phase-1/CHANGELOG.md` entry.

## Out of scope (explicitly, to keep this milestone bounded)

- Mapping `FRowingMetricSample`/`FRowingStrokeMetrics` to any Protobuf wire message — that is a separate review, likely build-order step 3 or 4's checkpoint, once `Concept2PM`/`LocalData` are the actual consumers.
- `WorkoutPlan`, `RouteDefinition`, `RaceResultRevision`, the `RealtimeEnvelope` — all future-phase content per the delivery plan.
- The full `LocalData` `sessions`/`sync_outbox`/`cloud_links` schema — a later Phase 1 milestone, once it can consume `FRowingSessionId`/state from this one.
- Any consumer wiring (`Concept2PM`, `LocalData`, Unreal, control API) — this milestone only adds the domain module and contract seed; nothing yet calls into them.
- Unreal module changes of any kind.

## Owner action required (outside this milestone's code)

Done. The self-hosted CI runner (`vir-m1`, `.github/workflows/native.yml`) had `protobuf 36.1` installed before this milestone merged: run [35233522666](https://github.com/pastry-personal5/virtual-indoor-rowing/actions/runs/35233522666) (2026-09-17T14:26Z, commit `4502285`) shows `make doctor` passing with `OK protoc: required 36.1; observed libprotoc 36.1`, and every subsequent `main` push has stayed green.

## Compatibility

Both the C++ session types and the `.proto` contract are new — no existing consumer to break. Once this lands, changes to `ERowingSessionState`/`ERowingSessionDisposition`/the state machine's valid edges, or to `session.proto`'s field numbers/types, need the same A0 contract-change review as `RowingDevice`'s existing public types.

## Verification plan for this milestone

- `make build` and `make test` (new `rowing_core_tests` cases plus a new contract-test target).
- `make format-check`.
- `python3 Scripts/check_public_header_dependencies.py` (already run by `make test` per `CMakeLists.txt`) — confirms no Protobuf/platform leakage into `RowingCore`'s public headers.
- `make doctor` — confirms the new `protoc` pin is checked.
- `git diff --check` / `git status --short` before handoff.
- No hardware evidence required — this milestone is pure domain/contract code.
