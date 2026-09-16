# Phase 0 Milestone 3 — Unreal Shipping toolchain spike

Status: In progress  
Owners: A7 build/release, A3 Unreal host, A6 verification  
Architecture review: A0  
Integration review: A8

## Purpose and boundary

This milestone proves the pinned UE 5.8.2, Xcode 26.1.1, and macOS Tahoe 26.6.2 stack can build, cook, stage, package, and run an unsigned, ad-hoc-built arm64 Shipping application that declares Bluetooth use. It is a bounded diagnostic host, not the Phase 1 walking skeleton. Per [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md), Developer ID signing and notarization are not part of this milestone; they become a required gate at Phase 4.

The host loads the existing `Concept2PM` plug-in solely to prove packaging of the dependency. It does not invoke that plug-in and contains no PM5 discovery, peripheral identifiers, telemetry, gameplay, maps authored by the project, workout logic, persistence, account state, UMG/CommonUI, Sparkle, or product settings. `/Engine/Maps/Entry` is the intentionally empty non-gameplay launch map.

Normal launches do not instantiate CoreBluetooth. Only `-ToolchainBluetoothProbe` creates a CoreBluetooth central on a dedicated serial queue. After a terminal authorization/power state it performs at most one 15-second scan, without reading or recording peripheral information. It records only a schema-versioned result state, duration, UTC timestamp, source revision, and toolchain fingerprint under `Saved/Logs`.

## Deliverables

- `make unreal-shipping` runs `RunUAT.sh BuildCookRun` for Mac arm64 Shipping, cook, stage, package, and archive under Git-ignored `Build/unreal-shipping/`.
- `make unreal-package-verify` validates the staged app itself: main executable and plug-in binary presence, arm64-only Mach-O architecture, exact copied `BuildVersions.json`, and the exact `NSBluetoothAlwaysUsageDescription`.
- `make toolchain-bluetooth-probe` launches only the validated staged app with the explicit probe flag and prints only the created redacted result path.
- `make release-sign-notarize` exists in tooling as a credential-owner-only procedure (no command-line credential values; preconfigured Developer ID and Keychain-profile names; rejects ad-hoc signing; signs nested code then the app with hardened runtime; rejects `get-task-allow`; creates a DMG; submits/staples through `notarytool`; performs Gatekeeper assessment), but per [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md) its required execution and evidence are rescheduled to the Phase 4 exit gate, not this milestone.
- The self-hosted Apple-silicon CI lane runs the unsigned commands only and retains redacted provenance/verification output. It has no signing secrets, notary profile, or hardware-in-loop work.

## Acceptance gate

Milestone 3 is complete when the unsigned portion passes, at one immutable source revision:

1. `make doctor`, native automated tests, formatting, `make unreal-smoke`, `make unreal-shipping`, and `make unreal-package-verify` passing on the pinned reference host.
2. A self-hosted Apple-silicon CI run on `main` with retained redacted provenance and verifier output.
3. Three redacted probe runs after TCC reset: fresh Allow, Deny, and System Settings repair followed by Allow. A normal launch must be observed not to prompt.

Milestone 3's completion does not pass the delivery Phase 0 exit gate, which evaluates all Phase 0 milestones together.

Developer ID signing, notarization, stapling, clean standard-user Gatekeeper assessment, hardened-runtime entitlement inspection, and a fresh-Allow probe from the notarized/stapled deliverable are, per [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md), a Phase 4 exit-gate requirement, not a condition of this milestone. When that protected execution happens at Phase 4, it must record only artifact hashes, notarization ID, entitlement result, source revision, and categorical probe result, and must never put identities, credentials, profiles, serials, user paths, peripheral IDs, payloads, or account data in the repository or report.

## Redacted evidence procedure

Run the unsigned commands from a clean immutable revision. Copy only the command name, revision, UTC timestamp, toolchain fingerprint, app/DMG SHA-256, categorical verification result, and generated probe-result path into [the evidence report](04-evidence-report.md). Before sharing a probe result, verify it contains only the six schema fields and one of `authorized_powered_on`, `denied`, `restricted`, `unsupported`, `powered_off`, `timeout`, or `corebluetooth_error`.

`make release-sign-notarize` is deferred to Phase 4 per [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md). Until then, the credential owner does not run it, and the evidence report records that row as Deferred to Phase 4, not Pending.

## Remaining work (2026-09-16)

Verified against the reference host and this repository's live CI:

- `make doctor` passes on this host (macOS 26.6.2, Xcode 26.1.1, cmake 4.3.2, ninja 1.13.0, Git LFS 3.8.0, Unreal 5.8 changelist 56702186/patch 2 all match `Config/BuildVersions.json`).
- Native build/tests and `make unreal-smoke` have local evidence of a prior successful run (`Build/native/Testing/Temporary/LastTest.log`, `Binaries/Mac/UnrealEditor.target`), but this has not yet been re-run and copied into [the evidence report](04-evidence-report.md) at an immutable revision.
- `make unreal-shipping` has never completed on this host — no `Build/unreal-shipping/` output exists — so `make unreal-package-verify` and `make toolchain-bluetooth-probe` cannot run yet either.
- The self-hosted Apple-silicon CI lane (`.github/workflows/unreal-shipping.yml`, `native.yml`) is not picking up jobs: every run on `main`, including the ADR-0008 commit, is stuck `queued` (oldest observed >7 hours). No runner appears registered/online.
- The three TCC-reset Bluetooth probe scenarios (fresh Allow, Deny, Settings-repair-then-Allow) and the normal-launch-no-prompt check have not been executed; all corresponding rows in the evidence report are still Pending.

Owner decisions recorded 2026-09-16:

- The self-hosted Apple-silicon runner is confirmed not set up yet (not merely offline); A7 owns registering it before this milestone's CI row can move off Pending.
- `make unreal-shipping` will be run by the owning engineer outside this session, not delegated here.
- The three TCC probe scenarios and the normal-launch-no-prompt check are scheduled as separate, dedicated follow-on work rather than blocking today's update.

## CI lane registered and green (2026-09-16, later same day)

Runner `vir-m1` (self-hosted, macOS, ARM64) is registered against this repository and running as a launchd service on the reference host, satisfying both `native.yml` and `unreal-shipping.yml`'s label requirements from one machine.

Registering it surfaced two real defects, both now fixed at `7c6449a5761c`:

- `Build/Mac/Resources/Info.Template.plist` (and the sibling `Sandbox.Server.entitlements`/`Sandbox.NoNet.entitlements`) supply the required `NSBluetoothAlwaysUsageDescription` but existed only as untracked files on the reference host. A clean CI checkout packaged an app missing that key; `make unreal-package-verify` correctly failed. Committed the missing files and ignored their generated siblings (`FileOpenOrder/`, `*.PackageVersionCounter`).
- `unreal-shipping.yml`'s "Verify staged package" step piped `make unreal-package-verify` through `tee` without `pipefail`, so the failing exit code above was swallowed and the job reported `success` regardless. Added `set -o pipefail`.
- Separately, `native.yml`'s checkout never fetched the `external/ftxui` git submodule, failing `make configure` on a clean runner; fixed with `submodules: true` on the checkout step (`714dcc1`).
- Added an explicit 60-minute job timeout to `unreal-shipping.yml` (native already had one).

Verified at `7c6449a5761c`: `M1 native quality gates` (https://github.com/pastry-personal5/virtual-indoor-rowing/actions/runs/35106758204) and `Unreal Shipping verifier` (https://github.com/pastry-personal5/virtual-indoor-rowing/actions/runs/35106758284) both ran on `main` and passed for real, including a genuine `OK unsigned Shipping package` from the verify step. This satisfies acceptance-gate item 2 (a self-hosted Apple-silicon CI run on `main` with retained redacted provenance and verifier output).

## Local unsigned toolchain pass, one revision (2026-09-16, owner-directed)

Item 1 is now satisfied. At `d4bad0f2aa97`, on the pinned reference host, in one sitting: `make doctor` (pass), native build/tests (13/13 pass), `make format-check` (clean), `make unreal-smoke` (Result: Succeeded), `make unreal-shipping` (BUILD SUCCESSFUL), and `make unreal-package-verify` (`OK unsigned Shipping package`, `app_sha256=72896216ffa983544040e398a3ea695b5cbddc320fc3228374abc02eb6c15add`) all passed. Recorded in [the evidence report](04-evidence-report.md).

One incidental finding: the Editor/Shipping build steps wrote an `AndroidFileServerEditor` default settings block (with a random per-run `SecurityToken`) into tracked `Config/DefaultEngine.ini`. This plugin has no relevance to this Mac-only project; the write-back was reverted before recording evidence so the revision stayed unmodified. Unaddressed follow-up: this will likely recur on the next from-scratch Editor/Shipping build unless the plugin is explicitly disabled in config — not done here since it wasn't asked for and is outside this milestone's scope.

Remaining for Milestone 3: acceptance-gate item 3 only — the three TCC-reset Bluetooth probe scenarios (fresh Allow, Deny, Settings-repair-then-Allow) and the normal-launch-no-prompt check, all human-only (real permission dialogs).
