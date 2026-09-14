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
| `make doctor` | `dbe71f4f4904-dirty` | Pass | 2026-09-14 KST; macOS 26.6.2 arm64, Xcode 26.1.1, SDK/clang, CMake/Ninja, Git LFS 3.8.0, and approved Unreal 5.8.2 fingerprint all matched. |
| Native configure | `dbe71f4f4904-dirty` | Pass | 2026-09-14 KST; arm64 Debug CMake/Ninja configuration via `make configure`. |
| Native arm64 build | `dbe71f4f4904-dirty` | Pass | 2026-09-14 KST; `make build`; modified TUI and metrics writer compiled and app bundle was ad-hoc signed. |
| Native unit/contract tests | `dbe71f4f4904-dirty` | Pass, 12/12 | 2026-09-14 KST; `make test` covers timestamped per-run aggregates, queue diagnostics, PM5 characteristic properties/counts/cadence histograms, parser categories, reconnect gaps, and percentile serialization. |
| Simulator integration tests | `dbe71f4f4904-dirty` | Pass | 2026-09-14 KST; deterministic replay integration suite passed under CTest. |
| C++ formatting and whitespace | `dbe71f4f4904-dirty` | Pass | 2026-09-14 KST; `make format-check` and `git diff --check`. |
| Empty Unreal Editor target | `dbe71f4f4904-dirty` | Blocked by sandbox | 2026-09-14 KST; UBT launched, then could not create UBA shared memory in `/tmp` or user config under Application Support. No compile result established. |
| Native GitHub Actions workflow | `dbe71f4f4904-dirty` | Pending | 2026-09-14 KST; public Actions API returned zero runs. The local workflow is untracked and the dirty source changes are not on `origin/main` (`dbe71f4f4904`); no remote CI result exists for this worktree. |
| Bluetooth usage-description inspection | `dbe71f4f4904-dirty` | Present in built bundle | 2026-09-14 KST; generated `pm5-tui.app/Contents/Info.plist` contains `NSBluetoothAlwaysUsageDescription`. |
| Development signature/TCC identity | `dbe71f4f4904-dirty` | Ad-hoc signature verifies; TCC cases pending | 2026-09-14 KST; `codesign --verify --deep --strict` passes for the built diagnostic bundle. Fresh approval/denial/settings-repair HIL cases remain pending. |

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
- Protocol review: BLE GATT table, printed pages 17–18 and 21–22, specifies `0x0031`/`0x0032` as 19/17-byte status notifications and `0x0035`/`0x0036` as 20/18-byte stroke notifications. The separate multiplexed-information section gives conflicting `0x0035`/`0x0036` lengths and is not the BLE GATT layout used by this decoder.

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
overflow or parser overread. The adapter now records these measurements for
future runs, but a repeat HIL capture is still required; this historical file is
not backfilled.

## Subsequent no-data launch

The next recorded launch, source revision `dbe71f4f4904-dirty`, ran from
2026-09-13T17:22:55.936Z to 17:23:00.545Z (2026-09-14 KST). The TUI entered
`Reconnecting` but was stopped before telemetry arrived. Its per-run metrics
file records zero samples, corrections, stale events, and faults, with null
queue summaries. This short launch neither contradicts the completed workout
capture nor establishes reconnect failure; it is not a hardware acceptance
case. The local metrics/log files remain Git-ignored.

## Profile-v3 HIL parser failure (investigation; acceptance still pending)

The 2026-09-14 01:39:27Z diagnostic launch used source revision
`dbe71f4f4904-dirty`. The exact PM5/634/8200-000372-178.067/IndoorRower tuple
was logged `Allowed` and entered `Ready` at 01:39:43.867Z. At 01:40:12.263Z,
the TUI logged a zero-valued stroke-metrics event followed by
`InvalidPacketLength` and a transition from `Ready` to `Unsupported`. The old
log omitted the offending characteristic ID and packet length, so it cannot
prove which stream or exact layout caused the rejection.

