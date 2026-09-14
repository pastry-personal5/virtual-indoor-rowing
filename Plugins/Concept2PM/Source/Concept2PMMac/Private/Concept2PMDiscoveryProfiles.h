#pragma once

#include "Concept2PMCore/Concept2PMProtocol.h"
#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"

#include <memory>
#include <vector>

// This injection seam is adapter-private. Product callers use the slim
// no-argument factory and only generated, reviewed capability profiles.
std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery(
	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles,
	FPM5HardwareProbeConfiguration ProbeConfiguration = {});
