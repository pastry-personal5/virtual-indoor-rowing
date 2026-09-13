#pragma once

#include "pm5_sim/MockRowingMachine.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pm5_sim
{
	struct FReplayTelemetryFrame
	{
		std::uint64_t AtNs = 0;
		FRowingMetricSample Sample;
	};

	class FReplayRowingMachine final : public IRowingMachine
	{
	  public:
		FReplayRowingMachine(FMockMachineScenario Scenario,
							 std::vector<FReplayTelemetryFrame> Frames,
							 std::size_t EventQueueCapacity = 512);

		FRowingCommandResult Connect() override;
		FRowingCommandResult Disconnect() override;
		ERowingConnectionState GetConnectionState() const override;
		FRowingMachineDiagnostics GetDiagnostics() const override;
		bool TryPollEvent(FRowingMachineEvent &OutEvent) override;

		// Advances virtual time and publishes every due frame in stable input
		// order.
		bool AdvanceTo(std::uint64_t MonotonicTimestampNs);
		std::size_t GetNextFrameIndex() const noexcept;
		std::size_t GetFrameCount() const noexcept;
		FMockRowingMachine &GetMockMachine() noexcept;

	  private:
		FMockRowingMachine Machine;
		std::vector<FReplayTelemetryFrame> Frames;
		std::size_t NextFrameIndex = 0;
	};
} // namespace pm5_sim