The code path exposed two defects in the new optional stroke streams: profile
v3 allowed only an 18-byte `0x0036` packet, while an [older official Concept2
BLE interface definition](https://www.concept2.nl/files/pdf/us/monitors/PM5_BluetoothSmartInterfaceDefinition.pdf)
documents a 15-byte form; and a parse failure on any
optional stream was treated as terminal for the whole device. The pinned
revision 0.34 reference uses the 18-byte form. The decoder has test coverage for
both published exact layouts and leaves the extra projected-work field absent
for the 15-byte form, but the active `Allowed` profile remains pinned to the
revision 0.34 18-byte layout. An unapproved 15-byte optional notification is
counted and reported with characteristic/length metadata without disabling the
required `0x0031`/`0x0032` status streams. This is a code-level correction, not
proof that the observed HIL packet was `0x0036`/15 bytes. Repeat HIL with profile
v4 and inspect the parser diagnostics before changing hardware acceptance.

## Latest HIL availability check (not a device test)

The 2026-09-14 KST interactive `make hil-pm5` launch used source revision
`dbe71f4f4904-dirty`. An explicit scan produced the categorical `Permission`
fault with TUI text “Bluetooth is unavailable on this Mac”; it found no
candidates and recorded zero samples, zero reconnects, zero stroke-metric
records, and one fault. The 48.013-second run was stopped cleanly. A read-only
`system_profiler SPBluetoothDataType -detailLevel mini` check returned
`controllerInfo == nil` and no controller details in this execution
environment. This is not evidence of a denied TCC prompt or a PM5 problem; the
host did not expose a Bluetooth controller to this run. The owner-only metrics
file remains local at
`Metrics/pm5-tui/pm5-tui-2026-09-13T18:39:41.604Z-371757248568416.jsonl`.

Run the HIL command on the interactive reference Mac with Bluetooth hardware
available and permission granted before attempting the profile-v4 device
cases. No PM5 was selected and no rowing occurred in this availability check.

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
| Optional characteristics declared | `0x0034` read/write, `0x0035` notify, and `0x0036` notify. The adapter continues without optional sources that are absent or fail subscription. Direct property outcomes remain pending the next HIL run. |
| Approved packet lengths | `0x0031`: 19 bytes; `0x0032`: 17 bytes; optional `0x0035`: 20 bytes and `0x0036`: 18 bytes, per the pinned revision 0.34 BLE GATT table. |
| Requested/observed sample rate | Prior profile: approximately 1 s between samples, causing false 500 ms stale faults because `0x0034` was absent. Version 2 requests 100 ms through `0x0034`; observed cadence is pending. |
| Capability-profile version/digest | Version 4; `PM5Capabilities.json` SHA-256 `e864a0eec0dd495126a449d59654361470b453590bc256c89a2e309e86bef4f1` |
| Reviewers | User approved the exact development profile on 2026-09-14; role-based HIL acceptance and milestone sign-off remain pending. |
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
| Core unit and conversion tests | Checked-in native fixtures | Pass | 2026-09-14 KST; `make test`; fixed-scale conversion, state, quality, regression, gap, reconnect, and formatting tests pass. |
| Public-contract dependency checks | Checked-in header allow-list | Pass | 2026-09-14 KST; public headers remain free of platform, PM5, terminal, and Unreal implementation dependencies. |
| Concept2 parser corpus | Checked-in published-layout fixtures | Pass | 2026-09-14 KST; approved status/stroke layouts and exact fixed-scale fields pass. |
| Truncation/sentinel/unknown tests | Checked-in published-layout fixtures | Pass | 2026-09-14 KST; every byte-truncation boundary, approved-length rejection, heart-rate sentinel, extrema, and unknown enums pass. |
| Merge callback-order tests | Checked-in synthetic facts | Pass | 2026-09-14 KST; callback ordering, join-window boundary, late correction, and stopped-source duplicate behavior pass. |
| Discovery/session state tests | `pm5-sim` deterministic scenarios | Pass | 2026-09-14 KST; permission, scan, selection, malformed/unsupported identity, stale recovery, and same-identity reconnect scenarios pass. |
| Simulator deterministic replay | `pm5-sim` golden replay | Pass | 2026-09-14 KST; repeated replay produces the same ordered events and digest. |
| Queue overflow/shutdown tests | `pm5-sim` bounded queues | Pass | 2026-09-14 KST; overflow and shutdown are explicit and diagnostics are preserved. |
| Per-run metrics serialization | Synthetic PM5/TUI diagnostics | Pass | 2026-09-14 KST; schema v3 adds independently timestamped stroke-detail events plus per-characteristic approved lengths and last parser-error length/time; lifecycle/fault records, redaction, Bluetooth callback codes, subscription outcomes, and owner-only file permissions pass; not hardware evidence. |
| Stroke parser corpus | Checked-in published BLE layouts | Pass | 2026-09-14 KST; `0x0035` force/timing/work/count and `0x0036` power/calorie/projection fields decode at exact 20-byte and published 15/18-byte BLE lengths, including truncation, unapproved 16-byte rejection, and extrema tests. The active profile remains pinned to 18 bytes for `0x0036`; no target-device packet evidence exists yet. |
| Non-interactive TUI smoke | Scripted synthetic events | Pass | 2026-09-14 KST; unavailable values, identity redaction, telemetry/correction, diagnostic-only samples, timestamped per-stroke record, stale/fault transitions, and structured run records pass. |

## Open blockers and risks

| Item | Owner | Resolution required |
|---|---|---|
| Unreal smoke is blocked by sandbox-denied shared-memory and user-configuration writes | A7 | Run `make unreal-smoke` in the approved host environment; UBT did not reach compilation here. |
| Native CI runner result is not recorded | A7 | `origin` exists, but the workflow and source changes are local only; the public Actions API currently reports no runs. After review and publication through the normal repository workflow, retain the redacted self-hosted Apple-silicon run result. |
| The development profile has no direct HIL property/rate/stroke-detail evidence | A1/A0 | Rerun the exact tuple with profile version 4. Record `0x0034` property/configuration outcome, `0x0035`/`0x0036` presence, lengths, counts/cadence and values compared with the PM5, then complete HIL acceptance review before widening layouts or claiming stroke-detail support. The prior six-minute capture predates this instrumentation and cannot prove stroke-detail capture. |
| Six-minute workout was performed, but HIL acceptance aggregates remain incomplete | A1/A6 | Schema-v3 runs can capture connection/actions, identity, notification-enable outcomes, categorized CoreBluetooth callback codes, characteristic cadence/parser data, per-stroke detail, queues, and reconnect gaps. Repeat HIL and compare stroke values/availability with the PM5; the prior capture cannot be backfilled. |
| Real-adapter `Allowed`, stale, reconnect, bounded-queue, and event-timestamp paths need HIL verification | A1/A0 | Deterministic unit/simulator coverage passes; complete targeted real-device cases before acceptance. |
| Formal TUI/public-contract integration review is pending | A1/A8 | TUI status fields and public-boundary tests are implemented; A8 must review contract conformance and diagnostics. |
| Development permission/TCC outcomes are not recorded | A1/A7 | Complete fresh approval, denial, repair, and retry cases; the app bundle identifier is `dev.virtualrowing.pm5-diagnostic` and the bundle is currently ad-hoc signed |

## Exit decision

| Decision | Date | Evidence reviewed | Approvers | Rationale |
|---|---|---|---|---|
| Not ready — implementation evidence incomplete | 2026-09-14 | Native host/test lanes, partial PM5 HIL capture, zero visible Actions runs, and blocked Unreal smoke documented here | Pending | Native tests and a user-confirmed real six-minute workout are evidenced, but Unreal compile, complete HIL aggregates/scenarios, published-worktree CI result, and required approvals remain open. |

Use `Ready`, `Not ready`, or `Blocked` only. `Ready` authorizes the next milestone decision; it does not claim completion of the broader product Phase 0 or Phase 1 gates.

## Sign-off

- A1 device evidence: Partial; six-minute real workout captured, HIL acceptance evidence incomplete.
- A2 core contract implementation: Implemented/tested locally; formal review pending.
- A6 deterministic test evidence: Native/simulator suite passes; formal sign-off pending.
- A7 toolchain/CI evidence: Doctor/native lanes pass locally; Unreal smoke and CI runner result pending.
- A0 architecture and capability approval: Pending.
- A8 integration recommendation: Pending.

Current milestone decision: **Not ready — implementation evidence incomplete.**
