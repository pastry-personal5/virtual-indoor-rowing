# Milestone 1 Phase 1 public interfaces

Status: Planned contract  
Owner: A0 — CTO / Principal Architect  
Last reviewed: 2026-09-13

## Contract principles

- Public interfaces describe a rowing machine, not a Concept2 PM5.
- Published values use integer fixed-scale units. Floating point is presentation-only.
- Optional source values remain absent; missing is never encoded as zero.
- All commands are non-blocking and all outcomes are observable as ordered events.
- Device bytes, callbacks, and vendor/platform identifiers stop at adapters.
- Measurement values are immutable facts. A consumer may derive presentation state but may not rewrite them.
- Public-contract changes require A0 review and a compatibility note before implementation.

Names below are normative for the first implementation. C++ structs follow Epic's `F` prefix convention even though the core is engine-independent.

## Identifiers and descriptors

### `FRowingMachineId`

An opaque, locally scoped identifier used only to select a discovered machine. Consumers may compare and copy it but must not parse or log its representation.

The CoreBluetooth peripheral identifier and PM serial number are adapter-private. Neither may be used as a domain identity.

### `ERowingMachineKind`

Initial values:

- `Unknown`
- `IndoorRower`
- `SkiErg`
- `BikeErg`

Only `IndoorRower` is accepted by this milestone. The other values prevent the public contract from treating all PM5 configurations as rowers.

### `FRowingMachineDescriptor`

| Field | Type | Meaning |
|---|---|---|
| `Id` | `FRowingMachineId` | Opaque local selection key |
| `DisplayLabel` | bounded UTF-8 string | Redacted user-facing label without raw serial |
| `SignalStrengthDbm` | optional signed integer | Most recent discovery RSSI; presentation hint only |
| `KindHint` | `ERowingMachineKind` | Untrusted discovery hint; identity read remains authoritative |

### `FRowingMachineInfo`

| Field | Type | Meaning |
|---|---|---|
| `Manufacturer` | bounded UTF-8 string | Device-reported manufacturer |
| `Model` | bounded UTF-8 string | Monitor model, without serial |
| `HardwareVersion` | bounded UTF-8 string | Exact observed hardware revision |
| `FirmwareVersion` | bounded UTF-8 string | Exact observed firmware revision |
| `MachineKind` | `ERowingMachineKind` | Parsed connected-erg type |
| `SupportedMetrics` | bitset | Metrics backed by structurally valid source notifications observed in the current connection; starts empty and expands as sources are proven, and the run-metrics JSON records both names and numeric flags |
| `CapabilityProfileVersion` | unsigned integer | Bundled profile version used for evaluation |
| `SupportState` | enum | `Allowed`, `Warn`, or `Blocked` |

Raw serial number is intentionally absent.

`MachineInfoObserved` may be emitted again when a newly observed optional source
expands `SupportedMetrics`. Profile decoder potential alone does not advertise a
metric: an absent characteristic, failed subscription, or invalid packet leaves
its metrics unavailable.

`Allowed` means the exact reviewed profile may proceed to `Ready`. `Warn` never enters `Ready` and never emits `MetricSampled`. A narrowly scoped PoC diagnostic profile may emit the separate `DiagnosticSampleObserved` event, which is display-only, unverified, and must not be consumed by workout, persistence, or ranking logic. Other Warn profiles permit identity inventory and redacted diagnostics only. `Blocked` stops after the safe identity/capability check. A profile may move to `Allowed` only through the evidence and review process described in this milestone.

## Connection state

`ERowingConnectionState` contains:

- `Idle`
- `Scanning`
- `Connecting`
- `Discovering`
- `ReadingIdentity`
- `Subscribing`
- `Ready`
- `Stale`
- `Reconnecting`
- `Unsupported`
- `Failed`
- `PermissionDenied`
- `DiagnosticOnly`

Advertising or CoreBluetooth connection alone never produces `Ready`. `Ready` requires an approved identity/capability tuple, required characteristic properties, enabled notifications, and at least one structurally valid general-status and additional-status-1 notification.

`DiagnosticOnly` is a local PoC state, not a readiness level. It may be reached only by an exact `Warn` profile explicitly marked diagnostic-only after both required passive notification streams are enabled and structurally valid packets from each have arrived. It cannot produce standard `MetricSampled` events or authorize controls.

State transitions are ordered and carry a categorical reason where applicable. `DiagnosticObservationConfirmed` is used only when the local unverified diagnostic path begins; it is not a readiness confirmation. A consumer must not infer success from the absence of an error.

The initial transition reasons are `None`, `UserRequested`, `OperationStarted`, `ReadinessConfirmed`, `TelemetryTimedOut`, `TelemetryResumed`, `LinkLost`, `ReconnectStarted`, `CapabilityRejected`, `OperationFailed`, `Shutdown`, and `DiagnosticObservationConfirmed`. New reasons are appended; existing numeric values remain reserved.

## Normalized telemetry

### `FRowingMetricSample`

