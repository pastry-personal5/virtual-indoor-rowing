#include "Concept2PMCore/Concept2PMProtocol.h"
#include "PM5CapabilityProfiles.generated.h"

#include <cassert>

using namespace Concept2PM;

namespace
{
	FPM5Identity ReviewedIdentity()
	{
		return {"PM5", "634", "8200-000372-178.069", ERowingMachineKind::IndoorRower};
	}

	// Phase 1 Milestone 2: `CreateConcept2PMDiscovery()` (the product,
	// no-arg factory) resolves to `Concept2PM::GetGeneratedPM5CapabilityProfiles()`,
	// generated from `Config/PM5Capabilities.json`. This is the actual
	// reviewed, non-diagnostic profile set the product path connects
	// against, so it — not a synthetic profile — is what this milestone's
	// acceptance/rejection tests must exercise.
	void concept2pm_reviewed_profile_accepted_no_diagnostic_fault()
	{
		const std::vector<FPM5CapabilityProfile> Profiles =
			GetGeneratedPM5CapabilityProfiles();
		const FCapabilityEvaluation Evaluation =
			EvaluateCapability(ReviewedIdentity(), Profiles);

		assert(Evaluation.Profile != nullptr);
		assert(!Evaluation.Profile->DiagnosticOnly);
		assert(Evaluation.SupportState == ERowingMachineSupportState::Allowed);

		// Mirrors FMacMachine::FinishIdentity's accept branch
		// (Concept2PMDiscoveryMac.mm): a non-diagnostic profile only reaches
		// Ready without an UnsupportedIdentity warning fault when its
		// support state is Allowed and it requires notify on both
		// GeneralStatus and AdditionalStatus1.
		bool RequiresGeneralStatusNotify = false;
		bool RequiresAdditionalStatus1Notify = false;
		for (const FPM5CharacteristicProfile &Characteristic :
			 Evaluation.Profile->Characteristics)
		{
			if (!Characteristic.Required)
				continue;
			if (Characteristic.ShortId == GeneralStatus && Characteristic.RequiresNotify)
				RequiresGeneralStatusNotify = true;
			if (Characteristic.ShortId == AdditionalStatus1 && Characteristic.RequiresNotify)
				RequiresAdditionalStatus1Notify = true;
		}
		assert(RequiresGeneralStatusNotify);
		assert(RequiresAdditionalStatus1Notify);
	}

	void concept2pm_unreviewed_identity_rejected_against_reviewed_profiles()
	{
		const std::vector<FPM5CapabilityProfile> Profiles =
			GetGeneratedPM5CapabilityProfiles();

		FPM5Identity WrongFirmware = ReviewedIdentity();
		WrongFirmware.FirmwareRevision = "0000-000000-000.000";
		assert(EvaluateCapability(WrongFirmware, Profiles).Profile == nullptr);
		assert(EvaluateCapability(WrongFirmware, Profiles).SupportState ==
			   ERowingMachineSupportState::Blocked);

		FPM5Identity WrongMachineKind = ReviewedIdentity();
		WrongMachineKind.MachineKind = ERowingMachineKind::Unknown;
		assert(EvaluateCapability(WrongMachineKind, Profiles).Profile == nullptr);
	}
} // namespace

int main()
{
	concept2pm_reviewed_profile_accepted_no_diagnostic_fault();
	concept2pm_unreviewed_identity_rejected_against_reviewed_profiles();
	return 0;
}
