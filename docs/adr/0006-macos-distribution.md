# ADR-0006: Developer ID distribution with a signed update channel

- Status: Accepted; distribution model unchanged, introduction timing superseded by [ADR-0008](0008-defer-macos-signing-to-phase-4.md) (unsigned ad-hoc builds through Phase 3, this ADR's pipeline required starting Phase 4)
- Date: 2026-09-12
- Owners: Release lead, security lead, product

## Context

macOS applications can ship through or outside the Mac App Store. The product needs Unreal packaging, frequent updates/content, Bluetooth, subscriptions, and a startup-controlled release cadence. Apple supports outside-store Developer ID signing and notarization. Updates are a high-value code-execution trust path.

## Decision

- Initially distribute outside the Mac App Store as an Apple-silicon application in a Developer ID-signed, hardened, notarized, stapled DMG.
- Use a pinned, security-reviewed Sparkle 2 release for application updates.
- Require both Apple code identity/notarization and EdDSA verification of update feed/archive; use HTTPS and immutable CDN objects.
- Protect release signing with isolated credentials, two-person approval, provenance/SBOM, clean-runner build, and rotation/revocation drills.
- Use separate internal/alpha/beta/stable signed feeds. Do not allow user-selectable arbitrary feed URLs.
- Keep downloaded Unreal content in a separately signed manifest/hash system; never download native plug-ins or executable mods.
- Do not enable App Sandbox for the direct build by default, but request no unnecessary entitlement/permission. A Mac App Store variant requires its own package/StoreKit/sandbox review.

## Consequences

- Release and hosted billing cadence remain under startup control, and Gatekeeper/notarization still protect users.
- The team owns website/CDN/update operations and must promptly monitor Sparkle/security issues.
- Direct distribution may have lower discovery/trust for some users than the store.
- Every nested Unreal/native binary and updater component needs correct signatures and notarization.
- An App Store build is not just the same binary uploaded later.

## Alternatives considered

- **Mac App Store first:** deferred because sandbox, review, billing, update, and content behavior should be spiked rather than becoming launch blockers.
- **Manual full-download updates only:** acceptable emergency fallback but rejected as final UX/security adoption path.
- **Build a custom privileged updater:** rejected; updater security is specialized and a maintained framework is safer when pinned/reviewed.
- **Unreal content system for native app replacement:** rejected; app replacement remains a macOS update responsibility.

## Validation and revisit

Every candidate tests clean install, read-only/translocated launch, last-stable update, corrupt/unsigned/replayed feed/archive, interrupted update, key rotation, rollback recovery, `codesign`, Gatekeeper, and notarization. Revisit Mac App Store when business evidence justifies the separate constraints.

Evidence: [product, integration, and distribution research](../archive/research/2026-09-12-product-and-integrations.md).
