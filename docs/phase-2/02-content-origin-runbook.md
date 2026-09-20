# Phase 2 Milestone 1 content-origin runbook

Status: Draft implementation procedure  
Owner: Release/platform and content/security

## Purpose

This runbook controls publication, withdrawal, and rollback of the signed Han
River content catalog. It does not cover application binaries, Sparkle, Apple
signing, or notarization.

## Access and custody

- The static origin is public-read HTTPS only; write access is restricted to
  the release/platform owner.
- The Ed25519 private key stays on the restricted offline release machine.
  It is never copied to CI, developer machines, the repository, or a support
  bundle. The client carries only the two verification keys recorded in
  `Config/ContentTrust.json`.
- Every candidate has a reviewed provenance inventory. `source_hash` values
  may be `pending-cook` only before the candidate is eligible for publication;
  promotion requires a real source hash, reviewer, and any attribution.

## Publish

Before a release-owner publication, run `make content-canary` to exercise the
origin/state-machine fixture. This does not authorize promotion and does not
replace a real UE 5.8 cook.

1. Run the route, asset-budget, reference, redirector, and provenance
   validators against the reviewed source.
2. Produce the external UE 5.8 IoStore pair and its required metadata Pak;
   the archive contains no alternate Pak-only payload, executable, script,
   plug-in, Blueprint bytecode, or undeclared entry.
3. Generate deterministic inventory and route-definition bytes, package them,
   and calculate the immutable archive URL and SHA-256.
4. On the restricted signer, issue a seven-day canonical manifest at a new,
   strictly higher `catalog_revision`, then sign it with `content-current` or
   the pre-staged `content-next` key.
5. Upload objects to new hash-addressed paths. Verify the remote digest before
   publishing the catalog. Never overwrite an existing object or manifest.
6. On a packaged Shipping canary, download, restart, mount, select Han River,
   and exercise its 5 km endpoint. Verify Standard while offline as well.

## Withdraw and roll back

- To withdraw Han River, publish a newer signed revision with `withdrawn=true`
  and a safe reason key. Do not delete existing objects or journal records.
- To roll back, publish a newer revision pointing at an earlier immutable,
  approved content set. A lower catalog revision is never a rollback.
- If a trusted verification key may be compromised, withdraw Han immediately,
  stop publication, and ship a manually reinstalled alpha build with new
  embedded keys. Standard and local history remain available.

## Incident evidence

Record the catalog revision, manifest/package/inventory hashes, canary build
and toolchain fingerprints, storage/network/mount outcome, and redacted error
category. Do not record signing keys, raw user paths, serials, tokens, or raw
telemetry.
