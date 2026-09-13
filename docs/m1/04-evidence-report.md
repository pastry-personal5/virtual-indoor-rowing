# Milestone 1 Phase 1 evidence report

Status: Implementation in progress; hardware acceptance evidence pending
Owner: A7 for toolchain evidence, A1 for HIL evidence, A6 for automated evidence  
Architecture review: A0  
Integration review: A8  
Last reviewed: 2026-09-14

## Evidence handling

This document records redacted facts and aggregate results. It must not contain:

- Mac serial number, hardware UUID, provisioning identifier, or user/account paths;
- PM serial number or raw CoreBluetooth peripheral identifier;
- raw BLE notification payloads or normalized telemetry streams;
- signing identities, tokens, credentials, or private artifact URLs.

Command output must be filtered to an explicit allow-list before it is copied here.

The local, Git-ignored `Metrics/pm5-tui/` directory is a user-authorized diagnostic
input for AI-assisted analysis. It holds one timestamped JSONL file per run with
normalized telemetry only; its contents are never copied into this report or a
support artifact without separate review.

Each evidence row must identify its source revision, runner or hardware class, timestamp/timezone, and measurement method where a count, percentile, loss rate, or latency is claimed. Missing evidence is recorded as `Pending`, not inferred from a successful connection or a simulator result.

## Current workstation baseline

Observed on 2026-09-13:

| Item | Required | Observed | Status |
|---|---|---|---|
| Host architecture | Apple silicon `arm64` | `arm64` | Matches |
| Reference computer | MacBook Pro, M5 Max, 128 GB | Matches reference class | Matches |
| macOS | Tahoe 26.6.2 or later | 26.6.2 | Matches |
| Xcode | 26.1.1 | 26.1.1, build 17B100 | Approved baseline |
| Active developer directory | Accepted Xcode developer directory | Repository-local `DEVELOPER_DIR` selects Xcode 26.1.1 | Matches |
| Apple clang | Apple clang 17.0.0 (`clang-1700.4.4.1`) | Apple clang 17.0.0 (`clang-1700.4.4.1`) | Matches |
| CMake | 4.3.2 | 4.3.2 installed and native configure passed | Matches |
| Ninja | 1.13.0 | 1.13.0 installed and native configure passed | Matches |
| Git LFS | 3.8.0 installed and initialized | 3.8.0; clean/smudge/process filters and executable pre-push hook are configured | Matches |
| Unreal Engine | Stock 5.8.2, changelist 56702186, approved `Build.version` digest | 5.8.2, changelist 56702186; digest `ff99fc3dd98e7c7fd2f5700334bc792dfb7baced3828cdee32940bb69581a6a4` | Approved baseline; smoke blocked by sandbox |

The selected developer directory is the pinned Xcode 26.1.1; the host's other installed Xcode version is not used for M1 builds. ADR-0001 remains authoritative until a compatibility change is approved.

## Toolchain evidence template

Complete after the repository toolchain commands exist.

| Check | Build/source revision | Result | Evidence location or note |
|---|---|---|---|
| `make doctor` | `144d1c3f4e80-dirty` | Pass | 2026-09-13 KST; pinned workstation checks passed, including Xcode/SDK/clang, CMake/Ninja, Git LFS version, and Unreal fingerprint. Command output not archived separately. |
| Native configure | `144d1c3f4e80-dirty` | Pass | 2026-09-13 KST; arm64 Debug CMake/Ninja configuration. |
| Native arm64 build | `144d1c3f4e80-dirty` | Pass | 2026-09-13 KST; no outstanding compile work in the configured tree. |
| Native unit/contract tests | `144d1c3f4e80-dirty` | Pass, 11/11 | 2026-09-13 KST; `make test` after the profile-generator compile check and TUI nullable-metric smoke additions. Includes core, protocol, simulator contract/integration, TUI logger and command smoke tests. |
| Simulator integration tests | `144d1c3f4e80-dirty` | Pass | 2026-09-13 KST; deterministic replay integration suite is included in CTest. |
| Empty Unreal Editor target | `144d1c3f4e80-dirty` | Blocked in sandbox | UnrealBuildTool started but could not create external shared-memory/configuration files. Rerun on the provisioned host; no compile result established. |
| Empty Unreal application target | Pending | Pending | No separate application target is defined in the current smoke command. |
| Bluetooth usage-description inspection | `144d1c3f4e80-dirty` | Present in built bundle | 2026-09-13 KST; generated `pm5-tui.app/Contents/Info.plist` contains `NSBluetoothAlwaysUsageDescription`. |
| Development signature/TCC identity | `144d1c3f4e80-dirty` | Ad-hoc signature verifies; TCC identity pending | 2026-09-13 KST; `codesign --verify --deep --strict` passes for the built diagnostic bundle. Permission approval/denial/retry HIL cases remain pending. |

