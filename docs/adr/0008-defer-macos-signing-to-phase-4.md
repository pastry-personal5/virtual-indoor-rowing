# ADR-0008: Defer Developer ID signing and notarization until Phase 4

- Status: Accepted
- Date: 2026-09-16
- Owners: CTO, release lead, security lead

## Context

[ADR-0006](0006-macos-distribution.md) sets the target macOS distribution model: Developer ID signing, Hardened Runtime, notarization/stapling, and a signed Sparkle 2 update channel. As drafted, the delivery plan and the [Phase 0 Milestone 3 toolchain spike](../phase-0/06-milestone-3-toolchain-shipping.md) required that protected pipeline starting in Phase 0, and [ADR-0001](0001-platform-and-toolchain.md) named sign/notarize as a Phase 0 validation requirement.

Standing up that pipeline early means isolated release credentials, two-person approval, notarization submission, and a maintained signed update feed before the toolchain, PM5, and durability spikes are even proven and before any retained/paying cohort exists to justify the operational cost (see [ADR-0007](0007-evidence-gated-product-delivery.md) and the [executive review](../architecture/00-executive-review.md)). Through delivery Phases 0–3 the only people running the app are the internal team and small, named, hand-held cohorts (design partners, invited alpha, private beta) who can be walked through an explicit Gatekeeper right-click-Open on an ad-hoc/development-built bundle.

## Decision

- Through Phase 0, Phase 1, Phase 2, and Phase 3, distribute only ad-hoc/development-built arm64 Shipping bundles — the unsigned artifact `make unreal-shipping` produces and `make unreal-package-verify` inspects. No Developer ID signing, no notarization, no stapling, and no Sparkle-managed update channel exist yet.
- Every tester in these phases (internal team, named design partners, invited alpha, private beta) launches via explicit Gatekeeper right-click-Open and receives updates by manual reinstall of a freshly built bundle, not an automatic channel.
- Phase 0 Milestone 3 proves only the unsigned build/cook/stage/package/probe path. The protected `make release-sign-notarize` procedure may exist in tooling, but its required execution and evidence are rescheduled to Phase 4; they are not a Milestone 3 or Phase 0/1/2/3 exit-gate requirement.
- Phase 4 introduces the full ADR-0006 target — organizational Apple Developer ID, Hardened Runtime, notarization/stapling, and the signed Sparkle 2 channel — exercised on real internal/private-beta distribution before any public or ranked exposure. Phase 4's exit gate requires the protected credential-owner signing/notarization execution and a rehearsed clean-install/corrupt-feed/rollback cycle.
- Phase 5 is unaffected: it already assumes a working signing/update pipeline (disaster restore, provider outage, signing/update revocation drills) and now builds on the one Phase 4 establishes.
- Content-manifest signing (`FR-013`, [data and protocols](../architecture/06-data-and-protocols.md) catalog integrity) is a separate content-integrity key, not Apple Developer ID code signing, and is unaffected — it stays on its existing Phase 2 schedule.

## Consequences

- Phase 0's exit gate, Phase 1's deliverable/exit gate, and Phase 2/3 scope no longer name Developer ID signing, notarization, Gatekeeper, or Sparkle as required evidence; [ADR-0001](0001-platform-and-toolchain.md)'s Phase 0 validation clause, the [delivery plan](../architecture/10-delivery-plan.md), [product scope](../architecture/01-product-scope.md), and the [executive review](../architecture/00-executive-review.md) are updated accordingly.
- Removes the isolated-credential/two-person-approval/notarization operational burden from Phases 0–3, at the cost of a manual, right-click-Open install/update experience for every tester through Phase 3 — acceptable because those cohorts stay small and named, not the general public.
- Update-integrity rehearsal (corrupt feed/archive, rollback, key rotation) does not happen until Phase 4; GA-facing objectives such as QA-012 and the release-acceptance gates in [verification strategy](../architecture/09-verification-strategy.md) describe the target state, not a Phase 0–3 requirement.
- Phase 4 gains additional scope (organizational Apple Developer ID, isolated signing credentials, two-person approval, Sparkle feed/CDN operations) on top of its existing group-row/racing scope; its timeline and staffing estimate must account for this.
- If a Phase 0–3 build ever needs distribution beyond a small named cohort, the signing timeline must be pulled forward rather than worked around; this ADR assumes cohorts stay small and named through Phase 3, consistent with the existing delivery plan.

## Alternatives considered

- **Sign from Phase 0 as originally planned:** rejected for now; it front-loads credential custody, notarization submission, and Sparkle feed operations before the toolchain, PM5, and durability spikes are proven and before a retained cohort justifies the cost.
- **Sign only starting at Phase 3 private beta:** considered; rejected because Phase 3's private beta cohort is still small, named, and invited, so the same right-click-Open workaround already covers it, and standing up the pipeline mid-beta would compete with account/sync/billing work landing in the same phase.
- **Never require Developer ID signing; rely on ad-hoc signing permanently:** rejected; a public/GA release still needs Gatekeeper trust and a real update channel, and ADR-0006's target distribution model is unchanged by this ADR.

## Validation and revisit

Revisit if a Phase 0–3 cohort needs to grow beyond a small named group before Phase 4, if Apple tightens the ad-hoc/unnotarized right-click-Open launch path, or if manual install/update support burden from design partners/alpha/beta testers exceeds the plan's tolerance.
