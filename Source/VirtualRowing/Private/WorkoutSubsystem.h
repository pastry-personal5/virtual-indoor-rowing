#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "WorkoutSubsystem.generated.h"

struct FWorkoutDisplay;
struct FWorkoutSnapshot;
class UWorkoutHudWidget;

/**
 * The only place the engine-independent workout runtime is reachable from Unreal
 * (docs/phase-1/05-milestone-5-unreal-hud.md). Game-thread only: it owns the
 * simulator machine and the current FWorkoutSession, drains the machine every
 * tick and forwards each event to the session, and exposes the latest display
 * value as a pull. Milestone 5 has no real-device path, so a launch without
 * -SimulatorDevice shows the idle "no device" display and never falls back to
 * simulated data.
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
	}

  private:
	struct FImpl;
	TSharedPtr<FImpl> Impl;

	UPROPERTY(Transient)
	TObjectPtr<UWorkoutHudWidget> Hud;

	bool bHudEnabled = true;
	// Frames to wait before trying to create the HUD again after a failed attempt.
	int32 HudRetryCountdown = 0;

	void EnsureHud();
};
