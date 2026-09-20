#include "WorkoutRuntime/RealDeviceController.h"

#include "WorkoutRuntime/AppSessionConfig.h"

#include <utility>
#include <variant>

FRealDeviceController::FRealDeviceController(FRealDeviceDependencies InDependencies)
	: Deps(std::move(InDependencies)),
	  // A build that must never touch the transport (the Editor) also leaves the real
	  // journal alone: the recovery scan writes to it and could end a live row of a
	  // packaged app running at the same time.
	  Recovery(Deps.bTransportAvailable ? RecoverInterruptedSession(Deps.AppDataDirectory / "journal") : FAppJournalRecovery{}),
	  Connector(Deps.MakeDiscovery)
{
}

FRealDeviceController::~FRealDeviceController()
{
	// Never finalize durable workout state from a destructor. A process that exits
	// without the explicit application-shutdown path must be recoverable on relaunch.
	Connector.Stop();
}

std::uint64_t FRealDeviceController::GetPanelGeneration() const noexcept
{
	return Connector.GetGeneration() + LocalGeneration;
}

IJournalSink &FRealDeviceController::ActiveSink()
{
	return Journal ? Journal->GetSink() : static_cast<IJournalSink &>(NoOpSink);
}

bool FRealDeviceController::StartConnector()
{
	if (!Connector.Start(Deps.Clock()))
		return false;
	++LocalGeneration;
	return true;
}

bool FRealDeviceController::Connect()
{
	bTransportUnavailable = false;
	bJournalDecisionPending = false;
	JournalError.clear();
	if (Connector.GetPhase() != EDeviceConnectPhase::Idle)
		return false;
	if (!Deps.bTransportAvailable)
	{
		bTransportUnavailable = true;
		++LocalGeneration;
		return false;
	}
	if (!Journal)
	{
		FAppJournal::FOpenResult Opened = FAppJournal::Open(Deps.AppDataDirectory / "journal", Deps.MakeCipher);
		if (!Opened.Journal)
		{
			JournalError = std::move(Opened.Error);
			bJournalDecisionPending = true;
			++LocalGeneration;
			return false;
		}
		Journal = std::move(Opened.Journal);
	}
	JournalStatus = EAppJournalStatus::Saving;
	return StartConnector();
}

bool FRealDeviceController::ConfirmRowWithoutSaving()
{
	if (!bJournalDecisionPending)
		return false;
	bJournalDecisionPending = false;
	JournalStatus = EAppJournalStatus::NotSaving;
	++LocalGeneration;
	return StartConnector();
}

void FRealDeviceController::CancelConnect()
{
	if (bTransportUnavailable)
	{
		bTransportUnavailable = false;
		++LocalGeneration;
		return;
	}
	if (bJournalDecisionPending)
	{
		bJournalDecisionPending = false;
		JournalError.clear();
		++LocalGeneration;
		return;
	}
	EndAndDropSession();
	Connector.Stop();
	++LocalGeneration;
}

bool FRealDeviceController::ScanForDevices()
{
	// Retry from a problem that never got as far as a discovery starts over.
	if (Connector.GetPhase() == EDeviceConnectPhase::Idle)
		return Connect();
	EndAndDropSession();
	return Connector.StartScan(Deps.Clock());
}

bool FRealDeviceController::SelectDevice(std::size_t Index)
{
	if (!Connector.Select(Index))
		return false;
	Pump();
	return true;
}

bool FRealDeviceController::SelectDeviceByToken(std::uint64_t Token)
{
	if (!Connector.SelectByToken(Token))
		return false;
	Pump();
	return true;
}

void FRealDeviceController::ForgetDevice()
{
	if (Connector.GetPhase() == EDeviceConnectPhase::Idle)
		return;
	EndAndDropSession();
	Connector.Forget();
}

