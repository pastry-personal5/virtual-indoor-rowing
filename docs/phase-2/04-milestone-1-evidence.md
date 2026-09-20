# Phase 2 Milestone 1 evidence record

Status: core-contract checks passed; runtime delivery and cooked-IoStore canary remain open
Owner: Client/content, release/platform, security
Recorded: 2026-09-20

This record separates automated evidence from the remaining human canary work.
It does not claim the Phase 2 exit gate.

## Verified in this worktree

| Requirement | Evidence |
|---|---|
| Canonical signed manifest, trusted-key rotation, revision, expiry, withdrawal, schema, signature and hash negatives | `Source/ContentRuntime/Tests/ContentRuntimeTests.cpp`; `make test` |
| Data-only package path, size, archive, IoStore-member, inventory and route binding | `Source/ContentRuntime/Private/ContentInventory.cpp`; `make test` |
| CC BY-SA 2.0, 3.0, 4.0 records and notice requirements | `TestCcBySaVersionsAndNoticeFailures`; `Content/Phase2/HanRiver/licenses/NOTICE.txt`; `make test` |
| Missing creator/source/version/modification/share-alike fields, duplicate/misplaced notice, and inventory mismatch | `TestCcBySaVersionsAndNoticeFailures`; `make test` |
| Local staged/verified/active/last-known-good/withdrawn lifecycle, revision monotonicity, resume records, active-workout blocking | `Source/LocalData/Tests/ContentRepositoryTests.cpp`; `make test` |
| Standard fallback and ID-only course selection without an official-distance write path | `Source/ContentRuntime/Private/ContentState.cpp`, `Source/VirtualRowing/Private/Tests/CoursePresentationSpec.cpp`; `make test` and `make unreal-smoke` |
| Notice exposure in the Unreal content subsystem and Content Licenses/Credits UI | `Source/VirtualRowing/Private/ContentSubsystem.*`, `WorkoutHudWidget.*`; `make unreal-smoke` |
| Unreal Editor compile smoke and native archive linkage | `make unreal-smoke`; passed in this worktree |

| Deterministic origin/download canary: signed catalog, archive integrity, interrupted resume, restart activation, last-known-good rollback, withdrawal, corrupt-artifact, and invalid-signature rejection | `make content-canary`; `Scripts/vir_dev/content.py` |

The canary produces a local catalog and immutable `HanRiver.vircontent` fixture
under `Build/content-origin-fixture/`. Its IoStore-named files are explicit
markers, not cooked Unreal data, and are never counted as mount evidence.

`make unreal-shipping` was attempted but is currently blocked before cook by
the managed runner denying AutomationTool's per-user `XmlConfigCache` path;
`make unreal-package-verify` therefore has no staged app to inspect.

## Required implementation and canary evidence still open

The repository currently has no `UContentSubsystem` catalog-fetch or
user-initiated transfer implementation: `SaveDownload`, package validation,
and install-state APIs are not yet orchestrated by a runtime delivery flow.
Implement and test catalog refresh, Content-page download initiation, HTTP
range resume, storage admission, validation/extraction, and next-launch
activation before treating delivery gates as passed.

The repository also does not contain a real cooked/published Han IoStore
artifact or a manual packaged canary capture. The following must be recorded
on a packaged Shipping installation before changing this record to complete:

- fresh offline Standard launch and workout;
- Han download, restart, mount, route selection, 5 km endpoint hold, and
  Content Licenses/Credits display;
- interrupted download/resume and next-launch activation;
- corrupt, unsigned, incompatible, expired, withdrawn, and mount-failed Han
  fallback cases;
- 100 GiB storage refusal and active-workout operation blocking;
- newer-revision rollback and origin withdrawal rehearsal; and
- reference-Mac route review captures at each named beat and endpoint.

The current automatic checks cannot prove Unreal IoStore mount, renderer
presentation, or a 5 km endpoint review. A release/content owner must provide
the external UE 5.8 cook and run the packaged canary against that immutable
artifact; no PM5 hardware is required for this content-only canary.

Before publication, the release/security owner must replace the checked-in
development RFC 8032 test-vector trust keys with two public keys whose private
halves exist only on the restricted release signer. The fixture's separate
`fixture-test` key is intentionally public and cannot be accepted by the app.

No Phase 2 exit-gate claim is made by the automated results above.
