#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"

#include "CourseRuntime/CoursePresentation.h"

#include "CourseSubsystem.generated.h"

class AGrayBoxCourseActor;
class ULevelStreamingDynamic;

/** Lifecycle of the downloaded Han level; it never gates the workout. */
enum class EAuthoredLevelState : uint8
{
	None,
	Pending,
	Shown,
	Rejected
};

/** Pull-only world adapter from the workout snapshot to reversible course motion. */
UCLASS()
class VIRTUALROWING_API UCourseSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

  public:
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	static bool SupportsWorldType(EWorldType::Type WorldType);
	const FCoursePresentationSnapshot &GetPresentation() const;
	ECourseAnimationQuality GetAnimationQuality() const;
	AGrayBoxCourseActor *GetCourseActorForTesting() const;
	// Called before the HUD enters the viewport so the same visible frame has a
	// world actor and active course camera behind it.
	void EnsurePresentation();
	void PumpForTesting(uint64 NowMonotonicNs);
	EAuthoredLevelState GetAuthoredLevelStateForTesting() const
	{
		return AuthoredLevelState;
	}
	// One-line state for the panel header: "shown", "loading", "rejected (course.level_*)", or why nothing was requested.
	FString GetAuthoredLevelSummary() const;
	// Development aid: multi-line, redacted account of the Han level load and its recent events.
	FString GetAuthoredLevelDiagnosticsText() const;
	// Stable, redacted category for the last authored-level failure; empty when none.
	const FString &GetAuthoredLevelFailure() const
	{
		return AuthoredLevelFailure;
	}

  private:
	FCoursePresentationRuntime Runtime;
	UPROPERTY(Transient)
	TObjectPtr<AGrayBoxCourseActor> CourseActor;
	FString LastSampleKey;
	FString LastRouteKey;
	FString ActorRouteKey;
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> AuthoredLevel;
	EAuthoredLevelState AuthoredLevelState = EAuthoredLevelState::None;
	FString AuthoredLevelFailure;
	uint64 SampleObservedNs = 0;

	// Development diagnostics only: nothing here feeds a decision.
	TArray<FString> LevelEvents;
	double LevelStartSeconds = 0.0;
	double LevelRequestedSeconds = 0.0;
	FString LevelAssetPath;
	FString LevelSkipReason;
	FString LevelRejectedClass;
	FString LastLevelStreamingState;
	int32 LevelPackageExists = -1;
	int32 LevelActorCount = 0;
	int32 LevelPlainActorCount = 0;
	int32 LevelInstancedComponentCount = 0;
	int32 LevelInstanceTotal = 0;
	bool bLevelAllowlistPassed = false;
	void NoteLevel(const FString &Message);

	void EnsureCourseActor();
	void DestroyCourseScene();
	void BeginAuthoredLevelLoad();
	void PollAuthoredLevel();
	void RejectAuthoredLevel(const FString &Category);
	void Pump(uint64 NowMonotonicNs);
};