For any numerical claim, add the requested/observed telemetry rate, observation duration, sample count, exclusions, and calculation method in the note. Do not compare results from different profiles or toolchain fingerprints as if they were one run.

Record after installation:

- Xcode exact version/build: 26.1.1 / 17B100.
- macOS SDK exact version/build: 26.1.
- clang exact version: Apple clang version 17.0.0 (clang-1700.4.4.1).
- Unreal exact version/changelist/fingerprint: 5.8.2 / 56702186 / `ff99fc3dd98e7c7fd2f5700334bc792dfb7baced3828cdee32940bb69581a6a4`.
- CMake/Ninja/Git LFS versions: 4.3.2 / 1.13.0 / 3.8.0; LFS filters and pre-push hook verified locally.
- `Config/BuildVersions.json` SHA-256: `1b205da82c82351d4ed21e00fae566344e3f4ff2d5265be8d9580dc668ac338a`.

Published PM5 protocol source pinned for the first profile review:

- URL: `https://cms.concept2.com/sites/default/files/2026-03/Concept2%20PM%20CSAFE%20Communication%20Definition.pdf`
- Accessed: 2026-09-13 KST.
- Document: Concept2 PM CSAFE Communication Definition, revision 0.34, dated 2025-07-17.
- SHA-256: `7ee2513c1084082092f3cfb8c9437682912535c549a7b948e5aba5321d562e48`.
- Protocol review: BLE GATT table, pages 17–18, specifies `0x0031` General Status as a 19-byte notification and `0x0032` Additional Status 1 as a 17-byte notification. The separate multiplexed-information section's different `0x0032` length is not the BLE GATT layout and is not used by this decoder.

The digest is copied into the exact development capability profile. The profile permits
the documented `0x0031` and `0x0032` notification layouts and conditionally enables
the documented `0x0034` 100 ms request only after CoreBluetooth confirms its `read` and
`write` properties. Downloading the published specification does not replace the
required hardware acceptance evidence.

## Exploratory PM5 diagnostic observation (not acceptance evidence)

The local rotating log at `Logs/pm5-tui/pm5-tui.log` contains a session on 2026-09-13 from 03:54:52Z to 03:56:04Z. It records explicit candidate selection, an identity read, transition to `DIAGNOSTIC ONLY (unverified)`, 36 `DiagnosticSampleObserved` events between 03:55:25Z and 03:56:00Z, and a deliberate disconnect. The sample count and interval are obtained by counting those redacted event timestamps; the log does not store sample values or raw BLE data. The user-provided terminal transcript identifies the observed tuple as PM5, hardware `634`, firmware `8200-000372-178.067`, capability `2`.

Two subsequent launches at 05:21:18Z and 05:21:33Z only issue `status` and stop; neither scans nor reconnects. They do not establish a relaunch-reconnect feature (which is Phase 2 scope). The current local log has 73 lines and SHA-256 `e93e9759ea17e441175b285fe9a08bc77f944a894f0ab140a75ebbf6c44e8cba`; it is ignored by Git and remains a workstation artifact, not a checked-in release artifact. Those existing records predate build-revision and identity-tuple logging, so they are not source-stamped.

This is evidence of a short passive notification/decoder path only. The log does not prove changing metric values, record characteristic notification counts or a requested/observed 100 ms rate, or cover stale/reconnect behavior. The profile was explicitly unverified and never entered `Ready`. It does not complete any real-hardware acceptance case or replace the required 6-minute run.

