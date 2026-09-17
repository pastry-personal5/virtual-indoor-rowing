#include "Concept2PMCore/Concept2PMWorkoutProtocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace Concept2PM;

namespace
{
	int FailureCount = 0;

	void Check(bool Condition, const char *Description)
	{
		if (!Condition)
		{
			std::fprintf(stderr, "FAILED: %s\n", Description);
			++FailureCount;
		}
	}

	void CheckBytesEqual(const std::vector<std::uint8_t> &Actual,
						 const std::vector<std::uint8_t> &Expected,
						 const char *Description)
	{
		if (Actual != Expected)
		{
			std::fprintf(stderr, "FAILED: %s (size %zu vs expected %zu)\n", Description, Actual.size(), Expected.size());
			++FailureCount;
		}
	}

	// Ground truth transcribed byte-for-byte from
	// docs/archive/research/2026-09-12-concept2-pm-csafe-communication-definition.md,
	// "Proprietary CSAFE Workout Configuration" and "Terminate Workout"
	// sample frames (Sample Functionality section).

	void TestJustRowContentsMatchPublishedExample()
	{
		// F1 76 07 01 01 01 13 02 01 01 61 F2
		const std::vector<std::uint8_t> ExpectedContents{0x76, 0x07, 0x01, 0x01, 0x01, 0x13, 0x02, 0x01, 0x01};
		// JustRow itself is out of this spike's Distance/Time/TimeInterval
		// scope, but its byte stream is the simplest independent cross-check
		// for the checksum algorithm (0x61) and BuildStandardFrame's framing,
		// since ExpectedContents here is a hand-typed literal rather than one
		// built through this file's internal (anonymous-namespace)
		// LongCommand/WrapProprietary helpers.
		const std::vector<std::uint8_t> Frame = BuildStandardFrame(ExpectedContents);
		const std::vector<std::uint8_t> ExpectedFrame{0xF1, 0x76, 0x07, 0x01, 0x01, 0x01, 0x13, 0x02, 0x01, 0x01, 0x61, 0xF2};
		CheckBytesEqual(Frame, ExpectedFrame, "JustRow frame matches published example exactly");
	}

