#include "Concept2PMCore/Concept2PMWorkoutProtocol.h"

#include <cassert>
#include <limits>

namespace Concept2PM
{
	namespace
	{
		// C2 proprietary wrapper (CSAFE_SETPMCFG_CMD) and the PM-specific
		// long commands it carries, per the "Proprietary CSAFE Workout
		// Configuration" and "Terminate Workout" sample frames.
		constexpr std::uint8_t OpProprietaryWrapper = 0x76;
		constexpr std::uint8_t SubSetWorkoutType = 0x01;
		constexpr std::uint8_t SubSetWorkoutDuration = 0x03;
		constexpr std::uint8_t SubSetRestDuration = 0x04;
		constexpr std::uint8_t SubSetScreenState = 0x13;
		constexpr std::uint8_t SubConfigureWorkout = 0x14;

		// From the pinned CSAFE definition's Appendix A "Workout Type" enum:
		// FIXEDDIST_NOSPLITS=2, FIXEDTIME_NOSPLITS=4, FIXEDTIME_INTERVAL=6.
		// This spike never sends a distinct split (its split duration would
		// equal the whole workout), so it must use the *_NOSPLITS opcodes,
		// not *_SPLITS: a real PM5 treats *_SPLITS as a genuinely different
		// workout with its own split/interval notification data.
		constexpr std::uint8_t WorkoutTypeFixedDistanceNoSplits = 0x02;
		constexpr std::uint8_t WorkoutTypeFixedTimeNoSplits = 0x04;
		constexpr std::uint8_t WorkoutTypeFixedTimeInterval = 0x06;

		constexpr std::uint8_t DurationIdentifierTime = 0x00;
		constexpr std::uint8_t DurationIdentifierDistance = 0x80;

		constexpr std::uint8_t ScreenTypeWorkout = 0x01;
		constexpr std::uint8_t ScreenValueWorkoutPrepareToRow = 0x01;
		constexpr std::uint8_t ScreenValueWorkoutTerminate = 0x02;

		constexpr std::uint8_t ProgrammingModeEnable = 0x01;

		void AppendU32Be(std::vector<std::uint8_t> &Out, std::uint32_t Value)
		{
			Out.push_back(static_cast<std::uint8_t>(Value >> 24U));
			Out.push_back(static_cast<std::uint8_t>(Value >> 16U));
			Out.push_back(static_cast<std::uint8_t>(Value >> 8U));
			Out.push_back(static_cast<std::uint8_t>(Value));
		}

		void AppendU16Be(std::vector<std::uint8_t> &Out, std::uint16_t Value)
		{
			Out.push_back(static_cast<std::uint8_t>(Value >> 8U));
			Out.push_back(static_cast<std::uint8_t>(Value));
		}

		std::vector<std::uint8_t> LongCommand(std::uint8_t Command,
											  const std::vector<std::uint8_t> &Data)
		{
			// The CSAFE long-command data-byte-count field is one byte; every
			// sub-command this file builds has a small, fixed data size, so
			// this is a programmer-error guard against a future extension
			// silently truncating the length prefix, not user-input validation.
			assert(Data.size() <= std::numeric_limits<std::uint8_t>::max());
			std::vector<std::uint8_t> Out;
			Out.push_back(Command);
			Out.push_back(static_cast<std::uint8_t>(Data.size()));
			Out.insert(Out.end(), Data.begin(), Data.end());
			return Out;
		}

		void AppendSubCommand(std::vector<std::uint8_t> &WrapperBody,
							  const std::vector<std::uint8_t> &SubCommand)
		{
			WrapperBody.insert(WrapperBody.end(), SubCommand.begin(), SubCommand.end());
		}

		std::vector<std::uint8_t>
		WrapProprietary(const std::vector<std::uint8_t> &WrapperBody)
		{
			// Same one-byte wrapper-length constraint as LongCommand above.
			assert(WrapperBody.size() <= std::numeric_limits<std::uint8_t>::max());
			std::vector<std::uint8_t> Out;
			Out.push_back(OpProprietaryWrapper);
			Out.push_back(static_cast<std::uint8_t>(WrapperBody.size()));
			Out.insert(Out.end(), WrapperBody.begin(), WrapperBody.end());
			return Out;
		}

