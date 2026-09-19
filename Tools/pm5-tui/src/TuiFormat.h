#pragma once

#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "RotatingFileLogger.h"
#include "RunMetricsWriter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "DisplaySnapshot.h"

namespace PM5Tui
{
	const char *ToString(ERowingMachineKind Value);

	const char *ToString(ERowingMachineSupportState Value);

	const char *ToString(ERowingConnectionReason Value);

	const char *ToString(ERowingConnectionState Value);

	const char *ToString(ERowingFaultCode Value);

	const char *ToString(ERowingWorkoutState Value);

	const char *ToString(ERowingState Value);

	const char *ToString(ERowingStrokeState Value);

	const char *ToString(Concept2PM::EDiagnosticWorkoutKind Value);

	const char *ToString(Concept2PM::EDiagnosticWorkoutProgramRejectReason Value);

	template <typename T>
	std::string OptionalValue(const std::optional<T> &Value)
	{
		return Value ? std::to_string(*Value) : "—";
	}

	// The three canonical example specs used by this bounded diagnostic
	// spike's acceptance gate: one distance workout, one time workout, and
	// one time-interval workout (docs/phase-0/07-milestone-4-spikes.md,
	// "Spike A"), matching the published CSAFE worked examples this file's
	// unit tests already reproduce byte-for-byte.
	Concept2PM::FDiagnosticWorkoutSpec MakeExampleDistanceWorkoutSpec();

	Concept2PM::FDiagnosticWorkoutSpec MakeExampleTimeWorkoutSpec();

	Concept2PM::FDiagnosticWorkoutSpec MakeExampleTimeIntervalWorkoutSpec();

	std::string FormatWorkoutProgramEvent(const FWorkoutProgramEvent &Event);

	std::string FormatQualityFlags(FRowingQualityFlags Flags);

	std::string FormatMetric(std::string_view Prefix, const FRowingMetricSample &Sample);

	void PrintMetric(std::string_view Prefix, const FRowingMetricSample &Sample);

} // namespace PM5Tui
