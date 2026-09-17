#pragma once

#include "RowingDevice/RowingMachineTypes.h"

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Values in this file are private to the Concept2 adapter.  No caller above
// this module needs to know a PM characteristic number or packet layout.
namespace Concept2PM
{
	inline constexpr std::uint16_t DeviceInformationService = 0x0010;
	// C2 PM Control primary service (diagnostic-only managed-workout spike,
	// Phase 0 Milestone 4 Spike A). Carries CSAFE command/response frames;
	// never subscribed to for passive telemetry.
	inline constexpr std::uint16_t ControlService = 0x0020;
	inline constexpr std::uint16_t ControlReceive = 0x0021;	 // WRITE: CSAFE command to the PM.
	inline constexpr std::uint16_t ControlTransmit = 0x0022; // READ: CSAFE response from the PM.
	inline constexpr std::uint16_t RowingService = 0x0030;
	inline constexpr std::uint16_t GeneralStatus = 0x0031;
	inline constexpr std::uint16_t AdditionalStatus1 = 0x0032;
	inline constexpr std::uint16_t AdditionalStatus2 = 0x0033;
	inline constexpr std::uint16_t StatusSampleRate = 0x0034;
	inline constexpr std::uint16_t StrokeData = 0x0035;
	inline constexpr std::uint16_t AdditionalStrokeData = 0x0036;
	inline constexpr std::uint16_t AdditionalStatus3 = 0x003E;

	enum class EPacketError : std::uint8_t
	{
		None,
		UnknownCharacteristic,
		LengthNotApproved,
		InvalidValue
	};

	struct FPacketError
	{
		EPacketError Code = EPacketError::None;
		std::uint16_t Characteristic = 0;
		std::size_t ExpectedLength = 0;
		std::size_t ActualLength = 0;
	};

	struct FPM5PacketLengthCount
	{
		std::size_t PacketLength = 0;
		std::uint64_t Count = 0;
	};

	// Bounded, redacted run aggregates for HIL diagnosis. Inter-arrival values
	// are bucketed in 10 ms bins through 990 ms, then one overflow bin.
	struct FPM5CharacteristicDiagnostics
	{
		static constexpr std::size_t IntervalBinCount = 101;
		static constexpr std::uint64_t LongGapThresholdNs = 500'000'000ULL;

		std::uint16_t Characteristic = 0;
		bool PropertiesObserved = false;
		std::uint8_t ObservedProperties = 0;
		std::uint64_t NotificationEnableAttemptCount = 0;
		std::uint64_t NotificationEnableSuccessCount = 0;
		std::uint64_t NotificationEnableFailureCount = 0;
		std::uint64_t NotificationCount = 0;
		std::uint64_t FirstReceivedMonotonicNs = 0;
		std::uint64_t LastReceivedMonotonicNs = 0;
		std::uint64_t IntervalCount = 0;
		std::uint64_t IntervalTotalNs = 0;
		std::uint64_t MinIntervalNs = 0;
		std::uint64_t MaxIntervalNs = 0;
		std::uint64_t LongGapCount = 0;
		std::vector<std::size_t> ApprovedPacketLengths;
		std::vector<FPM5PacketLengthCount> ObservedPacketLengths;
		std::array<std::uint64_t, IntervalBinCount> IntervalHistogram{};
		std::array<std::uint64_t, 4> ParserErrorCounts{};
		EPacketError LastParserErrorCode = EPacketError::None;
		std::size_t LastParserErrorActualLength = 0;
		std::uint64_t LastParserErrorMonotonicNs = 0;

		void RecordNotification(std::uint64_t ReceivedMonotonicNs,
								std::size_t ActualPacketLength = 0)
		{
			if (ActualPacketLength != 0)
			{
				bool FoundLength = false;
				for (FPM5PacketLengthCount &Length : ObservedPacketLengths)
				{
					if (Length.PacketLength != ActualPacketLength)
						continue;
					++Length.Count;
					FoundLength = true;
					break;
				}
				if (!FoundLength)
					ObservedPacketLengths.push_back({ActualPacketLength, 1});
			}
			if (NotificationCount == 0)
				FirstReceivedMonotonicNs = ReceivedMonotonicNs;
			else if (ReceivedMonotonicNs < LastReceivedMonotonicNs)
			{
				++NotificationCount;
				return;
			}
			else
			{
				const std::uint64_t IntervalNs =
					ReceivedMonotonicNs - LastReceivedMonotonicNs;
				++IntervalCount;
				IntervalTotalNs += IntervalNs;
				if (IntervalCount == 1 || IntervalNs < MinIntervalNs)
					MinIntervalNs = IntervalNs;
				MaxIntervalNs = IntervalNs > MaxIntervalNs ? IntervalNs : MaxIntervalNs;
				if (TracksContinuousStatusCadence() &&
					IntervalNs >= LongGapThresholdNs)
					++LongGapCount;
				const std::uint64_t Bin = IntervalNs / 10'000'000ULL;
				const std::size_t Index = static_cast<std::size_t>(
					Bin < IntervalBinCount ? Bin : IntervalBinCount - 1);
				++IntervalHistogram[Index];
			}
			LastReceivedMonotonicNs = ReceivedMonotonicNs;
			++NotificationCount;
		}

