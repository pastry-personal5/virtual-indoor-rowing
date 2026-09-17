#include "rowing/v1/session.pb.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	// Appends one protobuf field, tag-and-value, as the wire format actually
	// encodes it: tag = (field_number << 3) | wire_type, varint-encoded in
	// 7-bit little-endian groups with a continuation bit. A single raw byte
	// only encodes field numbers 0-15; field 99 requires two tag bytes, so
	// truncating it silently aliases onto a low, assigned field number instead.
	std::string EncodeVarint(std::uint64_t Value)
	{
		std::string Bytes;
		do
		{
			std::uint8_t Byte = static_cast<std::uint8_t>(Value & 0x7F);
			Value >>= 7;
			if (Value != 0)
				Byte |= 0x80;
			Bytes.push_back(static_cast<char>(Byte));
		} while (Value != 0);
		return Bytes;
	}

	// Proves the CMake protoc codegen pipeline works end-to-end: a message
	// built from generated code round-trips through serialization unchanged.
	void session_state_changed_event_round_trips_through_serialization()
	{
		rowing::v1::SessionStateChangedEvent Original;
		Original.set_contract_version(1);
		Original.set_session_id(std::string("0123456789abcdef", 16));
		Original.set_previous_state(rowing::v1::SESSION_STATE_ACTIVE);
		Original.set_new_state(rowing::v1::SESSION_STATE_ENDED);
		Original.set_reason(rowing::v1::SESSION_STATE_REASON_USER_COMPLETED);
		Original.set_disposition(rowing::v1::SESSION_DISPOSITION_COMPLETED);

		std::string Wire;
		EXPECT_TRUE(Original.SerializeToString(&Wire));

		rowing::v1::SessionStateChangedEvent Decoded;
		EXPECT_TRUE(Decoded.ParseFromString(Wire));

		EXPECT_TRUE(Decoded.contract_version() == 1);
		EXPECT_TRUE(Decoded.session_id() == Original.session_id());
		EXPECT_TRUE(Decoded.previous_state() == rowing::v1::SESSION_STATE_ACTIVE);
		EXPECT_TRUE(Decoded.new_state() == rowing::v1::SESSION_STATE_ENDED);
		EXPECT_TRUE(Decoded.reason() == rowing::v1::SESSION_STATE_REASON_USER_COMPLETED);
		EXPECT_TRUE(Decoded.disposition() == rowing::v1::SESSION_DISPOSITION_COMPLETED);
	}

	// Design rule 8 (docs/architecture/06-data-and-protocols.md): "Unknown
	// optional fields are tolerated." A message serialized with a field the
	// reader's schema does not know must not fail to parse the fields it does
	// know — simulated here by parsing a truncated, appended-field payload.
	void unknown_trailing_field_is_tolerated_without_losing_known_fields()
	{
		rowing::v1::SessionStateChangedEvent Original;
		Original.set_contract_version(1);
		Original.set_new_state(rowing::v1::SESSION_STATE_ACTIVE);

		std::string Wire;
		EXPECT_TRUE(Original.SerializeToString(&Wire));
		// Append a well-formed but unassigned field (number 99, varint type).
		constexpr int UnknownFieldNumber = 99;
		constexpr int VarintWireType = 0;
		Wire += EncodeVarint((static_cast<std::uint64_t>(UnknownFieldNumber) << 3) | VarintWireType);
		Wire += EncodeVarint(1);

		rowing::v1::SessionStateChangedEvent Decoded;
		EXPECT_TRUE(Decoded.ParseFromString(Wire));
		EXPECT_TRUE(Decoded.contract_version() == 1);
		EXPECT_TRUE(Decoded.new_state() == rowing::v1::SESSION_STATE_ACTIVE);
		EXPECT_TRUE(Decoded.GetReflection()->GetUnknownFields(Decoded).field_count() == 1);
	}
} // namespace

int main()
{
	session_state_changed_event_round_trips_through_serialization();
	unknown_trailing_field_is_tolerated_without_losing_known_fields();

	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
