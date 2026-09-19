#pragma once

#include "RowingDevice/IRowingMachine.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace RowingSim
{
	enum class EMockPermission : std::uint8_t
	{
		Granted,
		Denied
	};

	struct FMockReconnectIdentity
	{
		FRowingMachineId Id;
		FRowingMachineInfo Info;
		std::uint64_t GapDurationMs = 0;
		bool IdentityReadable = true;
		bool IdentityWellFormed = true;
		bool RequiredServicesPresent = true;
		bool RequiredCharacteristicsPresent = true;
		bool RequiredPropertiesValid = true;
		bool InitialStatusValid = true;
		bool AdditionalStatus1Valid = true;
		std::optional<FRowingMetricSample> FirstSample;
	};

	struct FMockMachineScenario
	{
		FRowingMachineDescriptor Descriptor;
		FRowingMachineInfo Info;
		bool ConnectionSucceeds = true;
		bool IdentityReadable = true;
		bool IdentityWellFormed = true;
		bool RequiredServicesPresent = true;
		bool RequiredCharacteristicsPresent = true;
		bool RequiredPropertiesValid = true;
		bool InitialStatusValid = true;
		bool AdditionalStatus1Valid = true;
		std::optional<FMockReconnectIdentity> ReconnectIdentity;
	};

	struct FMockDiscoveryScenario
	{
		EMockPermission Permission = EMockPermission::Granted;
		std::vector<FMockMachineScenario> Machines;
		std::uint64_t ScanTimeoutMs = 15000;
		std::size_t EventQueueCapacity = 512;
	};

	class FMockRowingMachine final : public IRowingMachine
	{
	  public:
		explicit FMockRowingMachine(
			FMockMachineScenario Scenario,
			std::size_t EventQueueCapacity = 512,
			std::uint64_t StaleAfterMs = 500,
			std::uint64_t BlockingWarningAfterMs = 1500);
		~FMockRowingMachine() override;

		FMockRowingMachine(const FMockRowingMachine &) = delete;
		FMockRowingMachine &operator=(const FMockRowingMachine &) = delete;

		FRowingCommandResult Connect() override;
		FRowingCommandResult Disconnect() override;
		ERowingConnectionState GetConnectionState() const override;
		FRowingMachineDiagnostics GetDiagnostics() const override;
		bool TryPollEvent(FRowingMachineEvent &OutEvent) override;

		// Deterministic test controls. Time is a caller-supplied monotonic
		// nanosecond value.
		bool AdvanceTo(std::uint64_t MonotonicTimestampNs);
		bool PublishTelemetry(FRowingMetricSample Sample);
		// Injects a malformed source packet at the public boundary without
		// retaining or exposing its payload bytes.
		bool RejectMalformedSample(
			std::uint64_t ExpectedBytes,
			std::uint64_t ActualBytes);
		bool SimulateLinkLoss();
		void Shutdown() noexcept;
		std::size_t GetQueuedEventCount() const noexcept;
		std::size_t GetEventQueueCapacity() const noexcept;
		bool HasOverflowed() const noexcept;

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	class FMockRowingMachineDiscovery final : public IRowingMachineDiscovery
	{
	  public:
		explicit FMockRowingMachineDiscovery(FMockDiscoveryScenario Scenario);
		~FMockRowingMachineDiscovery() override;

		FMockRowingMachineDiscovery(const FMockRowingMachineDiscovery &) =
			delete;
		FMockRowingMachineDiscovery &
		operator=(const FMockRowingMachineDiscovery &) = delete;

		FRowingCommandResult StartScan() override;
		FRowingCommandResult StopScan() override;
		bool TryPollDiscoveryEvent(FRowingMachineEvent &OutEvent) override;
		std::unique_ptr<IRowingMachine>
		CreateMachine(const FRowingMachineId &MachineId) override;

		// Advances an explicit scan to a deterministic monotonic time. An empty
		// scan emits ScanTimeout at the configured deadline.
		bool AdvanceScanTo(std::uint64_t MonotonicTimestampNs);

		void SetPermission(EMockPermission Permission);
		void Shutdown() noexcept;
		std::size_t GetQueuedEventCount() const noexcept;
		bool HasOverflowed() const noexcept;

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
} // namespace RowingSim
