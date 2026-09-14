# Milestone 1 Phase 1 evidence report

Status: Complete for revised M1 Phase 1 exit gate; follow-on hardware evidence deferred
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

## Firmware 178.069 HIL probe (implementation evidence; acceptance incomplete)

The local schema-v4 metrics run beginning 2026-09-14T10:30:10.937Z records
the exact PM5/634/8200-000372-178.069/IndoorRower tuple as `Allowed` and
`Ready`. The owner-only, Git-ignored file is retained at
`Metrics/pm5-tui/pm5-tui-2026-09-14T10:30:10.937Z-2161639422625.jsonl`; its
raw packet payloads are not copied into this report.

The run lasted 476,463 ms and recorded a 14,658 ms reconnect gap before
readiness. The requested 100 ms rate write succeeded once. Required `0x0031`
and `0x0032` streams recorded 3,685 and 3,683 notifications respectively, with
no reported long gaps or queue overflows. Optional `0x0035` recorded 311 valid
20-byte notifications. Optional `0x0036` recorded 158 15-byte notifications;
all were safely rejected by profile v5, which allowed only 18 bytes, and each
was reported as an `InvalidPacketLength` warning without interrupting the
required status streams. The v6 exact-tuple profile admits both published,
decoder-tested 15- and 18-byte `0x0036` layouts. This is direct packet-metadata
evidence for the target tuple, but it does not establish PM-display value
agreement, a deliberate disconnect scenario, or HIL acceptance.

## Firmware 178.069 profile-v6 confirmation (implementation evidence; acceptance incomplete)

The subsequent local schema-v4 run beginning 2026-09-14T10:45:37.965Z used the
v6 profile and retained its owner-only, Git-ignored metrics at
`Metrics/pm5-tui/pm5-tui-2026-09-14T10:45:37.965Z-3088667475750.jsonl`.
It recorded the same exact tuple as `Allowed` and `Ready`. Its 436,952 ms
duration included a 1,710 ms reconnect gap. The 100 ms rate write succeeded;
`0x0031` and `0x0032` recorded 3,471 and 3,469 valid notifications with no
reported long gaps. Optional `0x0035` recorded 325 valid 20-byte notifications,
and optional `0x0036` recorded 165 valid 15-byte notifications with parser
result `none` under the approved `[15, 18]` layout set. The run reported zero
faults, zero stale events, and zero acquisition/event/probe queue overflows.

This confirms that v6 accepts the observed target-device layout without
weakening the exact-tuple boundary. It does not by itself prove the PM-display
value comparison, deliberate disconnect/reconnect case, permission cases, or
formal HIL acceptance.

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
for the 15-byte form. At the time, the active `Allowed` profile remained pinned
to the revision 0.34 18-byte layout. The subsequent `.069` HIL probe records
the 15-byte `0x0036` form, so profile v6 admits both decoder-tested layouts for
that exact tuple. The probe does not replace the remaining hardware-acceptance
scenarios.

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
| Approved packet lengths | `0x0031`: 19 bytes; `0x0032`: 17 bytes; optional `0x0035`: 20 bytes and `0x0036`: 15 or 18 bytes. The target `.069` probe observed the 15-byte form; both layouts have deterministic decoder coverage. |
| Requested/observed sample rate | Prior profile: approximately 1 s between samples, causing false 500 ms stale faults because `0x0034` was absent. Version 2 requests 100 ms through `0x0034`; observed cadence is pending. |
| Capability-profile version/digest | Version 6; `PM5Capabilities.json` SHA-256 `019091d7230f48ca3e3564e697964299d1a1690a7df072857d947b72909dfe7e` |
| Reviewers | User confirmed A0 public-contract/capability approval and A8 integration approval on 2026-09-14. The approvals do not substitute for the remaining required CI and HIL evidence. |
| Support decision and rationale | `Allowed` only for the exact PM5/634/8200-000372-178.069/IndoorRower tuple and the two reviewed status streams. Hardware acceptance remains pending. |

Identity discovery alone does not authorize telemetry readiness. The profile still requires
the exact identity, advertised required properties, subscriptions, and structurally valid
first notifications before `Ready`.

## Hardware scenario results