void FRealDeviceController::CreateSession()
{
	FWorkoutSessionDependencies Dependencies;
	Dependencies.Machine = Connector.GetMachine();
	Dependencies.Sink = &ActiveSink();
	Dependencies.UnixTimeMs = Deps.UnixTimeMs;
	Dependencies.RandomByte = Deps.RandomByte;
	Session = std::make_unique<FWorkoutSession>(std::move(Dependencies), MakeAppSessionConfig(true));
	LastSessionState = ERowingSessionState::Created;
	Session->Tick(Deps.Clock());
	++LocalGeneration;
}

void FRealDeviceController::EndAndDropSession()
{
	if (!Session)
		return;
	const FRowingSessionId SessionId = Session->GetSnapshot().SessionId;
	if (IRowingMachine *Machine = Connector.GetMachine())
	{
		// Bring the session up to date so the final snapshot has everything the device delivered.
		FRowingMachineEvent Event;
		while (Machine->TryPollEvent(Event))
			Session->Ingest(Event);
	}
	Session->End(Deps.Clock());
	FlushLatencyForSession(SessionId);
	Session.reset();
	++LocalGeneration;
}

bool FRealDeviceController::EndSession()
{
	if (!Session)
		return false;
	const FRowingSessionId SessionId = Session->GetSnapshot().SessionId;
	Pump();
	const bool bEnded = Session->End(Deps.Clock());
	ArmRowStopIfRowEnded();
	FlushLatencyForSession(SessionId);
	++LocalGeneration;
	return bEnded;
}

void FRealDeviceController::ShutdownGracefully()
{
	EndAndDropSession();
	Connector.Stop();
}

bool FRealDeviceController::StartNewSession()
{
	if (!Session || Session->GetSnapshot().State != ERowingSessionState::Ended || !Connector.GetMachine())
		return false;
	Session.reset();
	CreateSession();
	// The adapter announces the machine once per connection, and the ended session
	// consumed it: hand the new session the same capability record.
	if (LastMachineInfoEvent)
		Session->Ingest(*LastMachineInfoEvent);
	return true;
}

void FRealDeviceController::ArmRowStopIfRowEnded()
{
	const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
	// Edge-triggered: only the transition into Ended arms, so a row that already stopped
	// before the user ended it does not hold back the next one.
	if (Snapshot.State == ERowingSessionState::Ended && LastSessionState != ERowingSessionState::Ended && bDeviceRowing)
		bAwaitingRowStop = true;
	LastSessionState = Snapshot.State;
}

void FRealDeviceController::Pump()
{
	const std::uint64_t NowNs = Deps.Clock();
	Connector.Tick(NowNs);
	if (Connector.GetMachineGeneration() != SeenMachineGeneration)
	{
		SeenMachineGeneration = Connector.GetMachineGeneration();
		LastMachineInfoEvent.reset();
		bAwaitingRowStop = false;
		bDeviceRowing = false;
		// A machine change the connector made on its own (the remembered PM5 was
		// adopted) replaces whatever session there was.
		EndAndDropSession();
		if (Connector.GetMachine())
			CreateSession();
	}
	IRowingMachine *Machine = Connector.GetMachine();
	if (!Session || !Machine)
		return;
	// The only poller: the session never polls (bPollMachine is false).
	FRowingMachineEvent Event;
	while (Machine->TryPollEvent(Event))
	{
		if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
		{
			// After an End mid-row the PM5 keeps reporting that row's cumulative values;
			// only a sample showing the row has stopped may reach the next session.
			bDeviceRowing = Sample->WorkoutState == ERowingWorkoutState::Active && Sample->RowingState != ERowingState::Inactive;
			if (bAwaitingRowStop)
			{
				if (bDeviceRowing)
					continue;
				bAwaitingRowStop = false;
			}
			if (PendingSampleTimestampNs == 0)
				PendingSampleTimestampNs = Event.MonotonicTimestampNs;
		}
		else if (std::holds_alternative<FRowingMachineInfo>(Event.Payload))
			LastMachineInfoEvent = Event;
		Session->Ingest(Event);
	}
	Session->Tick(NowNs);
	ArmRowStopIfRowEnded();
}

