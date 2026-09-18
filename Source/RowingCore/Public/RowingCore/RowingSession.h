#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Session-continuity domain: the "Local workout continuity" truth owned by
// RowingCore/LocalData, per docs/architecture/README.md's four-truths table.
// Deliberately separate from RowingDevice's ERowingConnectionState, which is
// BLE-adapter connection state, not session-recording continuity.

// Opaque 128-bit session identifier per the canonical-identifiers rule in
// docs/architecture/06-data-and-protocols.md: client-created, UUIDv7 preferred,
// serialized as 16 bytes on the wire and as a canonical lowercase hex string
// for logs/JSON. Never parsed for meaning beyond equality/ordering.
class FRowingSessionId final
{
  public:
	static constexpr std::size_t ByteLength = 16;
	using FBytes = std::array<std::uint8_t, ByteLength>;

	FRowingSessionId() = default;

	static FRowingSessionId FromBytes(const FBytes &Bytes)
	{
		return FRowingSessionId(Bytes);
	}

	// RFC 9562 UUID version 7: 48-bit Unix ms timestamp, version/variant bits
	// set per spec, remaining bits filled from the supplied entropy source.
	// NextRandomByte must return uniformly distributed bytes; RowingCore does
	// not choose the entropy source itself so it stays testable/deterministic.
	template <typename FByteSource>
	static FRowingSessionId GenerateV7(std::uint64_t UnixTimeMs, FByteSource &&NextRandomByte)
	{
		FBytes Bytes{};
		Bytes[0] = static_cast<std::uint8_t>((UnixTimeMs >> 40) & 0xFF);
		Bytes[1] = static_cast<std::uint8_t>((UnixTimeMs >> 32) & 0xFF);
		Bytes[2] = static_cast<std::uint8_t>((UnixTimeMs >> 24) & 0xFF);
		Bytes[3] = static_cast<std::uint8_t>((UnixTimeMs >> 16) & 0xFF);
		Bytes[4] = static_cast<std::uint8_t>((UnixTimeMs >> 8) & 0xFF);
		Bytes[5] = static_cast<std::uint8_t>(UnixTimeMs & 0xFF);
		for (std::size_t Index = 6; Index < ByteLength; ++Index)
			Bytes[Index] = NextRandomByte();
		Bytes[6] = static_cast<std::uint8_t>((Bytes[6] & 0x0F) | 0x70); // version 7
		Bytes[8] = static_cast<std::uint8_t>((Bytes[8] & 0x3F) | 0x80); // variant 10
		return FRowingSessionId(Bytes);
	}

	const FBytes &GetBytes() const noexcept
	{
		return Value;
	}

	std::string ToCanonicalString() const
	{
		static constexpr char HexDigits[] = "0123456789abcdef";
		std::string Result;
		Result.reserve(36);
		for (std::size_t Index = 0; Index < ByteLength; ++Index)
		{
			Result.push_back(HexDigits[(Value[Index] >> 4) & 0x0F]);
			Result.push_back(HexDigits[Value[Index] & 0x0F]);
			if (Index == 3 || Index == 5 || Index == 7 || Index == 9)
				Result.push_back('-');
		}
		return Result;
	}

	friend bool operator==(const FRowingSessionId &, const FRowingSessionId &) = default;

  private:
	explicit FRowingSessionId(const FBytes &Bytes)
		: Value(Bytes) {}

	FBytes Value{};
};

enum class ERowingSessionState : std::uint8_t
{
	Created,
	Active,
	ConnectionLost,
	Ended
};

// Reuses the exact disposition vocabulary from the Session record in
// docs/architecture/06-data-and-protocols.md so a later journal/session
// mapping does not need to invent a second naming for the same concept.
enum class ERowingSessionDisposition : std::uint8_t
{
	Completed,
	Interrupted,
	Aborted
};

