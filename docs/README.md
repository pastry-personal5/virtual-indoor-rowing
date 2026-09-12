# Architecture documentation

This documentation is the starting point for product, engineering, security, operations, QA, and content teams. It describes the intended production system, not merely an Unreal prototype.

The supported desktop baseline is macOS Tahoe 26.6.2 or later on Apple silicon; see [ADR-0001](adr/0001-platform-and-toolchain.md).

## Reading order

1. [Executive product and architecture review](architecture/00-executive-review.md)
2. [Product scope and quality bar](architecture/01-product-scope.md)
3. [System architecture](architecture/02-system-architecture.md)
4. [macOS and Unreal client](architecture/03-macos-unreal-client.md)
5. [Concept2 PM5 integration](architecture/04-concept2-pm5-integration.md)
6. [Online platform and real-time racing](architecture/05-online-platform.md)
7. [Data model and protocols](architecture/06-data-and-protocols.md)
8. [Security, privacy, safety, and fair play](architecture/07-security-privacy-safety.md)
9. [Delivery and operations](architecture/08-delivery-and-operations.md)
10. [Verification strategy](architecture/09-verification-strategy.md)
11. [Phased delivery plan](architecture/10-delivery-plan.md)

## Accepted architecture decisions

- [ADR-0001: macOS, Unreal, and toolchain baseline](adr/0001-platform-and-toolchain.md)
- [ADR-0002: PM5 BLE integration through an in-process native plug-in](adr/0002-pm5-ble-integration.md)
- [ADR-0003: purpose-built authoritative race service](adr/0003-authoritative-race-service.md)
- [ADR-0004: offline-first session journal and idempotent synchronization](adr/0004-offline-first-session-journal.md)
- [ADR-0005: AWS single-region, multi-AZ launch topology](adr/0005-cloud-topology.md)
- [ADR-0006: Developer ID distribution with a signed update channel](adr/0006-macos-distribution.md)

## Proposed architecture decisions

- [ADR-0007: Evidence-gated product delivery](adr/0007-evidence-gated-product-delivery.md)

## Research archive

Research is separated from prescriptive architecture so that facts can be rechecked without silently changing a decision:

- [Platform and Unreal research](archive/research/2026-09-12-platform-and-unreal.md)
- [Concept2 PM5 research](archive/research/2026-09-12-concept2-pm5.md)
- [Product, integrations, and distribution research](archive/research/2026-09-12-product-and-integrations.md)

## Document rules

- Architecture documents are normative where they use **must**, **shall**, or an accepted ADR.
- Research notes are dated evidence, not requirements.
- Every externally visible protocol and persisted schema is versioned before implementation.
- A change that reverses an accepted ADR requires a superseding ADR. Small refinements can be reviewed as ordinary documentation changes.
- Product analytics, health-adjacent data use, billing, trademarks, and engine licensing require legal/product approval in addition to engineering review.

## Proposed repository layout

```text
Build/                         Unreal BuildGraph and packaging definitions
Config/                        pinned toolchain and release-channel manifests
Content/                       Unreal source content (Git LFS)
Plugins/Concept2PM/            native CoreBluetooth and PM protocol plug-in
Source/VirtualRowing/          Unreal game modules
Source/VirtualRowingEditor/    editor-only tooling and validation
Contracts/proto/               versioned client/cloud Protobuf contracts
Services/control-api/          Go control-plane modular monolith
Services/race-server/          Go authoritative real-time service
Services/worker/               asynchronous finalization/integration jobs
Tools/pm5-sim/                 protocol simulator and capture replayer
Tools/content-pipeline/        route metadata and manifest generation
Infra/terraform/               cloud infrastructure as code
Tests/fixtures/pm5/            sanitized golden packet captures
docs/                          product and engineering documentation
```

Generated Unreal directories (`Binaries`, `DerivedDataCache`, `Intermediate`, and `Saved`) must not be committed.
