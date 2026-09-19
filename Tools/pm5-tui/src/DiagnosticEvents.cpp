#include "DiagnosticEvents.h"

#include "TuiFormat.h"

namespace PM5Tui
{
	FPM5HardwareProbeConfiguration MakeHardwareProbeConfiguration(
		bool HardwareProbeEnabled)
	{
		FPM5HardwareProbeConfiguration Configuration;
		Configuration.CaptureRawTelemetry = HardwareProbeEnabled;
		return Configuration;
	}

	void LogConnectionStateChanged(const FRowingConnectionStateChanged &Changed,
								   PM5Tui::FRotatingFileLogger &Logger)
	{
		Logger.Log(PM5Tui::ELogLevel::Info,
				   "event=ConnectionStateChanged previous=" +
					   std::string(ToString(Changed.PreviousState)) + " state=" +
					   ToString(Changed.NewState) + " reason=" +
					   ToString(Changed.Reason));
	}

	void LogTelemetryStale(const FRowingTelemetryStale &Stale,
						   PM5Tui::FRotatingFileLogger &Logger)
	{
		Logger.Log(PM5Tui::ELogLevel::Warning,
				   "event=TelemetryStale last_sequence=" +
					   std::to_string(Stale.LastSequence) + " age_ms=" +
					   std::to_string(Stale.AgeMs));
	}

	void LogWorkoutProgramEvent(const FWorkoutProgramEvent &Event,
								PM5Tui::FRotatingFileLogger &Logger)
	{
		std::string Message =
			"event=WorkoutProgramEvent verified=" +
			std::string(Event.Verified ? "true" : "false") +
			" was_abort=" + std::string(Event.WasAbort ? "true" : "false") +
			" requested_kind=" + ToString(Event.RequestedSpec.Kind) +
			" requested_distance_mm=" + OptionalValue(Event.RequestedSpec.DistanceMm) +
			" requested_duration_ms=" + OptionalValue(Event.RequestedSpec.DurationMs) +
			" requested_interval_rest_ms=" +
			OptionalValue(Event.RequestedSpec.IntervalRestMs);
		if (Event.Verified)
		{
			Message += " readback_type=" + std::string(ToString(Event.Readback.Type)) +
					   " readback_duration_ms=" + OptionalValue(Event.Readback.DurationMs);
		}
		else
		{
			Message += " reject_reason=" + std::string(ToString(Event.RejectReason));
		}
		Logger.Log(Event.Verified ? PM5Tui::ELogLevel::Info : PM5Tui::ELogLevel::Warning,
				   Message);
	}

	void LogTuiStopped(const PM5Tui::FRunMetricsSummary &Summary,
					   PM5Tui::FRotatingFileLogger &Logger)
	{
		std::ostringstream Message;
		Message << "event=TuiStopped samples=" << Summary.SampleCount
				<< " corrections=" << Summary.CorrectionCount
				<< " stale_events=" << Summary.StaleEventCount
				<< " faults=" << Summary.FaultCount
				<< " duplicate_samples=" << Summary.DuplicateSampleCount
				<< " source_gap_samples=" << Summary.SourceGapSampleCount
				<< " time_regression_samples=" << Summary.TimeRegressionSampleCount
				<< " distance_regression_samples=" << Summary.DistanceRegressionSampleCount
				<< " missing_field_samples=" << Summary.MissingFieldSampleCount
				<< " last_sample_sequence=" << Summary.LastSampleSequence
				<< " reconnect_count=" << Summary.ReconnectCount
				<< " total_reconnect_gap_ms=" << Summary.TotalReconnectGapMs
				<< " longest_reconnect_gap_ms=" << Summary.LongestReconnectGapMs;
		Message << " stroke_metrics_records=" << Summary.StrokeMetricsRecordCount;
		if (Summary.EventQueue)
			Message << " event_queue_high_water=" << Summary.EventQueue->HighWaterMark
					<< " event_queue_overflow=" << Summary.EventQueue->OverflowCount;
		if (Summary.AcquisitionQueue)
			Message << " acquisition_queue_high_water="
					<< Summary.AcquisitionQueue->HighWaterMark
					<< " acquisition_queue_overflow="
					<< Summary.AcquisitionQueue->OverflowCount;
		if (Summary.PM5Diagnostics &&
			Summary.PM5Diagnostics->ProbeCapture.Enabled)
		{
			const auto &Probe = Summary.PM5Diagnostics->ProbeCapture;
			Message << " probe_packets_observed=" << Probe.ObservedPacketCount
					<< " probe_packets_captured=" << Probe.CapturedPacketCount
					<< " probe_queue_overflow="
					<< Probe.EvidenceQueueOverflowCount
					<< " probe_limit_dropped=" << Probe.LimitDroppedPacketCount;
		}
		Logger.Log(PM5Tui::ELogLevel::Info, Message.str());
	}

