#include "WorkoutSubsystem.h"

#include "WorkoutDevicePanelWidget.h"
#include "WorkoutHudWidget.h"
#include "CourseSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "RowingDevice/IRowingMachine.h"
#include "WorkoutRuntime/AppSessionConfig.h"
#include "WorkoutRuntime/JournalSink.h"
#include "WorkoutRuntime/NoOpJournalSink.h"
#include "WorkoutRuntime/RealDeviceController.h"
#include "WorkoutRuntime/WorkoutDisplay.h"
#include "WorkoutRuntime/WorkoutSession.h"
#include "RowingSim/ReplayRowingMachine.h"
#include "RowingSim/TelemetryFixtures.h"

#if PLATFORM_MAC
#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "LocalDataMac/CryptoKitBlobCipher.h"
#endif

#include <chrono>
#include <memory>
#include <random>

DEFINE_LOG_CATEGORY_STATIC(LogWorkoutSubsystem, Log, All);

namespace
{
	uint64 SteadyClockNs()
	{
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// The real device's Keychain item and journal live under the app's own identity,
	// separate from the diagnostic TUI's.
	constexpr const char *KeychainService = "dev.virtualrowing.app";
	constexpr const char *KeychainAccount = "workout-journal-data-key";
	// Under ~/Library/Application Support (not FPlatformProcess::UserSettingsDir(), which
	// on Apple platforms is the engine-shared ~/Library/Application Support/Epic/).
	constexpr const TCHAR *AppDataFolderName = TEXT("dev.virtualrowing.app");

	bool IsBluetoothTransportAvailable()
	{
#if PLATFORM_MAC && !WITH_EDITOR
		return true;
#else
		return false;
#endif
	}

	FDeviceConnector::FDiscoveryFactory MakeDefaultDiscoveryFactory()
	{
#if PLATFORM_MAC
		return []() -> std::unique_ptr<IRememberingMachineDiscovery>
		{ return CreateConcept2PMDiscovery(); };
#else
		return []() -> std::unique_ptr<IRememberingMachineDiscovery>
		{ return nullptr; };
#endif
	}

	FAppJournal::FCipherFactory MakeDefaultCipherFactory()
	{
#if PLATFORM_MAC
		return []() -> std::unique_ptr<LocalData::IBlobCipher>
		{
			LocalDataMac::FDataKey Key = LocalDataMac::LoadOrCreateKeychainDataKey(KeychainService, KeychainAccount);
			std::unique_ptr<LocalData::IBlobCipher> Cipher = std::make_unique<LocalDataMac::FCryptoKitBlobCipher>(Key);
			Key.fill(0);
			return Cipher;
		};
#else
		return []() -> std::unique_ptr<LocalData::IBlobCipher>
		{ return nullptr; };
#endif
	}

	struct FFixtureEntry
	{
		const TCHAR *Name;
		RowingSim::FGoldenTelemetryFixture (*Make)();
	};

	RowingSim::FGoldenTelemetryFixture MakeNoRowing()
	{
		return RowingSim::MakeNoRowingFixture();
	}
	RowingSim::FGoldenTelemetryFixture MakeDeviceCompleted()
	{
		return RowingSim::MakeDeviceCompletedFixture();
	}
	RowingSim::FGoldenTelemetryFixture MakeDeviceTerminated()
	{
		return RowingSim::MakeDeviceTerminatedFixture();
	}

	RowingSim::FGoldenTelemetryFixture MakeRandom10Minutes()
	{
		return RowingSim::MakeRandomStrokeFixture(10);
	}
	RowingSim::FGoldenTelemetryFixture MakeRandom30Minutes()
	{
		return RowingSim::MakeRandomStrokeFixture(30);
	}
	RowingSim::FGoldenTelemetryFixture MakeStateMissing()
	{
		RowingSim::FGoldenTelemetryFixture Fixture = RowingSim::MakeRandomStrokeFixture(10);
		Fixture.Name = "state_missing";
		for (RowingSim::FReplayTelemetryFrame &Frame : Fixture.Frames)
			Frame.Sample.StrokeState = ERowingStrokeState::Unknown;
		return Fixture;
	}

	// Varying stroke rate, power, and pace, so the HUD visibly tracks live data;
	// steady30min is a constant-speed script whose pace and rate never change.
	// The simulator's virtual time never advances by more than this per Pump. After a
	// pause, a debugger break, or a stall, catching up in one step would publish
	// every missed frame at once and overflow the machine's bounded event queue
	// (a terminal fault); instead the stall is skipped and the fixture continues.
	constexpr uint64 MaxSimStepNs = 1000000000ULL;

	constexpr const TCHAR *DefaultFixtureName = TEXT("random30min");

	const FFixtureEntry FixtureTable[] = {
		{TEXT("easy30s"), &RowingSim::MakeEasy30SecondFixture},
		{TEXT("sprint500m"), &RowingSim::Make500mSprintFixture},
		{TEXT("race2000m"), &RowingSim::Make2000mRaceFixture},
		{DefaultFixtureName, &MakeRandom30Minutes},
		{TEXT("random10min"), &MakeRandom10Minutes},
		{TEXT("steady30min"), &RowingSim::Make30MinuteSteadyStateFixture},
		{TEXT("intervals"), &RowingSim::MakeIntervalFixture},
		{TEXT("abrupt_stop"), &RowingSim::MakeAbruptStopFixture},
		{TEXT("packet_loss"), &RowingSim::MakePacketLossFixture},
		{TEXT("state_missing"), &MakeStateMissing},
		{TEXT("no_rowing"), &MakeNoRowing},
		{TEXT("device_completed"), &MakeDeviceCompleted},
		{TEXT("device_terminated"), &MakeDeviceTerminated},
	};

	const FFixtureEntry *FindFixture(const FString &Name)
	{
		for (const FFixtureEntry &Entry : FixtureTable)
		{
			if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
				return &Entry;
		}
		return nullptr;
	}
} // namespace

struct UWorkoutSubsystem::FImpl
{
	FMonotonicClock Clock = &SteadyClockNs;
	// Simulator runs journal nothing: no database and no Keychain item.
	FNoOpJournalSink Sink;
	const FFixtureEntry *Fixture = nullptr;

