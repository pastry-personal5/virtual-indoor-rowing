# Milestone 1 Phase 2: diagnostic TUI relaunch reconnect

Status: Planned  
Owner: A1 — Device engineering  
Architecture review: A0  
Integration review: A8  
Last reviewed: 2026-09-13

## Purpose

Phase 2 extends the M1 diagnostic TUI with refinement and documentation of the automatic reconnection functionality that was implemented in Phase 1. The automatic reconnection feature allows the TUI to remember a user's last selected PM5 and attempt to reconnect to it on subsequent launches without requiring another scan.

This is diagnostic-device preference, not workout persistence, pairing policy, PM support approval, or product-level remembered-device behavior. It does not change the Phase 1 requirement that `Ready` is possible only after the selected device's identity, machine type, capability profile, characteristics, subscriptions, and first required notifications are revalidated.

## Scope and non-goals

In scope:

- Refine and document the automatic reconnection functionality implemented in Phase 1
- Ensure proper handling of edge cases for reconnection attempts
- Add deterministic simulator, adapter, and TUI tests for relaunch behavior
- Provide a user-driven `forget` command that deletes the remembered reference
- Maintain consistency with existing diagnostic behavior

Out of scope:

- Persisting a discovery result merely because it was observed.
- Persisting a PM serial number, raw BLE payload, telemetry, workout/session state, or account data.
- Background Bluetooth work, daemon/helper behavior, automatic scanning, automatic selection of a nearby candidate, or a reconnect retry loop across relaunch.
- Relaxing the capability allow-list, identity checks, notification requirements, stale policy, or same-device reconnect rules.
- Replacing the later product's SQLite preferences/session journal design.

## Decision and privacy boundary

Only an **explicitly selected** candidate becomes the remembered reconnect target. A scan by itself never writes a preference. This preserves the Phase 1 rule that a nearby PM5 is never selected implicitly.

`Concept2PMMac` owns persistence because the CoreBluetooth peripheral identifier remains adapter-private. The stored record contains only:

```text
schema_version
adapter-local opaque peripheral reference
saved_at UTC instant
```

It contains no display label, PM serial, raw CoreBluetooth identifier in diagnostics, capability tuple, telemetry, or source measurements. The record is local to the diagnostic app's preferences/Application Support domain, is not synced, exported, logged, or sent to a network service. It is non-secret device metadata, so Keychain is not required for this narrow PoC preference; filesystem/app access controls remain in effect.

The persistent representation and its storage API are private to `Concept2PMMac`. `FRowingMachineId`, `IRowingMachine`, and other public interfaces remain unchanged.

## Relaunch lifecycle

```text
TUI launch
  -> load one private remembered reference
  -> none: remain Idle; user may scan
  -> present: retrieve only that CoreBluetooth peripheral by identifier
      -> unavailable: emit a redacted recoverable reconnect fault; remain Idle
      -> available: enter Reconnecting and connect only that peripheral
          -> re-read identity, machine kind, profile, properties, subscriptions, and required statuses
              -> all valid: Ready, then emit ConnectionRestored with relaunch gap marked unknown
              -> any mismatch/failure: fail closed; do not emit MetricSampled or Ready
```

The relaunch gap is intentionally not converted to a duration or synthetic telemetry. A successful automatic connection is still subject to the ordinary stale/disconnect behavior after readiness. If it fails, the TUI does not broaden into a background scan; the user can run `scan` and explicitly select a candidate.

`forget` removes the reference immediately, cancels an in-flight automatic attempt where applicable, and leaves the TUI in `Idle`. It has no effect on macOS Bluetooth pairing state.

## TUI behavior

At launch, the TUI displays one of:

- `No remembered PM5; use scan to select one.`
- `Reconnecting to remembered PM5…`
- `Remembered PM5 unavailable; use scan or forget.`
- `Remembered PM5 rejected: <redacted categorical reason>.`

The existing `scan`, `stop`, and `quit` commands remain. Phase 2 adds `forget` and a bounded status line identifying whether a local selection is remembered; it never prints the stored reference.

## Implementation plan

1. Refine the automatic reconnection implementation from Phase 1
2. Add comprehensive tests for the reconnection logic 
3. Implement the `forget` command functionality
4. Update TUI display logic to show reconnection status
5. Ensure proper error handling and graceful degradation

## Required verification

- Automatic reconnection works with a valid stored device
- Fallback behavior when stored device is unavailable works correctly
- Manual selection still works as expected
- `forget` command properly clears the stored reference
- TUI displays appropriate status messages during reconnection attempts
- No regression in existing diagnostic functionality

## Implementation plan

1. Add an adapter-private `IRememberedPeripheralStore` seam with a macOS preferences implementation and deterministic in-memory test implementation. Its only operations are load, save, and clear of the versioned opaque reference.
2. Save the reference only after TUI-originated explicit selection is accepted. Do not save from advertisements, passive retrieval, or a different reconnect candidate.
3. During discovery construction, load the reference and enqueue a non-blocking identifier-retrieval/reconnect attempt on the existing CoreBluetooth serial queue.
4. Reuse the existing selected-device connection state machine. Require complete identity/capability/notification revalidation before `Ready`; treat a new or changed identity as terminal for the automatic attempt and retain no new target implicitly.
5. Add `forget` handling and bounded redacted TUI output. The TUI invokes a private adapter command; it does not read or format the stored value.
6. Keep all preference I/O off the TUI and CoreBluetooth delegate callback paths. Shutdown cancels work before objects are destroyed.

## Required verification

- First launch with no preference remains `Idle` and does not scan/connect.
- Explicit selection saves exactly one opaque reference; discovery-only candidates save none.
- A second simulated launch retrieves and connects only the saved target.
- Missing remembered peripheral emits a categorical recoverable outcome and does not scan automatically.
- Changed identity, wrong machine kind, unsupported profile, invalid properties, or missing initial statuses never reaches `Ready` and never emits telemetry.
- A successful relaunch reconnect emits ordered reconnect/state events; it fabricates neither gap metrics nor duration.
- `forget` clears the record, cancels an active attempt, and a later launch has no reconnect target.
- Stored-preference schema version rejection fails closed and is recoverable by `forget`.
- TUI output and logs contain neither the opaque reference nor PM serial/telemetry.
- Manual HIL evidence covers successful relaunch, PM absent at relaunch, changed/wrong candidate, forget, and clean quit.

## Exit rule

Phase 2 is ready for integration review only when the native/simulator checks pass and manual hardware evidence confirms that the same reviewed PM5 reconnects after relaunch while all mismatch cases fail closed. It does not close the Phase 1 hardware, toolchain, or 6-minute soak gates.
