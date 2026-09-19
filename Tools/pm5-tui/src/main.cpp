#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "DiagnosticEvents.h"
#include "DisplaySnapshot.h"
#include "InteractiveTui.h"
#include "RotatingFileLogger.h"
#include "RunMetricsWriter.h"
#include "TuiFormat.h"
#ifdef VIR_PM5_TUI_JOURNAL
#include "WorkoutJournalDriver.h"
#endif

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

int main(int argc, char **argv)
{
	bool HardwareProbeEnabled = false;
	bool JournalEnabled = false;
	int ArgumentIndex = 1;
	for (; ArgumentIndex < argc; ++ArgumentIndex)
	{
		const std::string_view Argument(argv[ArgumentIndex]);
		if (Argument == "--hardware-probe")
			HardwareProbeEnabled = true;
		else if (Argument == "--journal")
			JournalEnabled = true;
		else
			break;
	}
	if (ArgumentIndex >= argc)
		return PM5Tui::RunInteractiveTui(HardwareProbeEnabled, JournalEnabled);
	if (std::string_view(argv[ArgumentIndex]) == "--help")
	{
		std::cout << "PM5 Diagnostic\n"
				  << "Interactive: run without arguments.\n"
				  << "Hardware probe: --hardware-probe (owner-only bounded raw telemetry capture).\n"
				  << "Workout journal: --journal (interactive only; journals rows into a Keychain-sealed, owner-only\n"
				  << "  database under Metrics/pm5-tui/journal/, which holds athlete data: never commit or share it).\n"
				  << "Script: --script <status|scan|stop|select N|connect|disconnect|"
				  << "program-distance|program-time|program-interval|abort-workout|forget|quit>...\n"
				  << "  program-distance/program-time/program-interval/abort-workout are\n"
				  << "  diagnostic-only managed-workout commands (Phase 0 Milestone 4 Spike A);\n"
				  << "  they require a connected, Ready PM5.\n";
		return 0;
	}
	if (std::string_view(argv[ArgumentIndex]) != "--script")
	{
		std::cerr << "unknown option; run with --help for usage\n";
		return 2;
	}
	if (JournalEnabled)
	{
		std::cerr << "--journal is interactive only and cannot be combined with --script\n";
		return 2;
	}

	const FPM5HardwareProbeConfiguration ProbeConfiguration =
		MakeHardwareProbeConfiguration(HardwareProbeEnabled);
	PM5Tui::FRotatingFileLogger Logger("Logs/pm5-tui");
	PM5Tui::FRunMetricsWriter Metrics(
		"Metrics/pm5-tui", VIR_SOURCE_REVISION, ProbeConfiguration);
	Logger.Log(PM5Tui::ELogLevel::Info, "event=TuiStarted source_revision=" VIR_SOURCE_REVISION);
	if (HardwareProbeEnabled)
		Logger.Log(PM5Tui::ELogLevel::Warning,
				   "event=HardwareProbeStarted raw_telemetry_capture=true owner_only=true");
	LogMetricsAvailability(Metrics, Logger);
	// The shipped diagnostic executable exercises only reviewed profiles. The
	// adapter's separate diagnostic-only factory remains available to targeted
	// tests and does not permit Ready.
	auto Discovery = HardwareProbeEnabled
						 ? CreateConcept2PMHardwareProbeDiscovery(ProbeConfiguration)
						 : CreateConcept2PMDiscovery();
	std::unique_ptr<IRowingMachine> Machine;
	std::vector<FRowingMachineDescriptor> Candidates;
	FDisplaySnapshot Snapshot;
	std::vector<std::string> Commands;
	for (int Index = ArgumentIndex + 1; Index < argc; ++Index)
		Commands.emplace_back(argv[Index]);

	for (const std::string &Command : Commands)
	{
		if (Command.empty() || Command.find_first_not_of(" \t") == std::string::npos)
			continue;
		if (Command == "quit")
			break;
		if (Command == "status")
			PrintStatus(
				Machine.get(), Snapshot, Candidates.size(), HardwareProbeEnabled);
		else if (Command == "scan")
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStarted);
			Candidates.clear();
			Discovery->StartScan();
		}
		else if (Command == "stop")
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStopped);
			Discovery->StopScan();
		}
		else if (Command == "connect")
		{
			if (!Machine)
				std::cout << "no device selected; use select command to choose a candidate\n";
			else
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ConnectRequested);
				Machine->Connect();
			}
		}
		else if (Command == "disconnect" && Machine)
		{
			Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::DisconnectRequested);
			Machine->Disconnect();
		}
		else if (Command == "program-distance" || Command == "program-time" ||
				 Command == "program-interval" || Command == "abort-workout")
		{
			// Diagnostic-only managed-workout commands are reachable only
			// under --hardware-probe (make hil-pm5), matching the
			// interactive TUI's gating and the DiagnosticOnly precedent's
			// owner-run real-hardware restriction; never a production
			// workout control surface.
			if (!HardwareProbeEnabled)
			{
				std::cout << "diagnostic workout commands require --hardware-probe (make hil-pm5)\n";
				continue;
			}
			auto *PM5Diagnostics =
				Machine ? dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get())
						: nullptr;
			if (!PM5Diagnostics)
			{
				std::cout << "no device selected or not connected; use select and connect first\n";
				continue;
			}
			FRowingCommandResult Result;
			if (Command == "program-distance")
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ProgramDistanceWorkoutRequested);
				Result = PM5Diagnostics->ProgramDiagnosticWorkout(MakeExampleDistanceWorkoutSpec());
			}
			else if (Command == "program-time")
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ProgramTimeWorkoutRequested);
				Result = PM5Diagnostics->ProgramDiagnosticWorkout(MakeExampleTimeWorkoutSpec());
			}
			else if (Command == "program-interval")
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::ProgramTimeIntervalWorkoutRequested);
				Result = PM5Diagnostics->ProgramDiagnosticWorkout(MakeExampleTimeIntervalWorkoutSpec());
			}
			else
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::AbortWorkoutRequested);
				Result = PM5Diagnostics->AbortDiagnosticWorkout();
			}
			std::cout << "workout command " << Command
					  << (Result.IsAccepted() ? " accepted\n" : " rejected: PM5 not Ready\n");
		}
		else if (Command == "forget")
		{
			if (Machine)
			{
				Machine->Disconnect();
				if (auto *PM5Diagnostics =
						dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
					PM5Diagnostics->FinalizePM5ProbeCapture();
				Machine.reset();
			}
			Discovery->ForgetRememberedMachine();
			std::cout << "remembered PM5 forgotten; disconnected\n";
		}
		else if (Command == "__test_identity")
		{
			FRowingMachineInfo Info;
			Info.Model = "PM5";
			Info.HardwareVersion = "test-hardware";
			Info.FirmwareVersion = "test-firmware";
			Info.MachineKind = ERowingMachineKind::IndoorRower;
			Info.SupportedMetrics =
				ToRowingMetricSet(ERowingMetric::StrokePower) |
				ToRowingMetricSet(ERowingMetric::PeakDriveForce);
			Info.SupportState = ERowingMachineSupportState::Warn;
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Info)}, Logger, Metrics, Snapshot);
		}
		else if (Command == "__test_discovered_candidate")
		{
			FRowingMachineDescriptor Candidate;
			Candidate.DisplayLabel = "private-candidate-name-must-not-be-captured";
			Candidate.SignalStrengthDbm = -54;
			Candidate.KindHint = ERowingMachineKind::IndoorRower;
			Metrics.RecordDiscoveredCandidate(Candidate, 1'234'567'890ULL);
		}
		else if (Command == "__test_metric_correction")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 42;
			Sample.SourceElapsedMs = 123456;
			Sample.DistanceMm = 500000;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.RowingState = ERowingState::Active;
			Sample.StrokeState = ERowingStrokeState::Recovery;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::LateCorrection);
			PrintEvent(FRowingMachineEvent{.Payload = FRowingMetricCorrection{42, std::move(Sample)}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_metric_sample")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 7;
			Sample.SourceElapsedMs = 7000;
			Sample.DistanceMm = 12300;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.RowingState = ERowingState::Active;
			Sample.StrokeState = ERowingStrokeState::Drive;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::MissingField);
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Sample)}, Logger, Metrics, Snapshot);
		}
		else if (Command == "__test_diagnostic_metric_sample")
		{
			FRowingMetricSample Sample;
			Sample.Sequence = 99;
			Sample.SourceElapsedMs = 99000;
			Sample.DistanceMm = 99000;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.QualityFlags = ToRowingQualityFlags(ERowingQualityFlag::MissingField);
			PrintEvent(FRowingMachineEvent{.Payload = FRowingDiagnosticSample{std::move(Sample)}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_stroke_metrics")
		{
			FRowingStrokeMetrics Stroke;
			Stroke.Source = ERowingStrokeMetricsSource::KinematicsAndForce;
			Stroke.SourceElapsedMs = 123450;
			Stroke.StrokeCount = 1234;
			Stroke.CumulativeDistanceMm = 678900;
			Stroke.DriveLengthMm = 1490;
			Stroke.DriveTimeMs = 1280;
			Stroke.RecoveryTimeMs = 2580;
			Stroke.StrokeDistanceMm = 1000;
			Stroke.PeakDriveForceDeciLb = 250;
			Stroke.AverageDriveForceDeciLb = 150;
			Stroke.WorkPerStrokeDeciJoules = 10000;
			PrintEvent(FRowingMachineEvent{
						   .MonotonicTimestampNs = 1'234'567'890ULL,
						   .Payload = std::move(Stroke)},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_connection_state")
		{
			PrintEvent(FRowingMachineEvent{.Payload = FRowingConnectionStateChanged{
											   ERowingConnectionState::Subscribing,
											   ERowingConnectionState::Ready,
											   ERowingConnectionReason::ReadinessConfirmed}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_telemetry_stale")
		{
			PrintEvent(FRowingMachineEvent{.Payload = FRowingTelemetryStale{7, 500}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_connection_restored")
		{
			FRowingMachineInfo Info;
			Info.Model = "PM5";
			Info.MachineKind = ERowingMachineKind::IndoorRower;
			PrintEvent(FRowingMachineEvent{.Payload = FRowingConnectionRestored{
											   Info, 2750}},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command == "__test_fault")
		{
			FRowingFault Fault;
			Fault.Code = ERowingFaultCode::Disconnected;
			Fault.Severity = ERowingFaultSeverity::Recoverable;
			Fault.Operation = ERowingOperation::Reconnect;
			Fault.ConnectionState = ERowingConnectionState::Reconnecting;
			Fault.DiagnosticText = "redacted diagnostic detail";
			Fault.ExpectedValue = 18;
			Fault.ActualValue = 15;
			PrintEvent(FRowingMachineEvent{.Payload = std::move(Fault)},
					   Logger,
					   Metrics,
					   Snapshot);
		}
		else if (Command.rfind("select ", 0) == 0)
		{
			std::istringstream Input(Command.substr(7));
			std::size_t Selection = 0;
			if (!(Input >> Selection) || Selection == 0)
				std::cout << "candidate numbers start at 1; run scan and select a listed number (for example: select 1)\n";
			else if (Selection > Candidates.size())
				std::cout << "no candidate " << Selection << "; run scan and select a listed number\n";
			else
			{
				Metrics.RecordAction(PM5Tui::EPM5TuiRunAction::CandidateSelected,
									 static_cast<std::uint64_t>(Selection));
				Discovery->StopScan();
				Machine = Discovery->CreateMachine(Candidates[Selection - 1].Id);
			}
		}
		else
			std::cout << "unknown command\n";
		if (!Machine)
			Machine = Discovery->TryTakeRelaunchMachine();
		DrainEvents(*Discovery, Machine.get(), Candidates, Logger, Metrics, Snapshot);
	}
	if (Machine)
	{
		if (auto *PM5Diagnostics =
				dynamic_cast<IConcept2PMRunDiagnostics *>(Machine.get()))
			PM5Diagnostics->FinalizePM5ProbeCapture();
		DrainEvents(*Discovery, Machine.get(), Candidates, Logger, Metrics, Snapshot);
	}
	const auto Summary = MakeRunMetricsSummary(Snapshot, Machine.get());
	LogTuiStopped(Summary, Logger);
	Metrics.RecordRunStopped(Summary);
	if (!Metrics.IsAvailable())
		Logger.Log(PM5Tui::ELogLevel::Error, "event=RunMetricsWriteFailed");
	return 0;
}
