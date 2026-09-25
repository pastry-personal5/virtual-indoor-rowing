# Han River packaging runbook

Status: Release-owner procedure
Owner: Client/content, release/platform, security
Last reviewed: 2026-09-23

## Purpose and boundary

This runbook produces the reviewed external Unreal IoStore cook and, on the
restricted release machine, the signed immutable `HanRiver.vircontent`
artifact and catalog that refer to it. The [content-origin runbook](02-content-origin-runbook.md)
owns publication, withdrawal, and rollback.

This procedure does not sign or notarize the macOS application, upload files,
or prove that a packaged Shipping application can mount the content. A
successful cook and packager run are necessary inputs to those later checks,
not substitutes for them.

The steps run in three places:

| Place | Work | Required access |
|---|---|---|
| Clean Apple-silicon cook host | Verify source, build the unsigned app, cook the external Han chunk | Reviewed source revision, Git LFS objects, approved UE/Xcode |
| Restricted signing machine | Assemble `HanRiver.vircontent` and sign its catalog | The same source revision, the fresh cook trio, the protected content key |
| Restricted origin operator | Upload immutable objects and advance `catalog-current.pb` | Origin write access; the signing key is not needed |

The cook and signing roles may use one approved machine. Keep the generated
`Build/` output, signing key, catalogs, and any transfer copies outside Git.

The release consists of:

- `HanRiver.pak`, `HanRiver.utoc`, and `HanRiver.ucas`: the three external
  IoStore members produced by the cook;
- `HanRiver.vircontent`: an immutable archive containing those three members,
  canonical `route.pb`, `inventory.pb`, and `licenses/NOTICE.txt`;
- one signed catalog envelope, normally named
  `catalog-<catalog-sha256>.pb`, whose manifest contains the package URL,
  package digest, compressed size, compatibility, route metadata, expiry, and
  catalog revision.

Downloaded content is data only. It must not contain executable code, scripts,
native plug-ins, or Blueprint bytecode. The client verifies the signed catalog,
package size and digest, inventory, path policy, and route before activation;
activation occurs on a later launch.

## Prerequisites

Run from the repository root on the approved reference host. Use an immutable,
reviewed commit for release work; the commands below stop if the worktree is
dirty. Do not use a local cook as proof that another machine has the same LFS
objects:

```sh
set -e
set -o pipefail
cd /Volumes/Unreal_Engine_Volume/work/virtual-indoor-rowing
git lfs pull
export VIR_RELEASE_COMMIT="$(git rev-parse HEAD)"
printf 'release commit: %s\n' "$VIR_RELEASE_COMMIT"
test -z "$(git status --porcelain)"
make doctor
make build && make test
make format-check
make content-canary
```

`make content-canary` checks the deterministic source, catalog, download,
resume, withdrawal, rollback, and Standard-fallback fixture. It does not cook
Unreal content, sign a release, or provide Shipping mount evidence.

Use the approved Unreal Engine 5.8.2 installation. If Unreal is not detected,
set `UE_ROOT` to the directory containing `Engine/Build/BatchFiles/RunUAT.sh`:

```sh
export UE_ROOT="/path/to/UnrealEngine"
test -x "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
```

Review the source-controlled Han changes before cooking. The cook reads the
files currently on disk. The runtime level is
`Content/Phase2/HanRiver/Maps/L_HanRiver_BlueHour.umap`; its current Area 01
geometry uses 164 files in `Content/Phase2/HanRiver/Meshes/Area01/OSM/`.
These source packages must all be in the release revision and available through
Git LFS on the cook host. Keep `L_HanRiver_Area01_Review.umap`,
`L_HanRiver_Area01_BlueHour.umap`, and dated backup levels out of the release
revision. The Han-area commands also require `Scripts/han_area.py`,
`Scripts/osm_han_area.py`, `Scripts/unreal_han_area.py`,
`Scripts/unreal_osm_area.py`, `Scripts/blender_han_area.py`, and
`Scripts/test_han_area.py` in that revision.