	// Real device (Milestone 7). Null in a simulator run; created lazily and never
	// touches Bluetooth or the Keychain until the user connects.
	bool bSimulatorRequested = false;
	bool bHasTestDependencies = false;
	FDeviceConnector::FDiscoveryFactory TestDiscovery;
	FAppJournal::FCipherFactory TestCipher;
	std::filesystem::path TestAppDataDirectory;
	std::unique_ptr<FRealDeviceController> Real;

	// Owned by the subsystem, not the session: the session only consumes events.
	std::unique_ptr<RowingSim::FReplayRowingMachine> Machine;
	std::unique_ptr<FWorkoutSession> Session;
	// The simulator's virtual time and the session's Tick timebase are the same
	// monotonic value: nanoseconds since the machine was (re)started.
	uint64 OriginNs = 0;
	// Raw clock value at the previous Pump, for bounding the virtual-time step.
	uint64 LastPumpClockNs = 0;

	FWorkoutDisplay Display = MakeNoDeviceDisplay();
	uint64 LastRevision = 0;
	bool bDisplayDirty = false;
	bool bHadSnapshot = false;
	std::string LastSessionKey;
	uint64 DisplayGeneration = 0;

	const FWorkoutSnapshot *ActiveSnapshot() const
	{
		if (Real)
			return Real->GetSnapshot();
		return Session ? &Session->GetSnapshot() : nullptr;
	}

	uint64 SimNow() const
	{
		const uint64 Raw = Clock();
		return Raw > OriginNs ? Raw - OriginNs : 0;
	}

