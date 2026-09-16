# Phase 0 Milestone 3 — Unreal Shipping toolchain spike

Status: In progress  
Owners: A7 build/release, A3 Unreal host, A6 verification; protected release execution: credential owner  
Architecture review: A0  
Integration review: A8

## Purpose and boundary

This milestone proves the pinned UE 5.8.2, Xcode 26.1.1, and macOS Tahoe 26.6.2 stack can build, cook, stage, package, and run an arm64 Shipping application that declares Bluetooth use. It is a bounded diagnostic host, not the Phase 1 walking skeleton.

The host loads the existing `Concept2PM` plug-in solely to prove packaging of the dependency. It does not invoke that plug-in and contains no PM5 discovery, peripheral identifiers, telemetry, gameplay, maps authored by the project, workout logic, persistence, account state, UMG/CommonUI, Sparkle, or product settings. `/Engine/Maps/Entry` is the intentionally empty non-gameplay launch map.

Normal launches do not instantiate CoreBluetooth. Only `-ToolchainBluetoothProbe` creates a CoreBluetooth central on a dedicated serial queue. After a terminal authorization/power state it performs at most one 15-second scan, without reading or recording peripheral information. It records only a schema-versioned result state, duration, UTC timestamp, source revision, and toolchain fingerprint under `Saved/Logs`.

## Deliverables

- `make unreal-shipping` runs `RunUAT.sh BuildCookRun` for Mac arm64 Shipping, cook, stage, package, and archive under Git-ignored `Build/unreal-shipping/`.
- `make unreal-package-verify` validates the staged app itself: main executable and plug-in binary presence, arm64-only Mach-O architecture, exact copied `BuildVersions.json`, and the exact `NSBluetoothAlwaysUsageDescription`.
- `make toolchain-bluetooth-probe` launches only the validated staged app with the explicit probe flag and prints only the created redacted result path.
- `make release-sign-notarize` is a credential-owner-only procedure. It accepts no command-line credential values; it requires preconfigured Developer ID and Keychain-profile names, rejects ad-hoc signing, signs nested code then the app with hardened runtime, rejects `get-task-allow`, creates a DMG, submits/staples through `notarytool`, and performs Gatekeeper assessment.
- The self-hosted Apple-silicon CI lane runs the unsigned commands only and retains redacted provenance/verification output. It has no signing secrets, notary profile, or hardware-in-loop work.

## Acceptance gate

The unsigned portion requires, at one immutable source revision:

1. `make doctor`, native automated tests, formatting, `make unreal-smoke`, `make unreal-shipping`, and `make unreal-package-verify` passing on the pinned reference host.
2. A self-hosted Apple-silicon CI run on `main` with retained redacted provenance and verifier output.
3. Three redacted probe runs after TCC reset: fresh Allow, Deny, and System Settings repair followed by Allow. A normal launch must be observed not to prompt.

The protected release portion remains a separate required execution by the credential owner: Developer ID signing, notarization, stapling, clean standard-user Gatekeeper assessment, hardened-runtime entitlement inspection, and a fresh-Allow probe from the notarized/stapled deliverable. It must record only artifact hashes, notarization ID, entitlement result, source revision, and categorical probe result. It must never put identities, credentials, profiles, serials, user paths, peripheral IDs, payloads, or account data in the repository or report.

Milestone 3 cannot be marked complete until the protected credential-owner execution is recorded. Its completion would still not pass the delivery Phase 0 exit gate.

## Redacted evidence procedure

Run the unsigned commands from a clean immutable revision. Copy only the command name, revision, UTC timestamp, toolchain fingerprint, app/DMG SHA-256, categorical verification result, and generated probe-result path into [the evidence report](04-evidence-report.md). Before sharing a probe result, verify it contains only the six schema fields and one of `authorized_powered_on`, `denied`, `restricted`, `unsupported`, `powered_off`, `timeout`, or `corebluetooth_error`.

The credential owner runs `make release-sign-notarize` with preconfigured Keychain references only. The command intentionally does not print, accept, or persist credentials. Missing credential-owner evidence is recorded as Pending; an ad-hoc signature is not a substitute.