		bool NeedsByteStuffing(std::uint8_t Byte)
		{
			return Byte == CsafeStandardStartFlag || Byte == CsafeStopFlag ||
				   Byte == CsafeByteStuffingFlag ||
				   Byte == 0xF0; // Extended Frame Start Flag; reserved here too.
		}

		std::uint8_t StuffedSubstitute(std::uint8_t Byte)
		{
			switch (Byte)
			{
			case 0xF0:
				return 0x00;
			case CsafeStandardStartFlag:
				return 0x01;
			case CsafeStopFlag:
				return 0x02;
			case CsafeByteStuffingFlag:
				return 0x03;
			default:
				return Byte;
			}
		}

		ECsafeStateMachineState DecodeStateMachineState(std::uint8_t Value)
		{
			switch (Value)
			{
			case 0x00:
				return ECsafeStateMachineState::Error;
			case 0x01:
				return ECsafeStateMachineState::Ready;
			case 0x02:
				return ECsafeStateMachineState::Idle;
			case 0x03:
				return ECsafeStateMachineState::HaveId;
			case 0x05:
				return ECsafeStateMachineState::InUse;
			case 0x06:
				return ECsafeStateMachineState::Pause;
			case 0x07:
				return ECsafeStateMachineState::Finish;
			case 0x08:
				return ECsafeStateMachineState::Manual;
			case 0x09:
				return ECsafeStateMachineState::OffLine;
			default:
				return ECsafeStateMachineState::Unknown;
			}
		}
	} // namespace

	std::vector<std::uint8_t>
	BuildStandardFrame(const std::vector<std::uint8_t> &Contents)
	{
		std::uint8_t Checksum = 0;
		for (std::uint8_t Byte : Contents)
			Checksum ^= Byte;

		std::vector<std::uint8_t> Unstuffed = Contents;
		Unstuffed.push_back(Checksum);

		std::vector<std::uint8_t> Frame;
		Frame.push_back(CsafeStandardStartFlag);
		for (std::uint8_t Byte : Unstuffed)
		{
			if (NeedsByteStuffing(Byte))
			{
				Frame.push_back(CsafeByteStuffingFlag);
				Frame.push_back(StuffedSubstitute(Byte));
			}
			else
			{
				Frame.push_back(Byte);
			}
		}
		Frame.push_back(CsafeStopFlag);
		if (Frame.size() > CsafeMaxFrameSize)
			return {};
		return Frame;
	}

	FCsafeParsedResponse
	ParseStandardFrameResponse(const std::vector<std::uint8_t> &Bytes)
	{
		FCsafeParsedResponse Result;
		if (Bytes.size() < 3 || Bytes.front() != CsafeStandardStartFlag ||
			Bytes.back() != CsafeStopFlag)
			return Result;

		std::vector<std::uint8_t> Unstuffed;
		for (std::size_t Index = 1; Index + 1 < Bytes.size(); ++Index)
		{
			const std::uint8_t Byte = Bytes[Index];
			if (Byte == CsafeByteStuffingFlag)
			{
				if (Index + 2 >= Bytes.size())
					return Result; // Truncated stuffing pair.
				++Index;
				switch (Bytes[Index])
				{
				case 0x00:
					Unstuffed.push_back(0xF0);
					break;
				case 0x01:
					Unstuffed.push_back(CsafeStandardStartFlag);
					break;
				case 0x02:
					Unstuffed.push_back(CsafeStopFlag);
					break;
				case 0x03:
					Unstuffed.push_back(CsafeByteStuffingFlag);
					break;
				default:
					return Result; // Invalid stuffing substitute.
				}
			}
			else
			{
				Unstuffed.push_back(Byte);
			}
		}

		// A well-formed response needs at least a status byte plus its
		// checksum; anything shorter is malformed, not a checksum failure
		// (an empty pre-checksum payload legitimately XORs to a valid 0x00
		// checksum with no status byte behind it, which must not be
		// reported as ChecksumValid).
		if (Unstuffed.size() < 2)
			return Result;

		Result.FrameWellFormed = true;
		std::uint8_t Checksum = 0;
		for (std::uint8_t Byte : Unstuffed)
			Checksum ^= Byte;
		Result.ChecksumValid = Checksum == 0;
		if (!Result.ChecksumValid)
			return Result; // Status is left default; caller must check ChecksumValid first.

		Unstuffed.pop_back(); // Drop the checksum byte itself.
		const std::uint8_t StatusByte = Unstuffed.front();
		Result.Status.FrameToggle = (StatusByte & 0x80U) != 0;
		const std::uint8_t PreviousStatusBits = (StatusByte >> 4U) & 0x03U;
		Result.Status.PreviousFrameStatus =
			static_cast<ECsafePreviousFrameStatus>(PreviousStatusBits);
		Result.Status.StateMachineState =
			DecodeStateMachineState(StatusByte & 0x0FU);
		Result.ResponseContent.assign(Unstuffed.begin() + 1, Unstuffed.end());
		return Result;
	}