	// Shifts the origin so virtual time advances by at most MaxSimStepNs since the
	// previous call, however much wall time passed. Returns the virtual time.
	uint64 BoundedSimNow()
	{
		const uint64 Raw = Clock();
		if (Raw > LastPumpClockNs && Raw - LastPumpClockNs > MaxSimStepNs)
			OriginNs += (Raw - LastPumpClockNs) - MaxSimStepNs;
		// A clock that moved backwards does not rewind virtual time.
		LastPumpClockNs = Raw > LastPumpClockNs ? Raw : LastPumpClockNs;
		return Raw > OriginNs ? Raw - OriginNs : 0;
	}

	void StartMachineAndSession()
	{
		// The old session points at the old machine; drop it first.
		Session.reset();
		RowingSim::FGoldenTelemetryFixture GoldenFixture = Fixture->Make();
		Machine = std::make_unique<RowingSim::FReplayRowingMachine>(RowingSim::MakeSyntheticIndoorRowerScenario(), std::move(GoldenFixture.Frames));
		Machine->Connect();
		OriginNs = Clock();
		LastPumpClockNs = OriginNs;

		FWorkoutSessionDependencies Dependencies;
		Dependencies.Machine = Machine.get();
		Dependencies.Sink = &Sink;
		Dependencies.UnixTimeMs = []
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		};
		const auto Entropy = std::make_shared<std::random_device>();
		Dependencies.RandomByte = [Entropy]
		{
			return static_cast<std::uint8_t>((*Entropy)());
		};
		Session = std::make_unique<FWorkoutSession>(std::move(Dependencies), MakeAppSessionConfig(false));
		Session->Tick(0);
		bDisplayDirty = true;
		Refresh();
	}

	// Returns whether the display value changed (and so the generation increased).
	bool Refresh()
	{
		const FWorkoutSnapshot *Snapshot = ActiveSnapshot();
		if (!Snapshot)
		{
			// A session that went away (device cancelled or forgotten) returns the display
			// to the no-device state exactly once.
			if (bHadSnapshot)
			{
				bHadSnapshot = false;
				LastSessionKey.clear();
				bDisplayDirty = true;
			}
			if (!bDisplayDirty)
				return false;
			Display = MakeNoDeviceDisplay();
			bDisplayDirty = false;
			++DisplayGeneration;
			return true;
		}
		// A replaced session can reuse a revision number, so identity is compared too.
		const std::string SessionKey = Snapshot->SessionId.ToCanonicalString();
		if (!bHadSnapshot || SessionKey != LastSessionKey)
			bDisplayDirty = true;
		bHadSnapshot = true;
		LastSessionKey = SessionKey;
		if (!bDisplayDirty && Snapshot->Revision == LastRevision)
			return false;
		Display = MakeWorkoutDisplay(*Snapshot);
		LastRevision = Snapshot->Revision;
		bDisplayDirty = false;
		++DisplayGeneration;
		return true;
	}

	FRealDeviceDependencies MakeRealDependencies()
	{
		FRealDeviceDependencies Deps;
		Deps.MakeDiscovery = bHasTestDependencies ? TestDiscovery : MakeDefaultDiscoveryFactory();
		Deps.MakeCipher = bHasTestDependencies ? TestCipher : MakeDefaultCipherFactory();
		Deps.AppDataDirectory = bHasTestDependencies ? TestAppDataDirectory : std::filesystem::path(TCHAR_TO_UTF8(*FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support"), AppDataFolderName)));
		Deps.Clock = [this]
		{ return Clock(); };
		Deps.UnixTimeMs = []
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		};
		const auto Entropy = std::make_shared<std::random_device>();
		Deps.RandomByte = [Entropy]
		{
			return static_cast<std::uint8_t>((*Entropy)());
		};
		Deps.SourceRevision = TCHAR_TO_UTF8(VIR_SOURCE_REVISION);
		// The Editor has no Bluetooth usage description: touching CoreBluetooth there
		// would terminate it, so the real-device flow stops before it starts. Tests
		// script their own transport.
		Deps.bTransportAvailable = bHasTestDependencies || IsBluetoothTransportAvailable();
		return Deps;
	}
};

void UWorkoutSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	Impl = MakeShared<FImpl>();

