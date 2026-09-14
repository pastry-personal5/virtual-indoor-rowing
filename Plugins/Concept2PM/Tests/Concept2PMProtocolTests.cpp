#include "Concept2PMCore/Concept2PMProtocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

using namespace Concept2PM;

namespace
{
	constexpr std::size_t GeneralStatusLength = 19;
	constexpr std::size_t AdditionalStatus1Length = 17;
	constexpr std::size_t AdditionalStatus2Length = 20;
	constexpr std::size_t StrokeDataLength = 20;
	constexpr std::size_t AdditionalStrokeDataLength = 18;
	constexpr std::size_t LegacyAdditionalStrokeDataLength = 15;

	std::vector<std::uint8_t> GeneralStatusPacket()
	{
		// 123.45 s, 678.9 m, active/active/drive, drag 120.
		return {0x39, 0x30, 0x00, 0x85, 0x1A, 0x00, 0, 0, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 120};
	}

	std::vector<std::uint8_t> AdditionalStatus1Packet()
	{
		// 123.45 s, 10 m/s, 30 spm, unavailable heart rate, 250 ms/500 m.
		return {0x39, 0x30, 0x00, 0x10, 0x27, 30, 255, 0xFA, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
	}

	std::vector<std::uint8_t> AdditionalStatus2Packet()
	{
		// 123.45 s, 321 W average power, 456 calories.
		return {0x39, 0x30, 0x00, 0, 0x41, 0x01, 0xC8, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	}

	std::vector<std::uint8_t> StrokeDataPacket()
	{
		// 123.45 s, drive 149 cm/1.28 s, recovery 2.58 s, 25 lb peak,
		// 15 lb average, 1000 J work, stroke count 1234.
		return {0x39, 0x30, 0x00, 0x85, 0x1A, 0x00, 149, 128, 0x02, 0x01, 0x64, 0x00, 0xFA, 0x00, 0x96, 0x00, 0x10, 0x27, 0xD2, 0x04};
	}

	std::vector<std::uint8_t> AdditionalStrokeDataPacket()
	{
		// 123.45 s, 250 W, 1200 cal/hr, 1234 strokes, 300 s/1234 m
		// projected work and an opaque projected-work-other value.
		return {0x39, 0x30, 0x00, 0xFA, 0x00, 0xB0, 0x04, 0xD2, 0x04, 0x2C, 0x01, 0x00, 0xD2, 0x04, 0x00, 0x34, 0x12, 0x00};
	}

	struct FPacketFixture
	{
		std::uint16_t Characteristic;
		std::size_t Length;
		std::vector<std::uint8_t> (*MakePacket)();
	};

	constexpr std::array<FPacketFixture, 5> PacketFixtures{{
		{GeneralStatus, GeneralStatusLength, &GeneralStatusPacket},
		{AdditionalStatus1,
		 AdditionalStatus1Length,
		 &AdditionalStatus1Packet},
		{AdditionalStatus2,
		 AdditionalStatus2Length,
		 &AdditionalStatus2Packet},
		{StrokeData, StrokeDataLength, &StrokeDataPacket},
		{AdditionalStrokeData,
		 AdditionalStrokeDataLength,
		 &AdditionalStrokeDataPacket},
	}};

	bool HasFlag(const FRowingMetricSample &Sample, ERowingQualityFlag Flag)
	{
		return HasRowingQualityFlag(Sample.QualityFlags, Flag);
	}

	void parser_accepts_documented_packet_layouts()
	{
		const auto General = DecodePacket(
			GeneralStatus, GeneralStatusPacket(), {GeneralStatusLength});
		assert(General.IsValid());
		assert(General.GeneralStatus);
		assert(General.GeneralStatus->ElapsedMs == 123450);
		assert(General.GeneralStatus->DistanceMm == 678900);
		assert(General.GeneralStatus->WorkoutState ==
			   ERowingWorkoutState::Active);
		assert(General.GeneralStatus->RowingState == ERowingState::Active);
		assert(General.GeneralStatus->StrokeState ==
			   ERowingStrokeState::Drive);
		assert(General.GeneralStatus->DragFactor == 120);

		const auto Status1 = DecodePacket(AdditionalStatus1,
										  AdditionalStatus1Packet(),
										  {AdditionalStatus1Length});
		assert(Status1.IsValid());
		assert(Status1.AdditionalStatus1);
		assert(Status1.AdditionalStatus1->ElapsedMs == 123450);
		assert(Status1.AdditionalStatus1->SpeedMmPerS == 10000);
		assert(Status1.AdditionalStatus1->StrokeRateDeciSpm == 300);
		assert(!Status1.AdditionalStatus1->HeartRateBpm);
		assert(Status1.AdditionalStatus1->PaceMsPer500M == 2500);

		const auto Status2 = DecodePacket(AdditionalStatus2,
										  AdditionalStatus2Packet(),
										  {AdditionalStatus2Length});
		assert(Status2.IsValid());
		assert(Status2.AdditionalStatus2);
		assert(Status2.AdditionalStatus2->ElapsedMs == 123450);
		assert(Status2.AdditionalStatus2->AveragePowerW == 321);
		assert(Status2.AdditionalStatus2->Calories == 456);

		const auto Stroke = DecodePacket(
			StrokeData, StrokeDataPacket(), {StrokeDataLength});
		assert(Stroke.IsValid());
		assert(Stroke.StrokeData);
		assert(Stroke.StrokeData->ElapsedMs == 123450);
		assert(Stroke.StrokeData->CumulativeDistanceMm == 678900);
		assert(Stroke.StrokeData->DriveLengthMm == 1490);
		assert(Stroke.StrokeData->DriveTimeMs == 1280);
		assert(Stroke.StrokeData->RecoveryTimeMs == 2580);
		assert(Stroke.StrokeData->StrokeDistanceMm == 1000);
		assert(Stroke.StrokeData->PeakDriveForceDeciLb == 250);
		assert(Stroke.StrokeData->AverageDriveForceDeciLb == 150);
		assert(Stroke.StrokeData->WorkPerStrokeDeciJoules == 10000);
		assert(Stroke.StrokeData->StrokeCount == 1234);

		const auto AdditionalStroke = DecodePacket(
			AdditionalStrokeData,
			AdditionalStrokeDataPacket(),
			{AdditionalStrokeDataLength});
		assert(AdditionalStroke.IsValid());
		assert(AdditionalStroke.AdditionalStrokeData);
		assert(AdditionalStroke.AdditionalStrokeData->ElapsedMs == 123450);
		assert(AdditionalStroke.AdditionalStrokeData->StrokePowerW == 250);
		assert(AdditionalStroke.AdditionalStrokeData->CaloriesPerHour == 1200);
		assert(AdditionalStroke.AdditionalStrokeData->StrokeCount == 1234);
		assert(AdditionalStroke.AdditionalStrokeData->ProjectedWorkTimeMs == 300000);
		assert(AdditionalStroke.AdditionalStrokeData->ProjectedWorkDistanceMm == 1234000);
		assert(AdditionalStroke.AdditionalStrokeData->ProjectedWorkOtherRaw == 0x1234);

		auto LegacyAdditionalStrokeData = AdditionalStrokeDataPacket();
		LegacyAdditionalStrokeData.resize(LegacyAdditionalStrokeDataLength);
		const auto LegacyAdditionalStroke = DecodePacket(
			AdditionalStrokeData,
			LegacyAdditionalStrokeData,
			{LegacyAdditionalStrokeDataLength, AdditionalStrokeDataLength});
		assert(LegacyAdditionalStroke.IsValid());
		assert(LegacyAdditionalStroke.AdditionalStrokeData);
		assert(LegacyAdditionalStroke.AdditionalStrokeData->StrokePowerW == 250);
		assert(LegacyAdditionalStroke.AdditionalStrokeData->StrokeCount == 1234);
		assert(LegacyAdditionalStroke.AdditionalStrokeData->ProjectedWorkTimeMs == 300000);
		assert(LegacyAdditionalStroke.AdditionalStrokeData->ProjectedWorkDistanceMm == 1234000);
		assert(!LegacyAdditionalStroke.AdditionalStrokeData->ProjectedWorkOtherRaw);

		LegacyAdditionalStrokeData.resize(16);
		const auto PartialLegacyAdditionalStroke = DecodePacket(
			AdditionalStrokeData,
			LegacyAdditionalStrokeData,
			{LegacyAdditionalStrokeDataLength, AdditionalStrokeDataLength});
		assert(!PartialLegacyAdditionalStroke.IsValid());
		assert(PartialLegacyAdditionalStroke.Error.Code == EPacketError::LengthNotApproved);
	}

	void every_parser_rejects_each_truncated_length()
	{
		for (const FPacketFixture &Fixture : PacketFixtures)
		{
			const auto FullPacket = Fixture.MakePacket();
			assert(FullPacket.size() == Fixture.Length);
			for (std::size_t Length = 0; Length < Fixture.Length; ++Length)
			{
				auto Truncated = FullPacket;
				Truncated.resize(Length);
				const auto Decoded = DecodePacket(
					Fixture.Characteristic, Truncated, {Fixture.Length});
				assert(!Decoded.IsValid());
				assert(Decoded.Error.Code == EPacketError::LengthNotApproved);
				assert(Decoded.Error.ExpectedLength == Fixture.Length);
				assert(Decoded.Error.ActualLength == Length);
			}
		}
	}

	void every_parser_rejects_unapproved_lengths_and_characteristics()
	{
		for (const FPacketFixture &Fixture : PacketFixtures)
		{
			const auto Packet = Fixture.MakePacket();
			auto TooLong = Packet;
			TooLong.push_back(0);
			const auto LongResult = DecodePacket(
				Fixture.Characteristic, TooLong, {Fixture.Length});
			assert(!LongResult.IsValid());
			assert(LongResult.Error.Code == EPacketError::LengthNotApproved);
			assert(LongResult.Error.ActualLength == Fixture.Length + 1);

			const auto EmptyProfile = DecodePacket(
				Fixture.Characteristic, Packet, {});
			assert(!EmptyProfile.IsValid());
			assert(EmptyProfile.Error.Code == EPacketError::LengthNotApproved);
			assert(EmptyProfile.Error.ExpectedLength == 0);
		}

		const auto Unknown =
			DecodePacket(0xFFFF, GeneralStatusPacket(), {GeneralStatusLength});
		assert(!Unknown.IsValid());
		assert(Unknown.Error.Code == EPacketError::UnknownCharacteristic);
		assert(Unknown.Error.Characteristic == 0xFFFF);
	}

	void parser_preserves_extrema_and_documented_sentinels()
	{
		const auto AllMaximumGeneral = DecodePacket(
			GeneralStatus,
			std::vector<std::uint8_t>(GeneralStatusLength, 0xFF),
			{GeneralStatusLength});
		assert(AllMaximumGeneral.IsValid());
		assert(AllMaximumGeneral.GeneralStatus->ElapsedMs == 0xFFFFFFULL * 10ULL);
		assert(AllMaximumGeneral.GeneralStatus->DistanceMm ==
			   0xFFFFFFULL * 100ULL);
		assert(AllMaximumGeneral.GeneralStatus->DragFactor == 255);

		const auto AllMaximumStatus1 = DecodePacket(
			AdditionalStatus1,
			std::vector<std::uint8_t>(AdditionalStatus1Length, 0xFF),
			{AdditionalStatus1Length});
		assert(AllMaximumStatus1.IsValid());
		assert(AllMaximumStatus1.AdditionalStatus1);
		assert(AllMaximumStatus1.AdditionalStatus1->ElapsedMs ==
			   0xFFFFFFULL * 10ULL);
		assert(AllMaximumStatus1.AdditionalStatus1->SpeedMmPerS == 0xFFFF);
		assert(AllMaximumStatus1.AdditionalStatus1->StrokeRateDeciSpm == 2550);
		// 255 is the documented unavailable heart-rate sentinel.
		assert(!AllMaximumStatus1.AdditionalStatus1->HeartRateBpm);
		assert(AllMaximumStatus1.AdditionalStatus1->PaceMsPer500M == 655350);
		assert(AllMaximumStatus1.AdditionalStatus1->HasUnknownMachineType);

		const auto AllMaximumStatus2 = DecodePacket(
			AdditionalStatus2,
			std::vector<std::uint8_t>(AdditionalStatus2Length, 0xFF),
			{AdditionalStatus2Length});
		assert(AllMaximumStatus2.IsValid());
		assert(AllMaximumStatus2.AdditionalStatus2->ElapsedMs ==
			   0xFFFFFFULL * 10ULL);
		assert(AllMaximumStatus2.AdditionalStatus2->AveragePowerW == 65535);
		assert(AllMaximumStatus2.AdditionalStatus2->Calories == 65535);

		const auto AllMaximumStroke = DecodePacket(
			StrokeData,
			std::vector<std::uint8_t>(StrokeDataLength, 0xFF),
			{StrokeDataLength});
		assert(AllMaximumStroke.IsValid());
		assert(AllMaximumStroke.StrokeData->ElapsedMs == 0xFFFFFFULL * 10ULL);
		assert(AllMaximumStroke.StrokeData->DriveLengthMm == 2550);
		assert(AllMaximumStroke.StrokeData->WorkPerStrokeDeciJoules == 65535);
		assert(AllMaximumStroke.StrokeData->StrokeCount == 65535);

		const auto AllMaximumAdditionalStroke = DecodePacket(
			AdditionalStrokeData,
			std::vector<std::uint8_t>(AdditionalStrokeDataLength, 0xFF),
			{AdditionalStrokeDataLength});
		assert(AllMaximumAdditionalStroke.IsValid());
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->ElapsedMs ==
			   0xFFFFFFULL * 10ULL);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->StrokePowerW ==
			   65535);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->CaloriesPerHour ==
			   65535);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->StrokeCount ==
			   65535);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->ProjectedWorkTimeMs ==
			   0xFFFFFFULL * 1000ULL);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->ProjectedWorkDistanceMm ==
			   0xFFFFFFULL * 1000ULL);
		assert(AllMaximumAdditionalStroke.AdditionalStrokeData->ProjectedWorkOtherRaw ==
			   0xFFFFFF);
	}

	void pm5_characteristic_diagnostics_count_cadence_gaps_and_parser_errors()
	{
		using namespace Concept2PM;
		FPM5CharacteristicDiagnostics Stats;
		Stats.Characteristic = GeneralStatus;
		Stats.ApprovedPacketLengths = {19};
		Stats.RecordNotification(1'000'000'000ULL, 19);
		Stats.RecordNotification(1'100'000'000ULL, 19);
		Stats.RecordNotification(1'750'000'000ULL, 20);
		Stats.RecordParserError(EPacketError::LengthNotApproved);
		Stats.RecordParserError(EPacketError::InvalidValue, 18, 1'800'000'000ULL);

		assert(Stats.NotificationCount == 3);
		assert(Stats.FirstReceivedMonotonicNs == 1'000'000'000ULL);
		assert(Stats.LastReceivedMonotonicNs == 1'750'000'000ULL);
		assert(Stats.IntervalCount == 2);
		assert(Stats.IntervalTotalNs == 750'000'000ULL);
		assert(Stats.MinIntervalNs == 100'000'000ULL);
		assert(Stats.MaxIntervalNs == 650'000'000ULL);
		assert(Stats.LongGapCount == 1);
		assert(Stats.IntervalHistogram[10] == 1);
		assert(Stats.IntervalHistogram[65] == 1);
		assert(Stats.ParserErrorCounts[static_cast<std::size_t>(EPacketError::LengthNotApproved)] == 1);
		assert(Stats.ParserErrorCounts[static_cast<std::size_t>(EPacketError::InvalidValue)] == 1);
		assert(Stats.ApprovedPacketLengths == std::vector<std::size_t>{19});
		assert(Stats.ObservedPacketLengths.size() == 2);
		assert(Stats.ObservedPacketLengths[0].PacketLength == 19);
		assert(Stats.ObservedPacketLengths[0].Count == 2);
		assert(Stats.ObservedPacketLengths[1].PacketLength == 20);
		assert(Stats.ObservedPacketLengths[1].Count == 1);
		assert(Stats.LastParserErrorCode == EPacketError::InvalidValue);
		assert(Stats.LastParserErrorActualLength == 18);
		assert(Stats.LastParserErrorMonotonicNs == 1'800'000'000ULL);

		FPM5CharacteristicDiagnostics SameTimestamp;
		SameTimestamp.RecordNotification(10'000'000ULL);
		SameTimestamp.RecordNotification(10'000'000ULL);
		SameTimestamp.RecordNotification(20'000'000ULL);
		assert(SameTimestamp.IntervalCount == 2);
		assert(SameTimestamp.MinIntervalNs == 0);
		assert(SameTimestamp.IntervalHistogram[0] == 1);

		FPM5CharacteristicDiagnostics StrokeEvents;
		StrokeEvents.Characteristic = StrokeData;
		StrokeEvents.RecordNotification(1'000'000'000ULL, 20);
		StrokeEvents.RecordNotification(2'000'000'000ULL, 20);
		assert(!StrokeEvents.TracksContinuousStatusCadence());
		assert(StrokeEvents.IntervalCount == 1);
		assert(StrokeEvents.LongGapCount == 0);
	}

	void unknown_enums_are_explicitly_flagged()
	{
		for (const std::size_t Offset : {8U, 9U, 10U})
		{
			auto Bytes = GeneralStatusPacket();
			Bytes[Offset] = 0xFF;
			const auto Decoded =
				DecodePacket(GeneralStatus, Bytes, {GeneralStatusLength});
			assert(Decoded.IsValid());
			assert(Decoded.GeneralStatus->HasUnknownEnum);
			if (Offset == 8)
				assert(Decoded.GeneralStatus->WorkoutState ==
					   ERowingWorkoutState::Unknown);
			else if (Offset == 9)
				assert(Decoded.GeneralStatus->RowingState == ERowingState::Unknown);
			else
				assert(Decoded.GeneralStatus->StrokeState ==
					   ERowingStrokeState::Unknown);
		}

		auto UnknownMachineType = AdditionalStatus1Packet();
		UnknownMachineType[16] = 0xFF;
		const auto Decoded = DecodePacket(
			AdditionalStatus1,
			UnknownMachineType,
			{AdditionalStatus1Length});
		assert(Decoded.IsValid());
		assert(Decoded.AdditionalStatus1->HasUnknownMachineType);
	}

	FDecodedPacket DecodeFixture(const FPacketFixture &Fixture)
	{
		const auto Decoded = DecodePacket(
			Fixture.Characteristic, Fixture.MakePacket(), {Fixture.Length});
		assert(Decoded.IsValid());
		return Decoded;
	}

	void merger_joins_all_callback_orders()
	{
		std::array<std::size_t, PacketFixtures.size()> Order{};
		std::iota(Order.begin(), Order.end(), 0);
		do
		{
			FTelemetryMerger Merger;
			std::optional<FRowingMetricSample> EarlyOutput;
			for (std::size_t Index = 0; Index < Order.size(); ++Index)
			{
				const auto Packet = DecodeFixture(PacketFixtures[Order[Index]]);
				const auto Output = Merger.Push(Packet, 1000 + Index * 100);
				if (Output)
				{
					assert(!EarlyOutput);
					EarlyOutput = Output;
				}
			}
			assert(!EarlyOutput);
			const auto Sample = Merger.Flush(30'001'000);
			assert(Sample);
			assert(Sample->Sequence == 1);
			assert(Sample->SourceElapsedMs == 123450);
			assert(Sample->ReceivedMonotonicNs == 1000);
			assert(Sample->DistanceMm == 678900);
			assert(Sample->SpeedMmPerS == 10000);
			assert(Sample->StrokeRateDeciSpm == 300);
			assert(!Sample->HeartRateBpm);
			assert(Sample->PaceMsPer500M == 2500);
			assert(Sample->AveragePowerW == 321);
			assert(Sample->Calories == 456);
			assert(Sample->StrokePowerW == 250);
			assert(Sample->StrokeCount == 1234);
			assert(!HasFlag(*Sample, ERowingQualityFlag::MissingField));
			assert(!Merger.Flush(60'000'000));
		} while (std::next_permutation(Order.begin(), Order.end()));
	}

	void merger_flushes_exactly_on_the_join_window_boundary()
	{
		FTelemetryMerger Merger(100);
		const auto General = DecodeFixture(PacketFixtures[0]);
		const auto Status1 = DecodeFixture(PacketFixtures[1]);
		assert(!Merger.Push(General, 1000));
		assert(!Merger.Push(Status1, 1050));
		assert(!Merger.Flush(1099));
		const auto JoinedAtBoundary = Merger.Flush(1100);
		assert(JoinedAtBoundary);
		assert(JoinedAtBoundary->SpeedMmPerS == 10000);
		assert(!HasFlag(*JoinedAtBoundary, ERowingQualityFlag::MissingField));
		assert(!Merger.Flush(1101));

		FTelemetryMerger GeneralOnly(100);
		assert(!GeneralOnly.Push(General, 2000));
		assert(!GeneralOnly.Flush(2099));
		const auto GeneralAtBoundary = GeneralOnly.Flush(2100);
		assert(GeneralAtBoundary);
		assert(!GeneralAtBoundary->SpeedMmPerS);
		assert(HasFlag(*GeneralAtBoundary, ERowingQualityFlag::MissingField));
	}

	void merger_emits_full_sample_correction_for_late_status1()
	{
		FTelemetryMerger Merger(100);
		const auto General = DecodeFixture(PacketFixtures[0]);
		const auto Status1 = DecodeFixture(PacketFixtures[1]);
		const auto Status2 = DecodeFixture(PacketFixtures[2]);
		const auto Stroke = DecodeFixture(PacketFixtures[3]);
		const auto AdditionalStroke = DecodeFixture(PacketFixtures[4]);

		assert(!Merger.Push(General, 1000));
		assert(!Merger.Push(Status2, 1020));
		assert(!Merger.Push(Stroke, 1030));
		assert(!Merger.Push(AdditionalStroke, 1040));
		const auto Original = Merger.Flush(1100);
		assert(Original);
		assert(Original->Sequence == 1);
		assert(HasFlag(*Original, ERowingQualityFlag::MissingField));
		assert(!Original->SpeedMmPerS);
		assert(Original->AveragePowerW == 321);
		assert(Original->Calories == 456);
		assert(Original->StrokePowerW == 250);
		assert(Original->StrokeCount == 1234);
		assert(!Merger.TakePendingCorrection());

		assert(!Merger.Push(Status1, 1200));
		const auto Correction = Merger.TakePendingCorrection();
		assert(Correction);
		assert(Correction->TargetSampleSequence == Original->Sequence);
		const FRowingMetricSample &Corrected = Correction->CorrectedSample;
		assert(Corrected.Sequence == Original->Sequence);
		assert(Corrected.SourceElapsedMs == Original->SourceElapsedMs);
		assert(Corrected.ReceivedMonotonicNs == Original->ReceivedMonotonicNs);
		assert(Corrected.DistanceMm == Original->DistanceMm);
		assert(Corrected.WorkoutState == Original->WorkoutState);
		assert(Corrected.RowingState == Original->RowingState);
		assert(Corrected.StrokeState == Original->StrokeState);
		assert(Corrected.DragFactor == Original->DragFactor);
		assert(Corrected.AveragePowerW == Original->AveragePowerW);
		assert(Corrected.Calories == Original->Calories);
		assert(Corrected.StrokePowerW == Original->StrokePowerW);
		assert(Corrected.StrokeCount == Original->StrokeCount);
		assert(Corrected.SpeedMmPerS == 10000);
		assert(Corrected.StrokeRateDeciSpm == 300);
		assert(!Corrected.HeartRateBpm);
		assert(Corrected.PaceMsPer500M == 2500);
		assert(!HasFlag(Corrected, ERowingQualityFlag::MissingField));
		assert(HasFlag(Corrected, ERowingQualityFlag::LateCorrection));
		assert(!Merger.TakePendingCorrection());
	}

	void merger_rejects_old_late_status_and_consumes_one_correction()
	{
		const auto General = DecodeFixture(PacketFixtures[0]);
		const auto Status1 = DecodeFixture(PacketFixtures[1]);

		FTelemetryMerger OldPacketMerger(100);
		assert(!OldPacketMerger.Push(General, 1000));
		assert(OldPacketMerger.Flush(1100));
		auto OlderStatusBytes = AdditionalStatus1Packet();
		OlderStatusBytes[0] = 0x38; // 123.44 s, one centisecond earlier.
		const auto OlderStatus = DecodePacket(
			AdditionalStatus1,
			OlderStatusBytes,
			{AdditionalStatus1Length});
		assert(OlderStatus.IsValid());
		assert(!OldPacketMerger.Push(OlderStatus, 1200));
		assert(!OldPacketMerger.TakePendingCorrection());

		FTelemetryMerger DuplicateMerger(100);
		assert(!DuplicateMerger.Push(General, 1000));
		const auto Original = DuplicateMerger.Flush(1100);
		assert(Original);
		assert(!DuplicateMerger.Push(Status1, 1200));
		assert(!DuplicateMerger.Push(Status1, 1201));
		const auto SingleCorrection = DuplicateMerger.TakePendingCorrection();
		assert(SingleCorrection);
		assert(SingleCorrection->TargetSampleSequence == Original->Sequence);
		assert(!DuplicateMerger.TakePendingCorrection());
		assert(!DuplicateMerger.Push(Status1, 1202));
		assert(!DuplicateMerger.TakePendingCorrection());
	}

	void merger_marks_stopped_source_values_as_duplicates()
	{
		FTelemetryMerger Merger(100);
		auto StoppedGeneralBytes = GeneralStatusPacket();
		StoppedGeneralBytes[8] = 1;	 // Active workout.
		StoppedGeneralBytes[9] = 0;	 // Not currently rowing.
		StoppedGeneralBytes[10] = 0; // Waiting for the next stroke.
		const auto StoppedGeneral = DecodePacket(
			GeneralStatus, StoppedGeneralBytes, {GeneralStatusLength});
		assert(StoppedGeneral.IsValid());

		assert(!Merger.Push(StoppedGeneral, 1000));
		const auto First = Merger.Flush(1100);
		assert(First);
		assert(First->Sequence == 1);
		assert(First->WorkoutState == ERowingWorkoutState::Active);
		assert(First->RowingState == ERowingState::Inactive);
		assert(First->StrokeState == ERowingStrokeState::Waiting);
		assert(!HasFlag(*First, ERowingQualityFlag::Duplicate));

		assert(!Merger.Push(StoppedGeneral, 1200));
		const auto Repeated = Merger.Flush(1300);
		assert(Repeated);
		assert(Repeated->Sequence == 2);
		assert(Repeated->SourceElapsedMs == First->SourceElapsedMs);
		assert(Repeated->DistanceMm == First->DistanceMm);
		assert(Repeated->WorkoutState == ERowingWorkoutState::Active);
		assert(Repeated->RowingState == ERowingState::Inactive);
		assert(HasFlag(*Repeated, ERowingQualityFlag::Duplicate));
	}

	void merger_keeps_monotonic_high_water_marks_after_regressions()
	{
		FTelemetryMerger Merger(100);
		const auto HighWater = DecodeFixture(PacketFixtures[0]);
		assert(!Merger.Push(HighWater, 1000));
		assert(Merger.Flush(1100));

		auto FirstRegressionBytes = GeneralStatusPacket();
		// 100.00 s and 600.0 m, both below the published high-water marks.
		FirstRegressionBytes[0] = 0x10;
		FirstRegressionBytes[1] = 0x27;
		FirstRegressionBytes[2] = 0x00;
		FirstRegressionBytes[3] = 0x70;
		FirstRegressionBytes[4] = 0x17;
		FirstRegressionBytes[5] = 0x00;
		const auto FirstRegression = DecodePacket(
			GeneralStatus, FirstRegressionBytes, {GeneralStatusLength});
		assert(FirstRegression.IsValid());
		assert(!Merger.Push(FirstRegression, 1200));
		const auto FirstRegressedSample = Merger.Flush(1300);
		assert(FirstRegressedSample);
		assert(HasFlag(*FirstRegressedSample,
					   ERowingQualityFlag::TimeRegression));
		assert(HasFlag(*FirstRegressedSample,
					   ERowingQualityFlag::DistanceRegression));

		auto SecondRegressionBytes = GeneralStatusPacket();
		// 120.00 s and 650.0 m recover from the bad packet but remain below
		// the original 123.45 s / 678.9 m high-water marks.
		SecondRegressionBytes[0] = 0xE0;
		SecondRegressionBytes[1] = 0x2E;
		SecondRegressionBytes[2] = 0x00;
		SecondRegressionBytes[3] = 0x64;
		SecondRegressionBytes[4] = 0x19;
		SecondRegressionBytes[5] = 0x00;
		const auto SecondRegression = DecodePacket(
			GeneralStatus, SecondRegressionBytes, {GeneralStatusLength});
		assert(SecondRegression.IsValid());
		assert(!Merger.Push(SecondRegression, 1400));
		const auto SecondRegressedSample = Merger.Flush(1500);
		assert(SecondRegressedSample);
		assert(HasFlag(*SecondRegressedSample,
					   ERowingQualityFlag::TimeRegression));
		assert(HasFlag(*SecondRegressedSample,
					   ERowingQualityFlag::DistanceRegression));
	}

	void reconnect_continuation_rejects_resets_and_terminal_state_resumption()
	{
		FGeneralStatusFact Previous;
		Previous.ElapsedMs = 10'000;
		Previous.DistanceMm = 50'000;
		Previous.WorkoutState = ERowingWorkoutState::Active;

		FGeneralStatusFact Continued = Previous;
		Continued.ElapsedMs += 100;
		Continued.DistanceMm += 250;
		assert(IsReconnectContinuationCompatible(Previous, Continued));

		FGeneralStatusFact TimeReset = Continued;
		TimeReset.ElapsedMs = Previous.ElapsedMs - 1;
		assert(!IsReconnectContinuationCompatible(Previous, TimeReset));

		FGeneralStatusFact DistanceReset = Continued;
		DistanceReset.DistanceMm = Previous.DistanceMm - 1;
		assert(!IsReconnectContinuationCompatible(Previous, DistanceReset));

		FGeneralStatusFact WorkoutReset = Continued;
		WorkoutReset.WorkoutState = ERowingWorkoutState::WaitingToBegin;
		assert(!IsReconnectContinuationCompatible(Previous, WorkoutReset));

		FGeneralStatusFact Unknown = Continued;
		Unknown.WorkoutState = ERowingWorkoutState::Unknown;
		assert(!IsReconnectContinuationCompatible(Previous, Unknown));

		FGeneralStatusFact Complete = Continued;
		Complete.WorkoutState = ERowingWorkoutState::Complete;
		assert(IsReconnectContinuationCompatible(Previous, Complete));
		FGeneralStatusFact Restarted = Complete;
		Restarted.ElapsedMs += 100;
		Restarted.DistanceMm += 250;
		Restarted.WorkoutState = ERowingWorkoutState::Active;
		assert(!IsReconnectContinuationCompatible(Complete, Restarted));
	}

	void capability_profiles_and_event_kinds_remain_separate()
	{
		const FPM5Identity Identity{
			"PM5", "abc", "123", ERowingMachineKind::IndoorRower};
		assert(EvaluateCapability(Identity, {}).SupportState ==
			   ERowingMachineSupportState::Blocked);
		FPM5CapabilityProfile Allowed;
		Allowed.Version = 9;
		Allowed.MonitorModel = "PM5";
		Allowed.HardwareRevision = "abc";
		Allowed.FirmwareRevision = "123";
		Allowed.MachineKind = ERowingMachineKind::IndoorRower;
		Allowed.SupportState = ERowingMachineSupportState::Allowed;
		Allowed.Characteristics.push_back(
			{GeneralStatus,
			 true,
			 true,
			 ToPM5CharacteristicProperties(EPM5CharacteristicProperty::Notify),
			 {GeneralStatusLength},
			 ToRowingMetricSet(ERowingMetric::Distance)});
		const auto Evaluation = EvaluateCapability(Identity, {Allowed});
		assert(Evaluation.SupportState == ERowingMachineSupportState::Allowed);
		assert(Evaluation.ProfileVersion == 9);

		FPM5CapabilityProfile DiagnosticOnly;
		DiagnosticOnly.MonitorModel = "PM5";
		DiagnosticOnly.HardwareRevision = "abc";
		DiagnosticOnly.FirmwareRevision = "123";
		DiagnosticOnly.MachineKind = ERowingMachineKind::IndoorRower;
		DiagnosticOnly.SupportState = ERowingMachineSupportState::Warn;
		DiagnosticOnly.DiagnosticOnly = true;
		const std::vector<FPM5CapabilityProfile> DiagnosticProfiles{
			DiagnosticOnly};
		const auto DiagnosticEvaluation =
			EvaluateCapability(Identity, DiagnosticProfiles);
		assert(DiagnosticEvaluation.Profile != nullptr);
		assert(DiagnosticEvaluation.Profile->DiagnosticOnly);
		assert(DiagnosticEvaluation.SupportState ==
			   ERowingMachineSupportState::Warn);
		assert(EvaluateCapability(
				   {"PM5", "other", "123", ERowingMachineKind::IndoorRower},
				   DiagnosticProfiles)
				   .Profile == nullptr);

		const auto ExpectEventKind = [](
										 auto Payload,
										 ERowingMachineEventKind ExpectedKind,
										 std::uint8_t ExpectedValue)
		{
			FRowingMachineEvent Event;
			Event.Payload = std::move(Payload);
			assert(Event.GetKind() == ExpectedKind);
			assert(static_cast<std::uint8_t>(Event.GetKind()) == ExpectedValue);
		};
		ExpectEventKind(FRowingMachineDescriptor{},
						ERowingMachineEventKind::MachineDiscovered,
						0);
		ExpectEventKind(FRowingConnectionStateChanged{},
						ERowingMachineEventKind::ConnectionStateChanged,
						1);
		ExpectEventKind(FRowingMachineInfo{},
						ERowingMachineEventKind::MachineInfoObserved,
						2);
		ExpectEventKind(FRowingMetricSample{},
						ERowingMachineEventKind::MetricSampled,
						3);
		ExpectEventKind(FRowingTelemetryStale{},
						ERowingMachineEventKind::TelemetryStale,
						4);
		ExpectEventKind(FRowingConnectionRestored{},
						ERowingMachineEventKind::ConnectionRestored,
						5);
		ExpectEventKind(FRowingFault{},
						ERowingMachineEventKind::FaultObserved,
						6);
		ExpectEventKind(FRowingDiagnosticSample{},
						ERowingMachineEventKind::DiagnosticSampleObserved,
						7);

		FRowingMetricCorrection Correction;
		Correction.TargetSampleSequence = 42;
		Correction.CorrectedSample.Sequence = 42;
		ExpectEventKind(std::move(Correction),
						ERowingMachineEventKind::MetricCorrected,
						8);
		ExpectEventKind(FRowingStrokeMetrics{},
						ERowingMachineEventKind::StrokeMetricsObserved,
						9);
	}
} // namespace

int main()
{
	parser_accepts_documented_packet_layouts();
	every_parser_rejects_each_truncated_length();
	every_parser_rejects_unapproved_lengths_and_characteristics();
	parser_preserves_extrema_and_documented_sentinels();
	pm5_characteristic_diagnostics_count_cadence_gaps_and_parser_errors();
	unknown_enums_are_explicitly_flagged();
	merger_joins_all_callback_orders();
	merger_flushes_exactly_on_the_join_window_boundary();
	merger_emits_full_sample_correction_for_late_status1();
	merger_rejects_old_late_status_and_consumes_one_correction();
	merger_marks_stopped_source_values_as_duplicates();
	merger_keeps_monotonic_high_water_marks_after_regressions();
	reconnect_continuation_rejects_resets_and_terminal_state_resumption();
	capability_profiles_and_event_kinds_remain_separate();
	return 0;
}
