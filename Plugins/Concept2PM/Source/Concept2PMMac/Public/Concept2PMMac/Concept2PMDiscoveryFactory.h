#pragma once

#include "RowingDevice/IRowingMachine.h"

#include <memory>

// Hardware-neutral factories for callers that must not construct PM protocol
// profiles. The normal factory is fail-closed until a reviewed profile source
// is supplied; the diagnostic factory is explicitly development-only.
std::unique_ptr<IRowingMachineDiscovery> CreateConcept2PMDiscovery();
std::unique_ptr<IRowingMachineDiscovery> CreateConcept2PMDiagnosticDiscovery();
