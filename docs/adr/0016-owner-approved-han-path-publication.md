# ADR-0016: Owner approval gates Han v2 path publication

- Status: Accepted — owner-directed Phase 2 publication-scope change
- Date: 2026-09-27
- Owners: CTO / product, client/content
- Supersedes: Phase 2 Milestone 6's baseline/calibration and clearance-audit requirement as a prerequisite for adding or publishing a Han presentation path

## Context

Milestone 6 originally required a reproducible geographic baseline/calibration
calculation and a detailed clearance/camera-envelope audit before a v2 Han path
could be added or packaged. Those are valuable supporting evidence, but the
owner has decided they are not publication prerequisites for this internal
Phase 2 content slice.

The prior package guard also attempted to infer owner approval from generated
metadata. That was not an adequate expression of an owner decision.

## Decision

- A Han v2 path may be added and published when the owner approves it against
  the exact reviewed runtime-map bytes.
- The canonical path source must retain its deterministic geometry, semantic
  version, and source-input hashes, and record the map path, map SHA-256,
  approval date, and owner-decision record.
- Baseline/calibration, geographic displacement, clearance, curvature, and
  camera-envelope audits are no longer release-packaging prerequisites. They
  remain useful optional evidence and may be required by a later milestone or
  Phase 2 exit-gate decision.
- All existing signed-content validation remains unchanged: signature, hash,
  size, path, compatibility, version, immutable catalog revision, and
  Standard-route fallback still fail closed.

## Consequences

The release tool accepts an owner-approved, map-identity-bound path without
requiring audit measurements that are not needed for the owner’s present
internal-content decision. This does not claim geographic accuracy, obstacle
clearance, performance, accessibility, mounted-package evidence, or Phase 2
exit-gate completion.

## Validation and revisit

The path-source tests must prove that a changed map invalidates approval and
that a modified candidate cannot be approved. Continue to run the normal
external cook and signed-package validation before publication. Reintroduce a
specific audit gate through a superseding ADR if a later route, public scope,
or safety claim depends on it.
