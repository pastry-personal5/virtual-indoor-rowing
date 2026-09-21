# Phase 2 Milestone 1 evidence record

Status: completed 2026-09-21 by owner decision; packaged-canary checks below were not all captured
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
| Runtime catalog refresh, idle-only download, HTTP Range resume, storage admission, validation/extraction, verified staging, and next-launch activation | `Source/VirtualRowing/Private/ContentSubsystem.cpp`; `Source/VirtualRowing/Private/WorkoutHudWidget.cpp` |
| Han source-candidate route identity, six fixed beats, virtual-only waterline notice, review-art/reference provenance hashes, and reference-only notice | `Scripts/vir_dev/content.py`; `make content-canary` |

| Deterministic origin/download canary: signed catalog, archive integrity, interrupted resume, restart activation, last-known-good rollback, withdrawal, corrupt-artifact, and invalid-signature rejection | `make content-canary`; `Scripts/vir_dev/content.py` |
| Reviewed release packaging: real named external IoStore inputs, canonical route/inventory/notice archive, trusted prior-catalog revision check, seven-day `content-current` signature, and immutable hash-addressed package/catalog output | `make content-release-package`; `Scripts/test_content_release_packager.py`; `make test` |

The canary produces a local catalog and immutable `HanRiver.vircontent` fixture
under `Build/content-origin-fixture/`. Its IoStore-named files are explicit
markers, not cooked Unreal data, and are never counted as mount evidence.

`make unreal-smoke` is currently blocked before source compilation by the
managed runner denying UnrealBuildTool's per-user trace-backup path. The same
runner restriction remains relevant to `make unreal-shipping`; therefore
`make unreal-package-verify` has no new staged app to inspect.

## Required packaged canary evidence still open

The repository now orchestrates catalog refresh, user-initiated transfer,
HTTP Range resume, storage admission, validation/extraction, and next-launch
activation through `UContentSubsystem`. It remains unproven on a packaged
Shipping app and against the restricted-signing/public-origin artifact.

The repository does not contain a real cooked/published Han IoStore artifact
or a manual packaged canary capture. `make han-external-cook` now produces the
external `HanRiver.{pak,utoc,ucas}` trio (a local run cooked all 17 Han files
into one non-empty IoStore chunk with a valid TOC header; mounting is
unverified). The release owner must run `make content-release-package` with
`content-current.pem` and the reviewed external cook before publishing. The following must be recorded
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

The current automatic checks cannot prove that the supplied external input is
a mountable Unreal IoStore artifact, Unreal IoStore mount, renderer
presentation, or a 5 km endpoint review. A release/content owner must provide
the external UE 5.8 cook and run the packaged canary against that immutable
artifact; no PM5 hardware is required for this content-only canary.

The app now carries the release-owner-provided current and next public
verification keys; the `content-current` key was replaced on 2026-09-21. Their
private halves remain on the offline signer (`${HOME}/work/vir-tools/han-river/keys`).
Only `content-current` is required to publish; `content-next` is reserved for
rotation. `Config/ContentTrust.json` is not read by any code and disagrees
with the embedded keys; treat it as stale. The fixture's separate `fixture-test` key is intentionally public and
cannot be accepted by the app.

## Completion record (2026-09-21)

The milestone owner marked Milestone 1 completed on 2026-09-21. This is an
owner decision, not a claim that every check listed above was captured.

Demonstrated on a local packaged Shipping app (unsigned, ad-hoc), with the
origin served by a local Caddy server:

- `make han-external-cook` produced a non-empty `HanRiver.{pak,utoc,ucas}` trio
  with an Unreal IoStore TOC header;
- `make content-release-package` produced signed, hash-addressed releases
  (catalog revisions 1 and 2) under the embedded `content-current` key;
- the app fetched and verified the catalog, downloaded the package, activated it
  on the next launch, mounted it, and recorded `han-river-alpha-1` 1.0.0 as
  `active` at catalog revision 2.

Not captured, and therefore still unverified: fresh offline Standard launch and
workout; the Content Licenses/Credits display; interrupted download/resume;
corrupt, unsigned, incompatible, expired, withdrawn, and mount-failed fallback
cases in the packaged app; 100 GiB storage refusal and active-workout blocking;
newer-revision rollback and origin withdrawal rehearsal; and reference-Mac route
review captures. Known limitation: the downloaded map, materials, and meshes are
mounted but no code loads them, and switching routes after startup does not
rebuild the course scene; the Han look currently comes from the built-in
presentation kit. Simulator or automated results do not replace the missing
packaged evidence.

No Phase 2 exit-gate claim is made by the automated results above.
