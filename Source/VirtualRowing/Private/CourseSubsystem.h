#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"

#include "CourseRuntime/CoursePresentation.h"

#include "CourseSubsystem.generated.h"

class AGrayBoxCourseActor;

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

  private:
	FCoursePresentationRuntime Runtime;
	UPROPERTY(Transient)
	TObjectPtr<AGrayBoxCourseActor> CourseActor;
	FString LastSampleKey;
	uint64 SampleObservedNs = 0;

	void EnsureCourseActor();
	void Pump(uint64 NowMonotonicNs);
};