	const TCHAR *CommandLine = FCommandLine::Get();
	FString Fixture;
	const bool bValue = FParse::Value(CommandLine, TEXT("SimulatorDevice="), Fixture);
	if (bValue || FParse::Param(CommandLine, TEXT("SimulatorDevice")))
	{
		// Even a misspelled fixture means the simulator was asked for: never offer the
		// real-device flow instead.
		Impl->bSimulatorRequested = true;
		if (!StartSimulator(Fixture))
		{
			UE_LOG(LogWorkoutSubsystem, Warning, TEXT("Unknown -SimulatorDevice fixture '%s'; known fixtures: %s. Showing the no-device state."), *Fixture, *FString::Join(GetSimulatorFixtureNames(), TEXT(", ")));
		}
	}
	else
	{
		// Creating the controller starts nothing: Bluetooth and the Keychain wait for
		// the user's Connect. It only looks for an interrupted session to report.
		EnsureRealController();
	}
}

void UWorkoutSubsystem::Deinitialize()
{
	if (Hud)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
	}
	if (DevicePanel)
	{
		DevicePanel->RemoveFromParent();
		DevicePanel = nullptr;
	}
	if (Impl)
	{
		// Ends an active row cleanly (journal flushed), then disconnects.
		Impl->Real.reset();
	}
	Impl.Reset();
	Super::Deinitialize();
}

bool UWorkoutSubsystem::IsTickable() const
{
	return !IsTemplate(RF_ClassDefaultObject) && Impl.IsValid();
}

TStatId UWorkoutSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorkoutSubsystem, STATGROUP_Tickables);
}

void UWorkoutSubsystem::Tick(float DeltaTime)
{
	Pump();
	EnsureHud();
	EnsureDevicePanel();
	// Driven from here, not only from the widget's own tick: the panel must appear even if
	// Slate never ticks it.
	if (DevicePanel)
		DevicePanel->Sync();
}

TArray<FString> UWorkoutSubsystem::GetSimulatorFixtureNames()
{
	TArray<FString> Names;
	for (const FFixtureEntry &Entry : FixtureTable)
		Names.Add(Entry.Name);
	return Names;
}

bool UWorkoutSubsystem::StartSimulator(const FString &FixtureName)
{
	if (!Impl)
		Impl = MakeShared<FImpl>();
	const FFixtureEntry *Fixture = FindFixture(FixtureName.IsEmpty() ? FString(DefaultFixtureName) : FixtureName);
	if (!Fixture)
		return false;
	// The simulator and the real device never coexist.
	Impl->Real.reset();
	Impl->bSimulatorRequested = true;
	Impl->Fixture = Fixture;
	Impl->StartMachineAndSession();
	return true;
}

bool UWorkoutSubsystem::HasDevice() const
{
	if (Impl && Impl->Real)
		return Impl->Real->GetMode() == ERealDeviceMode::Attached;
	return Impl && Impl->Machine != nullptr;
}

bool UWorkoutSubsystem::HasSession() const
{
	if (Impl && Impl->Real)
		return Impl->Real->HasSession();
	return Impl && Impl->Session != nullptr;
}

const FWorkoutDisplay &UWorkoutSubsystem::GetDisplay() const
{
	static const FWorkoutDisplay NoDevice = MakeNoDeviceDisplay();
	return Impl ? Impl->Display : NoDevice;
}

const FWorkoutSnapshot *UWorkoutSubsystem::GetSnapshot() const
{
	return Impl ? Impl->ActiveSnapshot() : nullptr;
}

uint64 UWorkoutSubsystem::GetDisplayGeneration() const
{
	return Impl ? Impl->DisplayGeneration : 0;
}

bool UWorkoutSubsystem::EndSession()
{
	if (Impl && Impl->Real)
	{
		const bool bEnded = Impl->Real->EndSession();
		Impl->Refresh();
		return bEnded;
	}
	if (!Impl || !Impl->Session)
		return false;
	// Bring the session up to date first so the final snapshot includes everything
	// the device already delivered.
	Pump();
	const bool bEnded = Impl->Session->End(Impl->SimNow());
	Impl->Refresh();
	return bEnded;
}

