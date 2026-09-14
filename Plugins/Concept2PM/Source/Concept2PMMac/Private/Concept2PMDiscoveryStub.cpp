#include "Concept2PMDiscoveryProfiles.h"
#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "PM5CapabilityProfiles.generated.h"

#include <utility>

#if !defined(__APPLE__)
namespace
{
	class FUnavailableMachine final : public IRowingMachine
	{
	  public:
		FRowingCommandResult Connect() override
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}
		FRowingCommandResult Disconnect() override
		{
			return {ERowingCommandResultCode::Accepted};
		}
		ERowingConnectionState GetConnectionState() const override
		{
			return ERowingConnectionState::Failed;
		}
		FRowingMachineDiagnostics GetDiagnostics() const override
		{
			return {std::nullopt, {0, 512, 0, 0}};
		}
		bool TryPollEvent(FRowingMachineEvent &) override
		{
			return false;
		}
	};

	class FUnavailableDiscovery final : public IConcept2PMDiscovery
	{
	  public:
		explicit FUnavailableDiscovery(
			std::vector<Concept2PM::FPM5CapabilityProfile> InProfiles)
			: Profiles(std::move(InProfiles))
		{
		}

		FRowingCommandResult StartScan() override
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}
		FRowingCommandResult StopScan() override
		{
			return {ERowingCommandResultCode::Accepted};
		}
		bool TryPollDiscoveryEvent(FRowingMachineEvent &) override
		{
			return false;
		}
		std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() override
		{
			return nullptr;
		}
		void ForgetRememberedMachine() override
		{
		}
		std::unique_ptr<IRowingMachine>
		CreateMachine(const FRowingMachineId &) override
		{
			return nullptr;
		}

	  private:
		std::vector<Concept2PM::FPM5CapabilityProfile> Profiles;
	};
} // namespace

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery(
	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles,
	FPM5HardwareProbeConfiguration ProbeConfiguration)
{
	(void)ProbeConfiguration;
	return std::make_unique<FUnavailableDiscovery>(std::move(Profiles));
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery()
{
	return CreateConcept2PMDiscovery(
		Concept2PM::GetGeneratedPM5CapabilityProfiles());
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiagnosticDiscovery()
{
	return CreateConcept2PMDiscovery({});
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMHardwareProbeDiscovery(
	FPM5HardwareProbeConfiguration Configuration)
{
	Configuration.CaptureRawTelemetry = true;
	return CreateConcept2PMDiscovery(
		Concept2PM::GetGeneratedPM5CapabilityProfiles(), Configuration);
}
#endif
