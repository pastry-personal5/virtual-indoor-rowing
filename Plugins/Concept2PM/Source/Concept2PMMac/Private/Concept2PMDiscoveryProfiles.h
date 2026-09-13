#pragma once

#include "Concept2PMCore/Concept2PMProtocol.h"
#include "RowingDevice/IRowingMachine.h"

#include <memory>
#include <vector>

// This injection seam is adapter-private. Product callers use the slim
// no-argument factory and only generated, reviewed capability profiles.
std::unique_ptr<IRowingMachineDiscovery> CreateConcept2PMDiscovery(
	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles);