A later short observation on 2026-09-13 KST used the reviewed `Allowed` profile
and source revision `144d1c3f4e80-dirty`. It recorded one redacted PM identity,
256 normalized sample events, 148 late corrections, and one non-fatal
`ScanTimeout` during approximately 32 seconds of telemetry. The source values
advanced while rowing, then the PM continued to notify a stopped-row status;
those later records were explicitly marked `Duplicate` and, where applicable,
`MissingField`/`LateCorrection`. No `TelemetryStale` event is expected from this
observation because required general-status notifications continued to arrive.
This establishes a reproducible stopped-row merger case, not a measurement-rate,
reconnect, or clean-shutdown acceptance result. The ignored local log is not a
checked-in artifact, and no raw identifier, packet payload, or telemetry stream
is recorded here.

The later AI-readable per-run metrics capture
`Metrics/pm5-tui/pm5-tui-2026-09-13T14:42:36.662Z-357534315766333.jsonl`
records a 46.049-second run (UTC start/stop timestamps) with 265 sample events
and 103 correction events over about 33.3 seconds of telemetry. Source time and
distance advanced to 20.19 s and 51.5 m by sequence 180. The next 86 samples,
through sequence 265 over 10.816 seconds, repeated those source values while
the PM reported `workout_state=Active`, `rowing_state=Inactive`, and
`stroke_state=Waiting`; their quality flags include `Duplicate`, and most also
include `MissingField`. The matching TUI stop summary reports 102 duplicate
samples, 113 missing-field samples, zero stale events, and one fault. UTC record
timestamps are monotonic, and the file is owner-only (`0600`). This is a short
diagnostic capture that confirms the stopped-row behavior; it does not meet the
six-minute run or other hardware scenario gates. The raw per-run file remains a
local, Git-ignored diagnostic input and is not embedded in this report.

## Six-minute real-rowing run (performed; acceptance incomplete)

The user confirmed that the 2026-09-13 run was a real six-minute rowing effort.
The redacted local metrics file
`Metrics/pm5-tui/pm5-tui-2026-09-13T14:48:50.043Z-357907713663041.jsonl`
records the reviewed PM5/634/8200-000372-178.067/IndoorRower tuple at
`Ready`, followed by normalized telemetry and a PM workout transition to
`Complete`. PM source time reached 399.46 s (6:39.46) and distance reached
1,010,000 mm (1,010 m). The TUI process ran from 14:48:50.043Z through
14:58:14.039Z; samples were recorded from 14:49:03.096Z through 14:58:13.929Z.
The complete JSONL is owner-only and Git-ignored; it is retained locally rather
than copied into this report.

The matching rotating logs record `Ready` at 14:49:02.985Z, a disconnect while
the PM was still waiting to begin at 14:49:31.050Z, and return to `Ready` with
the same redacted identity at 14:49:35.440Z. Thus this run includes a successful
same-device reconnect before rowing, but it does not replace the deliberate
link-loss/reconnect scenario. The TUI stop summary reports 4,355 samples,
1,160 corrections, 1,159 duplicate samples, 1 source-gap sample, 1,304 samples
with missing fields, zero stale events, and two faults (`ScanTimeout` and
`Disconnected`). The sole time and distance regression flags occur at sequence
4230 when the completed workout transitions back to `WaitingToBegin` and both
PM values reset to zero; that reset is state-associated rather than an
unexplained in-workout regression. The same end-of-workout tail contains many
unchanged stopped-row samples, explaining the duplicate/missing-field totals.

This is evidence that a real workout was completed and normalized telemetry
was captured; it is not yet a Phase 1 acceptance pass. The capture does not
provide per-characteristic notification counts, a verified requested/observed
100 ms period or cadence distribution, gap-duration distribution, queue
high-water/overflow values, or parser-error categorization. The stop summary
also cannot establish all of those omitted measurements. No crash is recorded,
but missing queue and parser evidence cannot be treated as proof of zero
overflow or parser overread. The acceptance result therefore remains
incomplete pending a run with complete aggregate instrumentation and the other
hardware cases.

## PM5 identity and capability review template

Do not enter a serial number or peripheral identifier.

