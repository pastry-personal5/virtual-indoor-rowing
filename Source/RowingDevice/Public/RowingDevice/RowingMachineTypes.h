#pragma once

#include "RowingCore/RowingTelemetry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

inline constexpr std::size_t RowingDisplayLabelMaxUtf8Bytes = 96;
inline constexpr std::size_t RowingDeviceInfoTextMaxUtf8Bytes = 64;
inline constexpr std::size_t RowingDiagnosticTextMaxUtf8Bytes = 256;

// Public text must be valid UTF-8 and no larger than its corresponding bound.
// Producers validate these invariants before publishing an event.

class FRowingMachineId final
{
  public:
	FRowingMachineId() = default;

	// Adapter values remain deliberately unavailable for parsing or logging.
	static FRowingMachineId FromPrivateAdapterValue(std::string Value)
	{
		return FRowingMachineId(std::move(Value));
	}

	friend bool operator==(const FRowingMachineId &,
						   const FRowingMachineId &) = default;

  private:
	explicit FRowingMachineId(std::string Value)
		: PrivateAdapterValue(std::move(Value))
	{
	}

	std::string PrivateAdapterValue;
};

enum class ERowingMachineKind : std::uint8_t
{
	Unknown,
	IndoorRower,
	SkiErg,
	BikeErg
};

enum class ERowingMachineSupportState : std::uint8_t
{
	Allowed,
	Warn,
	Blocked
};

struct FRowingMachineDescriptor
{
	FRowingMachineId Id;
	std::string DisplayLabel;
	std::optional<std::int32_t> SignalStrengthDbm;
	ERowingMachineKind KindHint = ERowingMachineKind::Unknown;
};

struct FRowingMachineInfo
{
	std::string Manufacturer;
	std::string Model;
	std::string HardwareVersion;
	std::string FirmwareVersion;
	ERowingMachineKind MachineKind = ERowingMachineKind::Unknown;
	FRowingMetricSet SupportedMetrics = ToRowingMetricSet(ERowingMetric::None);
	std::uint32_t CapabilityProfileVersion = 0;
	ERowingMachineSupportState SupportState =
		ERowingMachineSupportState::Blocked;
};

struct FRowingQueueDiagnostics
{
	std::uint32_t CurrentDepth = 0;
	std::uint32_t Capacity = 0;
	std::uint32_t HighWaterMark = 0;
	std::uint64_t OverflowCount = 0;
};

struct FRowingMachineDiagnostics
{
	// Replay/mock implementations may have no transport acquisition queue.
	std::optional<FRowingQueueDiagnostics> AcquisitionQueue;
	FRowingQueueDiagnostics EventQueue;
};

enum class ERowingConnectionState : std::uint8_t
{
	Idle,
	Scanning,
	Connecting,
	Discovering,
	ReadingIdentity,
	Subscribing,
	Ready,
	Stale,
	Reconnecting,
	Unsupported,
	Failed,
	PermissionDenied,
	// Live data is available only for local diagnostics and is not workout-ready.
	DiagnosticOnly
};

enum class ERowingConnectionReason : std::uint8_t
{
	None,
	UserRequested,
	OperationStarted,
	ReadinessConfirmed,
	TelemetryTimedOut,
	TelemetryResumed,
	LinkLost,
	ReconnectStarted,
	CapabilityRejected,
	OperationFailed,
	Shutdown,
	DiagnosticObservationConfirmed
};

enum class ERowingFaultCode : std::uint8_t
{
	Permission,
	ScanTimeout,
	ConnectionTimeout,
	Disconnected,
	MissingService,
	MissingCharacteristic,
	UnsupportedIdentity,
	WrongMachineType,
	InvalidProperty,
	InvalidPacketLength,
	InvalidValue,
	QueueOverflow,
	StaleTelemetry,
	InternalLifecycleError
};

enum class ERowingFaultSeverity : std::uint8_t
{
	Info,
	Warning,
	Recoverable,
	Terminal
};

enum class ERowingOperation : std::uint8_t
{
	None,
	Scan,
	Connect,
	Discover,
	ReadIdentity,
	Subscribe,
	ReceiveTelemetry,
	Reconnect,
	Disconnect,
	Shutdown
};

struct FRowingFault
{
	ERowingFaultCode Code = ERowingFaultCode::InternalLifecycleError;
	ERowingFaultSeverity Severity = ERowingFaultSeverity::Terminal;
	ERowingOperation Operation = ERowingOperation::None;
	ERowingConnectionState ConnectionState = ERowingConnectionState::Idle;
	std::string DiagnosticText;
	std::optional<std::uint64_t> ExpectedValue;
	std::optional<std::uint64_t> ActualValue;
};

struct FRowingConnectionStateChanged
{
	ERowingConnectionState PreviousState = ERowingConnectionState::Idle;
	ERowingConnectionState NewState = ERowingConnectionState::Idle;
	ERowingConnectionReason Reason = ERowingConnectionReason::None;
};

struct FRowingTelemetryStale
{
	std::uint64_t LastSequence = 0;
	std::uint64_t AgeMs = 0;
};

