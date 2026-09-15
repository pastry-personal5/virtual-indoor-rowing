# Delivery Phase 0 Milestone 2: remembered PM5 and diagnostic TUI

Status: Complete — bounded diagnostic milestone; product/release acceptance remains separate
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-14

## Goal

Complete the bounded PM5 diagnostic foundation by making remembered-device
relaunch safe and reversible, and by keeping candidate selection visible while
telemetry is inspected. This remains a native diagnostic tool; it does not
begin the product walking skeleton or solo-alpha feature set.

## Remembered-device lifecycle

- Store only after an explicitly selected PM5 reaches `Ready` through the exact
  approved capability profile and required telemetry handshake.
- Store a versioned adapter-private preference with the CoreBluetooth
  peripheral UUID and expected model, hardware revision, firmware revision,
  and machine kind. Do not store a PM serial or put the UUID in logs, metrics,
  or UI.
- At launch, make one bounded reconnect cycle (up to three connection attempts)
  for that peripheral. Repeat identity,
  profile, service/property, subscription, and first-required-telemetry checks
  before accepting live telemetry. An invalid record is discarded; an identity
  mismatch or unsupported tuple fails closed.
- Forget clears the preference and disconnects/releases the active machine.
  The user may then scan and explicitly select a PM5. Forgetting persists
  across the next launch.
- A relaunch has no source baseline from the previous process. Do not estimate
  the missing interval, emit a restoration gap, or mark its first sample as a
  same-process reconnect.

The behavior is adapter-specific through `IConcept2PMDiscovery`; the public
`IRowingMachine` and `IRowingMachineDiscovery` contracts remain unchanged.

## Diagnostic TUI layout

- On terminals at least 100 columns wide, show the PM5 candidate menu in a
  right-hand pane occupying about one quarter of terminal width and stretching
  to the main content height above the status footer.
- Each item shows the redacted display label, RSSI when available, and machine
  kind. The explicitly selected PM5 carries a `[SELECTED]` tag; moving the menu
  cursor alone does not change that status. Selection remains explicit and
  drives the existing Select action.
- Keep telemetry, history, and scan/connect controls in the main pane. Place
  Forget PM5 in the action controls.
- Below 100 columns, stack the candidate menu above the main content and cap
  the list pane height so telemetry and connection status remain visible.

## Verification

Automated verification covers native compilation, the existing deterministic
same-identity and wrong-identity reconnect scenarios, no synthetic telemetry
during gaps, TUI scripted `forget`, and the public-header dependency boundary.
The macOS preference format and CoreBluetooth handoff additionally require
hardware confirmation because the test host uses the real CoreBluetooth
framework and per-user preferences.

Required HIL evidence on the reference Apple-silicon Mac and an approved
Model D/PM5 tuple:

1. Fresh Bluetooth approval; denial; and permission repair in System Settings.
2. PM-display value comparison during a live row, including source resolution.
3. Stale telemetry and deliberate same-device link loss/reconnect.
4. Wrong-device/changed-identity rejection before telemetry is accepted.
5. Relaunch reconnect to the remembered PM5, followed by forget and a second
   launch that does not reconnect automatically.
6. Clean disconnect and application shutdown.

### Human test results

| Scenario | Result | Evidence / limitation |
|---|---|---|
| Fresh Bluetooth permission approval | Pass (user-reported, 2026-09-14) | Reset the app's `BluetoothAlways` TCC decision, launched `make hil-pm5`, chose Allow, scanned, explicitly selected the PM5, and reached `Ready`. Source revision and PM5 tuple were not included in the report. |
| Permission denial and Settings repair | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |
| PM-display comparison and live-row aggregates | Pass (user-reported, 2026-09-14) | Companion profile-v6 HIL metrics are available below with source revision and PM5 tuple; paired PM-display checkpoint readings were not retained. |
| Stale telemetry and deliberate same-device reconnect | Pass (user-reported, 2026-09-14) | The newly found captures do not contain this deliberate same-process scenario; a separate remembered-device relaunch reconnect is logged below. |
| Wrong-device/changed-identity rejection | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |
| Relaunch reconnect, forget, and manual selection on next launch | Pass (user-reported, 2026-09-14) | A source-tagged remembered-device relaunch reaches `Ready` below; the forget/second-launch check remains user-reported without a retained artifact. |
| Clean disconnect and application shutdown | Pass (user-reported, 2026-09-14) | No source revision, PM5 tuple, or run evidence was included in the report. |