| Field | Redacted evidence |
|---|---|
| Test date/timezone | 2026-09-13 KST; prior short, passive diagnostic observation |
| App/source revision | `144d1c3f4e80-dirty`; profile authored after user-supplied tuple confirmation |
| PM monitor model | PM5 |
| Hardware revision | `634` |
| Firmware revision | `8200-000372-178.067` (user supplied as `178.067`; full identity value is recorded in the redacted diagnostic observation) |
| Connected erg machine kind | IndoorRower |
| Required characteristic properties | `0x0031` notify; `0x0032` notify, per the reviewed published BLE GATT table. Direct property-advertisement evidence remains pending. |
| Optional characteristics observed | `0x0034` is declared as `read`/`write`; the adapter fails closed if CoreBluetooth does not advertise both properties. Direct property outcome is pending the next HIL run. |
| Approved packet lengths | `0x0031`: 19 bytes; `0x0032`: 17 bytes, per the reviewed published BLE GATT table. |
| Requested/observed sample rate | Prior profile: approximately 1 s between samples, causing false 500 ms stale faults because `0x0034` was absent. Version 2 requests 100 ms through `0x0034`; observed cadence is pending. |
| Capability-profile version/digest | Version 2; `PM5Capabilities.json` SHA-256 `60b1ac8831daad96f1c831f2db6fd0a9f39a2eb350f6881fa90bf26d09a4b4a1` |
| Reviewers | User-directed initial development profile; formal A1/A0 sign-off pending. |
| Support decision and rationale | `Allowed` only for the exact PM5/634/8200-000372-178.067/IndoorRower tuple and the two reviewed status streams. Hardware acceptance remains pending. |

Identity discovery alone does not authorize telemetry readiness. The profile still requires
the exact identity, advertised required properties, subscriptions, and structurally valid
first notifications before `Ready`.

## Hardware scenario results

| Scenario | Result | Aggregate evidence/notes |
|---|---|---|
| Fresh permission approval | Pending | |
| Permission denial | Pending | |
| Settings repair and retry | Pending | |
| Explicit scan and target selection | Pending | |
| Multiple-candidate selection | Pending | |
| Identity/capability handshake | Observed; formal profile review pending | Redacted tuple PM5/634/8200-000372-178.067/IndoorRower; application logged `Allowed` and entered `Ready`. Direct characteristic-property evidence and reviewer sign-off remain pending. |
| Required notification readiness | Observed | Application entered `Ready` after subscribing; full notification counts remain unavailable. |
| Optional/sentinel rendering | Pending | |
| 500 ms stale transition | Pending | |
| 1.5 s blocking warning | Pending | |
| Deliberate disconnect | Pending | |
| Same-device reconnect and identity recheck | Incidental observation only | Link loss while waiting to begin at 14:49:31.050Z; same redacted identity returned to `Ready` at 14:49:35.440Z. Deliberate HIL case remains pending. |
| Wrong-device appearance during reconnect | Pending | |
| Clean disconnect and application shutdown | Pending | |

## Six-minute run template

- Capture wall duration: 563.996 s (TUI run start to stop); PM workout elapsed: 399.46 s, reaching `Complete`.
- PM capability profile: Version 2; reviewed tuple PM5/634/8200-000372-178.067/IndoorRower; app logged `Allowed` and `Ready`.
- Requested telemetry period: 100 ms is declared by profile; whether `0x0034` was advertised/configured in this run is not evidenced in the captured logs.
- Normalized sample/correction counts: 4,355 / 1,160.
- Notification count by characteristic: Not recorded.
- Observed cadence percentiles: Not recorded; do not infer characteristic cadence from merged sample timestamps.
- Gap count and longest gap: One `SourceGap`-flagged sample; duration not recorded.
- Stale transitions: 0; faults: 2 (`ScanTimeout`, `Disconnected`).
- Reconnect attempts/outcomes: One same-device reconnect before rowing; Ready restored in about 4.4 s. This was not a deliberate link-loss test.
- Parser errors by category: Not recorded.
- Queue high-water marks/overflows: Not present in this run's stop summary; cannot claim zero overflow.
- Duplicate/missing samples: 1,159 / 1,304.
- Time/distance regressions: One each, both on sequence 4230 at the reset from completed workout values (399.46 s, 1,010 m) to zero in `WaitingToBegin`; no in-workout regression observed in this capture.
- Process crashes or sanitizer findings: No crash recorded; sanitizer evidence not collected.
- Final result: Real six-minute rowing run performed and PM workout reached `Complete`; hardware acceptance remains incomplete because required aggregate measurements and other HIL cases are missing.

