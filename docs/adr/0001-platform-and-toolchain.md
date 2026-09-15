# ADR-0001: macOS, Unreal, and toolchain baseline

- Status: Accepted
- Date: 2026-09-12
- Owners: CTO, client lead, release lead

## Context

The product needs a stable macOS, Unreal Engine, and Xcode baseline for development and release qualification. Epic's current UE 5.8 matrix recommends Xcode 26.1.1 and identifies Xcode 26.4 as incompatible.

Unreal's standard game license can require a royalty after the published revenue threshold, so “free-to-use” cannot be treated as zero commercial licensing cost.

## Decision

- Use **macOS Tahoe 26.6.2 or later** as the runtime requirement.
- Launch on Apple silicon `arm64` only and benchmark the supplied M5 Max/128 GB configuration.
- Use stock Unreal Engine **5.8**, pinned to a tested patch/build fingerprint.
- Use **Xcode 26.1.1** as the initial pinned build toolchain, with automatic upgrades disabled on golden CI builders.
- Use Metal desktop rendering and avoid hardware ray tracing/MegaLights or beta Nanite/VSM behavior as a required launch capability.
- Keep product domain logic in engine-independent C++ modules and Apple APIs behind Objective-C++ adapters.
- Do not create an Unreal Engine fork unless a blocking, reproducible defect passes a separate ADR.
- Require legal/finance review of the current Unreal EULA, release notification/reporting, royalties, marketplace assets, and any custom license before commercial launch.

## Consequences

- A very narrow, high-performance reference target enables meaningful frame/thermal and device gates.
- Intel Macs and earlier macOS releases are unsupported at launch.
- macOS patches/Xcode updates cannot be adopted automatically; a compatibility lane must continually test them.
- Content must retain conservative fallbacks even though the reference GPU may run experimental features.
- Future platforms benefit from a domain core that does not depend on Metal, CoreBluetooth, or Actors.

## Alternatives considered

- **Always use latest Xcode/UE:** rejected because current official compatibility already documents a specific incompatible Xcode version.
- **Build universal arm64+x86_64:** deferred; it expands native dependency, cook, package, and test scope without serving the stated target.
- **Custom engine fork immediately:** rejected; stock engine lowers upgrade and build risk.

## Validation and revisit

Delivery Phase 0 must compile, cook, package, sign, notarize, and run a Bluetooth-enabled Shipping build with this exact trio. Revisit when Epic changes the support matrix, Apple security/SDK policy forces a move, the target OS changes, or a newer combination passes all client, HIL, packaging, and soak gates.

Evidence: [platform and Unreal research](../archive/research/2026-09-12-platform-and-unreal.md).