		bool TracksContinuousStatusCadence() const noexcept
		{
			return Characteristic == GeneralStatus ||
				   Characteristic == AdditionalStatus1 ||
				   Characteristic == AdditionalStatus2 ||
				   Characteristic == AdditionalStatus3;
		}

		void RecordParserError(EPacketError Error,
							   std::size_t ActualLength = 0,
							   std::uint64_t ReceivedMonotonicNs = 0) noexcept
		{
			const auto Index = static_cast<std::size_t>(Error);
			if (Index > 0 && Index < ParserErrorCounts.size())
			{
				++ParserErrorCounts[Index];
				LastParserErrorCode = Error;
				LastParserErrorActualLength = ActualLength;
				LastParserErrorMonotonicNs = ReceivedMonotonicNs;
			}
		}
	};

	struct FGeneralStatusFact
	{
		std::uint64_t ElapsedMs = 0;
		std::uint64_t DistanceMm = 0;
		ERowingWorkoutState WorkoutState = ERowingWorkoutState::Unknown;
		ERowingState RowingState = ERowingState::Unknown;
		ERowingStrokeState StrokeState = ERowingStrokeState::Unknown;
		std::optional<std::uint32_t> DragFactor;
		bool HasUnknownEnum = false;
	};

	struct FAdditionalStatus1Fact
	{
		std::uint64_t ElapsedMs = 0;
		std::optional<std::uint32_t> SpeedMmPerS;
		std::optional<std::uint32_t> StrokeRateDeciSpm;
		std::optional<std::uint32_t> HeartRateBpm;
		std::optional<std::uint32_t> PaceMsPer500M;
		bool HasUnknownMachineType = false;
	};

	struct FAdditionalStatus2Fact
	{
		std::uint64_t ElapsedMs = 0;
		std::optional<std::uint32_t> AveragePowerW;
		std::optional<std::uint32_t> Calories;
	};

	struct FStrokeDataFact
	{
		std::uint64_t ElapsedMs = 0;
		std::optional<std::uint64_t> CumulativeDistanceMm;
		std::optional<std::uint32_t> DriveLengthMm;
		std::optional<std::uint32_t> DriveTimeMs;
		std::optional<std::uint32_t> RecoveryTimeMs;
		std::optional<std::uint32_t> StrokeDistanceMm;
		std::optional<std::uint32_t> PeakDriveForceDeciLb;
		std::optional<std::uint32_t> AverageDriveForceDeciLb;
		std::optional<std::uint32_t> WorkPerStrokeDeciJoules;
		std::optional<std::uint64_t> StrokeCount;
	};

	struct FAdditionalStrokeDataFact
	{
		std::uint64_t ElapsedMs = 0;
		std::optional<std::uint32_t> StrokePowerW;
		std::optional<std::uint32_t> CaloriesPerHour;
		std::optional<std::uint64_t> StrokeCount;
		std::optional<std::uint64_t> ProjectedWorkTimeMs;
		std::optional<std::uint64_t> ProjectedWorkDistanceMm;
		std::optional<std::uint32_t> ProjectedWorkOtherRaw;
	};

	// Reconnect may resume the same PM workout only when cumulative source facts
	// remain monotonic and the workout state has not reset or resumed after a
	// terminal outcome. No measurement is corrected or synthesized here.
	bool IsReconnectContinuationCompatible(
		const FGeneralStatusFact &Previous,
		const FGeneralStatusFact &Candidate) noexcept;

	struct FDecodedPacket
	{
		std::optional<FGeneralStatusFact> GeneralStatus;
		std::optional<FAdditionalStatus1Fact> AdditionalStatus1;
		std::optional<FAdditionalStatus2Fact> AdditionalStatus2;
		std::optional<FStrokeDataFact> StrokeData;
		std::optional<FAdditionalStrokeDataFact> AdditionalStrokeData;
		FPacketError Error;
		bool IsValid() const noexcept
		{
			return Error.Code == EPacketError::None;
		}
	};

