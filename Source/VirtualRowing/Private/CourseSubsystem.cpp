#include "CourseSubsystem.h"

#include "GrayBoxCourseActor.h"
#include "WorkoutSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "WorkoutRuntime/WorkoutSnapshot.h"

#include <chrono>

namespace
{
	uint64 CourseClockNs()
	{
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
									   std::chrono::steady_clock::now().time_since_epoch())
									   .count());
	}

	FCourseTelemetryInput TranslateSnapshot(const FWorkoutSnapshot *Snapshot, uint64 SampleTimestampNs)
	{
		FCourseTelemetryInput Input;
		if (!Snapshot)
			return Input;
		Input.bHasSession = true;
		Input.SessionId = Snapshot->SessionId;
		Input.SessionState = Snapshot->State;
		Input.bFrozen = Snapshot->bInputFrozen;
		Input.bStale = Snapshot->ConnectionState == ERowingConnectionState::Stale ||
					   Snapshot->ConnectionState == ERowingConnectionState::Reconnecting;
		Input.bConnected = Snapshot->ConnectionState == ERowingConnectionState::Ready;
		if (Snapshot->LatestSample)
		{
			const FRowingMetricSample &Sample = *Snapshot->LatestSample;
			Input.bHasValidSample = true;
			Input.MeasuredDistanceMm = Sample.DistanceMm;
			Input.SpeedMmPerS = Sample.SpeedMmPerS;
			Input.SampleMonotonicNs = SampleTimestampNs;
			Input.WorkoutState = Sample.WorkoutState;
			Input.RowingState = Sample.RowingState;
			Input.StrokeState = Sample.StrokeState;
			Input.StrokeRateDeciSpm = Sample.StrokeRateDeciSpm;
		}
		if (Snapshot->LatestStrokeMetrics)
		{
			Input.DriveTimeMs = Snapshot->LatestStrokeMetrics->DriveTimeMs;
			Input.RecoveryTimeMs = Snapshot->LatestStrokeMetrics->RecoveryTimeMs;
		}
		return Input;
	}
} // namespace

void UCourseSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Collection.InitializeDependency<UWorkoutSubsystem>();
	Super::Initialize(Collection);
}

void UCourseSubsystem::Deinitialize()
{
	if (CourseActor)
	{
		CourseActor->Destroy();
		CourseActor = nullptr;
	}
	Runtime.Reset();
	Super::Deinitialize();
}

bool UCourseSubsystem::SupportsWorldType(EWorldType::Type WorldType)
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCourseSubsystem::IsTickable() const
{
	return !IsTemplate(RF_ClassDefaultObject) && GetGameInstance() != nullptr;
}

TStatId UCourseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCourseSubsystem, STATGROUP_Tickables);
}

void UCourseSubsystem::Tick(float)
{
	Pump(CourseClockNs());
}

void UCourseSubsystem::PumpForTesting(uint64 NowMonotonicNs)
{
	Pump(NowMonotonicNs);
}

void UCourseSubsystem::Pump(uint64 NowMonotonicNs)
{
	EnsureCourseActor();
	const UGameInstance *GameInstance = GetGameInstance();
	const UWorkoutSubsystem *Workout = GameInstance ? GameInstance->GetSubsystem<UWorkoutSubsystem>() : nullptr;
	const FWorkoutSnapshot *WorkoutSnapshot = Workout ? Workout->GetSnapshot() : nullptr;
	uint64 SampleTimestampNs = 0;
	if (WorkoutSnapshot && WorkoutSnapshot->LatestSample)
	{
		const FRowingMetricSample &Sample = *WorkoutSnapshot->LatestSample;
		const FString SampleKey = FString::Printf(TEXT("%s:%llu:%llu:%llu"), UTF8_TO_TCHAR(WorkoutSnapshot->SessionId.ToCanonicalString().c_str()), Sample.Sequence, Sample.ReceivedMonotonicNs, Sample.DistanceMm);
		if (SampleKey != LastSampleKey)
		{
			LastSampleKey = SampleKey;
			SampleObservedNs = NowMonotonicNs;
		}
		// Real adapter timestamps share steady-clock time with this subsystem. The
		// replay machine intentionally uses session-relative virtual time, so map
		// those samples to the instant the immutable snapshot was first observed.
		const bool bSharedClock = NowMonotonicNs >= Sample.ReceivedMonotonicNs &&
								  NowMonotonicNs - Sample.ReceivedMonotonicNs <= 60ULL * 1'000'000'000ULL;
		SampleTimestampNs = bSharedClock ? Sample.ReceivedMonotonicNs : SampleObservedNs;
	}
	else
	{
		LastSampleKey.Empty();
		SampleObservedNs = 0;
	}
	const FCoursePresentationSnapshot Snapshot = Runtime.Update(TranslateSnapshot(WorkoutSnapshot, SampleTimestampNs), NowMonotonicNs);
	if (CourseActor)
		CourseActor->ApplyPresentation(Snapshot);
}

void UCourseSubsystem::EnsureCourseActor()
{
	if (IsRunningCommandlet())
		return;
	UGameInstance *GameInstance = GetGameInstance();
	UWorld *World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !SupportsWorldType(World->WorldType))
		return;
	if (!CourseActor || !IsValid(CourseActor))
	{
		CourseActor = World->SpawnActor<AGrayBoxCourseActor>();
		if (!CourseActor)
			return;
		CourseActor->InitializeCourse();
	}
	// The controller can appear after the subsystem's first tick. Reasserting the
	// fixed view target also prevents gameplay input from replacing this milestone's
	// deliberately non-configurable inspection camera.
	if (APlayerController *Controller = World->GetFirstPlayerController())
	{
		if (Controller->GetViewTarget() != CourseActor)
			Controller->SetViewTarget(CourseActor);
	}
}

const FCoursePresentationSnapshot &UCourseSubsystem::GetPresentation() const
{
	return Runtime.GetSnapshot();
}

ECourseAnimationQuality UCourseSubsystem::GetAnimationQuality() const
{
	return Runtime.GetSnapshot().AnimationQuality;
}

AGrayBoxCourseActor *UCourseSubsystem::GetCourseActorForTesting() const
{
	return CourseActor;
}
