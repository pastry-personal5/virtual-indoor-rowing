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

namespace PM5Tui
{
	using FSteadyClock = std::chrono::steady_clock;

	struct FDisplaySnapshot
	{
		std::optional<FSteadyClock::time_point> LastTelemetryReceived;
		std::optional<FSteadyClock::time_point> LastStateChanged;
		std::optional<FRowingMetricSample> LatestSample;
		std::optional<ERowingMachineSupportState> SupportState;
		ERowingConnectionState ConnectionState = ERowingConnectionState::Idle;
		ERowingConnectionReason LastTransitionReason = ERowingConnectionReason::None;
		std::string LatestIssue = "None";
		std::uint64_t SampleCount = 0;
		std::uint64_t DiagnosticSampleCount = 0;
		std::uint64_t StrokeMetricsRecordCount = 0;
		std::uint64_t CorrectionCount = 0;
		std::uint64_t StaleCount = 0;
		std::uint64_t FaultCount = 0;
		std::uint64_t DuplicateSampleCount = 0;
		std::uint64_t SourceGapSampleCount = 0;
		std::uint64_t TimeRegressionSampleCount = 0;
		std::uint64_t DistanceRegressionSampleCount = 0;
		std::uint64_t MissingFieldSampleCount = 0;
		std::uint64_t LastSampleSequence = 0;
		std::uint64_t ReconnectCount = 0;
		std::uint64_t TotalReconnectGapMs = 0;
		std::uint64_t LongestReconnectGapMs = 0;
	};

	enum class EStatusTone : std::uint8_t
	{
		Neutral,
		Progress,
		Healthy,
		Warning,
		Critical,
		Diagnostic
	};

	struct FStatusLineView
	{
		EStatusTone Tone = EStatusTone::Neutral;
		std::string Headline;
		std::string Telemetry;
		std::string Detail;
	};

	const char *ToString(EStatusTone Value);

	std::string AgeMilliseconds(
		const std::optional<FSteadyClock::time_point> &Timestamp,
		FSteadyClock::time_point Now = FSteadyClock::now());

	FStatusLineView MakeStatusLineView(const FDisplaySnapshot &Snapshot,
									   std::size_t CandidateCount);

	void RecordSample(const FRowingMetricSample &Sample, FDisplaySnapshot &Snapshot);

	PM5Tui::FRunMetricsSummary MakeRunMetricsSummary(
		const FDisplaySnapshot &Snapshot,
		const IRowingMachine *Machine);

} // namespace PM5Tui