On a Git-writable authoring checkout, add the reviewed runtime assets and
command modules explicitly before submitting the release change. This path
list deliberately omits the review and dated backup maps:

```sh
git add -- \
  Content/Phase2/HanRiver/Maps/L_HanRiver_BlueHour.umap \
  Content/Phase2/HanRiver/Meshes/Area01/OSM \
  Scripts/han_area.py Scripts/osm_han_area.py Scripts/unreal_han_area.py \
  Scripts/unreal_osm_area.py Scripts/blender_han_area.py Scripts/test_han_area.py
git diff --cached --name-only
git lfs ls-files | rg 'L_HanRiver_BlueHour|Meshes/Area01/OSM/'
```

Review the staged list and LFS pointers before committing. The release cook
host checks out the resulting reviewed commit; it does not build from an
untracked artist workstation.

Run the source gate before cooking. It verifies the Unreal package headers,
checks every Han source package named by the runtime map, and refuses packages
that exist only as untracked local files. Run the same gate from a fresh clone
with LFS objects fetched before treating the cook as reproducible:

```sh
git lfs ls-files | rg 'L_HanRiver_BlueHour|Meshes/Area01/OSM/'
make han-area-test
make han-source-verify
```

The LFS listing is an inventory check. `make han-source-verify` reads the
actual binary packages and reports a missing asset, an LFS pointer left in
place of an asset, or an asset absent from the Git index. Stop on any failure;
do not use an older `Build/han-cook` trio to bypass it.

`make han-source-verify` does not replace an Editor reference check or a cook.
Do not commit or copy private journals, raw captures, PM serials, credentials,
private signing keys, or generated Unreal output.

## Produce the external cook

Use a fresh output directory. The command refuses to overwrite an existing
`HanRiver.pak`, `HanRiver.utoc`, or `HanRiver.ucas`:

```sh
export VIR_HAN_IOSTORE_DIR="$PWD/Build/han-cook/HanRiver-$(date +%Y%m%d-%H%M%S)"
make han-external-cook
```

The target uses Unreal `BuildCookRun` in Shipping mode, cooks
`/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour`, and explicitly includes the
Han `Materials` and `Meshes` directories. The map brings in its referenced
Area 01 OSM meshes; the directory cook also retains reviewed assets not yet
referenced by the map. Review and backup maps are excluded. Temporary Pak rules
route the content to `pakchunk1001`; those generated config files are removed after
the run and do not change ordinary Shipping packaging.

Confirm the structural output and record its digests:

```sh
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.pak"
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.utoc"
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.ucas"
shasum -a 256 "$VIR_HAN_IOSTORE_DIR"/HanRiver.{pak,utoc,ucas}
```

The cook validates non-empty members and the Unreal IoStore header in
`HanRiver.utoc`. It does not establish that the archive can be downloaded,
validated, extracted, mounted, or presented by a packaged application.

## Assemble and sign the release

Run this section only on the restricted release/signing machine. Keep the
private key outside the repository and owner-readable:

```sh
set -e
set -o pipefail
export VIR_TOOLS="${HOME}/work/vir-tools/han-river"
mkdir -p "$VIR_TOOLS"/{cooks,keys,catalogs,releases}
chmod 700 "$VIR_TOOLS" "$VIR_TOOLS/keys"
```

The release tool requires the key file to be named `content-current.pem`, to
be a regular non-symlink file with mode `600` or stricter, and to match the
`content-current` public key embedded in the application and tooling. An
existing approved key is the normal case. For one-time key provisioning only,
the key custodian first confirms no key exists and then creates it:

```sh
test ! -e "$VIR_TOOLS/keys/content-current.pem"
(umask 077; openssl genpkey \
  -algorithm ED25519 \
  -out "$VIR_TOOLS/keys/content-current.pem")
chmod 600 "$VIR_TOOLS/keys/content-current.pem"
```

