#include "DisplaySnapshot.h"

#include "TuiFormat.h"

namespace PM5Tui
{
	const char *ToString(EStatusTone Value)
	{
		switch (Value)
		{
		case EStatusTone::Neutral:
			return "Neutral";
		case EStatusTone::Progress:
			return "Progress";
		case EStatusTone::Healthy:
			return "Healthy";
		case EStatusTone::Warning:
			return "Warning";
		case EStatusTone::Critical:
			return "Critical";
		case EStatusTone::Diagnostic:
			return "Diagnostic";
		}
		return "Critical";
	}

	std::string AgeMilliseconds(
		const std::optional<FSteadyClock::time_point> &Timestamp,
		FSteadyClock::time_point Now)
	{
		return Timestamp
				   ? std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
										Now - *Timestamp)
										.count())
				   : std::string("—");
	}

	FStatusLineView MakeStatusLineView(const FDisplaySnapshot &Snapshot,
									   std::size_t CandidateCount)
	{
		FStatusLineView View;
		const auto Now = FSteadyClock::now();
		const auto TelemetryAgeMs = Snapshot.LastTelemetryReceived
										? std::optional<std::int64_t>(
											  std::chrono::duration_cast<std::chrono::milliseconds>(
												  Now - *Snapshot.LastTelemetryReceived)
												  .count())
										: std::nullopt;

		switch (Snapshot.ConnectionState)
		{
		case ERowingConnectionState::Idle:
			View.Tone = EStatusTone::Neutral;
			View.Headline = "DISCONNECTED — PRESS SCAN TO FIND A PM5";
			break;
		case ERowingConnectionState::Scanning:
			View.Tone = EStatusTone::Progress;
			View.Headline = "SCANNING FOR CONCEPT2 PM5 DEVICES";
			break;
		case ERowingConnectionState::Connecting:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTING TO SELECTED PM5";
			break;
		case ERowingConnectionState::Discovering:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTED — CHECKING PM5 SERVICES";
			break;
		case ERowingConnectionState::ReadingIdentity:
			View.Tone = EStatusTone::Progress;
			View.Headline = "CONNECTED — VERIFYING PM5 IDENTITY";
			break;
		case ERowingConnectionState::Subscribing:
			View.Tone = EStatusTone::Progress;
			View.Headline = "STARTING PM5 TELEMETRY";
			break;
		case ERowingConnectionState::Ready:
			if (!TelemetryAgeMs)
			{
				View.Tone = EStatusTone::Progress;
				View.Headline = "PM5 READY — WAITING FOR TELEMETRY";
			}
			else if (*TelemetryAgeMs <= 500)
			{
				View.Tone = EStatusTone::Healthy;
				View.Headline = "LIVE — PM5 READY";
			}
			else if (*TelemetryAgeMs <= 1500)
			{
				View.Tone = EStatusTone::Warning;
				View.Headline = "TELEMETRY LATE — CHECK THE PM5 LINK";
			}
			else
			{
				View.Tone = EStatusTone::Critical;
				View.Headline = "TELEMETRY LOST — ROWING INPUT PAUSED";
			}
			break;
		case ERowingConnectionState::Stale:
			View.Tone = EStatusTone::Critical;
			View.Headline = "STALE TELEMETRY — ROWING INPUT PAUSED";
			break;
		case ERowingConnectionState::Reconnecting:
			View.Tone = EStatusTone::Warning;
			View.Headline = "RECONNECTING TO THE REMEMBERED PM5 — INPUT PAUSED";
			break;
		case ERowingConnectionState::Unsupported:
			View.Tone = EStatusTone::Critical;
			View.Headline = "UNSUPPORTED PM5 — TELEMETRY REJECTED";
			break;
		case ERowingConnectionState::Failed:
			View.Tone = EStatusTone::Critical;
			View.Headline = "CONNECTION FAILED — REVIEW THE ISSUE BELOW";
			break;
		case ERowingConnectionState::PermissionDenied:
			View.Tone = EStatusTone::Critical;
			View.Headline = "BLUETOOTH PERMISSION REQUIRED";
			break;
		case ERowingConnectionState::DiagnosticOnly:
			View.Tone = EStatusTone::Diagnostic;
			View.Headline = "DIAGNOSTIC ONLY — NOT WORKOUT-READY DATA";
			break;
		}

		View.Telemetry = TelemetryAgeMs
							 ? "telemetry " + std::to_string(*TelemetryAgeMs) + " ms ago"
							 : "telemetry waiting";
		std::ostringstream Detail;
		Detail << "State " << ToString(Snapshot.ConnectionState)
			   << "  •  Reason " << ToString(Snapshot.LastTransitionReason);
		if (Snapshot.LatestIssue != "None")
			Detail << "  •  Last issue " << Snapshot.LatestIssue;
		Detail << "  •  " << View.Telemetry
			   << "  •  Samples " << Snapshot.SampleCount
			   << "  •  Reconnects " << Snapshot.ReconnectCount
			   << "  •  Candidates " << CandidateCount;
		if (Snapshot.SupportState)
			Detail << "  •  Support " << ToString(*Snapshot.SupportState);
		if (Snapshot.LatestSample)
			Detail << "  •  Quality "
				   << FormatQualityFlags(Snapshot.LatestSample->QualityFlags);
		View.Detail = Detail.str();
		return View;
	}

	void RecordSample(const FRowingMetricSample &Sample, FDisplaySnapshot &Snapshot)
	{
		Snapshot.LatestSample = Sample;
		++Snapshot.SampleCount;
		Snapshot.LastSampleSequence = Sample.Sequence;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::Duplicate))
			++Snapshot.DuplicateSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::SourceGap))
			++Snapshot.SourceGapSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::TimeRegression))
			++Snapshot.TimeRegressionSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::DistanceRegression))
			++Snapshot.DistanceRegressionSampleCount;
		if (HasRowingQualityFlag(Sample.QualityFlags, ERowingQualityFlag::MissingField))
			++Snapshot.MissingFieldSampleCount;
	}

	PM5Tui::FRunMetricsSummary MakeRunMetricsSummary(
		const FDisplaySnapshot &Snapshot,
		const IRowingMachine *Machine)
	{
		PM5Tui::FRunMetricsSummary Summary;
		Summary.SampleCount = Snapshot.SampleCount;
		Summary.DiagnosticSampleCount = Snapshot.DiagnosticSampleCount;
		Summary.StrokeMetricsRecordCount = Snapshot.StrokeMetricsRecordCount;
		Summary.CorrectionCount = Snapshot.CorrectionCount;
		Summary.StaleEventCount = Snapshot.StaleCount;
		Summary.FaultCount = Snapshot.FaultCount;
		Summary.DuplicateSampleCount = Snapshot.DuplicateSampleCount;
		Summary.SourceGapSampleCount = Snapshot.SourceGapSampleCount;
		Summary.TimeRegressionSampleCount = Snapshot.TimeRegressionSampleCount;
		Summary.DistanceRegressionSampleCount = Snapshot.DistanceRegressionSampleCount;
		Summary.MissingFieldSampleCount = Snapshot.MissingFieldSampleCount;
		Summary.LastSampleSequence = Snapshot.LastSampleSequence;
		Summary.ReconnectCount = Snapshot.ReconnectCount;
		Summary.TotalReconnectGapMs = Snapshot.TotalReconnectGapMs;
		Summary.LongestReconnectGapMs = Snapshot.LongestReconnectGapMs;
		Summary.FinalConnectionState =
			Machine ? Machine->GetConnectionState() : Snapshot.ConnectionState;

		if (Machine)
		{
			const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
			const auto ConvertQueue = [](const FRowingQueueDiagnostics &Queue)
			{
				return PM5Tui::FQueueMetricsSummary{
					Queue.CurrentDepth,
					Queue.Capacity,
					Queue.HighWaterMark,
					Queue.OverflowCount};
			};
			Summary.EventQueue = ConvertQueue(Diagnostics.EventQueue);
			if (Diagnostics.AcquisitionQueue)
				Summary.AcquisitionQueue = ConvertQueue(*Diagnostics.AcquisitionQueue);
			if (const auto *PM5Diagnostics =
					dynamic_cast<const IConcept2PMRunDiagnostics *>(Machine))
				Summary.PM5Diagnostics = PM5Diagnostics->GetPM5RunDiagnostics();
		}
		return Summary;
	}
} // namespace PM5Tui
