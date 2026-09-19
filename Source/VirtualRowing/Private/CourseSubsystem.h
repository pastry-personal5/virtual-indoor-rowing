#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "CourseRuntime/CoursePresentation.h"

#include "CourseSubsystem.generated.h"

class AGrayBoxCourseActor;

/** Pull-only Unreal adapter from the workout snapshot to reversible course motion. */
UCLASS()
class VIRTUALROWING_API UCourseSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
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
