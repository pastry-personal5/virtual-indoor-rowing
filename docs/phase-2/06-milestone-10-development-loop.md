# Phase 2 Milestone 10: shorten the development and content-iteration loops

Status: Planned — not started
Owner: Release/platform, client/content
Last reviewed: 2026-09-21

## Purpose

Two loops now dominate daily work on Milestone 2 and slow every later Phase 2
milestone:

1. **Code loop.** Close the Unreal Editor, `make unreal-shipping`, `make
   unreal-package-verify`, reopen the editor. The editor and the Shipping
   BuildCookRun cannot coexist.
2. **Content loop.** Edit the Han level in the editor, `make han-external-cook`,
   `make content-release-package` (signed, revision-bumped), publish to the
   origin, edit the catalog hash in `~/work/vir-tools/pack-source.sh`, launch the
   Shipping app, download, relaunch to activate.

This milestone is developer tooling only. It changes no product behavior, no
trust boundary, and no requirement. It passes no Phase 2 exit gate.

## Fixed constraints

- Shipping/packaged behavior is unchanged. The signed, hash-, size-, and
  path-validated mount path is **not** relaxed or bypassed in any Shipping
  build; any shortcut is compiled out of Shipping (`!UE_BUILD_SHIPPING`).
- The Shipping package and the mounted-package path stay the only evidence for
  packaging, Bluetooth/TCC, SQLite-in-Unreal, and download behavior. A faster loop
  is never evidence for those.
- Only `content-current` signs releases. The dev loop does not add a signing key.
- `make`/`Scripts/dev.py` remain the entry points. Owner-run rule: the agent does
  not run `make unreal-shipping` or launch the packaged app in-session; steps
  below marked **owner-run** are verified by the owner.

## What actually needs each slow step

| Need | Slow path today | Needed for it |
|---|---|---|
| Iterate C++/UI/course logic | Shipping build (native archive, UBT, full cook, stage, package) | Nothing: an uncooked Development build reads loose `Content/` |
| See an edited course level | Cook, package, sign, publish, download, relaunch | Nothing for authoring review: the level is already a loose asset in the project |
| Verify the mount/download/activation path | Same chain | This is the only case that needs the full chain, and it should be one command |
| PM5 hardware/Bluetooth run | Shipping app | Shipping app (unchanged) |

## Track A: code loop

### A0. Find the real editor-versus-shipping conflict (spike, first)

The cause is not recorded in the repository. Known evidence: the Milestone 2
changelog says a re-cook was "blocked in-session by the live editor holding MCP
port 8000", and the cook runs `UnrealEditor-Cmd` on the same project. Suspects,
to confirm one by one on a scratch copy of the command line:

1. The MCP plugin binds port 8000 in the cook commandlet. Fix candidate: disable
   or re-port it for BuildCookRun through a generated ini or `-ExecCmds`, the
   same generated-config pattern `han_external_cook` already uses.
2. Shared `Intermediate/`, `Binaries/`, and `Saved/` (UBT mutex, editor-held
   module dylib, asset registry, `Saved/` locks).
3. Editor Live Coding patching `Binaries/Mac`.

Deliverable: a written cause list and, where the cause is (1) or (2), a change so
`make unreal-shipping` and `make han-external-cook` run with the editor open.
If a cause cannot be removed (for example a genuine module lock), `make
unreal-shipping` fails fast with a clear message instead of a late UAT error.

### A1. `make unreal-dev-run` — uncooked Development run

Build the `VirtualRowing` Development Editor target through UBT (already what
`unreal-smoke` does) and launch it with `-game -windowed -ResX -ResY` against the
project's loose content: no cook, no pak, no stage. Passes the catalog URL and
other dev flags. Runs beside an open editor (`-ABSLOG`, separate log folder, MCP
port not bound by the game process).

Open risk to verify first: whether CoreBluetooth, the ICU startup, and the
SQLite file layer behave in `UnrealEditor.app -game`. The earlier probe defects
were Shipping-specific, so this loop may not reach real PM5 hardware. If it does
not, the loop uses the RowingSim path and the Shipping app remains the hardware
path.

### A2. `make unreal-shipping-fast` — code-only repackage

For the cases that need Shipping: reuse the previous cook and staged content when
no `Content/` or config changed (`-skipcook`/`-iterate`, `-skipstage` where
sound), rebuild only the Shipping module, and re-copy the embedded `.uproject`
and `BuildVersions.json` exactly as `unreal_shipping()` does. `make
unreal-shipping` remains the clean, full command; `-fast` refuses when it
detects changed content and says so. It also keeps `unreal-package-verify` as its
last step so a fast package still passes the same verifier.

### A3. Fold the two shell scripts into `dev.py`