	// Exact packet lengths must be supplied by the approved capability profile.
	// An empty vector rejects every notification, intentionally.
	FDecodedPacket
	DecodePacket(std::uint16_t Characteristic,
				 const std::vector<std::uint8_t> &Bytes,
				 const std::vector<std::size_t> &ApprovedLengths);

	enum class EPM5CharacteristicProperty : std::uint8_t
	{
		None = 0,
		Read = 1U << 0U,
		Write = 1U << 1U,
		WriteWithoutResponse = 1U << 2U,
		Notify = 1U << 3U,
		Indicate = 1U << 4U
	};

	constexpr std::uint8_t
	ToPM5CharacteristicProperties(EPM5CharacteristicProperty Property) noexcept
	{
		return static_cast<std::uint8_t>(Property);
	}

	constexpr bool HasPM5CharacteristicProperty(
		std::uint8_t Properties,
		EPM5CharacteristicProperty Property) noexcept
	{
		return (Properties & ToPM5CharacteristicProperties(Property)) != 0;
	}

	struct FPM5CharacteristicProfile
	{
		std::uint16_t ShortId = 0;
		bool Required = false;
		bool RequiresNotify = false;
		std::uint8_t RequiredProperties =
			ToPM5CharacteristicProperties(EPM5CharacteristicProperty::None);
		std::vector<std::size_t> AllowedPacketLengths;
		FRowingMetricSet ImplementedMetrics =
			ToRowingMetricSet(ERowingMetric::None);
	};

	struct FPM5CapabilityProfile
	{
		std::uint32_t Version = 0;
		std::string MonitorModel;
		std::string HardwareRevision;
		std::string FirmwareRevision;
		ERowingMachineKind MachineKind = ERowingMachineKind::Unknown;
		ERowingMachineSupportState SupportState =
			ERowingMachineSupportState::Blocked;
		// Diagnostic-only profiles may decode passive notifications but can
		// never authorize Ready or workout commands.
		bool DiagnosticOnly = false;
		// Zero means the profile has not approved a status-rate control write.
		// The PM5 adapter currently recognizes only the documented 100 ms rate.
		std::uint32_t RequestedStatusPeriodMs = 0;
		std::vector<FPM5CharacteristicProfile> Characteristics;
	};

	struct FPM5Identity
	{
		std::string MonitorModel;
		std::string HardwareRevision;
		std::string FirmwareRevision;
		ERowingMachineKind MachineKind = ERowingMachineKind::Unknown;
	};

	struct FCapabilityEvaluation
	{
		ERowingMachineSupportState SupportState =
			ERowingMachineSupportState::Blocked;
		std::uint32_t ProfileVersion = 0;
		const FPM5CapabilityProfile *Profile = nullptr;
	};

	FCapabilityEvaluation EvaluateCapability(
		const FPM5Identity &Identity,
		const std::vector<FPM5CapabilityProfile> &Profiles) noexcept;

	class FTelemetryMerger
	{
	  public:
		explicit FTelemetryMerger(std::uint64_t MergeWindowNs = 30'000'000ULL);
		~FTelemetryMerger();

		// A returned sample is immutable. It is emitted after general-status or
		// after the bounded join window; omitted fields remain absent.
		std::optional<FRowingMetricSample>
		Push(const FDecodedPacket &Packet, std::uint64_t ReceivedMonotonicNs);
		std::optional<FRowingMetricSample> Flush(std::uint64_t NowMonotonicNs);
		// At most one full replacement for the most recently published sample.
		// A caller takes it after Push/Flush before accepting another packet.
		std::optional<FRowingMetricCorrection> TakePendingCorrection();
		void MarkReconnected() noexcept;

	  private:
		struct FBucket;
		std::unique_ptr<FBucket> Pending;
		std::uint64_t NextSequence = 1;
		std::optional<std::uint64_t> LastElapsedMs;
		std::optional<std::uint64_t> LastDistanceMm;
		std::optional<FRowingMetricSample> LastPublishedSample;
		std::optional<FRowingMetricCorrection> PendingCorrection;
		bool Reconnected = false;
		std::uint64_t MergeWindowNs;

		std::optional<FRowingMetricSample> PublishPending();
		bool TryApplyLateCorrection(const FDecodedPacket &Packet,
									std::uint64_t ElapsedMs);
	};
} // namespace Concept2PM
