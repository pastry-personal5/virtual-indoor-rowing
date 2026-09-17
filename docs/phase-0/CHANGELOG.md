# Delivery Phase 0 diagnostic changelog

This document records changes to the bounded diagnostic milestones that contribute to delivery Phase 0. It does not assert completion of the delivery Phase 0 exit gate.

## 2026-09-14

- **Phase 0 Milestones 1–2:** Classified the diagnostic packet as a bounded contribution to delivery Phase 0.
- **Phase 0 Milestones 1–2:** Renamed internal “Phase 1/2” work to Milestones 1/2 so it cannot be confused with the product-delivery phases.

## 2026-09-17

- **Phase 0 Milestone 3:** Adopted [ADR-0009](../adr/0009-defer-bluetooth-tcc-scenario-matrix-to-phase-4.md): the human-only TCC-reset Bluetooth permission-scenario matrix (fresh Allow, Deny, Settings-repair-then-Allow, normal-launch-no-prompt) is no longer a condition of this milestone; it is rescheduled to the Phase 4 exit gate alongside the ADR-0008 signing/notarization rehearsal.
- **Phase 0 Milestone 3:** Completed. Acceptance-gate items 1 (unsigned native/Unreal build, format check, package verification) and 2 (self-hosted Apple-silicon CI on `main`) were already met on 2026-09-16; with item 3 deferred per ADR-0009, the milestone is closed.
- **Delivery plan:** Removed "fresh install/permission denial/repair pass" from the Phase 1 exit gate and added the Bluetooth TCC permission-scenario matrix to the Phase 4 exit gate, per ADR-0009. Updated `docs/architecture/02-system-architecture.md` and `docs/architecture/00-executive-review.md` to match.

## 2026-09-16

- **Phase 0 Milestone 3:** Added the bounded unsigned Unreal Shipping toolchain host, package verification, explicit Bluetooth-permission probe, and protected credential-owner release procedure. The signing/notarization/Gatekeeper evidence remains pending.
- **Phase 0 Milestone 3:** Added the reference volume's approved UE 5.8 discovery location, deterministic staged-app tree hashing in retained package-verification output, and explicit Hardened Runtime inspection alongside the existing `get-task-allow` rejection. These are verifier hardening changes only; the required CI and TCC evidence remains pending.
- **Phase 0 Milestone 3:** Adopted [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md): the protected Developer ID signing/notarization/Gatekeeper execution is no longer a condition of this milestone's completion. Rescoped the acceptance gate to the unsigned portion only and reclassified that evidence-report row from Pending to Deferred to Phase 4.
