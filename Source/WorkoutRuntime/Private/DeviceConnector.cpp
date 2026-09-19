#include "WorkoutRuntime/DeviceConnector.h"

#include <algorithm>
#include <utility>
#include <variant>

FDeviceConnector::FDeviceConnector(FDiscoveryFactory InFactory, FDeviceConnectorConfig InConfig)
	: Factory(std::move(InFactory)),
	  Config(InConfig)
{
}

FDeviceConnector::~FDeviceConnector()
{
	Stop();
}

std::string FDeviceConnector::MakeCandidateLabel(std::size_t OneBasedIndex, const std::optional<std::int32_t> &SignalStrengthDbm)
{
	std::string Label = "PM5 #" + std::to_string(OneBasedIndex);
	if (SignalStrengthDbm)
		Label += " (" + std::to_string(*SignalStrengthDbm) + " dBm)";
	return Label;
}

void FDeviceConnector::SetPhase(EDeviceConnectPhase NewPhase)
{
	if (Phase != NewPhase)
	{
		Phase = NewPhase;
		++Generation;
	}
}

void FDeviceConnector::SetProblem(EDeviceProblem NewProblem)
{
	if (Problem != NewProblem)
	{
		Problem = NewProblem;
		++Generation;
	}
}

bool FDeviceConnector::Start(std::uint64_t NowNs)
{
	if (Phase != EDeviceConnectPhase::Idle || !Factory)
		return false;
	Discovery = Factory();
	if (!Discovery)
	{
		SetProblem(EDeviceProblem::DiscoveryFailed);
		return false;
	}
	Candidates.clear();
	StartedNs = NowNs;
	SetProblem(EDeviceProblem::None);
	SetPhase(EDeviceConnectPhase::Starting);
	return true;
}

void FDeviceConnector::Tick(std::uint64_t NowNs)
{
	if (!Discovery || Phase == EDeviceConnectPhase::Attached)
		return;

	// The remembered-machine reconnect is the adapter's own; adopt it as soon as it
	// exists, before and after draining so a fault in the same batch cannot hide it.
	auto TryAdopt = [this]
	{
		// Only the start-up window: the adapter begins its remembered-machine reconnect
		// when the transport becomes ready, and asking costs a hop onto its queue.
		if (Machine || Phase != EDeviceConnectPhase::Starting)
			return;
		if (std::unique_ptr<IRowingMachine> Adopted = Discovery->TryTakeRelaunchMachine())
		{
			Machine = std::move(Adopted);
			++MachineGeneration;
			Candidates.clear();
			SetProblem(EDeviceProblem::None);
			SetPhase(EDeviceConnectPhase::Attached);
		}
	};

	TryAdopt();
	FRowingMachineEvent Event;
	while (Phase != EDeviceConnectPhase::Attached && Discovery->TryPollDiscoveryEvent(Event))
		HandleDiscoveryEvent(Event);
	TryAdopt();

	if (Phase == EDeviceConnectPhase::Starting && Problem == EDeviceProblem::None && NowNs >= StartedNs && NowNs - StartedNs >= Config.RememberedGraceNs)
		BeginScan();
}