	void LogMetricsAvailability(const PM5Tui::FRunMetricsWriter &Metrics,
								PM5Tui::FRotatingFileLogger &Logger)
	{
		if (Metrics.IsAvailable())
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=RunMetricsOpened path=" +
						   Metrics.CurrentMetricsPath().string());
		else
		{
			Logger.Log(PM5Tui::ELogLevel::Error, "event=RunMetricsUnavailable");
			std::cerr << "ERROR: per-run telemetry metrics could not be opened.\n";
		}
	}

	void PrintEvent(const FRowingMachineEvent &Event,
					PM5Tui::FRotatingFileLogger &Logger,
					PM5Tui::FRunMetricsWriter &Metrics,
					FDisplaySnapshot &Snapshot)
	{
		if (const auto *Info = std::get_if<FRowingMachineInfo>(&Event.Payload))
		{
			Snapshot.SupportState = Info->SupportState;
			Metrics.RecordMachineInfo(*Info, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Info, "event=MachineInfoObserved model=" + Info->Model + " hardware=" + Info->HardwareVersion + " firmware=" + Info->FirmwareVersion + " machine_kind=" + ToString(Info->MachineKind) + " support_state=" + ToString(Info->SupportState));
			std::cout << "identity model=" << Info->Model << " hardware=" << Info->HardwareVersion
					  << " firmware=" << Info->FirmwareVersion << " machine_kind=" << ToString(Info->MachineKind)
					  << " support_state=" << ToString(Info->SupportState) << '\n';
		}
		else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
		{
			Snapshot.LastTelemetryReceived = FSteadyClock::now();
			RecordSample(*Sample, Snapshot);
			Metrics.RecordMetricSample(*Sample, false);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
			PrintMetric("METRIC SAMPLE", *Sample);
		}
		else if (const auto *Sample =
					 std::get_if<FRowingDiagnosticSample>(&Event.Payload))
		{
			Snapshot.LastTelemetryReceived = FSteadyClock::now();
			++Snapshot.DiagnosticSampleCount;
			RecordSample(Sample->Sample, Snapshot);
			Metrics.RecordMetricSample(Sample->Sample, true);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=DiagnosticMetricSampled " +
						   FormatMetric("telemetry", Sample->Sample));
			PrintMetric("DIAGNOSTIC METRIC SAMPLE", Sample->Sample);
		}
		else if (const auto *Stroke = std::get_if<FRowingStrokeMetrics>(&Event.Payload))
		{
			++Snapshot.StrokeMetricsRecordCount;
			Metrics.RecordStrokeMetrics(*Stroke, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=StrokeMetricsObserved elapsed_ms=" +
						   std::to_string(Stroke->SourceElapsedMs) +
						   " stroke_count=" + OptionalValue(Stroke->StrokeCount) +
						   " stroke_power_w=" + OptionalValue(Stroke->StrokePowerW) +
						   " work_per_stroke_deci_joules=" +
						   OptionalValue(Stroke->WorkPerStrokeDeciJoules));
		}
		else if (const auto *Changed = std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
		{
			Metrics.RecordConnectionStateChanged(*Changed, Event.MonotonicTimestampNs);
			Snapshot.LastStateChanged = FSteadyClock::now();
			Snapshot.ConnectionState = Changed->NewState;
			Snapshot.LastTransitionReason = Changed->Reason;
			LogConnectionStateChanged(*Changed, Logger);
			std::cout << "connection previous=" << ToString(Changed->PreviousState)
					  << " state=" << ToString(Changed->NewState)
					  << " reason=" << ToString(Changed->Reason) << '\n';
		}
		else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Event.Payload))
		{
			++Snapshot.StaleCount;
			Snapshot.LatestIssue =
				"Stale telemetry (" + std::to_string(Stale->AgeMs) + " ms)";
			Metrics.RecordTelemetryStale(*Stale, Event.MonotonicTimestampNs);
			LogTelemetryStale(*Stale, Logger);
			std::cout << "stale last_sequence=" << Stale->LastSequence
					  << " age_ms=" << Stale->AgeMs << '\n';
		}
		else if (const auto *Restored = std::get_if<FRowingConnectionRestored>(&Event.Payload))
		{
			Metrics.RecordConnectionRestored(*Restored, Event.MonotonicTimestampNs);
			++Snapshot.ReconnectCount;
			Snapshot.TotalReconnectGapMs += Restored->GapDurationMs;
			Snapshot.LongestReconnectGapMs =
				std::max(Snapshot.LongestReconnectGapMs, Restored->GapDurationMs);
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=ConnectionRestored gap_ms=" +
						   std::to_string(Restored->GapDurationMs));
			std::cout << "reconnected gap_ms=" << Restored->GapDurationMs << '\n';
		}
		else if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload))
		{
			++Snapshot.FaultCount;
			Snapshot.LatestIssue = std::string(ToString(Fault->Code)) + ": " +
								   Fault->DiagnosticText;
			Metrics.RecordFault(*Fault, Event.MonotonicTimestampNs);
			Logger.Log(PM5Tui::ELogLevel::Warning,
					   "event=FaultObserved code=" + std::string(ToString(Fault->Code)) +
						   " detail=" + Fault->DiagnosticText);
			std::cout << "fault code=" << ToString(Fault->Code)
					  << " state=" << ToString(Fault->ConnectionState)
					  << " detail=" << Fault->DiagnosticText << '\n';
		}
		else if (const auto *Correction = std::get_if<FRowingMetricCorrection>(&Event.Payload))
		{
			++Snapshot.CorrectionCount;
			const auto &Sample = Correction->CorrectedSample;
			Metrics.RecordMetricCorrection(Correction->TargetSampleSequence, Sample);
			Logger.Log(PM5Tui::ELogLevel::Debug,
					   "event=MetricCorrected target_sample_sequence=" +
						   std::to_string(Correction->TargetSampleSequence) + ' ' +
						   FormatMetric("telemetry", Sample));
			std::cout << "CORRECTED METRIC SAMPLE target_sample_sequence=" << Correction->TargetSampleSequence
					  << " corrected_sample_sequence=" << Sample.Sequence << " elapsed_ms=" << Sample.SourceElapsedMs
					  << " distance_mm=" << Sample.DistanceMm << " heart_rate_bpm=" << OptionalValue(Sample.HeartRateBpm)
					  << " quality_flags=" << (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::LateCorrection) ? "LateCorrection" : "None")
					  << " workout_state=" << ToString(Sample.WorkoutState) << " rowing_state=" << ToString(Sample.RowingState)
					  << " stroke_state=" << ToString(Sample.StrokeState) << " last_telemetry_age_ms="
					  << (Snapshot.LastTelemetryReceived ? "0" : "—") << '\n';
		}
	}

	void DrainEvents(IRowingMachineDiscovery &Discovery,
					 IRowingMachine *Machine,
					 std::vector<FRowingMachineDescriptor> &Candidates,
					 PM5Tui::FRotatingFileLogger &Logger,
					 PM5Tui::FRunMetricsWriter &Metrics,
					 FDisplaySnapshot &Snapshot)
	{
		FRowingMachineEvent Event;
		while (Discovery.TryPollDiscoveryEvent(Event))
		{
			if (const auto *Candidate = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
			{
				Metrics.RecordDiscoveredCandidate(*Candidate, Event.MonotonicTimestampNs);
				Candidates.push_back(*Candidate);
				std::cout << "candidate=" << Candidates.size() << " name=\"" << Candidate->DisplayLabel << "\"\n";
			}
			else
				PrintEvent(Event, Logger, Metrics, Snapshot);
		}
		if (Machine)
		{
			while (Machine->TryPollEvent(Event))
				PrintEvent(Event, Logger, Metrics, Snapshot);
			if (auto *PM5Diagnostics =
					dynamic_cast<IConcept2PMRunDiagnostics *>(Machine))
			{
				FPM5ProbePacketEvidence Evidence;
				while (PM5Diagnostics->TryPollPM5ProbePacket(Evidence))
					Metrics.RecordProbePacket(Evidence);
				FWorkoutProgramEvent WorkoutEvent;
				while (PM5Diagnostics->TryPollWorkoutProgramEvent(WorkoutEvent))
				{
					LogWorkoutProgramEvent(WorkoutEvent, Logger);
					Metrics.RecordWorkoutProgramEvent(WorkoutEvent);
					std::cout << FormatWorkoutProgramEvent(WorkoutEvent) << '\n';
				}
			}
		}
	}

	void PrintStatus(const IRowingMachine *Machine,
					 const FDisplaySnapshot &Snapshot,
					 std::size_t CandidateCount,
					 bool HardwareProbeEnabled)
	{
		const FStatusLineView View = MakeStatusLineView(Snapshot, CandidateCount);
		std::cout << "status: tone=" << ToString(View.Tone)
				  << " headline=\"" << View.Headline << '"'
				  << " connection_state=" << ToString(Snapshot.ConnectionState)
				  << " discovery candidates=" << CandidateCount
				  << " last_transition_reason=" << ToString(Snapshot.LastTransitionReason)
				  << " last_telemetry_age_ms="
				  << AgeMilliseconds(Snapshot.LastTelemetryReceived);
		if (Snapshot.LastStateChanged)
			std::cout << " state_age_ms=" << AgeMilliseconds(Snapshot.LastStateChanged);
		if (Snapshot.SupportState)
			std::cout << " support_state=" << ToString(*Snapshot.SupportState);
		std::cout << " samples=" << Snapshot.SampleCount
				  << " reconnects=" << Snapshot.ReconnectCount
				  << " latest_issue=\"" << Snapshot.LatestIssue << '"';
		if (Machine)
		{
			const auto Diagnostics = Machine->GetDiagnostics();
			std::cout << " event_queue=" << Diagnostics.EventQueue.CurrentDepth << '/' << Diagnostics.EventQueue.Capacity;
			std::cout << (Diagnostics.AcquisitionQueue ? " acquisition_queue=available" : " acquisition_queue=unavailable");
		}
		else
			std::cout << " event_queue=unavailable acquisition_queue=unavailable";
		std::cout << " hardware_probe="
				  << (HardwareProbeEnabled ? "enabled" : "disabled");
		std::cout << '\n';
	}
} // namespace PM5Tui