	void TestProgramDistanceWorkoutMatchesPublishedStructure()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::Distance;
		Spec.DistanceMm = 2'000'000; // 2000 m, matching the published "Fixed Distance" example's distance.
		const std::vector<std::uint8_t> Contents = BuildProgramDiagnosticWorkoutContents(Spec);
		// F1 76 11 01 01 02 03 05 80 00 00 07 D0 14 01 01 13 02 01 01 .. F2
		// This spike never configures a distinct split (it would equal the
		// full workout distance), so it uses the *_NOSPLITS workout-type
		// opcode (2) and sends no SetSplitDuration sub-command at all,
		// rather than reproducing the published example's *_SPLITS opcode
		// (3) and its own, smaller split value.
		const std::vector<std::uint8_t> Expected{
			0x76, 0x11, // wrapper, body length 17
			0x01,
			0x01,
			0x02, // SetWorkoutType: FixedDistanceNoSplits
			0x03,
			0x05,
			0x80,
			0x00,
			0x00,
			0x07,
			0xD0, // SetWorkoutDuration: distance, 2000 m
			0x14,
			0x01,
			0x01, // ConfigureWorkout: enable
			0x13,
			0x02,
			0x01,
			0x01 // SetScreenState: prepare to row
		};
		CheckBytesEqual(Contents, Expected, "Distance workout contents match published opcode structure");
	}

	void TestProgramTimeWorkoutMatchesPublishedStructure()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::Time;
		Spec.DurationMs = 1'200'000; // 20:00, matching the published "Fixed Time" example's duration.
		const std::vector<std::uint8_t> Contents = BuildProgramDiagnosticWorkoutContents(Spec);
		// F1 76 11 01 01 04 03 05 00 00 01 D4 C0 14 01 01 13 02 01 01 .. F2
		// As with Distance, this uses the *_NOSPLITS opcode (4) and sends no
		// SetSplitDuration sub-command.
		const std::vector<std::uint8_t> Expected{
			0x76, 0x11, 0x01, 0x01, 0x04, // SetWorkoutType: FixedTimeNoSplits
			0x03,
			0x05,
			0x00,
			0x00,
			0x01,
			0xD4,
			0xC0, // SetWorkoutDuration: time, 1200.00s = 120000 cs = 0x0001D4C0
			0x14,
			0x01,
			0x01,
			0x13,
			0x02,
			0x01,
			0x01};
		CheckBytesEqual(Contents, Expected, "Time workout contents match published opcode structure");
	}

	void TestProgramTimeIntervalWorkoutMatchesPublishedExample()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::TimeInterval;
		Spec.DurationMs = 120'000;	  // 2:00 work, matching the published "Fixed Time Interval" example.
		Spec.IntervalRestMs = 30'000; // :30 rest.
		const std::vector<std::uint8_t> Contents = BuildProgramDiagnosticWorkoutContents(Spec);
		// F1 76 15 01 01 06 03 05 00 00 00 2E E0 04 02 00 1E 14 01 01 13 02 01 01 .. F2
		const std::vector<std::uint8_t> Expected{
			0x76, 0x15, 0x01, 0x01, 0x06, // SetWorkoutType: FixedTimeInterval
			0x03,
			0x05,
			0x00,
			0x00,
			0x00,
			0x2E,
			0xE0, // SetWorkoutDuration: time, 2:00 = 12000 cs = 0x2EE0
			0x04,
			0x02,
			0x00,
			0x1E, // SetRestDuration: 30 s
			0x14,
			0x01,
			0x01,
			0x13,
			0x02,
			0x01,
			0x01};
		CheckBytesEqual(Contents, Expected, "Fixed time interval contents match the published byte-exact example");

		// The published checksum table entry for this example ("0A") is
		// internally inconsistent with its own content bytes (it matches
		// the different Fixed Distance Interval example instead, evidence
		// of a transcription duplicate) — verified by re-deriving both
		// checksums from the prose XOR algorithm against every byte in
		// each example's distinct content. The Fixed Distance Interval
		// example's checksum (0x0A) round-trips correctly, which is the
		// cross-check retained here; this example checks structure/opcodes
		// only, not a hand-copied checksum digit.
	}

	void TestAbortWorkoutMatchesPublishedExample()
	{
		const std::vector<std::uint8_t> Contents = BuildAbortDiagnosticWorkoutContents();
		// F1 76 04 13 02 01 02 .. F2 (Terminate Workout sample frame).
		const std::vector<std::uint8_t> Expected{0x76, 0x04, 0x13, 0x02, 0x01, 0x02};
		CheckBytesEqual(Contents, Expected, "Abort contents match the published Terminate Workout example");

		// As with the Fixed Time Interval example, the published checksum
		// digit for this table row ("62") does not reproduce from its own
		// six content bytes under the prose-stated XOR algorithm (it
		// yields 0x60); BuildStandardFrame always computes the checksum
		// programmatically rather than trusting a transcribed digit, so
		// this discrepancy cannot affect the bytes actually sent.
		const std::vector<std::uint8_t> Frame = BuildStandardFrame(Contents);
		std::uint8_t Checksum = 0;
		for (std::uint8_t Byte : Contents)
			Checksum ^= Byte;
		Check(Frame[Frame.size() - 2] == Checksum,
			  "Abort frame checksum is the XOR of its own content bytes");
	}

	void TestDistanceIntervalOpcodeStructureFromPublishedExampleCrossValidatesChecksum()
	{
		// F1 76 15 01 01 07 03 05 80 00 00 01 F4 04 02 00 1E 14 01 01 13 02 01 01 0A F2
		// This example is outside this spike's Kind enum (distance-based
		// interval), but its full byte stream including checksum (0x0A) is
		// used here purely to cross-validate the checksum algorithm against
		// an unrelated, independently transcribed content sequence.
		const std::vector<std::uint8_t> Contents{
			0x76, 0x15, 0x01, 0x01, 0x07, 0x03, 0x05, 0x80, 0x00, 0x00, 0x01, 0xF4, 0x04, 0x02, 0x00, 0x1E, 0x14, 0x01, 0x01, 0x13, 0x02, 0x01, 0x01};
		const std::vector<std::uint8_t> Frame = BuildStandardFrame(Contents);
		const std::vector<std::uint8_t> ExpectedFrame = [&Contents]()
		{
			std::vector<std::uint8_t> Out{CsafeStandardStartFlag};
			Out.insert(Out.end(), Contents.begin(), Contents.end());
			Out.push_back(0x0A);
			Out.push_back(CsafeStopFlag);
			return Out;
		}();
		CheckBytesEqual(Frame, ExpectedFrame, "Published Fixed Distance Interval example's checksum (0x0A) reproduces exactly");
	}

	void TestByteStuffingRoundTrips()
	{
		// Synthetic content chosen to hit every reserved flag value, since
		// none of the published examples happen to contain one.
		const std::vector<std::uint8_t> Contents{0x01, 0xF0, 0xF1, 0xF2, 0xF3, 0x02};
		const std::vector<std::uint8_t> Frame = BuildStandardFrame(Contents);
		// Every reserved byte (content + checksum) must be stuffed: F3 followed by its substitute.
		Check(Frame.front() == CsafeStandardStartFlag, "Stuffed frame still starts with F1");
		Check(Frame.back() == CsafeStopFlag, "Stuffed frame still ends with F2");

		const FCsafeParsedResponse Parsed = ParseStandardFrameResponse(Frame);
		Check(Parsed.FrameWellFormed, "Round-tripped stuffed frame parses as well-formed");
		Check(Parsed.ChecksumValid, "Round-tripped stuffed frame's checksum validates");
		// Contents[0] = 0x01 is the "status byte" ParseStandardFrameResponse
		// decodes; verify it decodes to exactly the fields 0x01 encodes
		// (FrameToggle clear, PreviousFrameStatus=Ok, StateMachineState=Ready),
		// and that ResponseContent reproduces the remaining four reserved
		// bytes exactly — proving the byte-stuffing encode (NeedsByteStuffing/
		// StuffedSubstitute) and decode (the switch in ParseStandardFrameResponse)
		// tables agree with each other, not just that some checksum validates.
		Check(!Parsed.Status.FrameToggle, "Recovered status byte's frame-toggle bit matches the original");
		Check(Parsed.Status.PreviousFrameStatus == ECsafePreviousFrameStatus::Ok,
			  "Recovered status byte's previous-frame-status bits match the original");
		Check(Parsed.Status.StateMachineState == ECsafeStateMachineState::Ready,
			  "Recovered status byte's state-machine-state bits match the original");
		const std::vector<std::uint8_t> ExpectedResponseContent{0xF0, 0xF1, 0xF2, 0xF3, 0x02};
		CheckBytesEqual(Parsed.ResponseContent, ExpectedResponseContent, "Recovered response content matches the original reserved-byte content exactly");
	}

	void TestParseStandardFrameResponseDecodesStatusByte()
	{
		// Status byte 0x81: FrameToggle=1, PreviousFrameStatus=Ok(00), StateMachineState=InUse(? no)
		// Construct explicitly from Table 9 bit-mapping: 0x80 | (0x00<<4) | 0x05 = 0x85 (InUse, Ok, toggled).
		const std::vector<std::uint8_t> Contents{0x85};
		const std::vector<std::uint8_t> Frame = BuildStandardFrame(Contents);
		const FCsafeParsedResponse Parsed = ParseStandardFrameResponse(Frame);
		Check(Parsed.FrameWellFormed, "Status-only frame parses as well-formed");
		Check(Parsed.ChecksumValid, "Status-only frame checksum validates");
		Check(Parsed.Status.FrameToggle, "Frame toggle bit decodes as set");
		Check(Parsed.Status.PreviousFrameStatus == ECsafePreviousFrameStatus::Ok,
			  "Previous frame status decodes as Ok");
		Check(Parsed.Status.StateMachineState == ECsafeStateMachineState::InUse,
			  "State machine state decodes as InUse");
		Check(Parsed.ResponseContent.empty(), "No response content follows a status-only frame");
	}

	void TestParseStandardFrameResponseRejectsBadChecksum()
	{
		std::vector<std::uint8_t> Frame = BuildStandardFrame({0x01, 0x02});
		Frame[Frame.size() - 2] ^= 0xFF; // Corrupt the checksum byte.
		const FCsafeParsedResponse Parsed = ParseStandardFrameResponse(Frame);
		Check(Parsed.FrameWellFormed, "Malformed-checksum frame is still structurally well-formed");
		Check(!Parsed.ChecksumValid, "Corrupted checksum is detected");
	}

	void TestValidateDiagnosticWorkoutSpecRejectsMissingFields()
	{
		Check(ValidateDiagnosticWorkoutSpec({EDiagnosticWorkoutKind::Distance, std::nullopt, std::nullopt, std::nullopt}) ==
				  EDiagnosticWorkoutSpecError::MissingDistance,
			  "Distance spec without DistanceMm is rejected");
		Check(ValidateDiagnosticWorkoutSpec({EDiagnosticWorkoutKind::Time, std::nullopt, std::nullopt, std::nullopt}) ==
				  EDiagnosticWorkoutSpecError::MissingDuration,
			  "Time spec without DurationMs is rejected");
		FDiagnosticWorkoutSpec IntervalMissingRest;
		IntervalMissingRest.Kind = EDiagnosticWorkoutKind::TimeInterval;
		IntervalMissingRest.DurationMs = 60'000;
		Check(ValidateDiagnosticWorkoutSpec(IntervalMissingRest) ==
				  EDiagnosticWorkoutSpecError::MissingIntervalRest,
			  "Interval spec without IntervalRestMs is rejected");
		Check(BuildProgramDiagnosticWorkoutContents(IntervalMissingRest).empty(),
			  "An invalid spec builds no command bytes");
	}

	void TestValidateDiagnosticWorkoutSpecRejectsSubGranularityValues()
	{
		// Regression coverage for a code-review finding: a value present but
		// below the wire's unit granularity must be rejected, not silently
		// truncated to a degenerate 0-value command.
		FDiagnosticWorkoutSpec SubMeterDistance;
		SubMeterDistance.Kind = EDiagnosticWorkoutKind::Distance;
		SubMeterDistance.DistanceMm = 500; // 0.5 m: would truncate to 0 m.
		Check(ValidateDiagnosticWorkoutSpec(SubMeterDistance) == EDiagnosticWorkoutSpecError::InvalidDistance,
			  "Distance below 1 m is rejected rather than truncated to 0");
		Check(BuildProgramDiagnosticWorkoutContents(SubMeterDistance).empty(),
			  "A sub-granularity distance spec builds no command bytes");

		FDiagnosticWorkoutSpec SubCentisecondTime;
		SubCentisecondTime.Kind = EDiagnosticWorkoutKind::Time;
		SubCentisecondTime.DurationMs = 5; // Below the 10 ms centisecond unit.
		Check(ValidateDiagnosticWorkoutSpec(SubCentisecondTime) == EDiagnosticWorkoutSpecError::InvalidDuration,
			  "Duration below 1 centisecond is rejected rather than truncated to 0");
		Check(BuildProgramDiagnosticWorkoutContents(SubCentisecondTime).empty(),
			  "A sub-granularity duration spec builds no command bytes");

		FDiagnosticWorkoutSpec SubSecondRest;
		SubSecondRest.Kind = EDiagnosticWorkoutKind::TimeInterval;
		SubSecondRest.DurationMs = 60'000;
		SubSecondRest.IntervalRestMs = 500; // Below the 1000 ms second unit.
		Check(ValidateDiagnosticWorkoutSpec(SubSecondRest) == EDiagnosticWorkoutSpecError::InvalidIntervalRest,
			  "Interval rest below 1 s is rejected rather than truncated to 0");
		Check(BuildProgramDiagnosticWorkoutContents(SubSecondRest).empty(),
			  "A sub-granularity interval-rest spec builds no command bytes");
	}

	void TestValidateDiagnosticWorkoutSpecEnforcesPm5ParameterLimits()
	{
		// Regression coverage for a code-review finding: a value can meet
		// the wire's unit granularity yet still fall outside the PM5's own
		// documented parameter limits (pinned CSAFE definition, Table 19),
		// which is a command outside the published specification even
		// though it would fit the wire field's byte width. Boundary values
		// are exercised on both sides of each documented min/max.
		FDiagnosticWorkoutSpec DistanceTooShort;
		DistanceTooShort.Kind = EDiagnosticWorkoutKind::Distance;
		DistanceTooShort.DistanceMm = 99'000; // 99 m; Table 19 minimum is 100 m.
		Check(ValidateDiagnosticWorkoutSpec(DistanceTooShort) == EDiagnosticWorkoutSpecError::InvalidDistance,
			  "Distance below the PM5's 100 m minimum is rejected");

		FDiagnosticWorkoutSpec DistanceAtMinimum;
		DistanceAtMinimum.Kind = EDiagnosticWorkoutKind::Distance;
		DistanceAtMinimum.DistanceMm = 100'000; // Exactly 100 m.
		Check(ValidateDiagnosticWorkoutSpec(DistanceAtMinimum) == EDiagnosticWorkoutSpecError::None,
			  "Distance exactly at the PM5's 100 m minimum is accepted");

		FDiagnosticWorkoutSpec DistanceTooLong;
		DistanceTooLong.Kind = EDiagnosticWorkoutKind::Distance;
		DistanceTooLong.DistanceMm = 1'000'000'000; // 1,000,000 m; Table 19 maximum is 999999 m.
		Check(ValidateDiagnosticWorkoutSpec(DistanceTooLong) == EDiagnosticWorkoutSpecError::InvalidDistance,
			  "Distance above the PM5's 999999 m maximum is rejected");

		FDiagnosticWorkoutSpec TimeTooShort;
		TimeTooShort.Kind = EDiagnosticWorkoutKind::Time;
		TimeTooShort.DurationMs = 10'000; // :10; Table 19 minimum is :20.
		Check(ValidateDiagnosticWorkoutSpec(TimeTooShort) == EDiagnosticWorkoutSpecError::InvalidDuration,
			  "Duration below the PM5's :20 minimum is rejected");

		FDiagnosticWorkoutSpec TimeTooLong;
		TimeTooLong.Kind = EDiagnosticWorkoutKind::Time;
		TimeTooLong.DurationMs = 36'000'000; // 10:00:00; Table 19 maximum is 9:59:59.
		Check(ValidateDiagnosticWorkoutSpec(TimeTooLong) == EDiagnosticWorkoutSpecError::InvalidDuration,
			  "Duration above the PM5's 9:59:59 maximum is rejected");

		FDiagnosticWorkoutSpec IntervalWorkTooLong;
		IntervalWorkTooLong.Kind = EDiagnosticWorkoutKind::TimeInterval;
		IntervalWorkTooLong.DurationMs = 3'600'000; // 1:00:00; Table 19's interval-time maximum is 59:59.
		IntervalWorkTooLong.IntervalRestMs = 30'000;
		Check(ValidateDiagnosticWorkoutSpec(IntervalWorkTooLong) == EDiagnosticWorkoutSpecError::InvalidDuration,
			  "Interval work duration above the PM5's 59:59 maximum is rejected, tighter than the generic fixed-time cap");
	}

	void TestValidateDiagnosticWorkoutSpecEnforcesIntervalRestLimit()
	{
		// Table 19's rest-duration maximum is 9:55 (595 s), well inside the
		// wire's 16-bit seconds field — the parameter-limit check, not the
		// wire-width check, is what must reject a too-large value here.
		FDiagnosticWorkoutSpec OverLimitRest;
		OverLimitRest.Kind = EDiagnosticWorkoutKind::TimeInterval;
		OverLimitRest.DurationMs = 60'000;
		OverLimitRest.IntervalRestMs = 600'000; // 10:00 > the PM5's 9:55 maximum.
		Check(ValidateDiagnosticWorkoutSpec(OverLimitRest) == EDiagnosticWorkoutSpecError::InvalidIntervalRest,
			  "Interval rest above the PM5's 9:55 maximum is rejected");
		Check(BuildProgramDiagnosticWorkoutContents(OverLimitRest).empty(),
			  "An over-limit interval-rest spec builds no command bytes");

		FDiagnosticWorkoutSpec MaxValidRest;
		MaxValidRest.Kind = EDiagnosticWorkoutKind::TimeInterval;
		MaxValidRest.DurationMs = 60'000;
		MaxValidRest.IntervalRestMs = 595'000; // Exactly 9:55, the PM5's documented maximum.
		Check(ValidateDiagnosticWorkoutSpec(MaxValidRest) == EDiagnosticWorkoutSpecError::None,
			  "Interval rest exactly at the PM5's 9:55 maximum is accepted");
	}

	void TestParseStandardFrameResponseRejectsEmptyContent()
	{
		// Regression coverage for a code-review finding: BuildStandardFrame({})
		// legitimately produces a frame whose sole content byte is a 0x00
		// checksum (the XOR of no bytes) with no status byte behind it;
		// ParseStandardFrameResponse must report this as malformed, not as
		// a self-consistent ChecksumValid=true frame.
		const std::vector<std::uint8_t> EmptyFrame = BuildStandardFrame({});
		const FCsafeParsedResponse Parsed = ParseStandardFrameResponse(EmptyFrame);
		Check(!Parsed.FrameWellFormed, "A frame with no status byte behind its checksum is reported malformed");
		Check(!Parsed.ChecksumValid, "A frame with no status byte behind its checksum is never reported checksum-valid");
	}
	// Builds a synthetic echoed-response FCsafeParsedResponse whose
	// ResponseContent is exactly the command subcommand bytes a PM5 would
	// echo back for Spec (reusing BuildProgramDiagnosticWorkoutContents,
	// stripping the 0x76 wrapper header, since this spike's contract has no
	// real-PM5 evidence yet to transcribe a genuine echoed response from).
	FCsafeParsedResponse MakeEchoedResponse(const FDiagnosticWorkoutSpec &Spec)
	{
		const std::vector<std::uint8_t> Wrapped = BuildProgramDiagnosticWorkoutContents(Spec);
		FCsafeParsedResponse Response;
		Response.FrameWellFormed = true;
		Response.ChecksumValid = true;
		// Strip the wrapper's own [0x76][BodyLength] header; ResponseContent
		// models the subcommand stream ParseProgramVerifyReadback scans.
		Response.ResponseContent.assign(Wrapped.begin() + 2, Wrapped.end());
		return Response;
	}

	void TestParseProgramVerifyReadbackDecodesDistanceWorkout()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::Distance;
		Spec.DistanceMm = 2'000'000;
		const std::optional<FPM5ProgramReadback> Readback = ParseProgramVerifyReadback(MakeEchoedResponse(Spec));
		Check(Readback.has_value(), "Distance echo parses to a readback");
		if (Readback)
		{
			Check(Readback->Type == EDiagnosticWorkoutKind::Distance, "Distance echo decodes Type as Distance");
			Check(!Readback->DurationMs.has_value(), "Distance echo leaves DurationMs unset");
		}
	}

	void TestParseProgramVerifyReadbackDecodesTimeWorkout()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::Time;
		Spec.DurationMs = 1'200'000; // 20:00.
		const std::optional<FPM5ProgramReadback> Readback = ParseProgramVerifyReadback(MakeEchoedResponse(Spec));
		Check(Readback.has_value(), "Time echo parses to a readback");
		if (Readback)
		{
			Check(Readback->Type == EDiagnosticWorkoutKind::Time, "Time echo decodes Type as Time");
			Check(Readback->DurationMs == 1'200'000U, "Time echo round-trips DurationMs exactly");
		}
	}

	void TestParseProgramVerifyReadbackDecodesTimeIntervalWorkout()
	{
		FDiagnosticWorkoutSpec Spec;
		Spec.Kind = EDiagnosticWorkoutKind::TimeInterval;
		Spec.DurationMs = 120'000; // 2:00 work.
		Spec.IntervalRestMs = 30'000;
		const std::optional<FPM5ProgramReadback> Readback = ParseProgramVerifyReadback(MakeEchoedResponse(Spec));
		Check(Readback.has_value(), "TimeInterval echo parses to a readback");
		if (Readback)
		{
			Check(Readback->Type == EDiagnosticWorkoutKind::TimeInterval, "TimeInterval echo decodes Type as TimeInterval");
			Check(Readback->DurationMs == 120'000U, "TimeInterval echo round-trips work DurationMs exactly");
		}
	}

	void TestParseProgramVerifyReadbackRejectsUnrecognizedWorkoutType()
	{
		// Regression coverage for a code-review finding: an unrecognized
		// WorkoutType byte must be reported as a failed parse (nullopt), not
		// silently mapped to a spurious "verified" Distance workout.
		FCsafeParsedResponse Response;
		Response.FrameWellFormed = true;
		Response.ChecksumValid = true;
		Response.ResponseContent = {0x01, 0x01, 0xEE}; // SetWorkoutType, 1-byte data, unknown opcode 0xEE.
		const std::optional<FPM5ProgramReadback> Readback = ParseProgramVerifyReadback(Response);
		Check(!Readback.has_value(), "An unrecognized WorkoutType byte is reported as a failed parse, not Distance");
	}

	void TestParseProgramVerifyReadbackRejectsUnvalidatedFrame()
	{
		FCsafeParsedResponse Response;
		Response.FrameWellFormed = true;
		Response.ChecksumValid = false;
		Response.ResponseContent = {0x01, 0x01, 0x02};
		Check(!ParseProgramVerifyReadback(Response).has_value(),
			  "A response whose checksum did not validate is never parsed");
	}

	void TestParseProgramVerifyReadbackFindsFinalHeaderOnlySubcommand()
	{
		// Regression coverage for a code-review finding: the subcommand scan
		// must not use a strict '<' bound that excludes a final, header-only
		// (0-length-data) subcommand whose last header byte sits at
		// Content.size() - 1.
		FCsafeParsedResponse Response;
		Response.FrameWellFormed = true;
		Response.ChecksumValid = true;
		Response.ResponseContent = {0x01, 0x00}; // SetWorkoutType header with 0-length data.
		Check(!ParseProgramVerifyReadback(Response).has_value(),
			  "A trailing header-only SetWorkoutType with no type byte is scanned and rejected, not skipped");
	}
} // namespace