void FDeviceConnector::HandleDiscoveryEvent(const FRowingMachineEvent &Event)
{
	if (const auto *Descriptor = std::get_if<FRowingMachineDescriptor>(&Event.Payload))
	{
		if (Phase != EDeviceConnectPhase::Scanning)
			return;
		const auto Existing = std::find_if(Candidates.begin(), Candidates.end(), [&](const FDeviceCandidate &Candidate)
										   { return Candidate.Descriptor.Id == Descriptor->Id; });
		if (Existing != Candidates.end())
			Existing->Descriptor = *Descriptor;
		else
			Candidates.push_back({*Descriptor, NextCandidateToken++, {}});
		RebuildCandidateLabels();
		SetProblem(EDeviceProblem::None);
		return;
	}

	if (const auto *State = std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
	{
		if (State->NewState == ERowingConnectionState::PermissionDenied)
			SetProblem(EDeviceProblem::BluetoothPermission);
		return;
	}

	const auto *Fault = std::get_if<FRowingFault>(&Event.Payload);
	if (!Fault)
		return;
	switch (Fault->Code)
	{
	case ERowingFaultCode::Permission:
		SetProblem(Fault->ConnectionState == ERowingConnectionState::PermissionDenied ? EDeviceProblem::BluetoothPermission : EDeviceProblem::BluetoothNotReady);
		break;
	case ERowingFaultCode::ScanTimeout:
		if (Candidates.empty())
			SetProblem(EDeviceProblem::NoDeviceFound);
		break;
	case ERowingFaultCode::ConnectionTimeout:
		// The adapter could not retrieve the remembered machine: scan instead.
		if (Phase == EDeviceConnectPhase::Starting)
			BeginScan();
		break;
	default:
		if (Fault->Severity == ERowingFaultSeverity::Terminal)
			SetProblem(EDeviceProblem::DiscoveryFailed);
		break;
	}
}

void FDeviceConnector::RebuildCandidateLabels()
{
	// Nearest first: strongest signal first, unknown strength last, ties keep the
	// order they were discovered in.
	std::stable_sort(Candidates.begin(), Candidates.end(), [](const FDeviceCandidate &A, const FDeviceCandidate &B)
					 {
		const std::optional<std::int32_t> &SA = A.Descriptor.SignalStrengthDbm;
		const std::optional<std::int32_t> &SB = B.Descriptor.SignalStrengthDbm;
		if (SA.has_value() != SB.has_value())
			return SA.has_value();
		return SA && SB && *SA > *SB; });
	if (Candidates.size() > Config.MaxCandidates)
		Candidates.resize(Config.MaxCandidates);
	for (std::size_t Index = 0; Index < Candidates.size(); ++Index)
		Candidates[Index].Label = MakeCandidateLabel(Index + 1, Candidates[Index].Descriptor.SignalStrengthDbm);
	++Generation;
}

void FDeviceConnector::BeginScan()
{
	Candidates.clear();
	SetProblem(EDeviceProblem::None);
	Discovery->StartScan();
	SetPhase(EDeviceConnectPhase::Scanning);
	++Generation;
}

bool FDeviceConnector::StartScan(std::uint64_t NowNs)
{
	(void)NowNs;
	if (!Discovery)
		return false;
	DropMachine();
	BeginScan();
	return true;
}

bool FDeviceConnector::SelectByToken(std::uint64_t Token)
{
	const auto Found = std::find_if(Candidates.begin(), Candidates.end(), [&](const FDeviceCandidate &Candidate)
									{ return Candidate.Token == Token; });
	return Found != Candidates.end() && Select(static_cast<std::size_t>(Found - Candidates.begin()));
}

bool FDeviceConnector::Select(std::size_t Index)
{
	if (!Discovery || Phase != EDeviceConnectPhase::Scanning || Index >= Candidates.size())
		return false;
	Discovery->StopScan();
	std::unique_ptr<IRowingMachine> Created = Discovery->CreateMachine(Candidates[Index].Descriptor.Id);
	if (!Created)
	{
		SetProblem(EDeviceProblem::DiscoveryFailed);
		return false;
	}
	Machine = std::move(Created);
	++MachineGeneration;
	Candidates.clear();
	SetProblem(EDeviceProblem::None);
	SetPhase(EDeviceConnectPhase::Attached);
	Machine->Connect();
	return true;
}

void FDeviceConnector::DropMachine()
{
	if (!Machine)
		return;
	Machine->Disconnect();
	Machine.reset();
	++MachineGeneration;
	++Generation;
}

void FDeviceConnector::Forget()
{
	if (!Discovery)
		return;
	DropMachine();
	Discovery->ForgetRememberedMachine();
	BeginScan();
}

void FDeviceConnector::Stop()
{
	DropMachine();
	Discovery.reset();
	Candidates.clear();
	SetProblem(EDeviceProblem::None);
	SetPhase(EDeviceConnectPhase::Idle);
}
