#pragma once

#include "RunMetricsWriter.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace PM5Tui::Json
{
	const char *ToString(ERowingWorkoutState Value);

	const char *ToString(ERowingState Value);

	const char *ToString(ERowingStrokeState Value);

	const char *ToString(ERowingMachineKind Value);

	const char *ToString(ERowingMachineSupportState Value);

	const char *ToString(Concept2PM::EPacketError Value);

	const char *ToString(EPM5ProbeCaptureStopReason Value);

	const char *ToString(ERowingConnectionState Value);

	const char *ToString(ERowingConnectionReason Value);

	const char *ToString(ERowingFaultCode Value);

	const char *ToString(ERowingFaultSeverity Value);

	const char *ToString(ERowingOperation Value);

	const char *ToString(EPM5CallbackStage Value);

	const char *ToString(EPM5ErrorDomain Value);

	const char *ToString(EPM5TuiRunAction Value);

	const char *ToString(Concept2PM::EDiagnosticWorkoutKind Value);

	const char *ToString(Concept2PM::EDiagnosticWorkoutProgramRejectReason Value);

	const char *ToString(ERowingStrokeMetricsSource Value);

} // namespace PM5Tui::Json
