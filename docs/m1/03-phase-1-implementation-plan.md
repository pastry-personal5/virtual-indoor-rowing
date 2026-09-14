# Milestone 1 Phase 1 implementation plan

Status: In progress
Owner: A0 — CTO / Principal Architect  
Last reviewed: 2026-09-14

## Goal and success criteria

Prepare a reproducible Apple-silicon development foundation and demonstrate a real Concept2 Model D/PM5 producing validated, normalized, live telemetry through the hardware-neutral interfaces defined for this milestone.

Success means another contributor can set up the approved toolchain, run native tests without hardware, launch the terminal diagnostic application, explicitly select the intended PM5, observe live metrics, see failures and stale state clearly, and reproduce the real-device acceptance procedure.

## Work sequence

### 0. Complete preflight before implementation

Before source targets or public headers are added, A7 records the accepted Xcode path/build, Git LFS state, build-tool versions, and approved Unreal installation fingerprint in the evidence report. A0 confirms that the proposed capability-profile schema can express the exact tuple, bounds, and evidence metadata in [the architecture](01-architecture-and-ownership.md).

If a required tool is unavailable, the milestone remains blocked. Replacing the pinned toolchain, accepting a non-fingerprinted Unreal installation, or designing against an assumed PM5 layout is not a preflight workaround.

### 1. Freeze contracts and target boundaries

A0 approves:

- names and semantics in [public interfaces](02-public-interfaces.md);
- module and directory ownership;
- build target names and allowed dependency directions;
- the capability-profile schema;
- the milestone evidence and privacy rules.

No parallel implementation begins before this checkpoint. Later proposed public changes are reviewed centrally rather than merged independently by module owners.

### 2. Prepare the repository and toolchain

A7 establishes documentation-backed, reproducible foundations:

- root CMake/Ninja configuration for engine-independent libraries, adapters, tools, and tests;
- an empty Unreal project and plug-in descriptors sufficient for an UnrealBuildTool smoke compile, without maps or gameplay code;
- `Config/BuildVersions.json` as the machine-readable source for required and observed tool versions/fingerprints;
- `.gitignore`, `.gitattributes`, Git LFS setup, formatting/static-analysis configuration, and CI;
- stable `make doctor`, `make configure`, `make build`, `make test`, `make pm5-tui`, `make unreal-smoke`, and `make hil-pm5` entry points.

The approved Xcode is installed side by side at `/Applications/Xcode-26.1.1.app`. Repository scripts set `DEVELOPER_DIR` per invocation and do not require a global `xcode-select` change. Unreal acquisition remains a documented manual step because of Epic authentication/licensing.

`make doctor` must fail clearly on a wrong Xcode/SDK/architecture, missing Unreal, missing Git LFS, missing build tools, or an unrecorded fingerprint. It must report only an allow-list of non-sensitive system fields.

The TUI is built as a development-signed macOS application bundle containing `NSBluetoothAlwaysUsageDescription`. It is launched by executing the bundle's terminal binary. App Sandbox and release signing are not part of this milestone.

### 3. Implement the hardware-neutral core

A2 implements the approved `RowingCore` value types and private helpers for:

- exact unit conversion and checked integer bounds;
- nullable metric handling;
- normalized workout, rowing, and stroke state enums;
- quality flag accumulation;
- monotonic elapsed/distance validation without smoothing or correction;
- redacted formatting helpers that do not introduce product presentation policy.

A2 does not implement virtual velocity, interpolation, gameplay distance, workout progression, or scoring.

### 4. Implement the Concept2 diagnostic path

A1 implements the minimum production-shaped path:

1. Create a CoreBluetooth central on a dedicated serial dispatch queue.
2. Begin scanning only after explicit TUI action and filter for Concept2 services.
3. Emit redacted candidates and require explicit selection.
4. Connect only the selected peripheral and discover the device-information and rowing services.
5. Read model, hardware, firmware, manufacturer, and connected erg machine type.
6. Stop after identity inventory if the exact tuple is not in the development capability profile.
7. After A0/A1 profile review, validate required characteristics and properties.
8. Subscribe to required general status `0x0031` and additional status 1 `0x0032`.
9. Subscribe to additional status 2 `0x0033`, stroke data `0x0035`, additional stroke data `0x0036`, and additional status 3 `0x003E` only when declared by the approved profile.
10. Request the 100 ms status rate through `0x0034` only when supported.
11. Require valid `0x0031` and `0x0032` notifications before entering `Ready`.
12. Decode exact allowed lengths, little-endian fields, sentinels, and enums; emit normalized samples and explicit faults.
13. Detect stale telemetry, disconnect, and bounded same-device reconnection with identity revalidation.
14. Implement automatic reconnection functionality:
    - Store the last explicitly selected PM5 identifier locally in adapter-private preferences
    - On TUI launch, attempt to reconnect to the stored PM5 without requiring a new scan
    - If the stored PM5 is unavailable, fall back to normal scanning behavior
    - Show clear status messages about the reconnection attempt
    - Transfer the remembered-PM5 machine to the TUI's single `IRowingMachine`
      owner so normal event polling, explicit shutdown, and final diagnostics use
      the same path as a manually selected machine

The implementation uses only the published Concept2 specification and observed hardware evidence. It does not send CSAFE commands, attempt authentication, mutate PM state, or infer undocumented fields.

### 5. Implement the diagnostic TUI

A1 owns the minimum diagnostic surface. It uses the repository-pinned FTXUI
library for the interactive terminal surface and retains a bounded
non-interactive script mode for deterministic smoke tests.

Required controls:

- start/stop scan;
- select a numbered discovered candidate;
- connect/disconnect;
- quit cleanly.

Required display:

- connection state, last transition reason, and update age;
- redacted label, monitor model, hardware/firmware, machine kind, and capability state;
- elapsed time, cumulative distance, speed, pace/500 m, stroke rate, stroke/average power, calories, heart rate, drag factor, and stroke count when available;
- normalized workout, rowing, and stroke states;
- quality flags, stale warning, queue depth, and categorical fault.

Connection/readiness and blocking fault state remain visible in a high-contrast
status line fixed to the bottom of the interactive viewport. It distinguishes
live, in-progress, warning, critical, and diagnostic-only conditions without
depending on color alone, and continues updating telemetry age when events stop.

Unavailable optional metrics render as `—`. The display never substitutes zero or
derives missing official metrics. By explicit local diagnostic authorization, each
TUI run writes one owner-only JSONL file below `Metrics/pm5-tui/`; it contains
timestamped normalized samples and corrections plus redacted run aggregates for
local AI-assisted analysis. Aggregates include queue state, reconnect gaps, and
adapter-private per-characteristic counts, observed properties, cadence,
notification-enable outcomes, and categorized parser errors. Schema version 3
also records timestamped discovery, identity, connection-state, stale,
reconnect, categorical fault, and sparse per-stroke detail events. Stroke
records retain PM elapsed time and stroke count and are independent of the
continuous sample cadence. Missing optional sources remain absent. The PM's
undocumented-unit projected-work field is retained only as a raw value;
adapter summaries retain per-callback Bluetooth error domains/codes without
localized error text. Warn-profile diagnostic samples are captured separately
from workout-ready samples. It contains no raw BLE packets, PM serial number,
peripheral identifier, account data, or network export, is ignored by Git, and
is not a workout/session journal.
A non-interactive fallback renders bounded snapshots for simulator smoke tests.

### 6. Build deterministic test support

A6 implements `MockRowingMachine` and `ReplayRowingMachine` behind the same `IRowingMachineDiscovery` and `IRowingMachine` interfaces.

Required deterministic scenarios:

- Bluetooth permission denied;
- empty scan and scan timeout;
- multiple discovered machines;
- connection failure;
- malformed identity and wrong machine kind;
- unsupported hardware/firmware tuple;
- idle valid telemetry;
- steady rowing with nullable metrics;
- malformed sample and unknown enum;
- telemetry gap and stale thresholds;
- disconnect, wrong-device appearance, and same-device reconnect;
- bounded queue overflow and clean shutdown.

Simulator facts must be labeled synthetic and must never be presented as PM5 compatibility evidence. Parser-specific byte fixtures and their interpretation remain A1-owned.

## Verification

### Automated native checks

