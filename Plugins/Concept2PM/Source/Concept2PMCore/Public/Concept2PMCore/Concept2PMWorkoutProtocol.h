#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Diagnostic-only managed-workout CSAFE command/response support for
// delivery Phase 0 Milestone 4 Spike A (docs/phase-0/07-milestone-4-spikes.md).
// Built only from published Concept2 CSAFE commands recorded in
// docs/archive/research/2026-09-12-concept2-pm-csafe-communication-definition.md.
// This is the wire-level counterpart of the diagnostic contract in
// docs/phase-0/02-public-interfaces.md ("Diagnostic managed-workout
// interface"). It never encodes a command outside that published
// specification, and it is not a product managed-workout implementation.
namespace Concept2PM
{
	inline constexpr std::uint8_t CsafeStandardStartFlag = 0xF1;
	inline constexpr std::uint8_t CsafeStopFlag = 0xF2;
	inline constexpr std::uint8_t CsafeByteStuffingFlag = 0xF3;

	// Byte-stuffs Contents (a fully built long/short-command byte sequence,
	// not yet including the checksum), appends the XOR checksum, and wraps
	// the result with the standard-frame start/stop flags. Contents must not
	// itself include the checksum byte. Returns empty if the framed result
	// would exceed the CSAFE PM physical-link's documented 120-byte maximum
	// frame size (including start/stop flags, checksum, and byte stuffing).
	std::vector<std::uint8_t> BuildStandardFrame(const std::vector<std::uint8_t> &Contents);

	inline constexpr std::size_t CsafeMaxFrameSize = 120;

	enum class ECsafePreviousFrameStatus : std::uint8_t
	{
		Ok,
		Reject,
		Bad,
		NotReady
	};

	enum class ECsafeStateMachineState : std::uint8_t
	{
		Error,
		Ready,
		Idle,
		HaveId,
		InUse,
		Pause,
		Finish,
		Manual,
		OffLine,
		Unknown
	};

	struct FCsafeStatus
	{
		bool FrameToggle = false;
		ECsafePreviousFrameStatus PreviousFrameStatus = ECsafePreviousFrameStatus::Ok;
		ECsafeStateMachineState StateMachineState = ECsafeStateMachineState::Unknown;
	};

	struct FCsafeParsedResponse
	{
		bool FrameWellFormed = false;
		bool ChecksumValid = false;
		// Only meaningful when ChecksumValid is true; a checksum failure
		// (or a malformed frame) leaves Status at its default values, which
		// must not be read as a genuinely decoded Ok/Unknown status.
		FCsafeStatus Status;
		// Unstuffed response content following the status byte: the echoed
		// command/response sequence, unparsed. A caller that needs a
		// specific echoed command's response data scans this.
		std::vector<std::uint8_t> ResponseContent;
	};

	// Unstuffs and checksum-validates one standard-frame response. Bytes
	// must begin at CsafeStandardStartFlag and end at CsafeStopFlag; a
	// malformed or truncated frame returns FrameWellFormed=false.
	FCsafeParsedResponse ParseStandardFrameResponse(const std::vector<std::uint8_t> &Bytes);

	// Diagnostic-only workout kinds this bounded spike builds. Interval is
	// scoped to a fixed-time work/rest interval workout with an undefined
	// repeat count, matching the published examples and PM state-transition
	// behavior for "Fixed Time Interval" (Appendix E): the PM repeats until
	// a deliberate terminate/abort, so no interval-count command is sent.
	enum class EDiagnosticWorkoutKind : std::uint8_t
	{
		Distance,
		Time,
		TimeInterval
	};

	struct FDiagnosticWorkoutSpec
	{
		EDiagnosticWorkoutKind Kind = EDiagnosticWorkoutKind::Distance;
		std::optional<std::uint32_t> DistanceMm;	 // Required for Distance.
		std::optional<std::uint32_t> DurationMs;	 // Required for Time and TimeInterval (per-work-segment for TimeInterval).
		std::optional<std::uint32_t> IntervalRestMs; // Required for TimeInterval.
	};

