#include "InteractiveTui.h"

#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "DiagnosticEvents.h"
#include "DisplaySnapshot.h"
#include "RotatingFileLogger.h"
#include "RunMetricsWriter.h"
#include "TuiFormat.h"
#ifdef VIR_PM5_TUI_JOURNAL
#include "WorkoutJournalDriver.h"
#endif

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef VIR_SOURCE_REVISION
#define VIR_SOURCE_REVISION "unknown"
#endif

using namespace PM5Tui;

namespace
{
	class FInteractiveTui
	{
	  public:
		explicit FInteractiveTui(bool InHardwareProbeEnabled = false,
								 bool InJournalEnabled = false)
			: HardwareProbeEnabled(InHardwareProbeEnabled),
			  ProbeConfiguration(
				  MakeHardwareProbeConfiguration(InHardwareProbeEnabled)),
			  Logger("Logs/pm5-tui"),
			  Metrics("Metrics/pm5-tui", VIR_SOURCE_REVISION, ProbeConfiguration),
			  Discovery(InHardwareProbeEnabled
							? CreateConcept2PMHardwareProbeDiscovery(ProbeConfiguration)
							: CreateConcept2PMDiscovery())
		{
			Logger.Log(PM5Tui::ELogLevel::Info,
					   "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
			if (HardwareProbeEnabled)
				Logger.Log(PM5Tui::ELogLevel::Warning,
						   "event=HardwareProbeStarted raw_telemetry_capture=true owner_only=true");
			LogMetricsAvailability(Metrics, Logger);
			if (InJournalEnabled)
				OpenJournal();
		}

		int Run()
		{
			ftxui::App Screen = ftxui::App::TerminalOutput();
			auto Component = BuildComponent(Screen);
			ftxui::Loop Loop(&Screen, Component);
			while (!Loop.HasQuitted())
			{
				DrainEvents();
				// Age and liveness labels must keep moving even if the PM5 stops
				// producing events.
				Screen.PostEvent(ftxui::Event::Custom);
				Loop.RunOnce();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (Machine)
			{
				DrainEvents();
				EndJournalSession();
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				DrainEvents();
			}
			const auto Summary = MakeRunMetricsSummary(Snapshot, Machine.get());
			LogTuiStopped(Summary, Logger);
			Metrics.RecordRunStopped(Summary);
			if (!Metrics.IsAvailable())
				Logger.Log(PM5Tui::ELogLevel::Error,
						   "event=RunMetricsWriteFailed");
			return 0;
		}

	  private:
		static constexpr std::size_t MaxHistoryEntries = 3;

		bool HardwareProbeEnabled = false;
		FPM5HardwareProbeConfiguration ProbeConfiguration;
		PM5Tui::FRotatingFileLogger Logger;
		PM5Tui::FRunMetricsWriter Metrics;
		std::unique_ptr<IConcept2PMDiscovery> Discovery;
		std::unique_ptr<IRowingMachine> Machine;
#ifdef VIR_PM5_TUI_JOURNAL
		std::unique_ptr<PM5Tui::FWorkoutJournalDriver> Journal;
#endif
		std::vector<FRowingMachineDescriptor> Candidates;
		std::vector<std::string> CandidateLabels{"No candidates. Press Scan."};
		std::vector<std::string> History{"Ready. Press Scan to discover a PM5."};
		FDisplaySnapshot Snapshot;
		std::string Identity = "No device selected";
		std::optional<FRowingMachineId> SelectedMachineId;
		int SelectedCandidate = 0;

		void AddHistory(std::string Entry)
		{
			if (History.size() == MaxHistoryEntries)
				History.erase(History.begin());
			History.push_back(std::move(Entry));
		}

		std::string FormatCandidateLabel(
			const FRowingMachineDescriptor &Candidate) const
		{
			std::ostringstream Label;
			if (SelectedMachineId && Candidate.Id == *SelectedMachineId)
				Label << "[SELECTED] ";
			Label << Candidate.DisplayLabel;
			if (Candidate.SignalStrengthDbm)
				Label << " | " << *Candidate.SignalStrengthDbm << " dBm";
			Label << " | " << ToString(Candidate.KindHint);
			return Label.str();
		}

		void RefreshCandidateLabels()
		{
			CandidateLabels.clear();
			if (Candidates.empty())
			{
				CandidateLabels.emplace_back("No candidates. Press Scan.");
				return;
			}
			for (const FRowingMachineDescriptor &Candidate : Candidates)
				CandidateLabels.push_back(FormatCandidateLabel(Candidate));
		}

		void AddCandidate(const FRowingMachineDescriptor &Candidate)
		{
			Candidates.push_back(Candidate);
			RefreshCandidateLabels();
			AddHistory("Candidate " + std::to_string(Candidates.size()) + " discovered");
		}

		void RecordEvent(const FRowingMachineEvent &ObservedEvent)
		{
			const auto &Payload = ObservedEvent.Payload;
			if (const auto *Info = std::get_if<FRowingMachineInfo>(&Payload))
			{
				Snapshot.SupportState = Info->SupportState;
				Metrics.RecordMachineInfo(*Info, ObservedEvent.MonotonicTimestampNs);
				Logger.Log(PM5Tui::ELogLevel::Info,
						   "event=MachineInfoObserved model=" + Info->Model +
							   " hardware=" + Info->HardwareVersion +
							   " firmware=" + Info->FirmwareVersion +
							   " machine_kind=" + ToString(Info->MachineKind) +
							   " support_state=" + ToString(Info->SupportState));
				Identity = "Model " + Info->Model + " | Hardware " +
						   Info->HardwareVersion + " | Firmware " + Info->FirmwareVersion +
						   " | " + ToString(Info->MachineKind) + " | " +
						   ToString(Info->SupportState);
				AddHistory("Identity read: " +
						   std::string(ToString(Info->SupportState)));
			}
			else if (const auto *Sample = std::get_if<FRowingMetricSample>(&Payload))
			{
				Snapshot.LastTelemetryReceived = FSteadyClock::now();
				RecordSample(*Sample, Snapshot);
				Metrics.RecordMetricSample(*Sample, false);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricSampled " + FormatMetric("telemetry", *Sample));
			}
			else if (const auto *Sample =
						 std::get_if<FRowingDiagnosticSample>(&Payload))
			{
				Snapshot.LastTelemetryReceived = FSteadyClock::now();
				++Snapshot.DiagnosticSampleCount;
				RecordSample(Sample->Sample, Snapshot);
				Metrics.RecordMetricSample(Sample->Sample, true);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=DiagnosticMetricSampled " +
							   FormatMetric("telemetry", Sample->Sample));
			}
			else if (const auto *Stroke = std::get_if<FRowingStrokeMetrics>(&Payload))
			{
				++Snapshot.StrokeMetricsRecordCount;
				Metrics.RecordStrokeMetrics(*Stroke, ObservedEvent.MonotonicTimestampNs);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=StrokeMetricsObserved elapsed_ms=" +
							   std::to_string(Stroke->SourceElapsedMs) +
							   " stroke_count=" + OptionalValue(Stroke->StrokeCount) +
							   " stroke_power_w=" + OptionalValue(Stroke->StrokePowerW) +
							   " work_per_stroke_deci_joules=" +
							   OptionalValue(Stroke->WorkPerStrokeDeciJoules));
			}
			else if (const auto *Changed =
						 std::get_if<FRowingConnectionStateChanged>(&Payload))
			{
				Metrics.RecordConnectionStateChanged(
					*Changed, ObservedEvent.MonotonicTimestampNs);
				Snapshot.LastStateChanged = FSteadyClock::now();
				Snapshot.ConnectionState = Changed->NewState;
				Snapshot.LastTransitionReason = Changed->Reason;
				LogConnectionStateChanged(*Changed, Logger);
				AddHistory("Connection: " + std::string(ToString(Changed->PreviousState)) +
						   " → " + ToString(Changed->NewState) + " (" +
						   ToString(Changed->Reason) + ")");
			}
			else if (const auto *Stale = std::get_if<FRowingTelemetryStale>(&Payload))
			{
				++Snapshot.StaleCount;
				Metrics.RecordTelemetryStale(
					*Stale, ObservedEvent.MonotonicTimestampNs);
				LogTelemetryStale(*Stale, Logger);
				Snapshot.LatestIssue =
					"Stale telemetry (" + std::to_string(Stale->AgeMs) + " ms)";
				AddHistory(Snapshot.LatestIssue);
			}
			else if (const auto *Restored =
						 std::get_if<FRowingConnectionRestored>(&Payload))
			{
				Metrics.RecordConnectionRestored(
					*Restored, ObservedEvent.MonotonicTimestampNs);
				++Snapshot.ReconnectCount;
				Snapshot.TotalReconnectGapMs += Restored->GapDurationMs;
				Snapshot.LongestReconnectGapMs =
					std::max(Snapshot.LongestReconnectGapMs, Restored->GapDurationMs);
				Logger.Log(PM5Tui::ELogLevel::Info,
						   "event=ConnectionRestored gap_ms=" +
							   std::to_string(Restored->GapDurationMs));
				AddHistory("Same-device reconnect gap: " +
						   std::to_string(Restored->GapDurationMs) + " ms");
			}
			else if (const auto *Fault = std::get_if<FRowingFault>(&Payload))
			{
				++Snapshot.FaultCount;
				Metrics.RecordFault(*Fault, ObservedEvent.MonotonicTimestampNs);
				Logger.Log(PM5Tui::ELogLevel::Warning,
						   "event=FaultObserved code=" +
							   std::string(ToString(Fault->Code)) +
							   " detail=" + Fault->DiagnosticText);
				Snapshot.LatestIssue = std::string(ToString(Fault->Code)) + ": " +
									   Fault->DiagnosticText;
				AddHistory("Fault: " + std::string(ToString(Fault->Code)));
			}
			else if (const auto *Correction =
						 std::get_if<FRowingMetricCorrection>(&Payload))
			{
				++Snapshot.CorrectionCount;
				Metrics.RecordMetricCorrection(Correction->TargetSampleSequence,
											   Correction->CorrectedSample);
				Logger.Log(PM5Tui::ELogLevel::Debug,
						   "event=MetricCorrected target_sample_sequence=" +
							   std::to_string(Correction->TargetSampleSequence) + ' ' +
							   FormatMetric("telemetry", Correction->CorrectedSample));
				Snapshot.LatestSample = Correction->CorrectedSample;
				AddHistory("Late metric correction received");
			}
		}