ERealDeviceMode FRealDeviceController::GetMode() const
{
	if (bTransportUnavailable)
		return ERealDeviceMode::Problem;
	if (bJournalDecisionPending)
		return ERealDeviceMode::JournalDecision;
	switch (Connector.GetPhase())
	{
	case EDeviceConnectPhase::Idle:
		// A discovery that could not even be created leaves the phase Idle with a problem.
		return Connector.GetProblem() != EDeviceProblem::None ? ERealDeviceMode::Problem : ERealDeviceMode::Idle;
	case EDeviceConnectPhase::Starting:
		return Connector.GetProblem() != EDeviceProblem::None ? ERealDeviceMode::Problem : ERealDeviceMode::Starting;
	case EDeviceConnectPhase::Scanning:
		return Connector.GetProblem() != EDeviceProblem::None ? ERealDeviceMode::Problem : ERealDeviceMode::Scanning;
	case EDeviceConnectPhase::Attached:
		return ERealDeviceMode::Attached;
	}
	return ERealDeviceMode::Idle;
}

std::vector<std::string> FRealDeviceController::GetCandidateLabels() const
{
	std::vector<std::string> Labels;
	if (Connector.GetPhase() == EDeviceConnectPhase::Scanning)
	{
		for (const FDeviceCandidate &Candidate : Connector.GetCandidates())
			Labels.push_back(Candidate.Label);
	}
	return Labels;
}

std::vector<std::uint64_t> FRealDeviceController::GetCandidateTokens() const
{
	std::vector<std::uint64_t> Tokens;
	if (Connector.GetPhase() == EDeviceConnectPhase::Scanning)
	{
		for (const FDeviceCandidate &Candidate : Connector.GetCandidates())
			Tokens.push_back(Candidate.Token);
	}
	return Tokens;
}

const FWorkoutSnapshot *FRealDeviceController::GetSnapshot() const
{
	return Session ? &Session->GetSnapshot() : nullptr;
}

void FRealDeviceController::NoteDisplayApplied(std::uint64_t NowNs)
{
	if (PendingSampleTimestampNs != 0 && NowNs >= PendingSampleTimestampNs)
		Latency.Record(NowNs - PendingSampleTimestampNs);
	PendingSampleTimestampNs = 0;
}

std::string FRealDeviceController::FlushLatency()
{
	if (!Session)
		return {};
	return FlushLatencyForSession(Session->GetSnapshot().SessionId);
}

std::string FRealDeviceController::FlushLatencyForSession(const FRowingSessionId &SessionId)
{
	if (Latency.GetCount() == 0)
		return {};
	if (Journal)
	{
		LocalData::FSessionLatencySummary Summary;
		Summary.Id = SessionId;
		Summary.SourceRevision = Deps.SourceRevision;
		Summary.SampleCount = Latency.GetCount();
		Summary.RetainedCount = Latency.GetRetainedCount();
		Summary.DroppedCount = Latency.GetDroppedCount();
		Summary.P50Ns = Latency.GetPercentileNs(50.0);
		Summary.P95Ns = Latency.GetPercentileNs(95.0);
		Summary.P99Ns = Latency.GetPercentileNs(99.0);
		Summary.MaxNs = Latency.GetMaxNs();
		try
		{
			Journal->RecordSessionLatencySummary(Summary);
		}
		catch (const std::exception &Failure)
		{
			return Failure.what();
		}
	}
	const std::string Name = "hud-latency-" + std::to_string(Deps.UnixTimeMs ? Deps.UnixTimeMs() : 0) + ".json";
	std::string Error = WriteOwnerOnlyFile(Deps.AppDataDirectory / "metrics", Name, Latency.ToJson(Deps.SourceRevision));
	// Once the journal aggregate is durable, a metrics-file failure must not cause
	// a retry to collide with the immutable per-session row.
	if (Error.empty() || Journal)
		Latency.Reset();
	return Error;
}
