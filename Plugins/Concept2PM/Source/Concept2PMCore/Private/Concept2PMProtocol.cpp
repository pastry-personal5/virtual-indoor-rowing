#include "Concept2PMCore/Concept2PMProtocol.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Concept2PM
{
	namespace
	{
		std::uint32_t ReadU16(const std::vector<std::uint8_t> &Bytes,
							  std::size_t Offset)
		{
			return static_cast<std::uint32_t>(Bytes[Offset]) |
				   (static_cast<std::uint32_t>(Bytes[Offset + 1]) << 8U);
		}

		std::uint32_t ReadU24(const std::vector<std::uint8_t> &Bytes,
							  std::size_t Offset)
		{
			return static_cast<std::uint32_t>(Bytes[Offset]) |
				   (static_cast<std::uint32_t>(Bytes[Offset + 1]) << 8U) |
				   (static_cast<std::uint32_t>(Bytes[Offset + 2]) << 16U);
		}

		bool IsAllowedLength(const std::vector<std::size_t> &Lengths,
							 std::size_t Actual)
		{
			return std::find(Lengths.begin(), Lengths.end(), Actual) !=
				   Lengths.end();
		}

		ERowingWorkoutState DecodeWorkoutState(std::uint8_t Value,
											   bool &Unknown)
		{
			switch (Value)
			{
			case 0:
				return ERowingWorkoutState::WaitingToBegin;
			case 1:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
				return ERowingWorkoutState::Active;
			case 2:
				return ERowingWorkoutState::Paused;
			case 3:
				return ERowingWorkoutState::Resting;
			case 10:
			case 12:
			case 13:
				return ERowingWorkoutState::Complete;
			case 11:
				return ERowingWorkoutState::Terminated;
			default:
				Unknown = true;
				return ERowingWorkoutState::Unknown;
			}
		}

		ERowingState DecodeRowingState(std::uint8_t Value, bool &Unknown)
		{
			if (Value == 0)
				return ERowingState::Inactive;
			if (Value == 1)
				return ERowingState::Active;
			Unknown = true;
			return ERowingState::Unknown;
		}

		ERowingStrokeState DecodeStrokeState(std::uint8_t Value, bool &Unknown)
		{
			switch (Value)
			{
			case 0:
			case 1:
				return ERowingStrokeState::Waiting;
			case 2:
				return ERowingStrokeState::Drive;
			case 3:
				return ERowingStrokeState::Dwell;
			case 4:
				return ERowingStrokeState::Recovery;
			default:
				Unknown = true;
				return ERowingStrokeState::Unknown;
			}
		}

		bool IsKnownIndoorRowerMachineType(std::uint8_t Value)
		{
			return Value == 0 || Value == 1 || Value == 2 || Value == 3 ||
				   Value == 5 || Value == 7 || Value == 8 ||
				   (Value >= 16 && Value <= 20) || Value == 32 || Value == 64 ||
				   Value == 224;
		}
	} // namespace

	FDecodedPacket DecodePacket(std::uint16_t Characteristic,
								const std::vector<std::uint8_t> &Bytes,
								const std::vector<std::size_t> &ApprovedLengths)
	{
		FDecodedPacket Result;
		if (Characteristic != GeneralStatus &&
			Characteristic != AdditionalStatus1 &&
			Characteristic != AdditionalStatus2 &&
			Characteristic != StrokeData &&
			Characteristic != AdditionalStrokeData)
		{
			Result.Error = {EPacketError::UnknownCharacteristic,
							Characteristic,
							0,
							Bytes.size()};
			return Result;
		}
		if (!IsAllowedLength(ApprovedLengths, Bytes.size()))
		{
			Result.Error = {EPacketError::LengthNotApproved,
							Characteristic,
							ApprovedLengths.empty() ? 0
													: ApprovedLengths.front(),
							Bytes.size()};
			return Result;
		}

		// The packet layout itself still has a strict minimum even if a profile
		// was misconfigured. This prevents an approved length from becoming an
		// overread.
		const std::size_t Minimum = Characteristic == GeneralStatus		  ? 19
									: Characteristic == AdditionalStatus1 ? 17
									: Characteristic == AdditionalStatus2 ? 20
									: Characteristic == StrokeData		  ? 20
																		  : 15;
		if (Bytes.size() < Minimum)
		{
			Result.Error = {EPacketError::LengthNotApproved,
							Characteristic,
							Minimum,
							Bytes.size()};
			return Result;
		}

		if (Characteristic == GeneralStatus)
		{
			FGeneralStatusFact Fact;
			Fact.ElapsedMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 0)) * 10ULL;
			Fact.DistanceMm =
				static_cast<std::uint64_t>(ReadU24(Bytes, 3)) * 100ULL;
			Fact.WorkoutState =
				DecodeWorkoutState(Bytes[8], Fact.HasUnknownEnum);
			Fact.RowingState = DecodeRowingState(Bytes[9], Fact.HasUnknownEnum);
			Fact.StrokeState =
				DecodeStrokeState(Bytes[10], Fact.HasUnknownEnum);
			Fact.DragFactor = Bytes[18];
			Result.GeneralStatus = Fact;
		}
		else if (Characteristic == AdditionalStatus1)
		{
			FAdditionalStatus1Fact Fact;
			Fact.ElapsedMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 0)) * 10ULL;
			Fact.SpeedMmPerS = ReadU16(Bytes, 3);
			Fact.StrokeRateDeciSpm = static_cast<std::uint32_t>(Bytes[5]) * 10U;
			if (Bytes[6] != 255)
				Fact.HeartRateBpm = Bytes[6];
			Fact.PaceMsPer500M = ReadU16(Bytes, 7) * 10U;
			Fact.HasUnknownMachineType =
				!IsKnownIndoorRowerMachineType(Bytes[16]);
			Result.AdditionalStatus1 = Fact;
		}
		else if (Characteristic == AdditionalStatus2)
		{
			FAdditionalStatus2Fact Fact;
			Fact.ElapsedMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 0)) * 10ULL;
			Fact.AveragePowerW = ReadU16(Bytes, 4);
			Fact.Calories = ReadU16(Bytes, 6);
			Result.AdditionalStatus2 = Fact;
		}
		else if (Characteristic == StrokeData)
		{
			FStrokeDataFact Fact;
			Fact.ElapsedMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 0)) * 10ULL;
			Fact.CumulativeDistanceMm =
				static_cast<std::uint64_t>(ReadU24(Bytes, 3)) * 100ULL;
			Fact.DriveLengthMm = static_cast<std::uint32_t>(Bytes[6]) * 10U;
			Fact.DriveTimeMs = static_cast<std::uint32_t>(Bytes[7]) * 10U;
			Fact.RecoveryTimeMs = ReadU16(Bytes, 8) * 10U;
			Fact.StrokeDistanceMm = ReadU16(Bytes, 10) * 10U;
			Fact.PeakDriveForceDeciLb = ReadU16(Bytes, 12);
			Fact.AverageDriveForceDeciLb = ReadU16(Bytes, 14);
			Fact.WorkPerStrokeDeciJoules = ReadU16(Bytes, 16);
			Fact.StrokeCount = ReadU16(Bytes, 18);
			Result.StrokeData = Fact;
		}
		else
		{
			FAdditionalStrokeDataFact Fact;
			Fact.ElapsedMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 0)) * 10ULL;
			Fact.StrokePowerW = ReadU16(Bytes, 3);
			Fact.CaloriesPerHour = ReadU16(Bytes, 5);
			Fact.StrokeCount = ReadU16(Bytes, 7);
			Fact.ProjectedWorkTimeMs =
				static_cast<std::uint64_t>(ReadU24(Bytes, 9)) * 1000ULL;
			Fact.ProjectedWorkDistanceMm =
				static_cast<std::uint64_t>(ReadU24(Bytes, 12)) * 1000ULL;
			if (Bytes.size() >= 18)
				Fact.ProjectedWorkOtherRaw = ReadU24(Bytes, 15);
			Result.AdditionalStrokeData = Fact;
		}
		return Result;
	}

	bool IsReconnectContinuationCompatible(
		const FGeneralStatusFact &Previous,
		const FGeneralStatusFact &Candidate) noexcept
	{
		if (Previous.WorkoutState == ERowingWorkoutState::Unknown ||
			Candidate.WorkoutState == ERowingWorkoutState::Unknown ||
			Candidate.ElapsedMs < Previous.ElapsedMs ||
			Candidate.DistanceMm < Previous.DistanceMm)
		{
			return false;
		}

		if (Previous.WorkoutState == ERowingWorkoutState::Complete)
			return Candidate.WorkoutState == ERowingWorkoutState::Complete;
		if (Previous.WorkoutState == ERowingWorkoutState::Terminated)
			return Candidate.WorkoutState == ERowingWorkoutState::Terminated;
		return Previous.WorkoutState == ERowingWorkoutState::WaitingToBegin ||
			   Candidate.WorkoutState != ERowingWorkoutState::WaitingToBegin;
	}

	FCapabilityEvaluation EvaluateCapability(
		const FPM5Identity &Identity,
		const std::vector<FPM5CapabilityProfile> &Profiles) noexcept
	{
		FCapabilityEvaluation Result;
		if (Identity.MachineKind != ERowingMachineKind::IndoorRower)
		{
			return Result;
		}
		for (const FPM5CapabilityProfile &Profile : Profiles)
		{
			if (Profile.MonitorModel != Identity.MonitorModel ||
				Profile.HardwareRevision != Identity.HardwareRevision ||
				Profile.FirmwareRevision != Identity.FirmwareRevision ||
				Profile.MachineKind != Identity.MachineKind)
			{
				continue;
			}
			Result.SupportState = Profile.SupportState;
			Result.ProfileVersion = Profile.Version;
			Result.Profile = &Profile;
			return Result;
		}
		return Result;
	}

	struct FTelemetryMerger::FBucket
	{
		std::uint64_t ElapsedMs = 0;
		std::uint64_t FirstReceivedNs = 0;
		std::optional<FGeneralStatusFact> General;
		std::optional<FAdditionalStatus1Fact> Additional1;
		std::optional<FAdditionalStatus2Fact> Additional2;
		std::optional<FStrokeDataFact> Stroke;
		std::optional<FAdditionalStrokeDataFact> AdditionalStroke;
		FRowingQualityFlags Flags =
			ToRowingQualityFlags(ERowingQualityFlag::None);
	};

	FTelemetryMerger::FTelemetryMerger(std::uint64_t InMergeWindowNs)
		: MergeWindowNs(InMergeWindowNs)
	{
	}

	FTelemetryMerger::~FTelemetryMerger() = default;

	std::optional<FRowingMetricSample>
	FTelemetryMerger::Push(const FDecodedPacket &Packet,
						   std::uint64_t ReceivedMonotonicNs)
	{
		if (!Packet.IsValid())
			return std::nullopt;
		std::uint64_t Elapsed = 0;
		if (Packet.GeneralStatus)
			Elapsed = Packet.GeneralStatus->ElapsedMs;
		else if (Packet.AdditionalStatus1)
			Elapsed = Packet.AdditionalStatus1->ElapsedMs;
		else if (Packet.AdditionalStatus2)
			Elapsed = Packet.AdditionalStatus2->ElapsedMs;
		else if (Packet.StrokeData)
			Elapsed = Packet.StrokeData->ElapsedMs;
		else if (Packet.AdditionalStrokeData)
			Elapsed = Packet.AdditionalStrokeData->ElapsedMs;
		else
			return std::nullopt;

		if (TryApplyLateCorrection(Packet, Elapsed))
			return std::nullopt;

		auto Output = Flush(ReceivedMonotonicNs);
		if (!Pending || Pending->ElapsedMs != Elapsed)
		{
			if (Pending && !Output)
				Output = PublishPending();
			Pending = std::make_unique<FBucket>();
			Pending->ElapsedMs = Elapsed;
			Pending->FirstReceivedNs = ReceivedMonotonicNs;
		}
		if (Packet.GeneralStatus)
			Pending->General = Packet.GeneralStatus;
		if (Packet.AdditionalStatus1)
			Pending->Additional1 = Packet.AdditionalStatus1;
		if (Packet.AdditionalStatus2)
			Pending->Additional2 = Packet.AdditionalStatus2;
		if (Packet.StrokeData)
			Pending->Stroke = Packet.StrokeData;
		if (Packet.AdditionalStrokeData)
			Pending->AdditionalStroke = Packet.AdditionalStrokeData;
		return Output;
	}

	std::optional<FRowingMetricCorrection>
	FTelemetryMerger::TakePendingCorrection()
	{
		auto Correction = std::move(PendingCorrection);
		PendingCorrection.reset();
		return Correction;
	}

	std::optional<FRowingMetricSample>
	FTelemetryMerger::Flush(std::uint64_t NowMonotonicNs)
	{
		if (Pending && NowMonotonicNs >= Pending->FirstReceivedNs &&
			NowMonotonicNs - Pending->FirstReceivedNs >= MergeWindowNs)
		{
			return PublishPending();
		}
		return std::nullopt;
	}

	void FTelemetryMerger::MarkReconnected() noexcept
	{
		Reconnected = true;
		LastPublishedSample.reset();
		PendingCorrection.reset();
	}

	bool FTelemetryMerger::TryApplyLateCorrection(
		const FDecodedPacket &Packet,
		std::uint64_t ElapsedMs)
	{
		// Corrections are deliberately bounded to the sole latest publication.
		// A packet for any older timestamp would need retained history and a
		// deterministic correction order, neither of which this merger claims.
		if (!LastPublishedSample || LastPublishedSample->SourceElapsedMs != ElapsedMs ||
			PendingCorrection || !Packet.AdditionalStatus1 ||
			HasRowingQualityFlag(LastPublishedSample->QualityFlags,
								 ERowingQualityFlag::LateCorrection))
			return false;

		FRowingMetricSample Corrected = *LastPublishedSample;
		const FAdditionalStatus1Fact &Late = *Packet.AdditionalStatus1;
		Corrected.SpeedMmPerS = Late.SpeedMmPerS;
		Corrected.StrokeRateDeciSpm = Late.StrokeRateDeciSpm;
		Corrected.HeartRateBpm = Late.HeartRateBpm;
		Corrected.PaceMsPer500M = Late.PaceMsPer500M;
		Corrected.QualityFlags &=
			~ToRowingQualityFlags(ERowingQualityFlag::MissingField);
		Corrected.QualityFlags |= ToRowingQualityFlags(ERowingQualityFlag::LateCorrection);
		if (Late.HasUnknownMachineType)
			Corrected.QualityFlags |=
				ToRowingQualityFlags(ERowingQualityFlag::UnsupportedValue);

		PendingCorrection = {LastPublishedSample->Sequence, Corrected};
		LastPublishedSample = std::move(Corrected);
		return true;
	}

	std::optional<FRowingMetricSample> FTelemetryMerger::PublishPending()
	{
		if (!Pending)
			return std::nullopt;
		FBucket Bucket = std::move(*Pending);
		Pending.reset();
		if (!Bucket.General)
			return std::nullopt;

		FRowingMetricSample Sample;
		Sample.Sequence = NextSequence++;
		Sample.SourceElapsedMs = Bucket.General->ElapsedMs;
		Sample.ReceivedMonotonicNs = Bucket.FirstReceivedNs;
		Sample.DistanceMm = Bucket.General->DistanceMm;
		Sample.WorkoutState = Bucket.General->WorkoutState;
		Sample.RowingState = Bucket.General->RowingState;
		Sample.StrokeState = Bucket.General->StrokeState;
		Sample.DragFactor = Bucket.General->DragFactor;
		Sample.QualityFlags = Bucket.Flags;
		if (!Bucket.Additional1)
		{
			Sample.QualityFlags |=
				ToRowingQualityFlags(ERowingQualityFlag::MissingField);
		}
		else
		{
			Sample.SpeedMmPerS = Bucket.Additional1->SpeedMmPerS;
			Sample.StrokeRateDeciSpm = Bucket.Additional1->StrokeRateDeciSpm;
			Sample.HeartRateBpm = Bucket.Additional1->HeartRateBpm;
			Sample.PaceMsPer500M = Bucket.Additional1->PaceMsPer500M;
			if (Bucket.Additional1->HasUnknownMachineType)
				Sample.QualityFlags |=
					ToRowingQualityFlags(ERowingQualityFlag::UnsupportedValue);
		}
		if (Bucket.Additional2)
		{
			Sample.AveragePowerW = Bucket.Additional2->AveragePowerW;
			Sample.Calories = Bucket.Additional2->Calories;
		}
		if (Bucket.Stroke)
			Sample.StrokeCount = Bucket.Stroke->StrokeCount;
		if (Bucket.AdditionalStroke)
		{
			Sample.StrokePowerW = Bucket.AdditionalStroke->StrokePowerW;
			if (!Sample.StrokeCount)
				Sample.StrokeCount = Bucket.AdditionalStroke->StrokeCount;
		}
		if (Bucket.General->HasUnknownEnum)
			Sample.QualityFlags |=
				ToRowingQualityFlags(ERowingQualityFlag::UnsupportedValue);
		if (Reconnected)
		{
			Sample.QualityFlags |=
				ToRowingQualityFlags(ERowingQualityFlag::DeviceReconnected);
			Reconnected = false;
		}
		if (LastElapsedMs)
		{
			if (Sample.SourceElapsedMs < *LastElapsedMs)
				Sample.QualityFlags |=
					ToRowingQualityFlags(ERowingQualityFlag::TimeRegression);
			else if (Sample.SourceElapsedMs == *LastElapsedMs)
				Sample.QualityFlags |=
					ToRowingQualityFlags(ERowingQualityFlag::Duplicate);
			else if (Sample.SourceElapsedMs - *LastElapsedMs > 500)
				Sample.QualityFlags |=
					ToRowingQualityFlags(ERowingQualityFlag::SourceGap);
			if (Sample.SourceElapsedMs > *LastElapsedMs)
				LastElapsedMs = Sample.SourceElapsedMs;
		}
		else
			LastElapsedMs = Sample.SourceElapsedMs;
		if (LastDistanceMm)
		{
			if (Sample.DistanceMm < *LastDistanceMm)
				Sample.QualityFlags |=
					ToRowingQualityFlags(ERowingQualityFlag::DistanceRegression);
			else if (Sample.DistanceMm > *LastDistanceMm)
				LastDistanceMm = Sample.DistanceMm;
		}
		else
			LastDistanceMm = Sample.DistanceMm;
		LastPublishedSample = Sample;
		return Sample;
	}
} // namespace Concept2PM
