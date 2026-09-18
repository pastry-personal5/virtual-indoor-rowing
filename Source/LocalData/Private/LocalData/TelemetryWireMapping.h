#pragma once

#include "RowingDevice/RowingMachineTypes.h"

#include <stdexcept>
#include <string>

namespace LocalData::Private
{
	inline constexpr std::uint32_t TelemetryContractVersion = 1;

	class FWireMappingError final : public std::runtime_error
	{
	  public:
		explicit FWireMappingError(const std::string &Message)
			: std::runtime_error(Message)
		{
		}
	};

	// Domain <-> rowing.v1 wire mapping (Contracts/proto/rowing/v1/telemetry.proto).
	// Serialized bytes are the interface so Protobuf types never appear in a
	// header. Parse* throws FWireMappingError on malformed bytes, an unsupported
	// contract_version, or an UNSPECIFIED/unknown enumerator.
	std::string SerializeMetricSample(const FRowingMetricSample &Sample);
	FRowingMetricSample ParseMetricSample(const std::string &Bytes);

	std::string SerializeMachineInfo(const FRowingMachineInfo &Info);
	FRowingMachineInfo ParseMachineInfo(const std::string &Bytes);
} // namespace LocalData::Private