Do not generate a replacement key as a workaround for a mismatch. Key rotation
requires a separately reviewed client-key change and application rebuild.

On a separate signer, transfer only the three `HanRiver` cook members through
the approved transfer channel. The signer must check out the **same** reviewed
source commit because the packager reads the route beats, notice, and
provenance from its own checkout. Compare the transferred cook hashes with the
ones recorded on the cook host:

```sh
: "${VIR_RELEASE_COMMIT:?Set the reviewed commit recorded on the cook host}"
test "$(git rev-parse HEAD)" = "$VIR_RELEASE_COMMIT"
export VIR_HAN_IOSTORE_DIR="/absolute/path/to/the/transferred/HanRiver-cook"
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.pak"
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.utoc"
test -s "$VIR_HAN_IOSTORE_DIR/HanRiver.ucas"
shasum -a 256 "$VIR_HAN_IOSTORE_DIR"/HanRiver.{pak,utoc,ucas}
```

Set the approved HTTPS origin base in the signer's environment before running
the next block. It is the prefix for the package URL and the location of the
mutable catalog endpoint. It must have no query or fragment. The packager
rejects non-HTTPS URLs, but it cannot know whether the hostname is the intended
release origin:

```sh
: "${VIR_CONTENT_ORIGIN_BASE_URL:?Set the approved HTTPS origin base, ending in /vir}"
export VIR_CONTENT_ORIGIN_BASE_URL="${VIR_CONTENT_ORIGIN_BASE_URL%/}"
export VIR_CONTENT_SIGNING_KEY="$VIR_TOOLS/keys/content-current.pem"
export VIR_CONTENT_OUTPUT_DIR="$VIR_TOOLS/releases"
export VIR_CATALOG_URL="$VIR_CONTENT_ORIGIN_BASE_URL/catalog-current.pb"
test -f "$VIR_CONTENT_SIGNING_KEY"
```

For the first publication, there is no previous catalog and the revision must
be `1`. Confirm the origin really has no current catalog; a network error or
unexpected HTTP status is not evidence of an empty origin:

```sh
VIR_CATALOG_HTTP_STATUS="$(curl --silent --show-error --output /dev/null --write-out '%{http_code}' "$VIR_CATALOG_URL")"
test "$VIR_CATALOG_HTTP_STATUS" = 404
unset VIR_CONTENT_PREVIOUS_CATALOG
export VIR_CONTENT_CATALOG_REVISION=1
```

For every later publication, retrieve the catalog currently served by the
origin to a regular local file. Record its digest and look up the last approved
revision in the release ledger, then set an integer strictly greater than that
revision. The packager verifies the downloaded signature and revision before
signing the new catalog:

```sh
export VIR_CONTENT_PREVIOUS_CATALOG="$VIR_TOOLS/catalogs/catalog-current-$(date +%Y%m%d-%H%M%S).pb"
curl --fail --silent --show-error "$VIR_CATALOG_URL" -o "$VIR_CONTENT_PREVIOUS_CATALOG"
test -s "$VIR_CONTENT_PREVIOUS_CATALOG"
shasum -a 256 "$VIR_CONTENT_PREVIOUS_CATALOG"
: "${VIR_CONTENT_CATALOG_REVISION:?Set a revision higher than the approved previous revision}"
```

Create and inspect the release:

```sh
make content-release-package
export VIR_RELEASE_DIR="/absolute/path/printed/by/content-release-package"
test -s "$VIR_RELEASE_DIR/HanRiver.vircontent"
test -s "$VIR_RELEASE_DIR/release.json"
export VIR_PACKAGE_SHA256="$(shasum -a 256 "$VIR_RELEASE_DIR/HanRiver.vircontent" | awk '{print $1}')"
test "$(basename "$VIR_RELEASE_DIR")" = "$VIR_PACKAGE_SHA256"
export VIR_CATALOG_FILE="$(find "$VIR_RELEASE_DIR" -maxdepth 1 -type f -name 'catalog-*.pb' -print -quit)"
test -s "$VIR_CATALOG_FILE"
export VIR_CATALOG_SHA256="$(shasum -a 256 "$VIR_CATALOG_FILE" | awk '{print $1}')"
test "$(basename "$VIR_CATALOG_FILE")" = "catalog-$VIR_CATALOG_SHA256.pb"
cat "$VIR_RELEASE_DIR/release.json"
```