bool UWorkoutSubsystem::StartNewSession()
{
	if (Impl && Impl->Real)
	{
		const bool bStarted = Impl->Real->StartNewSession();
		Impl->Refresh();
		return bStarted;
	}
	if (!Impl || !Impl->Fixture || !Impl->Session || Impl->Session->GetSnapshot().State != ERowingSessionState::Ended)
		return false;
	Impl->StartMachineAndSession();
	return true;
}

void UWorkoutSubsystem::Pump()
{
	if (Impl && Impl->Real)
	{
		Impl->Real->Pump();
		// Samples that did not change the display are not carried into the next
		// measurement, which would inflate it.
		if (!Impl->Refresh())
			Impl->Real->DiscardPendingLatency();
		return;
	}
	if (!Impl || !Impl->Machine || !Impl->Session)
		return;
	const uint64 NowNs = Impl->BoundedSimNow();
	if (!Impl->Machine->AdvanceTo(NowNs))
		UE_LOG(LogWorkoutSubsystem, Warning, TEXT("The simulator machine rejected an advance to %llu ns; it has failed and will not recover."), NowNs);
	// The subsystem, not the session, drains the machine: an Ended session's Tick
	// returns before draining, so a session-owned drain would let the bounded queue
	// fill between sessions and mark the next one degraded.
	FRowingMachineEvent Event;
	while (Impl->Machine->TryPollEvent(Event))
		Impl->Session->Ingest(Event);
	Impl->Session->Tick(NowNs);
	Impl->Refresh();
}

void UWorkoutSubsystem::SetClockForTesting(FMonotonicClock InClock)
{
	if (!Impl)
		Impl = MakeShared<FImpl>();
	// Keep virtual time continuous across a clock swap on a running simulator: the
	// machine has already advanced to the old SimNow.
	const uint64 VirtualNow = Impl->Machine ? Impl->SimNow() : 0;
	Impl->Clock = MoveTemp(InClock);
	const uint64 Raw = Impl->Clock();
	Impl->OriginNs = Raw > VirtualNow ? Raw - VirtualNow : 0;
	Impl->LastPumpClockNs = Raw;
}

int32 UWorkoutSubsystem::GetPendingMachineEventCountForTesting() const
{
	return Impl && Impl->Machine ? static_cast<int32>(Impl->Machine->GetMockMachine().GetQueuedEventCount()) : 0;
}

void UWorkoutSubsystem::EnsureRealController()
{
	if (!Impl)
		Impl = MakeShared<FImpl>();
	if (Impl->Real || Impl->Fixture || Impl->bSimulatorRequested)
		return;
	Impl->Real = std::make_unique<FRealDeviceController>(Impl->MakeRealDependencies());
	Impl->bDisplayDirty = true;
	Impl->Refresh();
}

void UWorkoutSubsystem::SetRealDeviceDependenciesForTesting(FDeviceConnector::FDiscoveryFactory InDiscovery, FAppJournal::FCipherFactory InCipher, const FString &InAppDataDirectory)
{
	if (!Impl)
		Impl = MakeShared<FImpl>();
	Impl->Real.reset();
	Impl->bHasTestDependencies = true;
	Impl->TestDiscovery = MoveTemp(InDiscovery);
	Impl->TestCipher = MoveTemp(InCipher);
	Impl->TestAppDataDirectory = std::filesystem::path(TCHAR_TO_UTF8(*InAppDataDirectory));
	EnsureRealController();
}

bool UWorkoutSubsystem::BeginConnect()
{
	EnsureRealController();
	return Impl && Impl->Real && Impl->Real->Connect();
}

bool UWorkoutSubsystem::ConfirmRowWithoutSaving()
{
	return Impl && Impl->Real && Impl->Real->ConfirmRowWithoutSaving();
}

void UWorkoutSubsystem::CancelConnect()
{
	if (Impl && Impl->Real)
	{
		Impl->Real->CancelConnect();
		Impl->Refresh();
	}
}

