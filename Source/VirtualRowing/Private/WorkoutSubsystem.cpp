#include "WorkoutSubsystem.h"

#include "WorkoutHudWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "RowingDevice/IRowingMachine.h"
#include "WorkoutRuntime/JournalSink.h"
#include "WorkoutRuntime/WorkoutDisplay.h"
#include "WorkoutRuntime/WorkoutSession.h"
#include "pm5_sim/ReplayRowingMachine.h"
#include "pm5_sim/TelemetryFixtures.h"

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

	// Milestone 5 journals nothing in the app: simulator and automation runs must
	// never create a database or Keychain item. The sealed journal arrives with the
	// real-device milestone (on for a real device, off for the simulator).
	class FNoOpJournalSink final : public IJournalSink
	{
	  public:
		void CreateSession(const LocalData::FSessionRecord &) override {}
		void UpdateSessionState(const FRowingSessionId &, ERowingSessionState) override {}
		void RecordEvent(const FRowingSessionId &, LocalData::EJournalEventKind, std::uint64_t, std::uint64_t, const std::string &) override {}
		void RecordCapability(const FRowingSessionId &, std::uint64_t, std::uint64_t, const FRowingMachineInfo &) override {}
		void AppendSamples(const FRowingSessionId &, const std::vector<FRowingMetricSample> &) override {}
		void WriteSummary(const FRowingSessionId &, const std::string &, std::uint32_t) override {}
	};

	struct FFixtureEntry
	{
		const TCHAR *Name;
		pm5_sim::FGoldenTelemetryFixture (*Make)();
	};

	pm5_sim::FGoldenTelemetryFixture MakeNoRowing()
	{
		return pm5_sim::MakeNoRowingFixture();
	}
	pm5_sim::FGoldenTelemetryFixture MakeDeviceCompleted()
	{
		return pm5_sim::MakeDeviceCompletedFixture();
	}
	pm5_sim::FGoldenTelemetryFixture MakeDeviceTerminated()
	{
		return pm5_sim::MakeDeviceTerminatedFixture();
	}

	pm5_sim::FGoldenTelemetryFixture MakeRandom10Minutes()
	{
		return pm5_sim::MakeRandomStrokeFixture(10);
	}
	pm5_sim::FGoldenTelemetryFixture MakeRandom30Minutes()
	{
		return pm5_sim::MakeRandomStrokeFixture(30);
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
		{TEXT("easy30s"), &pm5_sim::MakeEasy30SecondFixture},
		{TEXT("sprint500m"), &pm5_sim::Make500mSprintFixture},
		{TEXT("race2000m"), &pm5_sim::Make2000mRaceFixture},
		{DefaultFixtureName, &MakeRandom30Minutes},
		{TEXT("random10min"), &MakeRandom10Minutes},
		{TEXT("steady30min"), &pm5_sim::Make30MinuteSteadyStateFixture},
		{TEXT("intervals"), &pm5_sim::MakeIntervalFixture},
		{TEXT("abrupt_stop"), &pm5_sim::MakeAbruptStopFixture},
		{TEXT("packet_loss"), &pm5_sim::MakePacketLossFixture},
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
	FNoOpJournalSink Sink;
	const FFixtureEntry *Fixture = nullptr;

	// Owned by the subsystem, not the session: the session only consumes events.
	std::unique_ptr<pm5_sim::FReplayRowingMachine> Machine;
	std::unique_ptr<FWorkoutSession> Session;
	// The simulator's virtual time and the session's Tick timebase are the same
	// monotonic value: nanoseconds since the machine was (re)started.
	uint64 OriginNs = 0;
	// Raw clock value at the previous Pump, for bounding the virtual-time step.
	uint64 LastPumpClockNs = 0;

	FWorkoutDisplay Display = MakeNoDeviceDisplay();
	uint64 LastRevision = 0;
	bool bDisplayDirty = true;
	uint64 DisplayGeneration = 0;

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
		pm5_sim::FGoldenTelemetryFixture GoldenFixture = Fixture->Make();
		Machine = std::make_unique<pm5_sim::FReplayRowingMachine>(pm5_sim::MakeSyntheticIndoorRowerScenario(), std::move(GoldenFixture.Frames));
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
		Session = std::make_unique<FWorkoutSession>(std::move(Dependencies));
		Session->Tick(0);
		bDisplayDirty = true;
		Refresh();
	}

	void Refresh()
	{
		if (!Session)
			return;
		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		if (bDisplayDirty || Snapshot.Revision != LastRevision)
		{
			Display = MakeWorkoutDisplay(Snapshot);
			LastRevision = Snapshot.Revision;
			bDisplayDirty = false;
			++DisplayGeneration;
		}
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
		if (!StartSimulator(Fixture))
		{
			UE_LOG(LogWorkoutSubsystem, Warning, TEXT("Unknown -SimulatorDevice fixture '%s'; known fixtures: %s. Showing the no-device state."), *Fixture, *FString::Join(GetSimulatorFixtureNames(), TEXT(", ")));
		}
	}
}

void UWorkoutSubsystem::Deinitialize()
{
	if (Hud)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
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
	Impl->Fixture = Fixture;
	Impl->StartMachineAndSession();
	return true;
}

bool UWorkoutSubsystem::HasDevice() const
{
	return Impl && Impl->Machine != nullptr;
}

bool UWorkoutSubsystem::HasSession() const
{
	return Impl && Impl->Session != nullptr;
}

const FWorkoutDisplay &UWorkoutSubsystem::GetDisplay() const
{
	static const FWorkoutDisplay NoDevice = MakeNoDeviceDisplay();
	return Impl ? Impl->Display : NoDevice;
}

const FWorkoutSnapshot *UWorkoutSubsystem::GetSnapshot() const
{
	return Impl && Impl->Session ? &Impl->Session->GetSnapshot() : nullptr;
}

uint64 UWorkoutSubsystem::GetDisplayGeneration() const
{
	return Impl ? Impl->DisplayGeneration : 0;
}

bool UWorkoutSubsystem::EndSession()
{
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
	if (!Impl || !Impl->Fixture || !Impl->Session || Impl->Session->GetSnapshot().State != ERowingSessionState::Ended)
		return false;
	Impl->StartMachineAndSession();
	return true;
}

void UWorkoutSubsystem::Pump()
{
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
