# Milestone 1 Phase 1: toolchain and PM5 diagnostic foundation

Status: Complete — bounded diagnostic foundation
Owner: A0 — CTO / Principal Architect  
Last reviewed: 2026-09-14

## Purpose

Milestone 1 Phase 1 establishes the first executable repository foundation and proves the smallest trustworthy path from a Concept2 Model D fitted with a PM5 to normalized rowing telemetry on the reference Apple-silicon Mac.

The proof of concept is a standalone native terminal UI. It shares the engine-independent C++ and Objective-C++ boundaries intended for the later Unreal plug-in, but it is not a product UI and does not begin gameplay implementation.

## Relationship to the product delivery plan

This milestone is a bounded foundation spike within the evidence work described as Phase 0 in [the phased delivery plan](../architecture/10-delivery-plan.md). It is not the complete Phase 1 walking skeleton described there.

Completing this milestone proves only:

- reproducible local foundations and CI-ready definitions for the native diagnostic path;
- basic PM5 discovery, selection, connection, identity, subscription, and reconnection;
- validated conversion of published PM5 fields into a hardware-neutral telemetry contract;
- live, ephemeral presentation in a diagnostic terminal UI;
- simulator and real-hardware evidence for the implemented path.

It does not satisfy the product Phase 0 or Phase 1 exit gates by itself.

## Supported reference setup

- Computer: MacBook Pro with Apple M5 Max and 128 GB unified memory.
- Operating system: macOS Tahoe 26.6.2 or later.
- Rower: Concept2 Model D with a PM5 configured as an indoor rower.
- Connection: Bluetooth Low Energy through CoreBluetooth.
- Product engine baseline: stock Unreal Engine 5.8 at an evidence-approved patch.
- Build baseline: Xcode 26.1.1 as required by ADR-0001.

The diagnostic executable may gather identity evidence from a PM5, but a device does not become supported merely because it advertises the expected service or accepts a connection. Readiness requires an exact evidence-approved hardware, firmware, machine-type, characteristic, and packet-layout profile.

## Readiness and decisions

| Item | Status | Rule |
|---|---|---|
| Product platform and engine | Resolved | Apple-silicon macOS, stock Unreal 5.8, and the ADR-0001 toolchain baseline apply. |
| Launch device boundary | Resolved | Model D with PM5 over BLE; the public contract remains hardware-neutral. |
| Capability profile | Complete for M1 Phase 1; follow-on HIL evidence deferred | The exact `.069` tuple reaches `Ready` under development profile version 6. The observed 15-byte optional `0x0036` layout and the published 18-byte layout are both admitted. PM-display comparison and expanded HIL cases remain follow-on work. |
| Toolchain baseline | Native and Unreal smoke lanes pass | `make doctor`, configure/build/test, format check, and `make unreal-smoke` pass on the pinned host. Sandboxed invocations can still be denied access to UBT's user-state files and are not smoke evidence. |
| Product features | Explicitly deferred | The TUI is a diagnostic tool, not a substitute for the Phase 1 walking skeleton. |

This bounded implementation is complete against the revised M1 Phase 1 exit
gate. Formal product readiness, published CI, and the deferred hardware cases
remain follow-on work; they are not worked around by widening support or
weakening requirements.

## In scope

- Repository and ownership boundaries.
- Public device and telemetry interfaces.
- Pinned toolchain manifest and diagnostic commands.
- Empty Unreal toolchain smoke host, without product behavior.
- Engine-independent C++ types and tests.
- Concept2 published-protocol codecs required for basic telemetry.
- CoreBluetooth discovery and session lifecycle.
- Deterministic mock/replay behavior at the public device boundary.
- Standalone TUI showing live normalized telemetry.
- Redacted aggregate toolchain and hardware evidence, plus explicit,
  owner-only, bounded raw rowing-telemetry capture in the local HIL probe.

## Out of scope

- Unreal gameplay, maps, rendering, UMG/CommonUI, animation, and route movement.
- Workout plans, PM programming, or CSAFE control commands.
- Session creation, SQLite journaling, history, export, or recovery.
- Cloud services, accounts, networking, Protobuf cloud contracts, or integrations.
- Race logic, smoothing, prediction, virtual distance, or scoring.
- USB support, PM3/PM4 support, and non-Concept2 machines.
- Developer ID release signing, notarization, Sparkle, and distribution packaging.
- Product persistence or upload of raw BLE captures, normalized telemetry, or
  diagnostic JSONL. The local, Git-ignored HIL probe evidence is a diagnostic
  tooling exception governed by the PM5 diagnostics/privacy rules.

## Deliverables and reading order

1. [Architecture and ownership](01-architecture-and-ownership.md)
2. [Public interfaces](02-public-interfaces.md)
3. [Implementation plan](03-phase-1-implementation-plan.md)
4. [Evidence report](04-evidence-report.md)
5. [Phase 2 TUI relaunch reconnect plan](05-phase-2-tui-relaunch-reconnect.md)
6. [Milestone changelog](CHANGELOG.md)

## Completion rule

The milestone is complete only when all required automated checks pass and the real Model D/PM5 hardware run meets the exit criteria in the implementation plan. Documentation, simulator success, or a successful BLE connection alone is not sufficient.

Any public-interface change, dependency reversal, or relaxation of the accepted toolchain/PM5 decisions requires A0 review. Merge readiness is reviewed by A8.