| Scenario | Result | Aggregate evidence/notes |
|---|---|---|
| Fresh permission approval | Pass (user-reported) | Recorded in Phase 2; source revision and PM5 tuple were not included. |
| Permission denial | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |
| Settings repair and retry | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |
| Explicit scan and target selection | Pending | |
| Multiple-candidate selection | Skipped (user decision) | No physical multi-PM5 selection run. Deterministic simulator selection coverage passes, but real-device behavior remains unverified. |
| Identity/capability handshake | Observed; formal profile review pending | Redacted tuple PM5/634/8200-000372-178.067/IndoorRower; application logged `Allowed` and entered `Ready`. Direct characteristic-property evidence and reviewer sign-off remain pending. |
| Required notification readiness | Observed | Application entered `Ready` after subscribing; full notification counts remain unavailable. |
| Optional/sentinel rendering | Pending | |
| 500 ms stale transition | Pass (user-reported, 2026-09-14) | Reported as part of the stale telemetry HIL case; no source revision, PM5 tuple, transition time, or metrics artifact was included. |
| 1.5 s blocking warning | Pass (user-reported, 2026-09-14) | Reported as part of the stale telemetry HIL case; no source revision, PM5 tuple, transition time, or metrics artifact was included. |
| Deliberate disconnect | Pass (user-reported, 2026-09-14) | Reported as part of the stale/reconnect HIL case; no source revision, PM5 tuple, or metrics artifact was included. |
| Same-device reconnect and identity recheck | Pass (user-reported, 2026-09-14) | Reported as part of the deliberate link-loss HIL case; no source revision, PM5 tuple, reconnect gap, or metrics artifact was included. |
| Wrong-device appearance during reconnect | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |
| Clean disconnect and application shutdown | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |

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
| Stroke parser corpus | Checked-in published BLE layouts | Pass | 2026-09-14 KST; `0x0035` force/timing/work/count and `0x0036` power/calorie/projection fields decode at exact 20-byte and published 15/18-byte BLE lengths, including truncation, unapproved 16-byte rejection, and extrema tests. The `.069` HIL probe observed the 15-byte `0x0036` layout; profile v6 admits both tested layouts. |
| Non-interactive TUI smoke | Scripted synthetic events | Pass | 2026-09-14 KST; unavailable values, identity redaction, telemetry/correction, diagnostic-only samples, timestamped per-stroke record, stale/fault transitions, and structured run records pass. |

## Deferred follow-on evidence

| Item | Owner | Resolution required |
|---|---|---|
| Published Apple-silicon CI | A7 | The local native suite passes; publish and retain a redacted self-hosted runner result before the next product milestone. |
| HIL evidence review | A1/A0/A7 | Phase 2 was closed for bounded diagnostic scope by owner decision; all six required scenarios are user-reported passes. The 2026-09-14 profile-v6 metrics add a source revision, exact tuple, live-row aggregates, and zero-overflow evidence; a separate artifact records remembered-device relaunch reconnect. Per-scenario comparison readings and stale/link-loss events are not present in these artifacts. Before product release qualification or support expansion, review complete redacted run metadata and resolve the physical multiple-candidate case skipped by user decision; other pending cases in the hardware scenario table remain open. |

## Exit decision

| Decision | Date | Evidence reviewed | Approvers | Rationale |
|---|---|---|---|---|
| Ready — revised M1 Phase 1 exit gate met | 2026-09-14 | Native host/test lanes, successful normal-host Unreal smoke, user-confirmed six-minute PM5 workout, and `.069` profile-v6 HIL confirmation | User as A0 and A8 | The bounded diagnostic foundation meets the revised exit gate. Published CI and the omitted manual HIL/TCC scenarios are explicitly deferred follow-on work, not evidence of product readiness. |

Use `Ready`, `Not ready`, or `Blocked` only. `Ready` authorizes the next milestone decision; it does not claim completion of the broader product Phase 0 or Phase 1 gates.

## Sign-off

- A1 device evidence: User-confirmed six-minute workout and `.069` profile-v6 HIL confirmation recorded; follow-on scenarios deferred.
- A2 core contract implementation: Implemented and tested locally.
- A6 deterministic test evidence: Native/simulator suite passes.
- A7 toolchain evidence: Doctor/native lanes and normal-host Unreal smoke pass; CI publication deferred.
- A0 architecture and capability approval: User-approved on 2026-09-14.
- A8 integration recommendation: User-approved on 2026-09-14.

Current milestone decision: **Ready — revised M1 Phase 1 exit gate met.**