	namespace
	{
		// Wire granularity (distance in whole meters, time/rest in whole
		// centiseconds/seconds) plus the PM5 parameter limits from the
		// pinned CSAFE definition's Table 19 ("PM5 Workout Configuration
		// Parameter Limits"). Both matter: a sub-granularity value would
		// silently truncate to a degenerate 0, and an out-of-table value is
		// a command outside the published specification even though it
		// would still fit the wire's byte width.
		constexpr std::uint32_t MinDistanceMm = 100'000U;		 // 100 m.
		constexpr std::uint32_t MaxDistanceMm = 999'999'000U;	 // 999999 m (fixed distance duration).
		constexpr std::uint32_t MinTimeDurationMs = 20'000U;	 // :20.
		constexpr std::uint32_t MaxTimeDurationMs = 35'999'000U; // 9:59:59 (fixed time duration).
		constexpr std::uint32_t MinIntervalWorkMs = 20'000U;	 // :20.
		constexpr std::uint32_t MaxIntervalWorkMs = 3'599'000U;	 // 59:59 (fixed interval time duration).
		constexpr std::uint32_t MaxIntervalRestMs = 595'000U;	 // 9:55 (rest duration).

		bool IsValidDistanceMm(std::uint32_t DistanceMm)
		{
			return DistanceMm % 1000U == 0U && DistanceMm >= MinDistanceMm &&
				   DistanceMm <= MaxDistanceMm;
		}

		bool IsValidDurationMs(std::uint32_t DurationMs, std::uint32_t MinMs, std::uint32_t MaxMs)
		{
			return DurationMs % 10U == 0U && DurationMs >= MinMs && DurationMs <= MaxMs;
		}

		bool IsValidIntervalRestMs(std::uint32_t IntervalRestMs)
		{
			// The wire's rest-duration field is a 16-bit seconds count
			// (CSAFE_PM_SET_RESTDURATION); Table 19 additionally caps it at
			// 9:55, well inside that field's range, so the field-width
			// check is redundant with the table limit but kept as a
			// defensive second bound against a future limit-table correction.
			if (IntervalRestMs % 1000U != 0U || IntervalRestMs > MaxIntervalRestMs)
				return false;
			const std::uint32_t RestSeconds = IntervalRestMs / 1000U;
			return RestSeconds <= std::numeric_limits<std::uint16_t>::max();
		}
	} // namespace

	EDiagnosticWorkoutSpecError
	ValidateDiagnosticWorkoutSpec(const FDiagnosticWorkoutSpec &Spec) noexcept
	{
		switch (Spec.Kind)
		{
		case EDiagnosticWorkoutKind::Distance:
			if (!Spec.DistanceMm)
				return EDiagnosticWorkoutSpecError::MissingDistance;
			return IsValidDistanceMm(*Spec.DistanceMm) ? EDiagnosticWorkoutSpecError::None
													   : EDiagnosticWorkoutSpecError::InvalidDistance;
		case EDiagnosticWorkoutKind::Time:
			if (!Spec.DurationMs)
				return EDiagnosticWorkoutSpecError::MissingDuration;
			return IsValidDurationMs(*Spec.DurationMs, MinTimeDurationMs, MaxTimeDurationMs)
					   ? EDiagnosticWorkoutSpecError::None
					   : EDiagnosticWorkoutSpecError::InvalidDuration;
		case EDiagnosticWorkoutKind::TimeInterval:
			if (!Spec.DurationMs)
				return EDiagnosticWorkoutSpecError::MissingDuration;
			if (!IsValidDurationMs(*Spec.DurationMs, MinIntervalWorkMs, MaxIntervalWorkMs))
				return EDiagnosticWorkoutSpecError::InvalidDuration;
			if (!Spec.IntervalRestMs)
				return EDiagnosticWorkoutSpecError::MissingIntervalRest;
			if (!IsValidIntervalRestMs(*Spec.IntervalRestMs))
				return EDiagnosticWorkoutSpecError::InvalidIntervalRest;
			return EDiagnosticWorkoutSpecError::None;
		}
		assert(false && "EDiagnosticWorkoutKind grew a case ValidateDiagnosticWorkoutSpec doesn't handle");
		return EDiagnosticWorkoutSpecError::MissingDuration;
	}

