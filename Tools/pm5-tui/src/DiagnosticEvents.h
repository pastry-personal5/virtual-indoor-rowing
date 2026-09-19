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
	FPM5HardwareProbeConfiguration MakeHardwareProbeConfiguration(
		bool HardwareProbeEnabled);

	void LogConnectionStateChanged(const FRowingConnectionStateChanged &Changed,
								   PM5Tui::FRotatingFileLogger &Logger);

	void LogTelemetryStale(const FRowingTelemetryStale &Stale,
						   PM5Tui::FRotatingFileLogger &Logger);

	// Full detail (requested spec, and readback or reject reason) is logged
	// here — not just verified/was_abort — so a real-PM5 acceptance run
	// (Phase 0 Milestone 4 Spike A step 4) has a durable record in
	// Logs/pm5-tui/pm5-tui.log to check the read-back type/duration against
	// the configured request, even in interactive mode where stdout is not
	// captured.
	void LogWorkoutProgramEvent(const FWorkoutProgramEvent &Event,
								PM5Tui::FRotatingFileLogger &Logger);

	void LogTuiStopped(const PM5Tui::FRunMetricsSummary &Summary,
					   PM5Tui::FRotatingFileLogger &Logger);

	void LogMetricsAvailability(const PM5Tui::FRunMetricsWriter &Metrics,
								PM5Tui::FRotatingFileLogger &Logger);

	void PrintEvent(const FRowingMachineEvent &Event,
					PM5Tui::FRotatingFileLogger &Logger,
					PM5Tui::FRunMetricsWriter &Metrics,
					FDisplaySnapshot &Snapshot);

	void DrainEvents(IRowingMachineDiscovery &Discovery,
					 IRowingMachine *Machine,
					 std::vector<FRowingMachineDescriptor> &Candidates,
					 PM5Tui::FRotatingFileLogger &Logger,
					 PM5Tui::FRunMetricsWriter &Metrics,
					 FDisplaySnapshot &Snapshot);

	void PrintStatus(const IRowingMachine *Machine,
					 const FDisplaySnapshot &Snapshot,
					 std::size_t CandidateCount,
					 bool HardwareProbeEnabled = false);

} // namespace PM5Tui