| Field | Type | Required | Unit/meaning |
|---|---|---:|---|
| `Sequence` | `uint64` | yes | Monotonic adapter-assigned sample sequence |
| `SourceElapsedMs` | `uint64` | yes while active | PM elapsed time converted exactly from centiseconds |
| `ReceivedMonotonicNs` | `uint64` | yes | Local monotonic receive timestamp; not wall time |
| `DistanceMm` | `uint64` | yes while active | PM cumulative distance; 0.1 m becomes exactly 100 mm |
| `SpeedMmPerS` | optional `uint32` | no | PM-reported speed |
| `PaceMsPer500M` | optional `uint32` | no | PM-reported current pace |
| `StrokeRateDeciSpm` | optional `uint32` | no | Integer SPM source multiplied by ten |
| `StrokePowerW` | optional `uint32` | no | Stroke-event power when available |
| `AveragePowerW` | optional `uint32` | no | PM aggregate power when available |
| `Calories` | optional `uint32` | no | PM source estimate |
| `HeartRateBpm` | optional `uint32` | no | Sentinel values become absent |
| `DragFactor` | optional `uint32` | no | Dimensionless PM source value |
| `StrokeCount` | optional `uint64` | no | Monotonic cross-check when available |
| `WorkoutState` | normalized enum | yes | Unknown numeric values map to `Unknown` |
| `RowingState` | normalized enum | yes | Unknown numeric values map to `Unknown` |
| `StrokeState` | normalized enum | yes | Unknown numeric values map to `Unknown` |
| `QualityFlags` | bitset | yes | Evidence about missing, stale, invalid, or discontinuous input |

The initial quality flags reuse the definitions in [data model and protocols](../architecture/06-data-and-protocols.md): `MissingField`, `SourceGap`, `Duplicate`, `TimeRegression`, `DistanceRegression`, `DeviceReconnected`, `LateCorrection`, `UnsupportedValue`, and `Outlier`. Flags may accumulate; no Boolean `Verified` field replaces the evidence.

### `FRowingStrokeMetrics`

Stroke detail is a separate sparse event, not a field that must align with the
10 Hz `FRowingMetricSample` cadence. PM sources are independent: consumers may
receive two records with the same `StrokeCount`, one for kinematics/force and
one for power/projection. Join only by the source stroke count when present;
never by arrival order. `SourceElapsedMs` and the enclosing event's monotonic
timestamp preserve both PM time and local receive timing.

| Field | Type | Unit/meaning |
|---|---|---|
| `Source` | enum | `KinematicsAndForce` or `PowerAndProjection` packet group |
| `SourceElapsedMs` | `uint64` | PM elapsed time |
| `StrokeCount` | optional `uint64` | PM stroke identifier/correlation key |
| `CumulativeDistanceMm` | optional `uint64` | PM cumulative distance carried by the stroke-data source |
| `DriveLengthMm` | optional `uint32` | Drive length in millimeters |
| `DriveTimeMs` | optional `uint32` | Drive time in milliseconds |
| `RecoveryTimeMs` | optional `uint32` | Recovery time in milliseconds |
| `StrokeDistanceMm` | optional `uint32` | PM stroke distance in millimeters |
| `PeakDriveForceDeciLb` | optional `uint32` | Peak force in 0.1 lb-force |
| `AverageDriveForceDeciLb` | optional `uint32` | Average drive force in 0.1 lb-force |
| `WorkPerStrokeDeciJoules` | optional `uint32` | Work in 0.1 joules |
| `StrokePowerW` | optional `uint32` | Stroke power in watts |
| `CaloriesPerHour` | optional `uint32` | PM stroke calorie rate |
| `ProjectedWorkTimeMs` | optional `uint64` | PM projected work time in milliseconds |
| `ProjectedWorkDistanceMm` | optional `uint64` | PM projected work distance in millimeters |
| `ProjectedWorkOtherRaw` | optional `uint32` | Opaque 24-bit PM value; unit/semantics intentionally unspecified |

This milestone does not define smoothed speed, predicted distance, virtual distance, course position, scoring, or workout summaries.

## Events

`FRowingMachineEvent` is an ordered tagged variant with these payload kinds:

| Kind | Payload | Coalescing |
|---|---|---|
| `MachineDiscovered` | `FRowingMachineDescriptor` | Latest RSSI may replace an older discovery update for the same ID |
| `ConnectionStateChanged` | prior state, new state, reason | Never coalesced |
| `MachineInfoObserved` | `FRowingMachineInfo` | Never coalesced |
| `MetricSampled` | `FRowingMetricSample` | Not coalesced in the Phase 1 diagnostic path |
| `TelemetryStale` | last sequence and age | Never coalesced |
| `ConnectionRestored` | identity-confirmed machine and gap duration | Never coalesced |
| `FaultObserved` | `FRowingFault` | Never coalesced |
| `DiagnosticSampleObserved` | `FRowingDiagnosticSample` | Never coalesced; unverified display-only data, never workout input |
| `MetricCorrected` | `FRowingMetricCorrection` | Never coalesced; replaces the complete prior sample named by `TargetSampleSequence` |
| `StrokeMetricsObserved` | `FRowingStrokeMetrics` | Never coalesced; sparse per-stroke facts, separate from continuous samples |