	std::vector<std::uint8_t>
	BuildProgramDiagnosticWorkoutContents(const FDiagnosticWorkoutSpec &Spec)
	{
		if (ValidateDiagnosticWorkoutSpec(Spec) != EDiagnosticWorkoutSpecError::None)
			return {};

		std::vector<std::uint8_t> Body;
		switch (Spec.Kind)
		{
		case EDiagnosticWorkoutKind::Distance:
		{
			const std::uint32_t Meters = *Spec.DistanceMm / 1000U;
			AppendSubCommand(Body, LongCommand(SubSetWorkoutType, {WorkoutTypeFixedDistanceNoSplits}));
			std::vector<std::uint8_t> DurationData{DurationIdentifierDistance};
			AppendU32Be(DurationData, Meters);
			AppendSubCommand(Body, LongCommand(SubSetWorkoutDuration, DurationData));
			break;
		}
		case EDiagnosticWorkoutKind::Time:
		{
			const std::uint32_t Centiseconds = *Spec.DurationMs / 10U;
			AppendSubCommand(Body, LongCommand(SubSetWorkoutType, {WorkoutTypeFixedTimeNoSplits}));
			std::vector<std::uint8_t> DurationData{DurationIdentifierTime};
			AppendU32Be(DurationData, Centiseconds);
			AppendSubCommand(Body, LongCommand(SubSetWorkoutDuration, DurationData));
			break;
		}
		case EDiagnosticWorkoutKind::TimeInterval:
		{
			const std::uint32_t Centiseconds = *Spec.DurationMs / 10U;
			const std::uint32_t RestSeconds = *Spec.IntervalRestMs / 1000U;
			AppendSubCommand(Body, LongCommand(SubSetWorkoutType, {WorkoutTypeFixedTimeInterval}));
			std::vector<std::uint8_t> DurationData{DurationIdentifierTime};
			AppendU32Be(DurationData, Centiseconds);
			AppendSubCommand(Body, LongCommand(SubSetWorkoutDuration, DurationData));
			std::vector<std::uint8_t> RestData;
			AppendU16Be(RestData, static_cast<std::uint16_t>(RestSeconds));
			AppendSubCommand(Body, LongCommand(SubSetRestDuration, RestData));
			break;
		}
		default:
			// A Kind that ValidateDiagnosticWorkoutSpec accepted but this
			// switch does not handle must fail closed rather than send a
			// truncated frame missing SetWorkoutType/SetWorkoutDuration.
			assert(false && "EDiagnosticWorkoutKind grew a case BuildProgramDiagnosticWorkoutContents doesn't handle");
			return {};
		}
		AppendSubCommand(Body, LongCommand(SubConfigureWorkout, {ProgrammingModeEnable}));
		AppendSubCommand(Body, LongCommand(SubSetScreenState, {ScreenTypeWorkout, ScreenValueWorkoutPrepareToRow}));
		return WrapProprietary(Body);
	}

	std::vector<std::uint8_t> BuildAbortDiagnosticWorkoutContents()
	{
		std::vector<std::uint8_t> Body =
			LongCommand(SubSetScreenState, {ScreenTypeWorkout, ScreenValueWorkoutTerminate});
		return WrapProprietary(Body);
	}

