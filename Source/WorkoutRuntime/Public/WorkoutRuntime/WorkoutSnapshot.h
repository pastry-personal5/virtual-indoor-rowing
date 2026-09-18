#pragma once

#include "RowingCore/RowingSession.h"
#include "RowingCore/RowingTelemetry.h"
#include "RowingDevice/RowingMachineTypes.h"

#include <cstdint>
#include <optional>

// What the UI reads (FR-003). An immutable value: FWorkoutSession replaces it
// wholesale on every change, and Revision increases so a consumer can cheaply
// tell whether anything changed. Never carries a value the device did not
// report; during a gap LatestSample stays frozen at the last device fact and
// bInputFrozen is set.
struct FWorkoutSnapshot
{
	std::uint64_t Revision = 0;
	FRowingSessionId SessionId;
	ERowingSessionState State = ERowingSessionState::Created;
	std::optional<ERowingSessionDisposition> Disposition;
	std::optional<ERowingSessionStateReason> EndReason;

	ERowingConnectionState ConnectionState = ERowingConnectionState::Idle;
	// True while the session is ConnectionLost: LatestSample is stale by design.
	bool bInputFrozen = false;

	std::optional<FRowingMetricSample> LatestSample;
	std::optional<FRowingStrokeMetrics> LatestStrokeMetrics;
	std::optional<ERowingFaultCode> LastFaultCode;

	std::uint64_t AcceptedSampleCount = 0;
	std::uint64_t RejectedSampleCount = 0;
	std::uint64_t IgnoredCorrectionCount = 0;
	std::uint64_t DroppedSampleCount = 0;
	std::uint32_t GapCount = 0;
	std::uint64_t TotalGapMs = 0;

	// False once any journal write has thrown. The local row keeps running.
	bool bJournalHealthy = true;
	std::uint64_t JournalErrorCount = 0;
};
