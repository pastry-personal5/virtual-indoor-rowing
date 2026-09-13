#pragma once

#include <cstdint>
#include <vector>

namespace Concept2PM
{
	// Private seam only: platform adapters copy GATT data into this value
	// before the pure C++ codec sees it. It deliberately has no CoreBluetooth
	// types.
	struct FTransportNotification
	{
		std::uint16_t Characteristic = 0;
		std::uint64_t ReceivedMonotonicNs = 0;
		std::vector<std::uint8_t> Bytes;
	};

	class IRowingDeviceTransport
	{
	  public:
		virtual ~IRowingDeviceTransport() = default;
		virtual bool
		TryPollNotification(FTransportNotification &OutNotification) = 0;
	};
} // namespace Concept2PM