The packager validates the three real IoStore members, creates canonical route
and inventory metadata, includes the notice, signs a seven-day catalog, enforces
the 100 GiB compressed-content cap, and refuses to overwrite a hash-addressed
release. It performs no upload. The output directory contains:

```text
<package-sha256>/
  HanRiver.vircontent
  catalog-<catalog-sha256>.pb
  release.json
```

Compare the two calculated digests with `package_sha256` and `catalog_sha256`
in `release.json`. Confirm the catalog revision, `content-current` key ID,
semantic version, content set, and `package_url`. The URL must begin with the
approved `VIR_CONTENT_ORIGIN_BASE_URL` and end with
`/$VIR_PACKAGE_SHA256/HanRiver.vircontent`. If a field is wrong, stop and fix
the input; do not edit the signed catalog, release JSON, or archive after
creation. The catalog expires seven days after signing, so sign close to the
planned publication and packaged canary.

## Serve a local origin with Caddy

Caddy is useful for checking the generated URL tree and HTTP transfer behavior
before an origin upload. This optional step can run on a content test host with
copies of the **public** release artifacts; it does not need the signing key.
This is a loopback smoke server, not a production origin and not a way around
the packager's HTTPS requirement.

Stage the release under the exact path encoded by `package_url`. The current
content constants give
`han-river-alpha-1/1.0.1/<package-sha256>/HanRiver.vircontent` below `/vir`.
Use the `VIR_RELEASE_DIR` from the inspection step. If this is another host,
point it at the copied public release directory. Recalculate the names from
the copied bytes:

```sh
set -e
set -o pipefail
: "${VIR_RELEASE_DIR:?Set the reviewed hash-addressed release directory}"
export VIR_LOCAL_DIR="$(mktemp -d "${TMPDIR:-/tmp}/vir-han-origin.XXXXXX")"
export VIR_ORIGIN_ROOT="$VIR_LOCAL_DIR/www"
export VIR_PACKAGE_SHA256="$(shasum -a 256 "$VIR_RELEASE_DIR/HanRiver.vircontent" | awk '{print $1}')"
test "$(basename "$VIR_RELEASE_DIR")" = "$VIR_PACKAGE_SHA256"
export VIR_CATALOG_FILE="$(find "$VIR_RELEASE_DIR" -maxdepth 1 -type f -name 'catalog-*.pb' -print -quit)"
export VIR_CONTENT_SET="han-river-alpha-1"
export VIR_CONTENT_VERSION="1.0.1"
test -s "$VIR_RELEASE_DIR/HanRiver.vircontent"
test -s "$VIR_CATALOG_FILE"
mkdir -p "$VIR_ORIGIN_ROOT/vir/$VIR_CONTENT_SET/$VIR_CONTENT_VERSION/$VIR_PACKAGE_SHA256"
cp "$VIR_RELEASE_DIR/HanRiver.vircontent" \
  "$VIR_ORIGIN_ROOT/vir/$VIR_CONTENT_SET/$VIR_CONTENT_VERSION/$VIR_PACKAGE_SHA256/HanRiver.vircontent"
cp "$VIR_CATALOG_FILE" "$VIR_ORIGIN_ROOT/vir/catalog-current.pb"
```

Create a temporary Caddyfile outside the repository and outside the served
directory:

```sh
export VIR_CADDYFILE="$VIR_LOCAL_DIR/Caddyfile"
cat > "$VIR_CADDYFILE" <<'EOF'
http://127.0.0.1:8080 {
	bind 127.0.0.1
	root * {$VIR_ORIGIN_ROOT}
	file_server
}
EOF
```

Validate and run Caddy in this terminal. `VIR_ORIGIN_ROOT` must remain exported
in Caddy's process environment because `{$VIR_ORIGIN_ROOT}` is substituted
when Caddy reads the Caddyfile (see the [Caddyfile environment-variable
syntax](https://caddyserver.com/docs/caddyfile/concepts#environment-variables)).
Run the HTTP checks below from a second
terminal after exporting the same `VIR_CONTENT_SET`, `VIR_CONTENT_VERSION`,
`VIR_PACKAGE_SHA256`, `VIR_CATALOG_FILE`, and `VIR_ORIGIN_ROOT` values there:

```sh
caddy validate --config "$VIR_CADDYFILE" --adapter caddyfile
caddy run --config "$VIR_CADDYFILE" --adapter caddyfile
```

Check the catalog and the package URL, including a resumable range request:

```sh
set -e
set -o pipefail
export VIR_LOCAL_VERIFY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/vir-han-local-check.XXXXXX")"
curl --fail --silent --show-error \
  http://127.0.0.1:8080/vir/catalog-current.pb \
  -o "$VIR_LOCAL_VERIFY_DIR/catalog.pb"
cmp "$VIR_CATALOG_FILE" "$VIR_LOCAL_VERIFY_DIR/catalog.pb"
curl --fail --silent --show-error --head \
  "http://127.0.0.1:8080/vir/$VIR_CONTENT_SET/$VIR_CONTENT_VERSION/$VIR_PACKAGE_SHA256/HanRiver.vircontent"
curl --fail --silent --show-error \
  -H 'Range: bytes=0-1023' \
  -D "$VIR_LOCAL_VERIFY_DIR/range.headers" \
  "http://127.0.0.1:8080/vir/$VIR_CONTENT_SET/$VIR_CONTENT_VERSION/$VIR_PACKAGE_SHA256/HanRiver.vircontent" \
  -o "$VIR_LOCAL_VERIFY_DIR/range.bin"
rg '^HTTP/|^Accept-Ranges:|^Content-Range:|^Content-Length:' "$VIR_LOCAL_VERIFY_DIR/range.headers"
rg '^HTTP/[^ ]+ 206 ' "$VIR_LOCAL_VERIFY_DIR/range.headers"
test "$(wc -c < "$VIR_LOCAL_VERIFY_DIR/range.bin")" -eq 1024
```

The range check is evidence about the local server only. The application will
use the signed HTTPS package URL, and the packager rejects an `http://` origin
base. Do not point a normal release manifest at this loopback URL. An
end-to-end client test needs a trusted HTTPS development origin whose hostname
and certificate match the signed URL, or the restricted real origin procedure.

## Deploy and verify

Follow [02-content-origin-runbook.md](02-content-origin-runbook.md) for the
restricted-origin publication, withdrawal, and rollback controls. Give the
origin operator the reviewed `release.json`, the two immutable artifacts, their
digests, and the source revision. The key never goes to the origin. On the
operator machine, set the approved base URL and the values just checked in
`release.json`; these commands derive the exact public object URLs:

```sh
set -e
set -o pipefail
: "${VIR_CONTENT_ORIGIN_BASE_URL:?Set the approved HTTPS origin base, ending in /vir}"
export VIR_CONTENT_ORIGIN_BASE_URL="${VIR_CONTENT_ORIGIN_BASE_URL%/}"
: "${VIR_RELEASE_DIR:?Set the reviewed hash-addressed release directory}"
export VIR_PACKAGE_SHA256="$(shasum -a 256 "$VIR_RELEASE_DIR/HanRiver.vircontent" | awk '{print $1}')"
export VIR_CATALOG_FILE="$(find "$VIR_RELEASE_DIR" -maxdepth 1 -type f -name 'catalog-*.pb' -print -quit)"
test -s "$VIR_CATALOG_FILE"
export VIR_CATALOG_SHA256="$(shasum -a 256 "$VIR_CATALOG_FILE" | awk '{print $1}')"
test "$(basename "$VIR_RELEASE_DIR")" = "$VIR_PACKAGE_SHA256"
test "$(basename "$VIR_CATALOG_FILE")" = "catalog-$VIR_CATALOG_SHA256.pb"
export VIR_PACKAGE_URL="$VIR_CONTENT_ORIGIN_BASE_URL/han-river-alpha-1/1.0.1/$VIR_PACKAGE_SHA256/HanRiver.vircontent"
export VIR_IMMUTABLE_CATALOG_URL="$VIR_CONTENT_ORIGIN_BASE_URL/$(basename "$VIR_CATALOG_FILE")"
export VIR_CURRENT_CATALOG_URL="$VIR_CONTENT_ORIGIN_BASE_URL/catalog-current.pb"
cat "$VIR_RELEASE_DIR/release.json"
```

Compare `VIR_PACKAGE_URL` with the JSON `package_url` before transfer. The
origin's immutable object keys are the URL paths after its host name:

| Local file | Public key below the approved `/vir` base |
|---|---|
| `HanRiver.vircontent` | `han-river-alpha-1/1.0.1/<package-sha256>/HanRiver.vircontent` |
| `catalog-<catalog-sha256>.pb` | `catalog-<catalog-sha256>.pb` |

The repository has no provider-specific origin upload command or bucket name.
Use the restricted origin's approved **create-if-absent** upload operation for
these two keys. Stop if either key already exists with different bytes. Keep
the old immutable objects. Do not update `catalog-current.pb` yet.

After upload, fetch the package over the public HTTPS path and hash the actual
response. The `pipefail` setting makes a failed `curl` fail the pipeline instead
of leaving a misleading hash of an empty response. Fetch and compare the
immutable catalog byte for byte:

```sh
VIR_REMOTE_PACKAGE_SHA256="$(curl --fail --silent --show-error "$VIR_PACKAGE_URL" | shasum -a 256 | awk '{print $1}')"
test "$VIR_REMOTE_PACKAGE_SHA256" = "$VIR_PACKAGE_SHA256"
export VIR_VERIFY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/vir-han-verify.XXXXXX")"
export VIR_REMOTE_CATALOG="$VIR_VERIFY_DIR/catalog.pb"
curl --fail --silent --show-error "$VIR_IMMUTABLE_CATALOG_URL" -o "$VIR_REMOTE_CATALOG"
cmp "$VIR_CATALOG_FILE" "$VIR_REMOTE_CATALOG"
curl --fail --silent --show-error \
  --range 0-1023 \
  -D "$VIR_VERIFY_DIR/package-range.headers" \
  "$VIR_PACKAGE_URL" -o "$VIR_VERIFY_DIR/package-range.bin"
rg '^HTTP/[^ ]+ 206 ' "$VIR_VERIFY_DIR/package-range.headers"
test "$(wc -c < "$VIR_VERIFY_DIR/package-range.bin")" -eq 1024
```

The range response confirms that this origin serves a resumable prefix. The
full-response SHA-256 and byte-for-byte catalog comparison are the publication
integrity checks; a `HEAD` response or provider-reported ETag is insufficient.

Build and inspect the unsigned app from the same source revision on the clean
cook host. For an internal Phase 2 build, this is not a Developer ID or
notarization procedure:

```sh
set -e
set -o pipefail
: "${VIR_RELEASE_COMMIT:?Set the reviewed commit recorded for this release}"
test "$(git rev-parse HEAD)" = "$VIR_RELEASE_COMMIT"
make unreal-shipping
make unreal-package-verify
export VIR_APP="$PWD/Build/unreal-shipping/archive/Mac/VirtualRowing-Mac-Shipping.app"
test -d "$VIR_APP"
```

On the reference Mac, launch that packaged app against the **immutable**
catalog URL. Pass the URL through the app's existing `-ContentCatalogUrl`
argument; the signed catalog supplies the package URL. If the app was copied to
the reference Mac, set `VIR_APP` to the copied bundle path there and carry over
the reviewed immutable catalog URL:

```sh
set -e
: "${VIR_APP:?Set the packaged app bundle path on this Mac}"
: "${VIR_IMMUTABLE_CATALOG_URL:?Set the reviewed immutable catalog HTTPS URL}"
open -n -a "$VIR_APP" --args "-ContentCatalogUrl=$VIR_IMMUTABLE_CATALOG_URL"
```

In the app, fetch the catalog, download Han River, quit, relaunch with the same
argument, select Han River while idle, and row to the 5 km endpoint. Confirm
that `L_HanRiver_BlueHour` streams, the Area 01 OSM buildings and water tiles
appear, and Content Licenses/Credits is available. Also check Standard while
offline. An Editor view, local loose-level run, or a successful cook is not
this packaged check.

Only after the remote digests and packaged canary pass, use the restricted
origin's approved conditional/atomic update to make `catalog-current.pb`
contain the new signed catalog bytes. It is the one intentionally mutable
object. Fetch it publicly and compare it to the immutable candidate:

```sh
export VIR_REMOTE_CURRENT="$VIR_VERIFY_DIR/catalog-current.pb"
curl --fail --silent --show-error "$VIR_CURRENT_CATALOG_URL" -o "$VIR_REMOTE_CURRENT"
cmp "$VIR_CATALOG_FILE" "$VIR_REMOTE_CURRENT"
```

Do not publish a catalog pointing at an absent package or replace an immutable
object. Retain the previously approved catalog and package for rollback. A
rollback is a **newer signed catalog revision** pointing to an earlier approved
immutable package; copying an old lower-revision catalog to
`catalog-current.pb` will be rejected by clients. The current packager has no
general command to retarget a catalog to an arbitrary older semantic version.
If that is needed, stop publication and use a separately reviewed rollback
signing procedure; do not edit protobuf bytes or reuse an old signature.

## Failure handling

### `Han content is not mounted`

The Han level can stream only after a signed, installed Han package is
activated and its `HanRiver.pak`/`.utoc`/`.ucas` trio mounts in the current
process. The level line is a downstream symptom. In the app, open **Show
details** and read **Content status**, **installed**, **mounted**, **han
unavailable because**, and the latest **mount** and **boot** events. A fresh
download is recorded as `verified` and intentionally activates only after the
app is fully quit and relaunched. A `failed` record is not active and cannot
make the level appear on further relaunches.

On the reference Mac, first confirm the app is the packaged Shipping build
under test, then launch it against the reviewed immutable catalog. `open -n`
starts a new process; quit any previous instance so the content state is not
confused with an older window:

```sh
set -e
: "${VIR_APP:?Set the packaged Shipping .app path}"
: "${VIR_IMMUTABLE_CATALOG_URL:?Set the reviewed immutable signed catalog HTTPS URL}"
test -d "$VIR_APP"
open -n -a "$VIR_APP" --args "-ContentCatalogUrl=$VIR_IMMUTABLE_CATALOG_URL"
```

If **Content status** says `content.catalog_url_missing`, provide the launch
argument above. If it says `content.catalog_network_failed`, fix the catalog
URL or origin response before retrying. If the catalog is ready and Han has
never been installed, click **Download Han River**, wait for
`content.download_verified_restart_required`, quit the app, and launch it again
with the same argument. On success, Details shows `installed: ... active`,
`mounted: v...`, and the Han River course becomes selectable. Selecting
Standard or viewing the level in Editor does not mount the external package.

If **Content status** says `revision_rollback`, the launch URL serves a catalog
older than the revision this app already accepted. The download button stays
disabled because no valid current offer is available. Relaunch with the
reviewed immutable catalog of that accepted revision or a newer signed
revision; do not clear the local ledger or edit a catalog to lower its number.
After the catalog reports `content.catalog_ready`, finish any active workout
before using Download. A failed installed version can be downloaded again;
it still requires a full quit and relaunch after verification.

If Details reports `content.mount_missing_iostore` or
`content.mount_failed`, preserve the redacted Details text as canary evidence.
The Shipping client now records whether the PakFile platform file was missing
or the mount was refused, including Unreal's IoStore error category and system
error number. Check that the installed release has all three members, that the
signed release hashes match the source cook, and that the app and cook use the
approved UE/Xcode fingerprint. If the app was at fault, rebuild it, download
the still-valid signed version again from the same immutable catalog, quit,
and relaunch. Failed versions are eligible for re-download; a relaunch alone
does not retry them. If the cook or package was at fault, re-cook and publish a
**new semantic version** with a higher signed catalog revision, then repeat
the immutable-catalog packaged canary above. Do not alter `rowing.sqlite3`,
copy a loose `.umap` into the app, rename an old failed trio as a new release,
or promote `catalog-current.pb` while the canary cannot mount and stream Han.
A repaired release is complete only when Details shows `mounted: v...` and the
authored level loads from that mounted package.

For the 2026-09-23 local failure, the existing `1.0.0` and `1.0.1` Han records
are both `failed` with `content.mount_failed`; their trio files are present.
Neither relaunch nor selecting Han can activate those records. Their exact
IoStore refusal category was not captured by the older Shipping build, so
the next packaged canary must use the diagnostic build before publication.

| Failure | Action |
|---|---|
| Source gate reports an untracked or missing asset | Add the reviewed source package to the release revision through Git LFS; fetch it on a clean cook host and rerun the gate. |
| Shipping cook says the `VirtualRowing` game module is missing | Run `make unreal-smoke` and confirm `Binaries/Mac/libUnrealEditor-VirtualRowing.dylib` exists. Rerun `make unreal-shipping` on the approved build host. If the cook still fails, inspect the UAT-built `VirtualRowingEditor.target` receipt and preserve the cook log; do not use an older archive. |
| UAT cannot write Unreal's per-user `XmlConfigCache` | Use the approved build host with its required Unreal user-settings directory writable. This is a host permission failure before packaging; do not treat an earlier archived `.app` as the new build. |
| Cook fails or its trio is incomplete | Preserve logs, correct the source or toolchain, and cook into a new output directory. Do not reuse a partial trio. |
| Key or previous-catalog verification fails | Stop signing and ask the key/release owner to investigate the identity or revision; never generate a replacement key as a shortcut. |
| Uploaded package or catalog differs from the signed release | Stop before promotion and quarantine the candidate under the restricted origin procedure. Do not overwrite its immutable URL or advance the catalog pointer. |
| Packaged canary fails | Leave `catalog-current.pb` unchanged and keep Standard available. Diagnose against the immutable catalog URL. |
| Public current catalog differs after promotion | Stop rollout, preserve both responses and origin logs, and follow the content-origin incident procedure. |

The release-owner evidence must cover catalog fetch, package download, restart,
validation, mount, route selection, the 5 km endpoint, Content Licenses/Credits,
Standard fallback while offline, interrupted download/resume, corrupt or
withdrawn content, rollback, storage refusal, and active-workout operation
blocking. Editor checks, the deterministic canary, the external cook, and the
local Caddy smoke server do not replace packaged Shipping evidence.

Record the source revision, UE/Xcode/macOS/architecture fingerprint, cook
member hashes, package and catalog hashes, catalog revision, origin response
codes, and redacted mount/download outcomes. Never record private keys, tokens,
raw user paths, PM serials, or raw telemetry.