### Newly found local HIL evidence

The latest owner-only, Git-ignored metrics file is
`Metrics/pm5-tui/pm5-tui-2026-09-14T12:59:55.182Z-11147926681875.jsonl`;
the matching rotating log records its start and clean stop. It records source
revision `4f7bb20885cb-dirty` and the allowed PM5/634/firmware
`8200-000372-178.069` IndoorRower tuple under capability profile v6.

The run lasted 464.740 s and recorded 3,635 normalized samples and 482 stroke
events. Active PM telemetry spans about 371.020 s and reaches 910.5 m. The
100 ms status-rate request succeeded. Required characteristics `0x0031` and
`0x0032` recorded 3,647 and 3,645 notifications, respectively; each had p50
95 ms, p95 185 ms, no measured long gaps, and no parser errors. Optional
`0x0035`/`0x0036` recorded 320 20-byte and 162 15-byte packets, both accepted
by profile v6. Acquisition/event queues and the bounded probe queue reported no
overflows or drops. One source-gap sample and 155 time/distance regression
flags were recorded after PM time and distance returned to zero in
`WaitingToBegin`; no fault was recorded. These are telemetry and aggregate
measurements, not paired PM-display checkpoint readings.

A separate run,
`Metrics/pm5-tui/pm5-tui-2026-09-14T12:58:24.205Z-11056948930916.jsonl`,
records launch in `Reconnecting`, identity/profile revalidation for the same
allowed PM5/634/178.069 tuple, return to `Ready`, and a 14.754 s
`connection_restored` gap. It supports the remembered-device relaunch case; it
does not show a deliberate same-process disconnect or stale transition. A
subsequent scan timed out, so the run ended with one recoverable `ScanTimeout`
fault after readiness had been restored. The available capture records no
`telemetry_stale` event.

Each run records the source revision, toolchain, PM5 tuple/profile, requested
status period, characteristic counts/cadence, sample population and duration,
reconnect gaps, queue high-water/overflow counts, parser-error categories, and
outcome. Missing measurements remain explicitly incomplete. This milestone does
not widen the supported device matrix.

The current environment has passed `make build`, all 12 native tests, and the
format check on tracked-source candidates. The formatter command skips Unreal
generated directories, which are not source inputs. All six required Milestone 2
manual scenarios are user-reported passes; the owner accepts them for this
bounded diagnostic milestone alongside the available local HIL artifacts.
Per-scenario metadata remains incomplete as noted above, so this decision does
not qualify product release or widen the supported device matrix. The host's
Bluetooth system report returned no controller during a separate availability
check; the recorded hardware captures came from an interactive run where a PM5
was available. The human-run `make unreal-smoke` subsequently succeeded;
UnrealBuildTool reported “Result: Succeeded” and a 2.23-second execution, and
wrote its trace under the user's Application Support directory. The earlier
access-denied attempt is superseded by this successful result. The trace remains
outside the repository.

## Exit gate

- Versioned remembered preference is saved only after `Ready`; launch reconnect
  validates the stored identity before readiness; malformed, missing, changed,
  or unsupported identity never produces accepted telemetry.
- Forget removes the remembered preference, disconnects the current remembered
  session, and leaves the next launch in manual-selection mode.
- Simulator/native checks pass, including same-device reconnect and
  wrong-device rejection without invented samples or distance.
- The operator reported all required HIL cases above as passed. A0 accepts the
  available redacted/local evidence and the documented per-case limitations for
  this bounded diagnostic milestone; they remain follow-on evidence before
  product release qualification or support-matrix expansion.
- Native build/tests and formatting checks pass; A8 integration review is
  recorded in the exit decision below.

## Exit decision

| Decision | Date | Evidence reviewed | Approvers | Rationale |
|---|---|---|---|---|
| Complete — bounded delivery Phase 0 Milestone 2 diagnostic scope | 2026-09-14 | All six operator-reported Milestone 2 HIL passes; source-tagged profile-v6 live-row metrics; remembered-device relaunch revalidation; `make build`, 12/12 native tests, `make format-check` | User as A0; local A8 integration review | The user accepted Milestone 2 completion. Artifact gaps, the absence of logged stale/link-loss transitions in the new captures, and the separately skipped physical multiple-candidate test remain explicit follow-on limitations; this is not product/release acceptance. |