		ftxui::Element TelemetryContent() const
		{
			if (!Snapshot.LatestSample)
				return ftxui::text("Waiting for a valid PM5 telemetry sample.");

			const auto &Sample = *Snapshot.LatestSample;
			return ftxui::vbox({
				ftxui::text("Elapsed: " + std::to_string(Sample.SourceElapsedMs) + " ms"),
				ftxui::text("Distance: " + std::to_string(Sample.DistanceMm) + " mm"),
				ftxui::text("Speed: " + OptionalValue(Sample.SpeedMmPerS) + " mm/s"),
				ftxui::text("Pace: " + OptionalValue(Sample.PaceMsPer500M) + " ms/500 m"),
				ftxui::text("Stroke rate: " + OptionalValue(Sample.StrokeRateDeciSpm) + " deci-spm"),
				ftxui::text("Stroke power: " + OptionalValue(Sample.StrokePowerW) + " W"),
				ftxui::text("Average power: " + OptionalValue(Sample.AveragePowerW) + " W"),
				ftxui::text("Calories: " + OptionalValue(Sample.Calories)),
				ftxui::text("Heart rate: " + OptionalValue(Sample.HeartRateBpm) + " bpm"),
				ftxui::text("Drag factor: " + OptionalValue(Sample.DragFactor)),
				ftxui::text("Stroke count: " + OptionalValue(Sample.StrokeCount)),
				ftxui::text(std::string("Workout: ") + ToString(Sample.WorkoutState)),
				ftxui::text(std::string("Rowing: ") + ToString(Sample.RowingState)),
				ftxui::text(std::string("Stroke state: ") + ToString(Sample.StrokeState)),
				ftxui::text("Quality: " + FormatQualityFlags(Sample.QualityFlags)),
			});
		}

