# Architecture

## Purpose and baseline

Virtual Indoor Rowing (VIR) is an offline-capable macOS rowing application. The completed Phase 1 build supports an Apple-silicon Mac running macOS Tahoe 26.6.2 or later, Unreal Engine 5.8 at an approved patch, and a Concept2 Model D with a PM5 connected by BLE. Phase 2 is planned, not started. Exact toolchain and distribution decisions are in [ADR-0001](docs/adr/0001-platform-and-toolchain.md), [ADR-0006](docs/adr/0006-macos-distribution.md), and [ADR-0008](docs/adr/0008-defer-macos-signing-to-phase-4.md).

The product is useful without a network connection. Online history, social features, and ranked races add services but must never prevent a local workout from completing.

## System at a glance

```text
PM5 ──BLE──> Concept2PM adapter ──> rowing domain ──> Unreal UI / world
                                      │       │
                                      │       └──> SQLite journal ──> cloud sync
                                      └──> race client ──WSS──> authoritative race room
```

The client communicates with the control plane over HTTPS and race rooms over WSS. The control plane stores durable account, catalog, entitlement, and session-summary data. Race rooms own ranked-race validation, timing, and rankings. See [system architecture](docs/architecture/02-system-architecture.md) for the complete topology.

## Authorities and truth

| Truth | Authority | Rule |
|---|---|---|
| Physical rowing facts | PM5 | Never synthesize distance after a gap or reconnect. |
| Local workout continuity | Append-only SQLite journal | A network failure cannot discard a completed workout. |
| Ranked result | Authoritative race server | Client transforms and Unreal physics never determine rank. |
| On-screen motion | Client presentation | It may predict and correct; it is never official. |

## Client boundaries

- `RowingCore`: engine-independent C++ domain types and state machines.
- `RowingDevice`: transport-neutral device contracts and telemetry normalization.
- `Concept2PM`: Objective-C++/CoreBluetooth and PM protocol adapter. Apple and Concept2 types stop here.
- `WorkoutRuntime`, `CourseRuntime`, and `RaceClient`: workout orchestration, local route presentation, and online-race client logic.
- `LocalData` and `OnlineClient`: asynchronous persistence/outbox and control-plane access.
- `RowingUI` and `RowingWorld`: UMG/CommonUI, Unreal actors, rendering, audio, and content. They consume snapshots; they do not parse devices, persist sessions, or determine results.
- `Diagnostics`: redacted observability and consented support tooling.

Dependencies move from platform and framework adapters toward domain contracts, never the reverse. `UObject`, Actor, Slate, and UMG use is restricted to the game thread. Domain code must run and test without Unreal, CoreBluetooth, a network, or a database runtime.

## Device and telemetry boundary

Expose a hardware-neutral rowing-machine interface. The PM5 adapter owns discovery, identity, capabilities, subscriptions, decoding, control commands, and reconnect policy. It emits normalized, ordered facts with source time, sequence, provenance, and quality flags.

Only validated device facts can affect a local workout. A disconnect freezes official local input, records the gap, and attempts bounded reconnect. An identity change, reset, or incompatible reconnect starts a safe recovery path; it cannot create synthetic meters. PM5-specific details remain below `RowingDevice`. See [PM5 integration](docs/architecture/04-concept2-pm5-integration.md).

## Persistence and cloud

SQLite in WAL mode holds the local event journal, outbox, and non-secret preferences. Development and single-user journals are owner-only plaintext under [ADR-0012](docs/adr/0012-plaintext-development-single-user-journals.md); encryption returns before any multi-user or release scope. Keychain holds refresh tokens and device-bound secrets when those capabilities exist. PostgreSQL holds durable cloud records; immutable compressed sample/replay objects live in object storage. Redis is only ephemeral presence, leases, queues, and caches. Queue consumers and finalization paths are idempotent.

Contracts are versioned. Protobuf is canonical for real-time and object events; OpenAPI defines control APIs. Persisted schemas, protocol fields, and rulesets are backward compatible for the supported client window. See [data and protocols](docs/architecture/06-data-and-protocols.md).

## Race model

A race room is a deterministic, single-owner event loop. It accepts sequenced metric frames, validates them against the ruleset and common clock, publishes snapshots, and emits one durable finalization event. The room may recover or fail, but it must not turn client prediction into an official result. A failed online race leaves the local workout valid, possibly unranked. See [ADR-0003](docs/adr/0003-authoritative-race-service.md).

## Security and operational rules

- Use least-privilege, short-lived credentials, TLS, and platform key storage; never commit secrets, signing identities, PM serial numbers, or production captures.
- Keep raw telemetry and health-adjacent data minimal, consented, redacted in diagnostics, and subject to retention/deletion policy.
- Developer ID signing, notarization, and the executable update channel begin in Phase 4; Phase 0–3 artifacts are unsigned ad-hoc builds. Signed content manifests remain a separate Phase 2 requirement. Do not update during an active workout.
- Pin toolchains and dependencies. Treat a release as the application, content, contracts, capability matrix, cloud deployment, migrations, and runbooks together.

The detailed requirements are in [security](docs/architecture/07-security-privacy-safety.md) and [delivery and operations](docs/architecture/08-delivery-and-operations.md).

## Change rules

Accepted ADRs are binding. Reverse one only with a superseding ADR. Keep public contracts small, versioned, and hardware-neutral. Changes to an `FR-*` or `QA-*` requirement require traceable verification. Every delivery phase must contain one or more milestones and may contain many; each milestone belongs to exactly one phase. Record milestone delivery history in the owning phase's `CHANGELOG.md`. Completing a milestone does not complete its phase or pass the phase exit gate, and changelogs do not change architecture.

## Detailed references

- [Product scope](docs/architecture/01-product-scope.md)
- [System architecture](docs/architecture/02-system-architecture.md)
- [Client](docs/architecture/03-macos-unreal-client.md)
- [PM5](docs/architecture/04-concept2-pm5-integration.md)
- [Online platform](docs/architecture/05-online-platform.md)
- [Data and protocols](docs/architecture/06-data-and-protocols.md)
- [Security](docs/architecture/07-security-privacy-safety.md)
- [Delivery](docs/architecture/08-delivery-and-operations.md)
- [Verification](docs/architecture/09-verification-strategy.md)
- [Delivery plan](docs/architecture/10-delivery-plan.md)
