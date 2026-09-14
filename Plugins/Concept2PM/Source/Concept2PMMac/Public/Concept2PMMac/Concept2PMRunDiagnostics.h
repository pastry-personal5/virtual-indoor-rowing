#pragma once

#include "Concept2PMCore/Concept2PMProtocol.h"

#include <cstdint>
#include <cstddef>
#include <vector>

enum class EPM5CallbackStage : std::uint8_t
{
	Connection,
	Disconnection,
	ServiceDiscovery,
	IdentityCharacteristicDiscovery,
	TelemetryCharacteristicDiscovery,
	IdentityRead,
	TelemetryValueUpdate,
	NotificationSubscription,
	StatusRateWrite
};

enum class EPM5ErrorDomain : std::uint8_t
{
	None,
	CoreBluetooth,
	CoreBluetoothATT,
	Foundation,
	Other
};

struct FPM5CallbackErrorDiagnostic
{
	EPM5CallbackStage Stage = EPM5CallbackStage::Connection;
	std::uint16_t Characteristic = 0;
	std::uint64_t Count = 0;
	bool HasError = false;
	EPM5ErrorDomain LastErrorDomain = EPM5ErrorDomain::None;
	std::int32_t LastErrorCode = 0;
	std::uint64_t LastErrorMonotonicNs = 0;
};

// Explicitly opt-in limits for the local hardware probe. Raw payload capture is
// disabled in every product/ordinary diagnostic path. The defaults permit a
// 60-minute HIL soak plus setup/teardown while bounding memory and disk use.
struct FPM5HardwareProbeConfiguration
{
	bool CaptureRawTelemetry = false;
	std::uint64_t MaxCaptureDurationMs = 65ULL * 60ULL * 1000ULL;
	std::uint64_t MaxCapturedPacketCount = 100'000;
	std::size_t MaxCapturedPayloadBytes = 64;
	std::uint32_t EvidenceQueueCapacity = 4'096;
};

enum class EPM5ProbeCaptureStopReason : std::uint8_t
{
	None,
	DurationLimit,
	PacketLimit,
	RunStopped
};

// A PM-specific, owner-only probe record. It deliberately excludes peripheral
// identifiers and identity characteristic payloads. PayloadBytes contains only
// rowing-service telemetry and may be truncated to the configured bound.
struct FPM5ProbePacketEvidence
{
	std::uint64_t PacketSequence = 0;
	std::uint64_t CharacteristicSequence = 0;
	std::uint16_t Characteristic = 0;
	std::uint64_t ReceivedMonotonicNs = 0;
	ERowingConnectionState ConnectionState = ERowingConnectionState::Idle;
	Concept2PM::EPacketError ParserResult = Concept2PM::EPacketError::None;
	std::vector<std::size_t> ApprovedPacketLengths;
	std::size_t OriginalPayloadLength = 0;
	std::vector<std::uint8_t> PayloadBytes;
	bool PayloadTruncated = false;
};

struct FPM5ProbeCaptureDiagnostics
{
	bool Enabled = false;
	bool Active = false;
	std::uint64_t StartedMonotonicNs = 0;
	std::uint64_t MaxCaptureDurationMs = 0;
	std::uint64_t MaxCapturedPacketCount = 0;
	std::size_t MaxCapturedPayloadBytes = 0;
	std::uint32_t EvidenceQueueCapacity = 0;
	std::uint32_t EvidenceQueueCurrentDepth = 0;
	std::uint32_t EvidenceQueueHighWaterMark = 0;
	std::uint64_t ObservedPacketCount = 0;
	std::uint64_t CapturedPacketCount = 0;
	std::uint64_t CapturedPayloadByteCount = 0;
	std::uint64_t TruncatedPayloadCount = 0;
	std::uint64_t EvidenceQueueOverflowCount = 0;
	std::uint64_t LimitDroppedPacketCount = 0;
	EPM5ProbeCaptureStopReason StopReason = EPM5ProbeCaptureStopReason::None;
};

// PM-specific diagnostic extension for the local HIL tool. Product consumers
// should use IRowingMachine and its hardware-neutral snapshots instead.
struct FPM5RunDiagnostics
{
	std::uint32_t RequestedStatusPeriodMs = 0;
	std::uint64_t StatusRateWriteAttemptCount = 0;
	std::uint64_t StatusRateWriteSuccessCount = 0;
	std::uint64_t StatusRateWriteFailureCount = 0;
	std::vector<Concept2PM::FPM5CharacteristicDiagnostics> Characteristics;
	std::vector<FPM5CallbackErrorDiagnostic> CallbackErrors;
	FPM5ProbeCaptureDiagnostics ProbeCapture;
};

class IConcept2PMRunDiagnostics
{
  public:
	virtual ~IConcept2PMRunDiagnostics() = default;
	virtual FPM5RunDiagnostics GetPM5RunDiagnostics() const = 0;
	virtual bool TryPollPM5ProbePacket(FPM5ProbePacketEvidence &OutEvidence) = 0;
	virtual void FinalizePM5ProbeCapture() = 0;
};