	std::optional<FPM5ProgramReadback>
	ParseProgramVerifyReadback(const FCsafeParsedResponse &ParsedResponse)
	{
		// Caller must validate the frame first.
		if (!ParsedResponse.FrameWellFormed || !ParsedResponse.ChecksumValid)
			return std::nullopt;

		// The response should contain echoed command bytes. For the bounded
		// program-verify/abort contract, we expect to see the CSAFE_PM_SET_WORKOUTTYPE
		// subcommand echoed back. The wire format for this bounded spike is:
		//   [CSAFE_PM_SET_WORKOUTTYPE]
		//     [1-byte: Command] [1-byte: DataLength] [N-bytes: SubCommandData]
		//
		// We don't implement full command-response matching here (that would
		// require storing the sent bytes), so we decode the Type field from the
		// subcommand data if we can locate it. The subcommands are ordered as
		// sent in BuildProgramDiagnosticWorkoutContents:
		//   1. SetWorkoutType (WorkoutTypeFixedDistanceNoSplits, FixedTimeNoSplits, or FixedTimeInterval)
		//   2. SetWorkoutDuration (or SetRestDuration for Interval)
		//   3. ConfigureWorkout
		//   4. SetScreenState (PrepareToRow or Terminate)
		//
		// Since only SetWorkoutType is needed for Type decoding, we scan for it
		// and decode the Type from its data.

		const std::vector<std::uint8_t> &Content = ParsedResponse.ResponseContent;

		if (Content.size() < 2)
			return std::nullopt; // Need at least Command + DataLength.

		// Scan through Content to find SetWorkoutType subcommand. The bound
		// is <= (not <): a final header-only (0-length-data) subcommand's
		// header may legitimately sit with its last byte at Content.size()-1.
		for (std::size_t Index = 0; Index + 2 <= Content.size(); ++Index)
		{
			const std::uint8_t SubCommand = Content[Index];
			const std::uint8_t DataLength = Content[Index + 1];

			if (SubCommand != SubSetWorkoutType)
			{
				Index += 1 + DataLength; // Skip this subcommand.
				continue;
			}

			// Found SetWorkoutType subcommand.
			if (Index + 2 + DataLength > Content.size())
				return std::nullopt; // Truncated subcommand data.

			// Workout type is the first byte after the header.
			if (DataLength < 1)
				return std::nullopt; // No type byte.

			const std::uint8_t WorkoutType = Content[Index + 2];

			// Map the workout type to our EDiagnosticWorkoutKind enum. An
			// unrecognized byte is a malformed response, not a Distance
			// workout: the caller must be able to trust that a non-nullopt
			// result actually reflects what the PM5 echoed back.
			EDiagnosticWorkoutKind Kind;
			switch (WorkoutType)
			{
			case WorkoutTypeFixedDistanceNoSplits:
				Kind = EDiagnosticWorkoutKind::Distance;
				break;
			case WorkoutTypeFixedTimeNoSplits:
				Kind = EDiagnosticWorkoutKind::Time;
				break;
			case WorkoutTypeFixedTimeInterval:
				Kind = EDiagnosticWorkoutKind::TimeInterval;
				break;
			default:
				return std::nullopt;
			}

			// Decode duration if present (for Time and TimeInterval types
			// only; per FPM5ProgramReadback's contract, Distance workouts
			// leave DurationMs unset). The SetWorkoutDuration subcommand
			// comes after SetWorkoutType in our command sequence.
			std::optional<std::uint32_t> DurationMs;
			if (Kind != EDiagnosticWorkoutKind::Distance)
			{
				for (std::size_t ScanIndex = 0; ScanIndex + 2 <= Content.size(); ++ScanIndex)
				{
					const std::uint8_t ScanSubCmd = Content[ScanIndex];
					const std::uint8_t ScanDataLen = Content[ScanIndex + 1];

					if (ScanSubCmd == SubSetWorkoutDuration)
					{
						if (ScanIndex + 2 + ScanDataLen > Content.size())
							break; // Truncated, can't decode.

						if (ScanDataLen < 5)
							break; // Not enough bytes for DurationIdentifier + U32BE.

						const std::uint8_t DurationIdentifier = Content[ScanIndex + 2];
						if (DurationIdentifier == DurationIdentifierTime)
						{
							// Extract 32-bit big-endian value.
							const std::uint32_t Centiseconds =
								(static_cast<std::uint32_t>(Content[ScanIndex + 3]) << 24) |
								(static_cast<std::uint32_t>(Content[ScanIndex + 4]) << 16) |
								(static_cast<std::uint32_t>(Content[ScanIndex + 5]) << 8) |
								Content[ScanIndex + 6];
							DurationMs = Centiseconds * 10U; // Convert centiseconds to milliseconds.
						}
						break;
					}

					ScanIndex += 1 + ScanDataLen; // Skip this subcommand.
				}
			}

			return FPM5ProgramReadback{Kind, std::move(DurationMs)};
		}

		// No SetWorkoutType subcommand found - treat as malformed response.
		return std::nullopt;
	}
} // namespace Concept2PM