- Core modules compile and test without Unreal, Objective-C runtime use, or physical hardware.
- Dependency checks reject Concept2, CoreBluetooth, Foundation, terminal, and Unreal types in public core headers.
- Every implemented characteristic parser covers approved lengths, truncation at each byte boundary, extrema, sentinels, unknown enums, unaligned input, and exact unit conversion.
- Merge tests cover every callback order used by the implemented characteristics and the fixed merge-window boundaries.
- Interface tests cover accepted/rejected commands, state ordering, queue bounds, single-consumer behavior, cancellation, and destruction.
- Simulator scenarios produce identical ordered events on repeated runs.
- The non-interactive TUI smoke test displays absent values and all critical state/fault classes correctly.

### Real-hardware checks

Use the reference M5 Max MacBook Pro and an available Model D/PM5. Evidence records the app/source build, OS, approved toolchain fingerprints, redacted PM hardware/firmware/machine tuple, capability-profile version, and test timestamps. It does not record raw identifiers or payloads.

Required cases:

1. Fresh Bluetooth permission approval.
2. Permission denial followed by repair in System Settings.
3. Scan with the target PM5 visible and explicit selection.
4. Identity inventory and exact capability-profile review.
5. Valid required subscriptions and 100 ms request where supported.
6. Idle-to-row telemetry with optional/sentinel values visible as absent.
7. Deliberate link loss and reconnection to the same identity.
8. Appearance of a different candidate during reconnect without auto-selection.
9. Six-minute live run.

The 6-minute run passes only with no crash, parser overread, silent queue overflow, fabricated metric, or unexplained time/distance regression. Aggregate notification counts, cadence, gaps, stale periods, reconnects, queue high-water marks, and categorized parser errors are recorded.

Any notification-loss or latency claim must state the measurement method, sample population, requested status rate, and excluded intervals. A run with unknown or incomplete measurement cannot be described as meeting a numerical target; it remains evidence with its limitation recorded.

### Repository checks

- `make doctor`
- `make configure`
- `make build`
- `make test`
- `make unreal-smoke`
- simulator-backed `make pm5-tui` smoke
- interactive `make hil-pm5`
- `git diff --check`
- `git status --short`

Hardware checks are manual/self-hosted and never required on ordinary pull-request CI. They remain required milestone evidence.

## Exit gate

Milestone 1 Phase 1 is complete when:

- the accepted Xcode and stock Unreal baseline are installed, fingerprinted, and pass their smoke lanes;
- native CI passes on Apple silicon without hardware;
- the simulator and real adapter use the identical public interfaces;
- the real PM5 reaches `Ready` only through an exact reviewed capability profile;
- live normalized telemetry is readable in the TUI with correct nullability and units;
- permission, stale, disconnect, reconnect, unsupported, and clean-shutdown behavior are explicit;
- the 6-minute hardware run passes and its redacted aggregate evidence is recorded;
- A0 approves public contracts/capability policy and A8 approves integration readiness.

The exit decision is recorded as `Ready`, `Not ready`, or `Blocked`. `Ready` means this diagnostic path is trustworthy enough to inform the next funded milestone; it does not mark the product Phase 0 or Phase 1 exit gate complete.

Failure does not justify widening profiles, weakening tool pins, ignoring malformed fields, or implementing product workarounds. Record the evidence, assign the risk, and revise the architecture through the normal review process.

## Parallel assignments

After the A0 contract checkpoint, the following work is safe in parallel because ownership does not overlap:

| Agent | Owned work | Explicit exclusions | Integration dependency |
|---|---|---|---|
| A1 | `Plugins/Concept2PM`, parser tests, real adapter, `pm5-tui` | Public-contract edits, persistence, gameplay, CSAFE control | Approved interfaces and A7 target names |
| A2 | Private `RowingCore` unit/state/quality helpers and core tests | BLE, PM layouts, smoothing, virtual movement, scoring | Approved telemetry types |
| A6 | `pm5-sim`, contract/integration scenarios, TUI simulator smoke | Production logic changes, unsupported PM claims, HIL replacement | Approved interfaces and executable injection seam |
| A7 | Build/config/scripts/CI, LFS/ignore policy, empty Unreal smoke host | Product architecture changes, relaxed gates, stored secrets | Approved target/dependency graph |

A1 and A6 coordinate scenario meanings, but A1 owns claims about real PM behavior. A2 and A1 may consume the public headers but do not edit them concurrently. A7 owns root build metadata; other agents provide target requirements instead of modifying the same orchestration files.