The report contains aggregate counters only. A failure requiring packet-level diagnosis must be reproduced with a reviewed synthetic/published-spec fixture, or raw capture must receive separate explicit approval and privacy handling before use.

## Automated evidence template

| Suite | Seed/fixture version | Result | Notes |
|---|---|---|---|
| Core unit and conversion tests | Checked-in native fixtures | Pass | 2026-09-13 KST, current dirty workspace; fixed-scale conversions, state handling, quality accumulation, regression, gap, reconnect, and redacted formatting checks pass. |
| Public-contract dependency checks | Checked-in header allow-list | Pass | 2026-09-13 KST; public headers remain free of platform, PM5, terminal, and Unreal implementation dependencies. |
| Concept2 parser corpus | Checked-in published-layout fixtures | Pass | 2026-09-13 KST; approved status/stroke layouts and exact fixed-scale fields pass. |
| Truncation/sentinel/unknown tests | Checked-in published-layout fixtures | Pass | 2026-09-13 KST; every byte-truncation boundary, approved-length rejection, heart-rate sentinel, extrema, and unknown enums pass. |
| Merge callback-order tests | Checked-in synthetic facts | Pass | 2026-09-13 KST; callback ordering, join-window boundary, late correction, and stopped-source duplicate behavior pass. |
| Discovery/session state tests | `pm5-sim` deterministic scenarios | Pass | 2026-09-13 KST; permission, scan, selection, malformed/unsupported identity, stale recovery, and same-identity reconnect scenarios pass. |
| Simulator deterministic replay | `pm5-sim` golden replay | Pass | 2026-09-13 KST; repeated replay produces the same ordered events and digest. |
| Queue overflow/shutdown tests | `pm5-sim` bounded queues | Pass | 2026-09-13 KST; overflow and shutdown are explicit and diagnostics are preserved. |
| Non-interactive TUI smoke | Scripted synthetic events | Pass | 2026-09-13 KST; unavailable values, identity redaction, telemetry/correction, transition, stale, and aggregate stop logging pass. |

## Open blockers and risks

| Item | Owner | Resolution required |
|---|---|---|
| Unreal smoke was blocked before compilation by sandbox writes outside the workspace | A7 | Rerun `make unreal-smoke` on the provisioned host; do not treat UBT startup as a pass |
| The initial development profile has no direct HIL property/rate evidence | A1/A0 | Rerun the exact tuple with profile version 2. Record `0x0034` property/configuration outcome, notification counts/cadence, and a formal support review. |
| HIL evidence lacks per-characteristic counters, queue summaries, a clean-stop record, and a six-minute acceptance run | A1/A6 | Add redacted aggregate/provenance output and complete the required hardware scenarios without storing raw identifiers or telemetry |
| Real-adapter `Allowed`, stale, reconnect, bounded-queue, and event-timestamp paths need implementation/verification | A1/A0 | Implement against the accepted contract and add deterministic tests before HIL acceptance |
| TUI required status display and protocol-boundary conformance need review | A1/A8 | Complete the TUI contract fields and ensure it receives no PM characteristic/profile implementation details |
| Development permission/TCC outcomes are not recorded | A1/A7 | Complete fresh approval, denial, repair, and retry cases; the app bundle identifier is `dev.virtualrowing.pm5-diagnostic` and the bundle is currently ad-hoc signed |
| CI runner results are not recorded | A7 | Run the checked-in native workflow on its provisioned Apple-silicon self-hosted runner and retain the redacted result |

## Exit decision

| Decision | Date | Evidence reviewed | Approvers | Rationale |
|---|---|---|---|---|
| Not ready — documentation baseline only | 2026-09-13 | This document; no implementation evidence | A0 | Toolchain and capability evidence are pending. |

Use `Ready`, `Not ready`, or `Blocked` only. `Ready` authorizes the next milestone decision; it does not claim completion of the broader product Phase 0 or Phase 1 gates.

## Sign-off

- A1 device evidence: Pending.
- A2 core contract implementation: Pending.
- A6 deterministic test evidence: Pending.
- A7 toolchain/CI evidence: Pending.
- A0 architecture and capability approval: Pending.
- A8 integration recommendation: Pending.

Current milestone decision: **Not ready — documentation baseline only.**