		static std::uint64_t NowNs()
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					FSteadyClock::now().time_since_epoch())
					.count());
		}

		// Opt-in sealed workout journal (--journal, make pm5-tui-journal). Never
		// touches ordinary runs, and a journal that cannot open leaves the TUI usable.
		void OpenJournal()
		{
#ifdef VIR_PM5_TUI_JOURNAL
			Journal = std::make_unique<PM5Tui::FWorkoutJournalDriver>("Metrics/pm5-tui/journal");
			if (!Journal->IsAvailable())
			{
				Logger.Log(PM5Tui::ELogLevel::Error, "event=WorkoutJournalUnavailable");
				AddHistory("Workout journal unavailable: " + Journal->GetError());
				return;
			}
			Logger.Log(PM5Tui::ELogLevel::Info, "event=WorkoutJournalOpened sealed=true owner_only=true");
			AddHistory("Workout journal open (sealed, owner-only)");
			if (!Journal->GetRecoveryNote().empty())
			{
				Logger.Log(PM5Tui::ELogLevel::Warning, "event=WorkoutJournalRecovered");
				AddHistory(Journal->GetRecoveryNote());
			}
#else
			AddHistory("Workout journal is not available in this build");
#endif
		}

		bool HasJournal() const
		{
#ifdef VIR_PM5_TUI_JOURNAL
			return Journal != nullptr;
#else
			return false;
#endif
		}

		void LogJournalLines(const std::vector<std::string> &Lines)
		{
			for (const std::string &Line : Lines)
				Logger.Log(PM5Tui::ELogLevel::Info, Line);
		}

		void EndJournalSession()
		{
#ifdef VIR_PM5_TUI_JOURNAL
			if (Journal)
			{
				LogJournalLines(Journal->EndSession(NowNs()));
				AddHistory("Workout session ended");
			}
#endif
		}

		void DrainEvents()
		{
			AdoptRelaunchMachine();
			FRowingMachineEvent Event;
			while (Discovery->TryPollDiscoveryEvent(Event))
			{
				if (const auto *Candidate = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
				{
					Metrics.RecordDiscoveredCandidate(*Candidate, Event.MonotonicTimestampNs);
					AddCandidate(*Candidate);
				}
				else
					RecordEvent(Event);
			}
			AdoptRelaunchMachine();
#ifdef VIR_PM5_TUI_JOURNAL
			if (Journal)
				Journal->SyncMachine(Machine.get(), NowNs());
#endif
			if (Machine)
			{
				while (Machine->TryPollEvent(Event))
				{
					RecordEvent(Event);
#ifdef VIR_PM5_TUI_JOURNAL
					if (Journal)
						Journal->Ingest(Event);
#endif
				}
#ifdef VIR_PM5_TUI_JOURNAL
				if (Journal)
					LogJournalLines(Journal->Tick(NowNs()));
#endif
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
				{
					FPM5ProbePacketEvidence Evidence;
					while (PM5Diagnostics->TryPollPM5ProbePacket(Evidence))
						Metrics.RecordProbePacket(Evidence);
					FWorkoutProgramEvent WorkoutEvent;
					while (PM5Diagnostics->TryPollWorkoutProgramEvent(WorkoutEvent))
					{
						LogWorkoutProgramEvent(WorkoutEvent, Logger);
						Metrics.RecordWorkoutProgramEvent(WorkoutEvent);
						AddHistory(FormatWorkoutProgramEvent(WorkoutEvent));
					}
				}
			}
		}

		void AdoptRelaunchMachine()
		{
			if (Machine)
				return;
			Machine = Discovery->TryTakeRelaunchMachine();
			if (Machine)
				AddHistory("Adopted remembered-PM5 reconnect session");
		}

		ftxui::Element StatusLine() const
		{
			FStatusLineView View = MakeStatusLineView(Snapshot, Candidates.size());
			if (Machine)
			{
				const FRowingMachineDiagnostics Diagnostics = Machine->GetDiagnostics();
				View.Detail += "  •  Event queue " +
							   std::to_string(Diagnostics.EventQueue.CurrentDepth) + "/" +
							   std::to_string(Diagnostics.EventQueue.Capacity) +
							   " high " +
							   std::to_string(Diagnostics.EventQueue.HighWaterMark) +
							   " dropped " +
							   std::to_string(Diagnostics.EventQueue.OverflowCount);
				if (Diagnostics.AcquisitionQueue)
				{
					View.Detail += "  •  BLE queue " +
								   std::to_string(
									   Diagnostics.AcquisitionQueue->CurrentDepth) +
								   "/" +
								   std::to_string(Diagnostics.AcquisitionQueue->Capacity) +
								   " high " +
								   std::to_string(Diagnostics.AcquisitionQueue->HighWaterMark) +
								   " dropped " +
								   std::to_string(Diagnostics.AcquisitionQueue->OverflowCount);
				}
#ifdef VIR_PM5_TUI_JOURNAL
				if (Journal)
					View.Detail += "  •  Journal " + Journal->StatusText();
#endif
				if (Diagnostics.EventQueue.OverflowCount != 0 ||
					(Diagnostics.AcquisitionQueue &&
					 Diagnostics.AcquisitionQueue->OverflowCount != 0))
				{
					View.Tone = EStatusTone::Critical;
					View.Headline = "QUEUE OVERFLOW — TELEMETRY CAPTURE MAY BE INCOMPLETE";
				}
				if (HardwareProbeEnabled)
				{
					if (const auto *PM5Diagnostics =
							dynamic_cast<const IConcept2PMRunDiagnostics *>(Machine.get()))
					{
						const auto Probe =
							PM5Diagnostics->GetPM5RunDiagnostics().ProbeCapture;
						View.Detail += "  •  RAW packets " +
									   std::to_string(Probe.CapturedPacketCount) +
									   " dropped " +
									   std::to_string(
										   Probe.EvidenceQueueOverflowCount +
										   Probe.LimitDroppedPacketCount);
						if (!Probe.Active ||
							Probe.EvidenceQueueOverflowCount != 0 ||
							Probe.LimitDroppedPacketCount != 0)
						{
							View.Tone = EStatusTone::Critical;
							View.Headline =
								"HARDWARE PROBE CAPTURE INCOMPLETE";
						}
					}
				}
			}
			if (HardwareProbeEnabled && View.Tone != EStatusTone::Critical)
			{
				View.Tone = EStatusTone::Diagnostic;
				View.Headline = "HARDWARE PROBE • " + View.Headline;
				if (!Machine)
					View.Detail += "  •  RAW telemetry capture armed";
			}
			if (HardwareProbeEnabled && !Metrics.IsAvailable())
			{
				View.Tone = EStatusTone::Critical;
				View.Headline = "HARDWARE PROBE FILE UNAVAILABLE — DO NOT ROW";
			}

			ftxui::Color Background = ftxui::Color::GrayDark;
			ftxui::Color Foreground = ftxui::Color::White;
			switch (View.Tone)
			{
			case EStatusTone::Neutral:
				break;
			case EStatusTone::Progress:
				Background = ftxui::Color::Blue;
				break;
			case EStatusTone::Healthy:
				Background = ftxui::Color::Green;
				Foreground = ftxui::Color::Black;
				break;
			case EStatusTone::Warning:
				Background = ftxui::Color::Yellow;
				Foreground = ftxui::Color::Black;
				break;
			case EStatusTone::Critical:
				Background = ftxui::Color::Red;
				break;
			case EStatusTone::Diagnostic:
				Background = ftxui::Color::Magenta;
				break;
			}

			const auto Headline = ftxui::hbox({
									  ftxui::text("  ● " + View.Headline),
									  ftxui::filler(),
									  ftxui::text(" " + View.Telemetry + "  "),
								  }) |
								  ftxui::bold | ftxui::color(Foreground) |
								  ftxui::bgcolor(Background);
			const auto Detail = ftxui::hbox({
									ftxui::text("  " + View.Detail),
									ftxui::filler(),
								}) |
								ftxui::color(ftxui::Color::White) |
								ftxui::bgcolor(ftxui::Color::GrayDark);
			return ftxui::vbox({Headline, Detail}) |
				   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 2);
		}

		void StartScan()
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStarted);
			Candidates.clear();
			CandidateLabels = {"Scanning for Concept2 PM5 devices..."};
			SelectedCandidate = 0;
			Discovery->StartScan();
			AddHistory("Scan started");
		}

		void SelectCandidate()
		{
			if (SelectedCandidate < 0 ||
				static_cast<std::size_t>(SelectedCandidate) >= Candidates.size())
			{
				Snapshot.LatestIssue =
					"Select a discovered candidate before connecting";
				return;
			}
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::CandidateSelected,
								 static_cast<std::uint64_t>(SelectedCandidate + 1));
			Discovery->StopScan();
			const FRowingMachineDescriptor &Candidate = Candidates[SelectedCandidate];
			Machine = Discovery->CreateMachine(Candidate.Id);
			SelectedMachineId = Candidate.Id;
			RefreshCandidateLabels();
			AddHistory("Candidate " + std::to_string(SelectedCandidate + 1) + " selected");
		}

		void Connect()
		{
			if (!Machine)
			{
				Snapshot.LatestIssue =
					"Select a discovered candidate before connecting";
				return;
			}
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ConnectRequested);
			Machine->Connect();
			AddHistory("Connection requested");
		}

		// Diagnostic-only managed-workout commands (Phase 0 Milestone 4 Spike
		// A). Reachable only under --hardware-probe (make hil-pm5), matching
		// the DiagnosticOnly/DiagnosticSampleObserved precedent's owner-run
		// real-hardware gating; never a production workout control surface.
		void ProgramWorkout(const Concept2PM::FDiagnosticWorkoutSpec &Spec,
							PM5Tui::EPM5TuiRunAction Action)
		{
			auto *PM5Diagnostics =
				Machine ? dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get())
						: nullptr;
			if (!PM5Diagnostics)
			{
				Snapshot.LatestIssue =
					"Connect to a Ready PM5 before programming a diagnostic workout";
				return;
			}
			Metrics.RecordAction(Action);
			const FRowingCommandResult Result =
				PM5Diagnostics->ProgramDiagnosticWorkout(Spec);
			AddHistory(Result.IsAccepted()
						   ? "Diagnostic workout program requested"
						   : "Diagnostic workout program rejected: PM5 not Ready");
		}

		void AbortWorkout()
		{
			auto *PM5Diagnostics =
				Machine ? dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get())
						: nullptr;
			if (!PM5Diagnostics)
			{
				Snapshot.LatestIssue =
					"Connect to a Ready PM5 before aborting a diagnostic workout";
				return;
			}
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::AbortWorkoutRequested);
			const FRowingCommandResult Result = PM5Diagnostics->AbortDiagnosticWorkout();
			AddHistory(Result.IsAccepted()
						   ? "Diagnostic workout abort requested"
						   : "Diagnostic workout abort rejected: PM5 not Ready");
		}

		void ForgetRememberedMachine()
		{
			if (Machine)
			{
				EndJournalSession();
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				Machine.reset();
			}
			Discovery->ForgetRememberedMachine();
			SelectedMachineId.reset();
			RefreshCandidateLabels();
			Identity = "No device selected";
			AddHistory("Remembered PM5 forgotten; disconnected");
		}

		ftxui::Component BuildComponent(ftxui::App &Screen)
		{
			const auto Scan = ftxui::Button("Scan", [this]
											{ StartScan(); });
			const auto Stop = ftxui::Button("Stop scan", [this]
											{ Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStopped); Discovery->StopScan(); AddHistory("Scan stopped"); });
			const auto Select = ftxui::Button("Select", [this]
											  { SelectCandidate(); });
			const auto ConnectButton = ftxui::Button("Connect", [this]
													 { Connect(); });
			const auto Disconnect = ftxui::Button("Disconnect", [this]
												  { if (Machine) { Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::DisconnectRequested); Machine->Disconnect(); } });
			const auto Forget = ftxui::Button("Forget PM5", [this]
											  { ForgetRememberedMachine(); });
			const auto EndSessionButton = ftxui::Button("End Session", [this]
														{ EndJournalSession(); });
			const auto Quit = ftxui::Button("Quit", [&Screen]
											{ Screen.Exit(); });
			const auto ProgramDistance = ftxui::Button("Program Distance", [this]
													   { ProgramWorkout(MakeExampleDistanceWorkoutSpec(), PM5Tui::EPM5TuiRunAction::ProgramDistanceWorkoutRequested); });
			const auto ProgramTime = ftxui::Button("Program Time", [this]
												   { ProgramWorkout(MakeExampleTimeWorkoutSpec(), PM5Tui::EPM5TuiRunAction::ProgramTimeWorkoutRequested); });
			const auto ProgramInterval = ftxui::Button("Program Interval", [this]
													   { ProgramWorkout(MakeExampleTimeIntervalWorkoutSpec(), PM5Tui::EPM5TuiRunAction::ProgramTimeIntervalWorkoutRequested); });
			const auto AbortWorkoutButton = ftxui::Button("Abort Workout", [this]
														  { AbortWorkout(); });
			const auto Menu = ftxui::Menu(&CandidateLabels, &SelectedCandidate);
			std::vector<ftxui::Component> ActionRows{
				ftxui::Container::Horizontal({Scan, Stop, Select, ConnectButton, Disconnect}),
			};
			// Diagnostic-only managed-workout commands are reachable only
			// under --hardware-probe (make hil-pm5): a real, owner-run PM5
			// session, matching the DiagnosticOnly precedent's gating.
			if (HardwareProbeEnabled)
				ActionRows.push_back(ftxui::Container::Horizontal(
					{ProgramDistance, ProgramTime, ProgramInterval, AbortWorkoutButton}));
			if (HasJournal())
				ActionRows.push_back(ftxui::Container::Horizontal({EndSessionButton}));
			ActionRows.push_back(ftxui::Container::Horizontal({Forget, Quit}));
			const auto Actions = ftxui::Container::Vertical(ActionRows);
			const auto Controls = ftxui::Container::Vertical({
				Actions,
				Menu,
			});
			auto Root = ftxui::Renderer(Controls, [this, Actions, Menu]
										{
				ftxui::Elements HistoryElements;
				for (const std::string &Entry : History)
					HistoryElements.push_back(ftxui::text(Entry));
				auto MainPane = ftxui::vbox({
					ftxui::text(HardwareProbeEnabled
								? "PM5 Hardware Probe — RAW TELEMETRY CAPTURE ACTIVE"
								: "PM5 Diagnostic") |
						ftxui::bold,
						ftxui::text("Controls: Tab/Shift-Tab to focus, arrows to choose a PM5, Enter to activate, q to quit."),
						Actions->Render(),
						ftxui::separator(),
						ftxui::text("Telemetry") | ftxui::bold,
						ftxui::separator(),
						ftxui::text("Identity: " + Identity),
						TelemetryContent(),
						ftxui::separator(),
						ftxui::text("Recent events") | ftxui::bold,
						ftxui::vbox(std::move(HistoryElements)) |
							ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 4),
						ftxui::filler(),
				}) | ftxui::border | ftxui::flex;
				auto CandidatePane = ftxui::vbox({
					ftxui::text("PM5 devices") | ftxui::bold,
					ftxui::separator(),
					Menu->Render() | ftxui::flex,
				}) | ftxui::border | ftxui::flex;
				const bool WideLayout = ftxui::Terminal::Size().dimx >= 100;
				auto Content = WideLayout
					? ftxui::hbox({
						  MainPane | ftxui::flex,
						  CandidatePane | ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
															std::max(24, ftxui::Terminal::Size().dimx / 4)),
					  })
					: ftxui::vbox({CandidatePane | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 8),
										MainPane | ftxui::flex});
				return ftxui::vbox({Content | ftxui::flex, StatusLine()}) |
					ftxui::border | ftxui::flex; });
			return Root | ftxui::CatchEvent([&Screen](ftxui::Event Event)
											{
				if (Event == ftxui::Event::Character('q'))
				{
					Screen.Exit();
					return true;
				}
				return false; });
		}
	};
} // namespace

namespace PM5Tui
{
	int RunInteractiveTui(bool HardwareProbeEnabled, bool JournalEnabled)
	{
		return FInteractiveTui(HardwareProbeEnabled, JournalEnabled).Run();
	}
} // namespace PM5Tui
