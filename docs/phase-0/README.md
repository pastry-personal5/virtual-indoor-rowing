# Delivery Phase 0: toolchain and PM5 diagnostic foundation

Status: Milestones 1–3 complete; delivery Phase 0 exit gate (docs/architecture/10-delivery-plan.md) not yet evaluated
Owner: A0 — CTO / Principal Architect  
Last reviewed: 2026-09-17

## Purpose

This packet contributes to delivery Phase 0 by establishing the first executable repository foundation and proving the smallest trustworthy path from a Concept2 Model D fitted with a PM5 to normalized rowing telemetry on the reference Apple-silicon Mac.

The proof of concept is a standalone native terminal UI. It shares the engine-independent C++ and Objective-C++ boundaries intended for the later Unreal plug-in, but it is not a product UI and does not begin gameplay implementation.

## Relationship to the product delivery plan

This packet contains multiple bounded diagnostic milestones within the evidence work described as delivery Phase 0 in [the phased delivery plan](../architecture/10-delivery-plan.md). Each milestone belongs only to Phase 0. Their completion does not complete the full delivery Phase 0 exit gate and does not begin the delivery Phase 1 walking skeleton.

Completing these diagnostic milestones proves only:

- reproducible local foundations and CI-ready definitions for the native diagnostic path;
- basic PM5 discovery, selection, connection, identity, subscription, and reconnection;
- validated conversion of published PM5 fields into a hardware-neutral telemetry contract;
- live, ephemeral presentation in a diagnostic terminal UI;
- simulator and real-hardware evidence for the implemented path.

It does not satisfy the delivery Phase 0 or Phase 1 exit gates by itself.

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
| Capability profile | Complete for the diagnostic milestones; follow-on HIL evidence deferred | The exact `.069` tuple reaches `Ready` under development profile version 6. The observed 15-byte optional `0x0036` layout and the published 18-byte layout are both admitted. PM-display comparison and expanded HIL cases remain follow-on work. |
| Toolchain baseline | Native, Unreal smoke, and Shipping toolchain lanes pass | `make doctor`, configure/build/test, format check, `make unreal-smoke`, `make unreal-shipping`, and `make unreal-package-verify` pass on the pinned host and in self-hosted CI. Sandboxed invocations can still be denied access to UBT's user-state files and are not smoke evidence. The Bluetooth TCC permission-scenario matrix is deferred to Phase 4 per [ADR-0009](../adr/0009-defer-bluetooth-tcc-scenario-matrix-to-phase-4.md). |
| Product features | Explicitly deferred | The TUI is a diagnostic tool, not a substitute for the delivery Phase 1 walking skeleton. |

This bounded implementation is complete against its diagnostic milestone gates, not the delivery Phase 0 exit gate. Formal product readiness, published CI, and the deferred hardware cases remain follow-on work; they are not worked around by widening support or weakening requirements.

## In scope

- Repository and ownership boundaries.
- Public device and telemetry interfaces.
- Pinned toolchain manifest and diagnostic commands.
- Empty Unreal toolchain smoke host, without product behavior.
- Unsigned Unreal arm64 Shipping build/cook/package verifier and an explicit, redacted Bluetooth-permission diagnostic.
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
- Gameplay, workout, PM5 discovery/metrics, product persistence, and product UI in the Shipping verifier.
- Sparkle update, corrupt-feed, and rollback validation; these remain a separate packaging/update spike.
- Product persistence or upload of raw BLE captures, normalized telemetry, or
  diagnostic JSONL. The local, Git-ignored HIL probe evidence is a diagnostic
  tooling exception governed by the PM5 diagnostics/privacy rules.

## Deliverables and reading order

1. [Architecture and ownership](01-architecture-and-ownership.md)
2. [Public interfaces](02-public-interfaces.md)
3. [Milestone 1 implementation plan](03-milestone-1-implementation-plan.md)
4. [Evidence report](04-evidence-report.md)
5. [Milestone 2 remembered-PM5 and TUI implementation](05-milestone-2-tui-relaunch-reconnect.md)
6. [Milestone 3 Unreal Shipping toolchain spike](06-milestone-3-toolchain-shipping.md)
7. [Delivery Phase 0 changelog](CHANGELOG.md)

## Diagnostic Milestone 2 follow-on

Diagnostic Milestone 2 hardens remembered-PM5 relaunch reconnect, adds a user-driven forget
flow, and places the selectable PM5 list in a tall right-side TUI pane. It was
completed on 2026-09-14 as a bounded diagnostic milestone after the owner
accepted the six user-reported Milestone 2 HIL passes and the available local
artifacts. This does not represent delivery Phase 2, the solo-alpha phase, or
product/release acceptance. Evidence limitations and the separately skipped
physical multiple-candidate test remain follow-on items before support expansion.

## Completion rule

Each milestone is complete only when its required automated checks pass and its required real Model D/PM5 hardware run meets the applicable exit criteria. Documentation, simulator success, or a successful BLE connection alone is not sufficient. Completing these milestones records Phase 0 progress but does not pass the delivery Phase 0 exit gate.

Any public-interface change, dependency reversal, or relaxation of the accepted toolchain/PM5 decisions requires A0 review. Merge readiness is reviewed by A8.
