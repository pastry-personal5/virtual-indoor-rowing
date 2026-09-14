#pragma once

#include "Concept2PMMac/Concept2PMRunDiagnostics.h"
#include "RowingDevice/IRowingMachine.h"

#include <memory>

// Adapter-specific ownership extension used by the local HIL tool. A remembered
// relaunch machine must be transferred to exactly one normal IRowingMachine
// owner; its events and diagnostics must not remain hidden behind discovery.
class IConcept2PMDiscovery : public IRowingMachineDiscovery
{
  public:
	virtual std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() = 0;
	// Clears only the adapter-private remembered PM5 preference. The caller owns
	// and must disconnect any machine already transferred from discovery.
	virtual void ForgetRememberedMachine() = 0;
};

// Factories for callers that must not construct PM protocol profiles. The
// normal factory is fail-closed until a reviewed profile source is supplied;
// the diagnostic factory is explicitly development-only.
std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery();
std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiagnosticDiscovery();

// User-consented, local HIL entry point. This still uses the reviewed profile
// set and normal readiness rules; it only enables bounded raw telemetry
// evidence through IConcept2PMRunDiagnostics.
std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMHardwareProbeDiscovery(
	FPM5HardwareProbeConfiguration Configuration = {});
