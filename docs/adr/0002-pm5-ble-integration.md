# ADR-0002: PM5 BLE integration through an in-process native plug-in

- Status: Accepted
- Date: 2026-09-12
- Owners: Device lead, client lead

## Context

Concept2 Model D describes the mechanical rower, but software communicates with its Performance Monitor. Older Model D units may not have a PM5. Concept2 publishes a current PM CSAFE/Bluetooth specification and says its old macOS SDK is outdated. macOS provides CoreBluetooth.

The integration must remain responsive during rendering load, preserve workout data, support protocol/firmware variation, and package cleanly under Hardened Runtime.

## Decision

- Launch support requires a PM5 configured as an indoor rower and accepted by a firmware/hardware capability matrix.
- Use BLE GATT through `CBCentralManager` in an Unreal C++ plug-in with a narrow Objective-C++ adapter.
- Run CoreBluetooth on its own serial dispatch queue; copy/validate notifications and feed bounded queues to engine-independent codec/domain workers.
- Subscribe to documented Concept2 status/stroke/interval characteristics and request 100 ms general/additional status where supported.
- Use published CSAFE commands for capability-gated managed workouts; never mix public and full proprietary modes or use undocumented authentication/commands.
- Maintain local workout continuity in the append-only session journal.
- Keep `IRowingDeviceTransport` and typed device-domain interfaces so USB/other devices can be added without touching gameplay.
- Do not ship Concept2's outdated macOS SDK.
- Keep the adapter in the main application process at launch. Reconsider an XPC helper only with evidence that process isolation materially improves session outcomes.

## Consequences

- Pairing and permission are owned by one visible app and packaging stays simpler.
- Render/game-thread stalls do not block the CoreBluetooth queue, although a total process crash still ends acquisition; the journal bounds data loss.
- Firmware compatibility becomes an explicit product asset with a real-device lab burden.
- BLE has no strong hardware attestation. Ranked labels and controls must reflect that.
- PM3/PM4 and USB are deferred.

## Alternatives considered

- **Old Concept2 macOS SDK:** rejected as Concept2 calls it outdated and unsupported on latest operating systems.
- **Direct BLE logic in Blueprint/game thread:** rejected for safety, parsing, testability, and frame-coupling reasons.
- **Separate XPC/daemon from day one:** deferred because permission, lifecycle, IPC, code-signing, updater, and recovery complexity is not yet justified.
- **USB-first:** deferred because the stated PM5 supports BLE and USB adds cable/setup and native USB policy work.
- **Derive rowing from keyboard/animation:** rejected; PM measurement is a product invariant.

## Validation and revisit

Revisit after Phase 0 HIL data, when BLE failed-session/support rate exceeds the agreed threshold, if the Apple app lifecycle prevents reliable acquisition, or when older monitor demand funds USB. Any new transport must pass the same normalized fixtures/session invariants.

Evidence: [Concept2 PM5 research](../archive/research/2026-09-12-concept2-pm5.md).