bool UWorkoutSubsystem::ScanForDevices()
{
	if (!Impl || !Impl->Real)
		return false;
	const bool bStarted = Impl->Real->ScanForDevices();
	Impl->Refresh();
	return bStarted;
}

bool UWorkoutSubsystem::SelectDeviceByToken(uint64 Token)
{
	if (!Impl || !Impl->Real)
		return false;
	const bool bSelected = Impl->Real->SelectDeviceByToken(Token);
	Impl->Refresh();
	return bSelected;
}

void UWorkoutSubsystem::ForgetDevice()
{
	if (Impl && Impl->Real)
	{
		Impl->Real->ForgetDevice();
		Impl->Refresh();
	}
}

void UWorkoutSubsystem::NoteDisplayApplied(uint64 Generation)
{
	if (Impl && Impl->Real && Generation == Impl->DisplayGeneration)
		Impl->Real->NoteDisplayApplied(Impl->Clock());
}

uint64 UWorkoutSubsystem::GetDevicePanelGeneration() const
{
	if (!Impl || !Impl->Real)
		return 0;
	// Only the journal-health bit of the snapshot is panel-visible; folding in the whole
	// display generation would rebuild the panel on every accepted sample.
	const FWorkoutSnapshot *Snapshot = Impl->Real->GetSnapshot();
	const uint64 JournalUnhealthy = Snapshot && !Snapshot->bJournalHealthy ? 1 : 0;
	return Impl->Real->GetPanelGeneration() * 2 + JournalUnhealthy;
}

FWorkoutDevicePanel UWorkoutSubsystem::GetDevicePanel() const
{
	FWorkoutDevicePanel Panel;
	if (!Impl || !Impl->Real)
		return Panel;
	const FRealDeviceController &Real = *Impl->Real;
	Panel.bRecoveredNotice = Real.WasInterruptedSessionRecovered();

	switch (Real.GetJournalStatus())
	{
	case EAppJournalStatus::Saving:
		Panel.JournalLine = TEXT("Saving this row to the encrypted local journal.");
		break;
	case EAppJournalStatus::NotSaving:
		Panel.JournalLine = TEXT("NOT BEING SAVED: this row will not be recorded.");
		Panel.bJournalNotSaved = true;
		break;
	case EAppJournalStatus::NotStarted:
		break;
	}
	if (const FWorkoutSnapshot *Snapshot = Real.GetSnapshot(); Snapshot && !Snapshot->bJournalHealthy)
	{
		Panel.JournalLine = TEXT("JOURNAL ERRORS: some of this row may not be saved.");
		Panel.bJournalNotSaved = true;
	}

	switch (Real.GetMode())
	{
	case ERealDeviceMode::Idle:
		Panel.Mode = EWorkoutDevicePanelMode::Idle;
		Panel.Message = Panel.bRecoveredNotice ? TEXT("Connect your Concept2 PM5 to start rowing.\nAn interrupted session from the last run was recovered.") : TEXT("Connect your Concept2 PM5 to start rowing.");
		break;
	case ERealDeviceMode::JournalDecision:
		Panel.Mode = EWorkoutDevicePanelMode::JournalDecision;
		Panel.Message = FString::Printf(TEXT("Your workout journal cannot be opened (%s).\nA row now would not be saved."), *FString(UTF8_TO_TCHAR(Real.GetJournalError().c_str())));
		break;
	case ERealDeviceMode::Starting:
		Panel.Mode = EWorkoutDevicePanelMode::Starting;
		Panel.Message = TEXT("Starting Bluetooth and looking for your last PM5...");
		break;
	case ERealDeviceMode::Scanning:
		Panel.Mode = EWorkoutDevicePanelMode::Scanning;
		for (const std::string &Label : Real.GetCandidateLabels())
			Panel.Candidates.Add(UTF8_TO_TCHAR(Label.c_str()));
		for (const std::uint64_t Token : Real.GetCandidateTokens())
			Panel.CandidateTokens.Add(Token);
		Panel.Message = Panel.Candidates.IsEmpty() ? TEXT("Searching for PM5s. Turn on the PM5 and open its Connect screen.") : TEXT("Choose your PM5 (nearest first).");
		break;
	case ERealDeviceMode::Problem:
		Panel.Mode = EWorkoutDevicePanelMode::Problem;
		switch (Real.GetProblem())
		{
		case EDeviceProblem::BluetoothPermission:
			Panel.Message = TEXT("Bluetooth access is denied for Virtual Rowing.\nAllow it in System Settings > Privacy & Security > Bluetooth, then retry.");
			break;
		case EDeviceProblem::BluetoothNotReady:
			Panel.Message = TEXT("Bluetooth is off or not available.\nTurn it on, then retry.");
			break;
		case EDeviceProblem::TransportUnavailable:
			Panel.Message = TEXT("Bluetooth is not available in the Unreal Editor.\nRun the packaged app to connect a PM5, or launch with -SimulatorDevice.");
			break;
		case EDeviceProblem::NoDeviceFound:
			Panel.Message = TEXT("No PM5 found.\nWake the PM5, open its Connect screen, then scan again.");
			break;
		default:
			Panel.Message = TEXT("Bluetooth discovery failed. Retry.");
			break;
		}
		break;
	case ERealDeviceMode::Attached:
		Panel.Mode = EWorkoutDevicePanelMode::Attached;
		break;
	}
	return Panel;
}