Every event contains an adapter event sequence and local monotonic timestamp. Events produced by one adapter are dequeued in producer order.

Event sequence and timestamp ordering are per event stream. Monotonic timestamps use a local steady-clock epoch, are non-decreasing, and are not wall-clock values. A correction repeats the original sample sequence and source elapsed time; `TargetSampleSequence` must equal `CorrectedSample.Sequence`. It carries the complete corrected snapshot and sets `LateCorrection`; it is a new adapter event with its own event sequence and timestamp. Diagnostic-only samples do not emit standard metric corrections.

### `FRowingFault`

The public fault contains:

- stable categorical code;
- severity: `Info`, `Warning`, `Recoverable`, or `Terminal`;
- operation/state in which it occurred;
- redacted bounded diagnostic text suitable for the TUI;
- optional expected/actual numeric metadata such as byte length.

It does not contain raw payloads, PM serial numbers, CoreBluetooth error objects, stack traces, or user/system identifiers.

Initial categories cover permission, scan timeout, connection timeout, disconnected, missing service, missing characteristic, unsupported identity, wrong machine type, invalid property, invalid packet length, invalid value, queue overflow, stale telemetry, and internal lifecycle error.

## Discovery and machine interfaces

The conceptual C++ shape is:

```cpp
class IRowingMachineDiscovery
{
public:
	virtual ~IRowingMachineDiscovery() = default;
	virtual FRowingCommandResult StartScan() = 0;
	virtual FRowingCommandResult StopScan() = 0;
	virtual bool TryPollDiscoveryEvent(FRowingMachineEvent& OutEvent) = 0;
	virtual std::unique_ptr<IRowingMachine> CreateMachine(
		const FRowingMachineId& MachineId) = 0;
};

class IRowingMachine
{
public:
	virtual ~IRowingMachine() = default;
	virtual FRowingCommandResult Connect() = 0;
	virtual FRowingCommandResult Disconnect() = 0;
	virtual ERowingConnectionState GetConnectionState() const = 0;
	virtual FRowingMachineDiagnostics GetDiagnostics() const = 0;
	virtual bool TryPollEvent(FRowingMachineEvent& OutEvent) = 0;
};
```

`FRowingCommandResult` reports `Accepted` or a stable immediate rejection category such as invalid current state or shutdown. It never reports that an asynchronous operation completed.

`IRowingMachine::GetDiagnostics()` returns a prompt, read-only snapshot of the bounded acquisition queue when present and the ordered public-event queue. Each queue snapshot reports current depth, capacity, high-water mark, and overflow count. Mock/replay transports may leave `AcquisitionQueue` absent when there is no transport queue; they must still report their event queue. Snapshots contain no vendor/platform types and are observational only.

## Lifecycle and concurrency contract

- Discovery begins only after explicit user action.
- A discovery object and each machine object have one controlling owner thread.
- Each event stream has exactly one polling consumer.
- Public calls return promptly and never wait for Bluetooth activity.
- The implementation owns its platform queues and workers.
- Queue snapshots do not wait for device activity. Queue depth never exceeds capacity; high-water mark and overflow count are monotonic for the lifetime of the machine instance.
- Destruction initiates cancellation, joins owned workers, and guarantees that no later callback reaches the consumer.
- Only the explicitly selected `FRowingMachineId` may reconnect during the session.
- Reconnection revalidates identity, machine type, capability profile, notification readiness, and monotonic source state.
- At a requested 100 ms telemetry rate, 500 ms without required general status enters `Stale`; 1.5 seconds produces a blocking device warning.
- `Stale` is a liveness condition, not a fabricated disconnect. A subsequent structurally valid required status may restore `Ready` only if identity and capability state are unchanged; an actual link loss follows the bounded reconnect path.
- An unsupported or changed tuple fails closed. Remote or configuration data may tighten support but may not enable parser code that is absent from the signed build.

## Private adapter seam

`IRowingDeviceTransport` is an implementation-private seam used by the real CoreBluetooth adapter and test transports. It may carry copied characteristic identifiers and byte buffers into `Concept2PMCore`, but it is not exposed to the TUI, Unreal, simulation engine, persistence, or online code.

The exact transport interface may evolve privately as long as the public ordering, lifecycle, bounds, and shutdown guarantees remain intact.

## Compatibility rules

- Adding an optional telemetry field is backward-compatible only when absence remains valid.
- Reinterpreting a unit, changing requiredness, changing event order, or changing lifecycle semantics is breaking.
- Removed enum values remain reserved and unknown numeric source values remain diagnosable.
- Public serialized wire/storage contracts are out of scope; these in-process C++ types must not be copied directly into future Protobuf or database schemas without separate review.
- `StrokeMetricsObserved` is appended as event-kind value 9; existing event-kind values 0–8 remain unchanged. Metrics-file serialization advances to schema version 3.