enum class ERowingSessionStateReason : std::uint8_t
{
	None,
	SessionStarted,
	DeviceReady,
	LinkLost,
	LinkRestored,
	UserCompleted,
	UserAborted,
	RecoveredAfterUncleanExit,
	// Phase 1 Milestone 4 (docs/phase-1/04-milestone-4-workout-runtime.md):
	// device-reported workout end, and the runtime's own reconnect window
	// elapsing while the link is down. Appended so existing values are stable.
	DeviceCompleted,
	DeviceTerminated,
	ReconnectWindowElapsed
};

struct FRowingSessionStateChanged
{
	ERowingSessionState PreviousState = ERowingSessionState::Created;
	ERowingSessionState NewState = ERowingSessionState::Created;
	ERowingSessionStateReason Reason = ERowingSessionStateReason::None;
	std::optional<ERowingSessionDisposition> Disposition;
};

// A pure, bounded state machine: no I/O, no clock, no persistence. It only
// enforces which session-continuity transitions are valid; callers observe
// success via the returned change, exactly like FRowingCommandResult reports
// acceptance rather than a synthesized outcome.
class FRowingSessionStateMachine final
{
  public:
	explicit FRowingSessionStateMachine(FRowingSessionId SessionId)
		: Id(SessionId)
	{
	}

	const FRowingSessionId &GetId() const noexcept
	{
		return Id;
	}
	ERowingSessionState GetState() const noexcept
	{
		return State;
	}

	// Returns the applied change, or std::nullopt if Reason does not name a
	// valid edge out of the current state. A rejected call leaves state
	// unchanged; it never throws and never fabricates a transition.
	std::optional<FRowingSessionStateChanged> TryTransition(ERowingSessionStateReason Reason)
	{
		const ERowingSessionState Previous = State;
		switch (Previous)
		{
		case ERowingSessionState::Created:
			if (Reason == ERowingSessionStateReason::SessionStarted || Reason == ERowingSessionStateReason::DeviceReady)
				return Apply(Previous, ERowingSessionState::Active, Reason, std::nullopt);
			if (Reason == ERowingSessionStateReason::UserAborted)
				return Apply(Previous, ERowingSessionState::Ended, Reason, ERowingSessionDisposition::Aborted);
			return std::nullopt;

		case ERowingSessionState::Active:
			if (Reason == ERowingSessionStateReason::LinkLost)
				return Apply(Previous, ERowingSessionState::ConnectionLost, Reason, std::nullopt);
			if (Reason == ERowingSessionStateReason::UserCompleted || Reason == ERowingSessionStateReason::DeviceCompleted)
				return Apply(Previous, ERowingSessionState::Ended, Reason, ERowingSessionDisposition::Completed);
			if (Reason == ERowingSessionStateReason::UserAborted || Reason == ERowingSessionStateReason::DeviceTerminated)
				return Apply(Previous, ERowingSessionState::Ended, Reason, ERowingSessionDisposition::Aborted);
			return std::nullopt;

		case ERowingSessionState::ConnectionLost:
			if (Reason == ERowingSessionStateReason::LinkRestored)
				return Apply(Previous, ERowingSessionState::Active, Reason, std::nullopt);
			if (Reason == ERowingSessionStateReason::RecoveredAfterUncleanExit || Reason == ERowingSessionStateReason::ReconnectWindowElapsed || Reason == ERowingSessionStateReason::UserAborted)
				return Apply(Previous, ERowingSessionState::Ended, Reason, ERowingSessionDisposition::Interrupted);
			return std::nullopt;

		case ERowingSessionState::Ended:
			return std::nullopt;
		}
	}

  private:
	std::optional<FRowingSessionStateChanged> Apply(ERowingSessionState Previous, ERowingSessionState Next, ERowingSessionStateReason Reason, std::optional<ERowingSessionDisposition> Disposition)
	{
		State = Next;
		return FRowingSessionStateChanged{Previous, Next, Reason, Disposition};
	}

	FRowingSessionId Id;
	ERowingSessionState State = ERowingSessionState::Created;
};
