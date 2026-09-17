#pragma once

#include "Concept2PMMac/Concept2PMRunDiagnostics.h"
#include "RowingCore/RowingTelemetry.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace PM5Tui
{
	enum class EPM5TuiRunAction
	{
		ScanStarted,
		ScanStopped,
		CandidateSelected,
		ConnectRequested,
		DisconnectRequested,
		ProgramDistanceWorkoutRequested,
		ProgramTimeWorkoutRequested,
		ProgramTimeIntervalWorkoutRequested,
		AbortWorkoutRequested
	};

	struct FQueueMetricsSummary
	{
		std::uint32_t CurrentDepth = 0;
		std::uint32_t Capacity = 0;
		std::uint32_t HighWaterMark = 0;
		std::uint64_t OverflowCount = 0;
	};

	struct FRunMetricsSummary
	{
		std::uint64_t SampleCount = 0;
		std::uint64_t DiagnosticSampleCount = 0;
		std::uint64_t StrokeMetricsRecordCount = 0;
		std::uint64_t CorrectionCount = 0;
		std::uint64_t StaleEventCount = 0;
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
		ERowingConnectionState FinalConnectionState = ERowingConnectionState::Idle;
		std::optional<FQueueMetricsSummary> AcquisitionQueue;
		std::optional<FQueueMetricsSummary> EventQueue;
		std::optional<FPM5RunDiagnostics> PM5Diagnostics;
	};

	class FRunMetricsWriter
	{
	  public:
		explicit FRunMetricsWriter(std::filesystem::path InDirectory,
								   std::string_view SourceRevision,
								   FPM5HardwareProbeConfiguration ProbeConfiguration = {});
		~FRunMetricsWriter();

		FRunMetricsWriter(const FRunMetricsWriter &) = delete;
		FRunMetricsWriter &operator=(const FRunMetricsWriter &) = delete;

		void RecordDiscoveredCandidate(const FRowingMachineDescriptor &Candidate,
									   std::uint64_t MonotonicTimestampNs);
		void RecordAction(EPM5TuiRunAction Action,
						  std::optional<std::uint64_t> SelectionIndex = std::nullopt);
		void RecordConnectionStateChanged(
			const FRowingConnectionStateChanged &Changed,
			std::uint64_t MonotonicTimestampNs);
		void RecordMachineInfo(const FRowingMachineInfo &Info,
							   std::uint64_t MonotonicTimestampNs);
		void RecordTelemetryStale(const FRowingTelemetryStale &Stale,
								  std::uint64_t MonotonicTimestampNs);
		void RecordConnectionRestored(const FRowingConnectionRestored &Restored,
									  std::uint64_t MonotonicTimestampNs);
		void RecordFault(const FRowingFault &Fault,
						 std::uint64_t MonotonicTimestampNs);
		void RecordMetricSample(const FRowingMetricSample &Sample,
								bool DiagnosticOnly = false);
		void RecordMetricCorrection(std::uint64_t TargetSampleSequence,
									const FRowingMetricSample &Sample);
		void RecordStrokeMetrics(const FRowingStrokeMetrics &Stroke,
								 std::uint64_t MonotonicTimestampNs);
		void RecordProbePacket(const FPM5ProbePacketEvidence &Evidence);
		void RecordWorkoutProgramEvent(const FWorkoutProgramEvent &Event);
		void RecordRunStopped(const FRunMetricsSummary &Summary);
		bool IsAvailable() const;
		std::filesystem::path CurrentMetricsPath() const;

	  private:
		void WriteRecord(std::string_view Record);

		std::filesystem::path MetricsPath;
		FPM5HardwareProbeConfiguration ProbeConfiguration;
		std::uint64_t RunStartedMonotonicNs = 0;
		mutable std::mutex Mutex;
		std::ofstream Output;
	};
} // namespace PM5Tui
