# Phase 2 Milestone 1: Han River content delivery with Standard fallback

Status: Completed 2026-09-21 by owner decision — see [04-milestone-1-evidence.md](04-milestone-1-evidence.md) for the evidence actually captured and the checks not captured
Owner: Client/content, release/platform, security  
Last reviewed: 2026-09-21

## Relationship to the delivery plan

This is the first scoped milestone under [Delivery Phase 2](../architecture/10-delivery-plan.md#phase-2--solo-alpha-and-content-pipeline-610-weeks). It implements the Phase 2 content portion of FR-013 without changing the phase exit gate. It does not establish executable signing, notarization, Sparkle, accounts, entitlement, or automatic application updates; those remain outside this milestone under [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md).

Core content contracts, validation, persistence, boot fallback, runtime catalog
refresh, user-initiated resumable transfer, validation/extraction, and
next-launch activation are implemented. This milestone is not complete until
the packaged Han canary evidence listed in [the evidence record](04-milestone-1-evidence.md)
is recorded.

## Outcome

The alpha presents two peer course choices:

- **Standard** is a newly cooked-in, low-detail 2 km closed route. It is always usable without a network or downloaded content.
- **Han River** is a downloadable 5 km open, point-to-point vertical slice. It is selected from a separate Content page, downloads only while idle, and activates after a successful next launch.

After the athlete reaches the 5 km Han River endpoint in Just Row, presentation holds the boat at the endpoint and presents `Route complete — continue rowing`. PM distance, HUD values, the local journal, and the workout remain live; presentation does not synthesize, cap, or otherwise alter official progress.

If Han River is absent, withdrawn, expired, corrupt, incompatible, or cannot mount, its course tile is disabled with a concise reason and Standard remains selectable. Standard is a normal peer route, not a hidden safe-mode destination.

## Content contract and trust boundary

`ContentManifest v1` is a canonical, versioned signed payload. It contains:

- monotonic `catalog_revision`, issuance time, and seven-day expiry;
- manifest/content schema and compatible client ranges;
- route ID, semantic version, content-set ID, route metadata hash, open/closed flag, and length in millimetres;
- immutable public HTTPS package URL, SHA-256, compressed and uncompressed sizes, and allowed-inventory digest;
- route withdrawal state and user-safe reason key; and
- Ed25519 key ID and signature over the canonical unsigned payload.

The app embeds a current and next public verification key. The restricted release machine alone holds the private signing key; ordinary developer machines and CI only verify. Planned key rotation uses the next embedded key. A compromise of either trusted key requires immediate Han withdrawal and a manually reinstalled alpha build with replacement embedded keys; Standard and local history remain usable.

`RouteDefinition` is engine-independent and exposes stable IDs, version/hash, content-set ID, length, open/closed behavior, checkpoints, compatibility, and presentation metadata. Runtime route selection uses this definition, never a raw Unreal package path.

Downloaded material is data only. Before mount, the validator must verify the signature, schema, compatibility, manifest/path/hash/size/inventory limits, and an explicit asset-class allowlist. It rejects Blueprint bytecode, scripts, native plug-ins, dylibs, redirectors, undeclared assets, traversal paths, and malformed or oversized containers. The Han package is external UE 5.8 IoStore content only; no Pak fallback is allowed. The first implementation checkpoint must prove data-only IoStore mount and route load in a packaged Shipping build. Failure blocks the milestone rather than weakening this boundary.

## Install, fallback, and storage behavior

The client persists content state as `staged`, `verified`, `active`, `last_known_good`, `withdrawn`, or `failed` through a LocalData repository. It retains exactly one active and one last-known-good content set.

Boot verifies retained content and opens the local menu without waiting for network. It selects compatible active content when available, otherwise Standard. A signed catalog refresh begins asynchronously at boot. A newer valid revision may withdraw or replace Han River after the refresh completes; a lower revision is rejected. Rollback publishes a newer catalog revision that points to a previously approved immutable content set. Existing artifact bytes and URLs are never overwritten.

The Content page is the only place a user can begin a download. Transfers resume into a staging directory and activation is pending until the next launch. No download, activation, withdrawal, or mount operation may start or change an active workout.

The compressed Han package is capped at 32 GiB (raised to 100 GiB by owner decision in [Milestone 2](05-milestone-2-han-river-wiring-and-production.md), 2026-09-21). Before download, the client requires at least 100 GiB free storage, leaving room for a staged replacement alongside active and last-known-good content. If the requirement is not met, it declines the transfer with required-space guidance and preserves existing content. Partial or invalid staged files are never mounted.

An installed Han catalog remains trusted for seven days from its signed issuance. If no successful refresh occurs before expiry, Han River is disabled until refresh succeeds; Standard remains usable indefinitely offline. A successful signed withdrawal similarly disables new Han entry without altering historical session records.

## Pipeline and ownership

The release/platform owner provisions the public-read, TLS-protected internal static origin, immutable artifact paths, catalog publication, withdrawal, rollback, and monitoring. Public readability is acceptable because content is non-secret and authenticity comes from the manifest signature, not URL secrecy.

The content pipeline is:

```text
reviewed source + license record
  -> editor route/asset/budget validator
  -> deterministic cook and IoStore package
  -> inventory and hashes
  -> offline manifest signing
  -> immutable static-origin publication
  -> packaged canary and catalog promotion
```

Every Han River asset/reference must be original, public-domain, or licensed
under any version of CC BY-SA after a per-asset compatibility review. Other
third-party licenses require the existing legal/provenance review before the
asset can be cooked or published. Every CC BY-SA inventory record must include
the exact license/version and license URL, author/creator, canonical source
URL, source SHA-256, modification description, and compatible share-alike
release designation.

Every package must contain exactly one `licenses/NOTICE.txt`, recorded as the
`LicenseNotice` inventory class and included in the signed inventory. It must
list every CC BY-SA asset, its creator, source, license, URL, and changes made,
and include the applicable CC BY-SA license text or the stable reference
required by that version. A CC BY-SA image and every crop, retouch, texture,
mesh, or other adaptation remain available under the same or a compatible
CC BY-SA license. This boundary covers the source image and its adaptations
only; unrelated VIR code and original assets remain proprietary.

## Implementation boundaries

In scope:

- cooked Standard route; Han River vertical-slice route definition and data-only content package;
- engine-independent manifest, route, verification, and install-state contracts;
- LocalData migration/repository and `UContentSubsystem` boot, Content-page, mount, and course-tile behavior;
- route validator, deterministic package/sign/publish tooling, fixture catalog, and provenance inventory;
- content-specific diagnostics with redacted stable error categories; and
- the static-origin operational procedure and incident/rollback runbook.

Out of scope:

- a production-quality/full Han River environment, more routes, accounts, entitlement, cloud catalog APIs, automatic downloads, user-authored content, executable mods, PM-managed workouts, Sparkle, notarization, and executable update delivery;
- relaxing the existing PM authority, session-journal durability, privacy, or game-thread boundaries; and
- making a content failure block Standard, the local menu, or workout completion.

## Verification and milestone gate

Automated coverage must include canonical manifest round trips; both trusted keys; rotation, revision, expiry, rollback, and withdrawal; malformed/oversized parser inputs; signature/hash/size/path/inventory/schema failures; staged-resume and cleanup; active/last-known-good retention; and storage-admission refusal.

Unreal automation must prove cooked Standard availability without downloaded content, ID-based route selection, next-launch Han activation, 5 km endpoint behavior, and no write path from course presentation to official distance.

Packaged Shipping evidence must cover fresh offline Standard use; Han download/restart/play; interrupted resume; corrupt, unsigned, incompatible, expired, withdrawn, and mount-failed Han handling; 100 GiB storage refusal; a newer-revision rollback; and proof that an active workout blocks content operations.

Required commands are:

```sh
make build && make test
make format-check
make content-release-package # restricted release-owner signer and reviewed cook required
make unreal-native-app
make unreal-smoke
make unreal-shipping
make unreal-package-verify
```

The milestone passes only when the Shipping IoStore proof, validator and signature-negative suite, cooked Standard fallback flow, origin withdrawal/rollback rehearsal, licensed Han provenance record, and required automated/package evidence pass. Passing it does not pass the Phase 2 exit gate.
