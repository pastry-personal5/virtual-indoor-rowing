#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "WorkoutRuntime/AppJournal.h"
#include "WorkoutRuntime/DeviceConnector.h"

#include "WorkoutSubsystem.generated.h"

struct FWorkoutDisplay;
struct FWorkoutSnapshot;
class UWorkoutHudWidget;
class UWorkoutDevicePanelWidget;

/** What the device panel shows; derived from the engine-independent controller. */
enum class EWorkoutDevicePanelMode : uint8
{
	// Simulator run: there is no device flow to show.
	Hidden,
	Idle,
	JournalDecision,
	Starting,
	Scanning,
	Problem,
	Attached
};

struct FWorkoutDevicePanel
{
	EWorkoutDevicePanelMode Mode = EWorkoutDevicePanelMode::Hidden;
	// Plain-language state; always present in a visible mode, never a dead end.
	FString Message;
	// Nearest first, e.g. "PM5 #1 (-52 dBm)". Only while scanning.
	TArray<FString> Candidates;
	// Parallel to Candidates: what a click selects by, stable while the list re-sorts.
	TArray<uint64> CandidateTokens;
	// The journal line: saving, or the persistent not-saved warning.
	FString JournalLine;
	bool bJournalNotSaved = false;
	// One notice per launch when an interrupted session was recovered.
	bool bRecoveredNotice = false;
};

/**
 * The only place the engine-independent workout runtime is reachable from Unreal
 * (docs/phase-1/05-milestone-5-unreal-hud.md, and for the real device
 * docs/phase-1/07-milestone-7-real-pm5-app-wiring.md). Game-thread only: it owns
 * either the simulator machine and its FWorkoutSession, or, without
 * -SimulatorDevice, an FRealDeviceController that owns the Bluetooth connection
 * flow, the owner-only journal and the session. It drains the machine every tick and
 * forwards each event to the session, and exposes the latest display value as a
 * pull. It never falls back from one path to the other, so simulated data is never
 * mistaken for a real device. Nothing touches Bluetooth or the Keychain until the
 * user asks to connect.
 */
UCLASS()
class VIRTUALROWING_API UWorkoutSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

  public:
	using FMonotonicClock = TFunction<uint64()>;

	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// Selects the simulator machine and starts the first session. FixtureName is a
	// -SimulatorDevice fixture key (see GetSimulatorFixtureNames); empty selects the
	// default. Returns false and leaves the subsystem unchanged for an unknown name.
	bool StartSimulator(const FString &FixtureName);
	static TArray<FString> GetSimulatorFixtureNames();

	bool HasDevice() const;
	bool HasSession() const;

	// Real device (Milestone 7). Each action is a user click; none runs at launch.
	// BeginConnect opens the journal first (the Keychain may prompt) and then creates
	// the Bluetooth discovery. If the journal cannot be opened the panel offers
	// "Row without saving", retry, or cancel; a row never silently goes unjournaled.
	bool BeginConnect();
	bool ConfirmRowWithoutSaving();
	void CancelConnect();
	bool ScanForDevices();
	bool SelectDeviceByToken(uint64 Token);
	void ForgetDevice();
	FWorkoutDevicePanel GetDevicePanel() const;
	uint64 GetDevicePanelGeneration() const;
	// The HUD calls this when it applies a display generation, which closes the
	// software-latency measurement for the samples that produced it.
	void NoteDisplayApplied(uint64 Generation);

	// Pull values for the HUD. The display generation increases whenever the display
	// value changes (a new snapshot revision or a replaced session) so a consumer can
	// skip re-formatting when it is unchanged. Valid until the next Tick or action.
	const FWorkoutDisplay &GetDisplay() const;
	const FWorkoutSnapshot *GetSnapshot() const;
	uint64 GetDisplayGeneration() const;

	// End completes an active session and aborts one that never started or lost its
	// link. Returns false if there is no live session. Start New builds a fresh
	// session (and, for the simulator, a rewound fixture); it is only valid once the
	// previous session has ended. The ended session's final snapshot stays visible
	// until Start New replaces it.
	bool EndSession();
	bool StartNewSession();

	// Drains the machine into the session and ticks it. Called from Tick; exposed so
	// the Automation spec can drive it deterministically with SetClockForTesting.
	void Pump();
	void SetClockForTesting(FMonotonicClock InClock);
	// Events the machine has queued that the subsystem has not yet drained.
	int32 GetPendingMachineEventCountForTesting() const;
	void SetHudEnabled(bool bInEnabled)
	{
		bHudEnabled = bInEnabled;
		bPanelEnabled = bInEnabled;
	}
	// Replaces the real-device dependencies (and any existing controller) so the
	// Automation spec can script Bluetooth and the Keychain. Call before Connect.
	void SetRealDeviceDependenciesForTesting(FDeviceConnector::FDiscoveryFactory InDiscovery, FAppJournal::FCipherFactory InCipher, const FString &InAppDataDirectory);

  private:
	struct FImpl;
	TSharedPtr<FImpl> Impl;

	UPROPERTY(Transient)
	TObjectPtr<UWorkoutHudWidget> Hud;
	UPROPERTY(Transient)
	TObjectPtr<UWorkoutDevicePanelWidget> DevicePanel;

	bool bHudEnabled = true;
	// Frames to wait before trying to create the HUD again after a failed attempt.
	int32 HudRetryCountdown = 0;
	bool bPanelEnabled = true;
	int32 PanelRetryCountdown = 0;

	void EnsureHud();
	void EnsureDevicePanel();
	void EnsureRealController();
};