void UWorkoutSubsystem::EnsureDevicePanel()
{
	if (!bPanelEnabled || !Impl || !Impl->Real || (DevicePanel && DevicePanel->IsInViewport()))
		return;
	if (IsRunningCommandlet() || IsRunningDedicatedServer() || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		return;
	if (PanelRetryCountdown > 0)
	{
		--PanelRetryCountdown;
		return;
	}
	UGameInstance *GameInstance = GetGameInstance();
	APlayerController *Controller = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!Controller || !Controller->IsLocalController())
		return;
	if (DevicePanel)
	{
		DevicePanel->RemoveFromParent();
		DevicePanel = nullptr;
	}
	UWorkoutDevicePanelWidget *NewPanel = CreateWidget<UWorkoutDevicePanelWidget>(Controller, UWorkoutDevicePanelWidget::StaticClass());
	if (!NewPanel)
	{
		PanelRetryCountdown = 120;
		return;
	}
	// Above the HUD: while the device flow is up it covers the empty metrics.
	NewPanel->AddToViewport(10);
	if (!NewPanel->IsInViewport())
	{
		NewPanel->RemoveFromParent();
		PanelRetryCountdown = 120;
		return;
	}
	DevicePanel = NewPanel;
	DevicePanel->Sync();
}

void UWorkoutSubsystem::EnsureHud()
{
	if (!bHudEnabled || (Hud && Hud->IsInViewport()))
		return;
	if (IsRunningCommandlet() || IsRunningDedicatedServer() || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		return;
	if (HudRetryCountdown > 0)
	{
		--HudRetryCountdown;
		return;
	}
	UGameInstance *GameInstance = GetGameInstance();
	APlayerController *Controller = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!Controller || !Controller->IsLocalController())
		return;
	if (UWorld *World = Controller->GetWorld())
	{
		if (UCourseSubsystem *Course = World->GetSubsystem<UCourseSubsystem>())
			Course->EnsurePresentation();
	}

	// A previous widget that fell out of the viewport (for example across a world
	// change) is replaced, not orphaned.
	if (Hud)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
	}
	UWorkoutHudWidget *NewHud = CreateWidget<UWorkoutHudWidget>(Controller, UWorkoutHudWidget::StaticClass());
	if (!NewHud)
	{
		HudRetryCountdown = 120;
		return;
	}
	NewHud->AddToViewport();
	if (!NewHud->IsInViewport())
	{
		// No viewport client yet: do not touch the input mode, and retry shortly
		// instead of allocating a widget every frame.
		NewHud->RemoveFromParent();
		HudRetryCountdown = 120;
		return;
	}
	Hud = NewHud;
	Hud->FocusPrimaryAction(Controller);
}
