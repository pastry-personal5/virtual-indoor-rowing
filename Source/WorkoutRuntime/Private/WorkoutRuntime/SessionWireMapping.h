#pragma once

#include "WorkoutRuntime/WorkoutSummary.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace WorkoutRuntime::Private
{
	inline constexpr std::uint32_t SessionContractVersion = 1;

	class FSessionWireMappingError final : public std::runtime_error
	{
	  public:
		explicit FSessionWireMappingError(const std::string &Message)
			: std::runtime_error(Message)
		{
		}
	};

	struct FLinkGap
	{
		std::uint64_t GapDurationMs = 0;
		bool bDeviceReported = false;
	};

	// Domain <-> rowing.v1 wire mapping (session_summary.proto, and
	// LinkGapRecorded in session.proto). Serialized bytes are the interface so
	// Protobuf types never appear in a header. Parse* throws
	// FSessionWireMappingError on malformed bytes or an unsupported
	// contract_version.
	std::string SerializeSessionSummary(const FWorkoutSummary &Summary);
	FWorkoutSummary ParseSessionSummary(const std::string &Bytes);

	std::string SerializeLinkGap(const FLinkGap &Gap);
	FLinkGap ParseLinkGap(const std::string &Bytes);
} // namespace WorkoutRuntime::Private
