#include "pm5_sim/ReplayRowingMachine.h"

#include <utility>

namespace pm5_sim
{
	FReplayRowingMachine::FReplayRowingMachine(
		FMockMachineScenario Scenario,
		std::vector<FReplayTelemetryFrame> Frames,
		std::size_t EventQueueCapacity)
		: Machine(std::move(Scenario), EventQueueCapacity),
		  Frames(std::move(Frames))
	{
	}

	FRowingCommandResult FReplayRowingMachine::Connect()
	{
		return Machine.Connect();
	}

	FRowingCommandResult FReplayRowingMachine::Disconnect()
	{
		return Machine.Disconnect();
	}

	ERowingConnectionState FReplayRowingMachine::GetConnectionState() const
	{
		return Machine.GetConnectionState();
	}

	FRowingMachineDiagnostics FReplayRowingMachine::GetDiagnostics() const
	{
		return Machine.GetDiagnostics();
	}

	bool FReplayRowingMachine::TryPollEvent(FRowingMachineEvent &OutEvent)
	{
		return Machine.TryPollEvent(OutEvent);
	}

	bool FReplayRowingMachine::AdvanceTo(std::uint64_t MonotonicTimestampNs)
	{
		while (NextFrameIndex < Frames.size() &&
			   Frames[NextFrameIndex].AtNs <= MonotonicTimestampNs)
		{
			const FReplayTelemetryFrame &Frame = Frames[NextFrameIndex];
			if (!Machine.AdvanceTo(Frame.AtNs) ||
				!Machine.PublishTelemetry(Frame.Sample))
			{
				return false;
			}
			++NextFrameIndex;
		}
		return Machine.AdvanceTo(MonotonicTimestampNs);
	}

	std::size_t FReplayRowingMachine::GetNextFrameIndex() const noexcept
	{
		return NextFrameIndex;
	}

	std::size_t FReplayRowingMachine::GetFrameCount() const noexcept
	{
		return Frames.size();
	}

	FMockRowingMachine &FReplayRowingMachine::GetMockMachine() noexcept
	{
		return Machine;
	}
} // namespace pm5_sim