struct FRowingConnectionRestored
{
	FRowingMachineInfo MachineInfo;
	std::uint64_t GapDurationMs = 0;
};

enum class ERowingMachineEventKind : std::uint8_t
{
	MachineDiscovered,
	ConnectionStateChanged,
	MachineInfoObserved,
	MetricSampled,
	TelemetryStale,
	ConnectionRestored,
	FaultObserved,
	DiagnosticSampleObserved,
	MetricCorrected,
	StrokeMetricsObserved
};

struct FRowingDiagnosticSample
{
	FRowingMetricSample Sample;
};

struct FRowingMetricCorrection
{
	// Consumers replace the complete sample with this corrected snapshot.
	std::uint64_t TargetSampleSequence = 0;
	FRowingMetricSample CorrectedSample;
};

enum class ERowingStrokeMetricsSource : std::uint8_t
{
	KinematicsAndForce,
	PowerAndProjection
};

// A sparse, timestamped stroke record. PM characteristics have independent
// update paths, so two records with the same StrokeCount may complement each
// other; consumers must not join them by arrival order alone.
struct FRowingStrokeMetrics
{
	ERowingStrokeMetricsSource Source =
		ERowingStrokeMetricsSource::KinematicsAndForce;
	std::uint64_t SourceElapsedMs = 0;
	std::optional<std::uint64_t> StrokeCount;
	std::optional<std::uint64_t> CumulativeDistanceMm;
	std::optional<std::uint32_t> DriveLengthMm;
	std::optional<std::uint32_t> DriveTimeMs;
	std::optional<std::uint32_t> RecoveryTimeMs;
	std::optional<std::uint32_t> StrokeDistanceMm;
	std::optional<std::uint32_t> PeakDriveForceDeciLb;
	std::optional<std::uint32_t> AverageDriveForceDeciLb;
	std::optional<std::uint32_t> WorkPerStrokeDeciJoules;
	std::optional<std::uint32_t> StrokePowerW;
	std::optional<std::uint32_t> CaloriesPerHour;
	std::optional<std::uint64_t> ProjectedWorkTimeMs;
	std::optional<std::uint64_t> ProjectedWorkDistanceMm;
	// Concept2's BLE table names this field "projected work other" but does
	// not define a stable unit. Preserve the raw 24-bit value without inference.
	std::optional<std::uint32_t> ProjectedWorkOtherRaw;
};

using FRowingMachineEventPayload = std::variant<FRowingMachineDescriptor,
												FRowingConnectionStateChanged,
												FRowingMachineInfo,
												FRowingMetricSample,
												FRowingTelemetryStale,
												FRowingConnectionRestored,
												FRowingFault,
												FRowingDiagnosticSample,
												FRowingMetricCorrection,
												FRowingStrokeMetrics>;

static_assert(std::variant_size_v<FRowingMachineEventPayload> == 10);
static_assert(static_cast<std::uint8_t>(ERowingMachineEventKind::DiagnosticSampleObserved) == 7);
static_assert(static_cast<std::uint8_t>(ERowingMachineEventKind::MetricCorrected) == 8);
static_assert(static_cast<std::uint8_t>(ERowingMachineEventKind::StrokeMetricsObserved) == 9);

struct FRowingMachineEvent
{
	std::uint64_t AdapterEventSequence = 0;
	std::uint64_t MonotonicTimestampNs = 0;
	FRowingMachineEventPayload Payload;

	ERowingMachineEventKind GetKind() const noexcept
	{
		if (std::holds_alternative<FRowingMachineDescriptor>(Payload))
			return ERowingMachineEventKind::MachineDiscovered;
		if (std::holds_alternative<FRowingConnectionStateChanged>(Payload))
			return ERowingMachineEventKind::ConnectionStateChanged;
		if (std::holds_alternative<FRowingMachineInfo>(Payload))
			return ERowingMachineEventKind::MachineInfoObserved;
		if (std::holds_alternative<FRowingMetricSample>(Payload))
			return ERowingMachineEventKind::MetricSampled;
		if (std::holds_alternative<FRowingTelemetryStale>(Payload))
			return ERowingMachineEventKind::TelemetryStale;
		if (std::holds_alternative<FRowingConnectionRestored>(Payload))
			return ERowingMachineEventKind::ConnectionRestored;
		if (std::holds_alternative<FRowingFault>(Payload))
			return ERowingMachineEventKind::FaultObserved;
		if (std::holds_alternative<FRowingDiagnosticSample>(Payload))
			return ERowingMachineEventKind::DiagnosticSampleObserved;
		if (std::holds_alternative<FRowingMetricCorrection>(Payload))
			return ERowingMachineEventKind::MetricCorrected;
		return ERowingMachineEventKind::StrokeMetricsObserved;
	}
};

enum class ERowingCommandResultCode : std::uint8_t
{
	Accepted,
	InvalidCurrentState,
	Shutdown
};

struct FRowingCommandResult
{
	ERowingCommandResultCode Code = ERowingCommandResultCode::Accepted;

	bool IsAccepted() const noexcept
	{
		return Code == ERowingCommandResultCode::Accepted;
	}
};
