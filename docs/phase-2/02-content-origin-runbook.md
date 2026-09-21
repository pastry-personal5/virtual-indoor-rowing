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
  bundle. The client carries only the current and next verification keys in
  `UContentSubsystem::TrustedKeys()`.
- Every candidate has a reviewed provenance inventory. `source_hash` values
  may be `pending-cook` only before the candidate is eligible for publication;
  promotion requires a real source hash, reviewer, and any attribution.

## Publish

Before a release-owner publication, run `make content-canary` to exercise the
origin/state-machine fixture. This does not authorize promotion and does not
replace a real UE 5.8 cook.

1. Run the route, asset-budget, reference, redirector, and provenance
   validators against the reviewed source.
2. Produce the external UE 5.8 IoStore pair and its required metadata Pak with
   `make han-external-cook`. It cooks the Han map and the whole
   `Content/Phase2/HanRiver` directory, routes that content into its own
   `pakchunk1001` chunk through a temporary, git-ignored
   `Config/GeneratedPakFileRules.ini` and `Config/GeneratedGame.ini` (removed
   after the run, so ordinary Shipping packages are unaffected), and copies the
   chunk to `HanRiver.pak`, `HanRiver.utoc`, and `HanRiver.ucas` in
   `$VIR_HAN_IOSTORE_DIR` (default `Build/han-cook/HanRiver`). It fails unless
   each file is non-empty and the `.utoc` has the Unreal IoStore header, and it
   refuses to overwrite an existing cook. The cook is not proof of mounting;
   that still needs the packaged canary. The archive contains no alternate Pak-only payload, executable, script,
   plug-in, Blueprint bytecode, or undeclared entry.
3. On the signing machine, retrieve the currently published immutable
   `content-current` catalog to a local file. Do not use a hand-entered prior
   revision: the release tool verifies that catalog and derives its revision.
   For the very first publication there is no catalog: leave
   `VIR_CONTENT_PREVIOUS_CATALOG` unset and set
   `VIR_CONTENT_CATALOG_REVISION=1`. The tool refuses any other revision
   without a previous catalog.
4. Run the reviewed release packager. It consumes the three real external
   cooked files named `HanRiver.pak`, `HanRiver.utoc`, and `HanRiver.ucas`;
   generates canonical `route.pb` and `inventory.pb`; includes
   `licenses/NOTICE.txt`; creates a seven-day manifest at a strictly higher
   revision; and signs only with the embedded `content-current` key.

   ```sh
   export VIR_TOOLS="${HOME}/work/vir-tools/han-river"
   export VIR_HAN_IOSTORE_DIR="$VIR_TOOLS/cooks/HanRiver"
   export VIR_CONTENT_SIGNING_KEY="$VIR_TOOLS/keys/content-current.pem"
   export VIR_CONTENT_PREVIOUS_CATALOG="$VIR_TOOLS/catalogs/catalog-current.pb"  # omit for the first publication
   export VIR_CONTENT_CATALOG_REVISION=42                                        # must be 1 for the first publication
   export VIR_CONTENT_ORIGIN_BASE_URL="https://content.example.invalid/vir"
   export VIR_CONTENT_OUTPUT_DIR="$VIR_TOOLS/releases"
   make han-external-cook
   make content-release-package
   ```

   One-time key setup (only `content-current` is required; the release tool
   never signs with `content-next`):

   ```sh
   mkdir -p "$VIR_TOOLS"/{cooks/HanRiver,keys,catalogs,releases}
   chmod 700 "$VIR_TOOLS" "$VIR_TOOLS/keys"
   (umask 077; openssl genpkey -algorithm ED25519 -out "$VIR_TOOLS/keys/content-current.pem")
   chmod 600 "$VIR_TOOLS/keys/content-current.pem"
   # public half, 64 hex chars, embedded in the app
   openssl pkey -in "$VIR_TOOLS/keys/content-current.pem" -pubout -outform DER | tail -c 32 | xxd -p -c 64
   ```

   The public half must equal the `content-current` key in
   `Source/VirtualRowing/Private/ContentSubsystem.cpp` (`TrustedKeys()`) and
   `CURRENT_PUBLIC_KEY` in `Scripts/vir_dev/content.py`; change both together
   and rebuild the app. Catalogs signed under a previous key no longer verify.
   The tool needs the system `openssl` to support `pkeyutl -rawin`; macOS
   LibreSSL may not, so put Homebrew OpenSSL 3 first on `PATH` if signing fails.
   For rotation, `content-next` may be generated the same way and embedded in
   `TrustedKeys()`, but rotation is a separately reviewed procedure.

   `content-current.pem` must be owner-only (for example, `chmod 600` on the
   restricted signer). The command rejects missing/empty/symlinked cook
   members or a non-Unreal `HanRiver.utoc` header, a signer that
   does not match the app's `content-current` public key, an unsigned or
   untrusted previous catalog, non-increasing revisions, non-HTTPS origins,
   and an existing output hash directory. It performs no upload.
5. Review `release.json`, then upload the emitted hash-addressed
   `HanRiver.vircontent` and `catalog-<sha256>.pb` exactly once. Verify the
   remote digest before publishing the catalog. Never overwrite an existing
   object or manifest.
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
