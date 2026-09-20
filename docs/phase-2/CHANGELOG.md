# Phase 2 changelog

## Unreleased

- Phase 2 Milestone 1 implementation gates are now covered by engine-independent
  manifest, inventory, license-notice, package-validation, LocalData lifecycle,
  Standard-fallback, and Unreal integration code. The evidence record is in
  [04-milestone-1-evidence.md](04-milestone-1-evidence.md). A published Han
  artifact/catalog and packaged canary remain required before the milestone is
  marked complete.
- Added `make content-canary` and `make content-fixture`. The automated canary
  now records deterministic origin publication, interrupted resume, restart
  activation, fallback, withdrawal, and newer-revision rollback. Its marker
  artifact is intentionally not treated as cooked-IoStore evidence; a real UE
  5.8 external cook and packaged canary remain release-owner work.
- Hardened the deterministic canary to verify its fixture signature and archive
  entry hashes, and to reject a corrupt artifact or altered signature. It uses
  an explicitly separate public RFC test key; client trust-key provisioning is
  still restricted release-owner work.
- Corrected the milestone/evidence status: repository checks cover content
  contracts and boot fallback, but runtime catalog refresh, user-initiated
  resumable download, validation/extraction orchestration, and next-launch
  activation are not implemented yet. They remain required before a delivery
  or packaged-canary claim.
- Course selection is now exposed on the blocking launch/device surface as
  well as the workout HUD: Standard 2 km is always selectable while idle;
  Han River 5 km remains visibly disabled with a safe reason until validated
  content activates. This implementation evidence does not pass a Phase 2
  gate.
- Removed the speculative future-milestone sequence. Future milestones are
  scoped only when their planning begins.