int main()
{
	TestJustRowContentsMatchPublishedExample();
	TestProgramDistanceWorkoutMatchesPublishedStructure();
	TestProgramTimeWorkoutMatchesPublishedStructure();
	TestProgramTimeIntervalWorkoutMatchesPublishedExample();
	TestAbortWorkoutMatchesPublishedExample();
	TestDistanceIntervalOpcodeStructureFromPublishedExampleCrossValidatesChecksum();
	TestByteStuffingRoundTrips();
	TestParseStandardFrameResponseDecodesStatusByte();
	TestParseStandardFrameResponseRejectsBadChecksum();
	TestValidateDiagnosticWorkoutSpecRejectsMissingFields();
	TestValidateDiagnosticWorkoutSpecRejectsSubGranularityValues();
	TestValidateDiagnosticWorkoutSpecEnforcesPm5ParameterLimits();
	TestValidateDiagnosticWorkoutSpecEnforcesIntervalRestLimit();
	TestParseStandardFrameResponseRejectsEmptyContent();
	TestParseProgramVerifyReadbackDecodesDistanceWorkout();
	TestParseProgramVerifyReadbackDecodesTimeWorkout();
	TestParseProgramVerifyReadbackDecodesTimeIntervalWorkout();
	TestParseProgramVerifyReadbackRejectsUnrecognizedWorkoutType();
	TestParseProgramVerifyReadbackRejectsUnvalidatedFrame();
	TestParseProgramVerifyReadbackFindsFinalHeaderOnlySubcommand();

	if (FailureCount != 0)
	{
		std::fprintf(stderr, "%d check(s) failed\n", FailureCount);
		return 1;
	}
	std::printf("All Concept2PMWorkoutProtocol checks passed\n");
	return 0;
}