`quick-build.sh` and `quick-run.sh` are ad-hoc root-level helpers outside the
`make`/`dev.py` entry points (`quick-run.sh` sources
`~/work/vir-tools/pack-source.sh` and hard-codes a window size). Replace them with `make unreal-shipping-fast && make
unreal-shipping-run`, where `unreal-shipping-run` reads the catalog URL from the
dev origin state (B2) instead of a hand-edited hash.

## Track B: content loop

### B1. Loose-level dev mode (no cook, no package, no download)

In non-Shipping builds only, add a `-CourseLevelSource=Loose` switch:
`UCourseSubsystem::BeginAuthoredLevelLoad` streams the same fixed
`/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour` from the project's loose
content and treats Han as available, skipping `IsHanContentMounted()`. The
route-ID-to-level table, the actor and component allowlist check, and the
fallback-kit-on-rejection behavior are unchanged, so the level is still
validated exactly as in production. Add `vir.ReloadCourse` (console command, dev
builds) that unloads and re-streams the level, so a saved editor change appears
without restarting the app. Combined with A1, the content edit loop is:
save in the editor → `vir.ReloadCourse`.

This makes the whole of Milestone 2 step 5 (production art) reviewable without
any packaging. It does not test that the cooked level loads from the mount; B2
does.

Test: extend `CoursePresentationSpec` for the loose source (allowlist still
rejects a bad actor; reload restores the kit on failure) and add a
`ContentRuntime` test that the Shipping table is not affected by the dev switch.

### B2. `make han-dev-publish` — one command for the mounted-package path

For when the mount path itself must be exercised:

1. `han-external-cook` (with A0 and a `-map`-scoped, `-skipbuild` cook option
   measured for speed);
2. `content-release-package` with the revision derived from the previous local
   catalog automatically and the prior catalog path kept in a fixed dev folder
   (`~/work/vir-tools/dev-origin/`), rather than exported by hand;
3. copy `HanRiver.vircontent` and `catalog-<sha>.pb` into that folder, verify
   the digest, and serve it on `https://localhost/vir` (or a documented local
   TLS server);
4. write the new catalog URL to `dev-origin/current.url`, which
   `unreal-shipping-run` and `unreal-dev-run` read.

The runbook's rules (never overwrite a published object, digest before catalog,
strictly increasing revision) stay in force; this only automates them for the
local dev origin. The app's existing "update available" Content-page flow then
does the download, and activation is the existing next-launch step, so the
packaged loop becomes: `make han-dev-publish` → relaunch app → Download → relaunch.
A `-ContentAutoDownload` non-Shipping flag can remove the click if it proves
worth it. Requires the `content-current` private key already under
`~/work/vir-tools`; the key is never read by any new code path beyond what
`content-release-package` already does.

## Work order

1. **Baseline (owner-run for Shipping steps):** time today's code loop and
   content loop end to end, per step, and record them in the evidence record.
   Without this the milestone has no gate.
2. **A0 spike.** Cause of the editor conflict; fix or fail-fast.
3. **B1 loose-level mode + `vir.ReloadCourse`.** Highest value, lowest risk,
   compile-only in Development.
4. **A1 `unreal-dev-run`,** including the Bluetooth/SQLite/ICU viability check.
5. **B2 `han-dev-publish`,** then A3 replacing the quick scripts.
6. **A2 `unreal-shipping-fast`** last: the riskiest to get subtly stale, so it
   keeps a full-build fallback and a refuse-on-change guard.

## Gate

- Baseline and after timings recorded per step on the reference Mac.
- Code loop: an editor-open developer can run a C++ change in the game without
  closing the editor and without a Shipping build (A1). Shipping build no longer
  requires closing the editor, or fails fast with the cause (A0).
- Content loop: a saved level edit is visible in a running Development game via
  `vir.ReloadCourse` with no cook (B1), and the mounted-package path is one
  command plus one relaunch (B2). Owner-run: the Shipping mount path still
  passes after these changes, and `make unreal-package-verify` still passes.
- No Shipping code path gains a bypass: a test asserts the dev switches are
  absent (compiled out) in Shipping and the route table and allowlist are
  untouched.
- `make build && make test`, `make format-check`, `make unreal-smoke`, and
  `Scripts/test_unreal_packaging.py` coverage for every new command pass.
- Timing targets are proposed after the baseline and accepted by the owner at
  close; none is asserted in advance.

## Out of scope

Live Coding for Shipping, remote/CI build caches (UBA is deliberately off),
changing the release packaging or signing procedure, any relaxation of content
validation, and PM5 hardware evidence.

## Open decisions for the owner

1. A1 assumes the Development game can stand in for Shipping for everything but
   hardware/Bluetooth. If the owner also wants PM5 hardware in the fast loop,
   A1's viability check decides whether that needs a signed dev bundle.
2. Whether the dev origin serves from a local HTTPS server or the existing remote
   origin (B2 assumes local, to avoid publishing throwaway revisions).