	enum class EDiagnosticWorkoutSpecError : std::uint8_t
	{
		None,
		MissingDistance,
		MissingDuration,
		MissingIntervalRest,
		// DistanceMm below the 1000 mm (1 m) wire granularity, or not a
		// whole number of meters.
		InvalidDistance,
		// DurationMs below the 10 ms (1 centisecond) wire granularity, or
		// not a whole number of centiseconds.
		InvalidDuration,
		// IntervalRestMs below the 1000 ms (1 s) wire granularity, not a
		// whole number of seconds, or too large to fit the wire's 16-bit
		// seconds field (over 65535 s).
		InvalidIntervalRest
	};

	// Validates Spec carries the fields its Kind requires, that each value
	// meets the wire format's unit granularity, and that it fits the wire
	// field it is encoded into without narrowing/wraparound. Never builds a
	// partial, degenerate, or silently-truncated command from an invalid
	// spec.
	EDiagnosticWorkoutSpecError
	ValidateDiagnosticWorkoutSpec(const FDiagnosticWorkoutSpec &Spec) noexcept;

	// Builds the unframed C2 proprietary-wrapper (CSAFE_SETPMCFG_CMD, 0x76)
	// contents that configure and arm the given diagnostic workout. Returns
	// empty when the spec fails ValidateDiagnosticWorkoutSpec.
	std::vector<std::uint8_t>
	BuildProgramDiagnosticWorkoutContents(const FDiagnosticWorkoutSpec &Spec);

	// Builds the unframed Terminate Workout contents (CSAFE_PM_SET_SCREENSTATE
	// / SCREENVALUEWORKOUT_TERMINATEWORKOUT wrapped in the 0x76 wrapper).
	std::vector<std::uint8_t> BuildAbortDiagnosticWorkoutContents();

	enum class EDiagnosticWorkoutProgramRejectReason : std::uint8_t
	{
		// Response could not be parsed (malformed frame, truncated stuffing pair,
		// invalid stuffing substitute, checksum mismatch). Never observed in the
		// wild; caught by ParseStandardFrameResponse's return value.
		MalformedResponse,
		// PM5 returned a Non-Acknowledge (NAK) byte in the frame status field.
		// This can happen when the command is sent to a PM5 that is not in a
		// compatible state (e.g., already armed with a different workout program).
		PM5Nak,
		// The echoed command data does not match what we sent (detectable after
		// verifying the command bytes against the sent bytes). This signals a
		// protocol-level mismatch, not a PM5 configuration error.
		WrongCommand,
		// PM5 did not acknowledge the command within the diagnostic session's
		// timeout window. The command was sent but no response was received.
		Timeout,
		// Catch-all for any other response format or content anomaly that indicates
		// the PM5's response does not conform to the expected program verify/abort
		// format for this bounded spike.
		Other
	};

	// PM5 read-back of the configured workout program, for program-verify
	// validation. Only Type and Duration are decoded here; split-duration and
	// rest-duration are not read back by this bounded spike's contract, so they
	// are omitted.
	struct FPM5ProgramReadback
	{
		EDiagnosticWorkoutKind Type = EDiagnosticWorkoutKind::Distance;
		std::optional<std::uint32_t> DurationMs; // For Time and TimeInterval workout types.
	};

	// Parses the echoed command data from a successful program verify abort
	// response, extracting the readback of Type and Duration (if applicable).
	// Returns an empty, malformed result if the response content cannot be
	// reliably parsed (e.g., unexpected data length, non-zero padding, or data
	// that does not match the expected wire format for a program verify/abort).
	// The caller should always verify FCsafeParsedResponse.FrameWellFormed and
	// FCsafeParsedResponse.ChecksumValid before calling this; on failure to
	// parse, the result's Type is EDiagnosticWorkoutKind::Distance and Duration
	// is std::nullopt (safe defaults), indicating the caller must fall back to
	// treating the outcome as a rejection (WrongCommand) rather than assuming
	// the command succeeded.
	std::optional<FPM5ProgramReadback>
	ParseProgramVerifyReadback(const FCsafeParsedResponse &ParsedResponse);
} // namespace Concept2PM
