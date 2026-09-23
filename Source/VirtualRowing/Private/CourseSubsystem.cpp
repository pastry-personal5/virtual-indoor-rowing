#include "CourseSubsystem.h"

#include "GrayBoxCourseActor.h"
#include "ContentSubsystem.h"
#include "WorkoutSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "ContentRuntime/CourseLevel.h"
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

	FString RouteKeyOf(const ContentRuntime::FRouteDefinition &Route)
	{
		return UTF8_TO_TCHAR((Route.RouteId + ":" + Route.SemanticVersion + ":" + Route.ContentSetId).c_str());
	}
} // namespace

void UCourseSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
}

void UCourseSubsystem::Deinitialize()
{
	DestroyCourseScene();
	Runtime.Reset();
	Super::Deinitialize();
}

bool UCourseSubsystem::SupportsWorldType(EWorldType::Type WorldType)
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCourseSubsystem::IsTickable() const
{
	const UWorld *World = GetWorld();
	return !IsTemplate(RF_ClassDefaultObject) && World != nullptr && SupportsWorldType(World->WorldType);
}

TStatId UCourseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCourseSubsystem, STATGROUP_Tickables);
}

void UCourseSubsystem::Tick(float)
{
	Pump(CourseClockNs());
}

void UCourseSubsystem::EnsurePresentation()
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
	const UWorld *World = GetWorld();
	const UGameInstance *GameInstance = World ? World->GetGameInstance() : nullptr;
	const UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (Content)
	{
		const ContentRuntime::FRouteDefinition &Route = Content->GetSelectedRoute();
		const FString RouteKey = RouteKeyOf(Route);
		if (RouteKey != LastRouteKey)
		{
			Runtime.SelectRoute(Route);
			LastRouteKey = RouteKey;
		}
	}
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
	const FCourseTelemetryInput Telemetry = TranslateSnapshot(WorkoutSnapshot, SampleTimestampNs);
	const FCoursePresentationSnapshot Snapshot = Runtime.Update(Telemetry, NowMonotonicNs);
	if (CourseActor)
		CourseActor->ApplyPresentation(Snapshot, Telemetry, NowMonotonicNs);
}

void UCourseSubsystem::NoteLevel(const FString &Message)
{
	LevelEvents.Add(FString::Printf(TEXT("+%.1fs %s"), FPlatformTime::Seconds() - LevelStartSeconds, *Message));
	while (LevelEvents.Num() > 12)
		LevelEvents.RemoveAt(0);
}

FString UCourseSubsystem::GetAuthoredLevelSummary() const
{
	switch (AuthoredLevelState)
	{
	case EAuthoredLevelState::Pending:
		return TEXT("loading...");
	case EAuthoredLevelState::Shown:
		return TEXT("shown");
	case EAuthoredLevelState::Rejected:
		return FString::Printf(TEXT("rejected (%s); built-in kit is showing"), *AuthoredLevelFailure);
	default:
		return LevelSkipReason.IsEmpty() ? FString(TEXT("not requested")) : FString::Printf(TEXT("not requested: %s"), *LevelSkipReason);
	}
}

FString UCourseSubsystem::GetAuthoredLevelDiagnosticsText() const
{
	TArray<FString> Lines;
	const TCHAR *StateName = TEXT("not requested");
	switch (AuthoredLevelState)
	{
	case EAuthoredLevelState::Pending:
		StateName = TEXT("loading");
		break;
	case EAuthoredLevelState::Shown:
		StateName = TEXT("shown");
		break;
	case EAuthoredLevelState::Rejected:
		StateName = TEXT("rejected");
		break;
	default:
		break;
	}
	Lines.Add(FString::Printf(TEXT("level state: %s%s"), StateName, AuthoredLevelFailure.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *AuthoredLevelFailure)));
	if (AuthoredLevelState == EAuthoredLevelState::None)
		Lines.Add(FString::Printf(TEXT("why: %s"), LevelSkipReason.IsEmpty() ? TEXT("no load was attempted yet") : *LevelSkipReason));
	if (!LevelAssetPath.IsEmpty())
		Lines.Add(FString::Printf(TEXT("asset: %s"), *LevelAssetPath));
	if (LevelPackageExists >= 0)
		Lines.Add(FString::Printf(TEXT("package found in mounted content: %s"), LevelPackageExists ? TEXT("yes") : TEXT("NO")));
	if (AuthoredLevel && IsValid(AuthoredLevel))
	{
		const ULevel *Loaded = AuthoredLevel->GetLoadedLevel();
		Lines.Add(FString::Printf(TEXT("streaming: %s, loaded level: %s, visible: %s"), EnumToString(AuthoredLevel->GetLevelStreamingState()), Loaded ? TEXT("yes") : TEXT("no"), Loaded && Loaded->bIsVisible ? TEXT("yes") : TEXT("no")));
		if (AuthoredLevelState == EAuthoredLevelState::Pending)
			Lines.Add(FString::Printf(TEXT("waiting: %.1fs"), FPlatformTime::Seconds() - LevelRequestedSeconds));
	}
	if (LevelActorCount > 0)
		Lines.Add(FString::Printf(TEXT("contents: %d actors, %d plain, %d instanced components, %d instances"), LevelActorCount, LevelPlainActorCount, LevelInstancedComponentCount, LevelInstanceTotal));
	if (AuthoredLevel && IsValid(AuthoredLevel) && AuthoredLevelState == EAuthoredLevelState::Shown)
	{
		// Where the level actually is versus where the camera looks from: a level that
		// is "shown" but far from the view would still read as the built-in kit.
		FBox Bounds(ForceInit);
		int32 Rendering = 0;
		if (const ULevel *Loaded = AuthoredLevel->GetLoadedLevel())
		{
			for (const AActor *Actor : Loaded->Actors)
			{
				if (!Actor)
					continue;
				for (const UActorComponent *Component : Actor->GetComponents())
				{
					if (const UInstancedStaticMeshComponent *Instanced = Cast<UInstancedStaticMeshComponent>(Component))
					{
						Bounds += Instanced->Bounds.GetBox();
						Rendering += Instanced->IsRegistered() && Instanced->IsVisible() ? 1 : 0;
					}
				}
			}
		}
		if (Bounds.IsValid)
			Lines.Add(FString::Printf(TEXT("level bounds (m): X %.0f..%.0f  Y %.0f..%.0f  Z %.0f..%.0f; %d components registered and visible"), Bounds.Min.X / 100.0, Bounds.Max.X / 100.0, Bounds.Min.Y / 100.0, Bounds.Max.Y / 100.0, Bounds.Min.Z / 100.0, Bounds.Max.Z / 100.0, Rendering));
		else
			Lines.Add(TEXT("level bounds: none (no instanced component has a valid bound)"));
		const UWorld *World = GetWorld();
		const APlayerController *Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (Controller && Controller->PlayerCameraManager)
		{
			const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
			Lines.Add(FString::Printf(TEXT("camera (m): %.0f, %.0f, %.0f%s"), Camera.X / 100.0, Camera.Y / 100.0, Camera.Z / 100.0, Bounds.IsValid ? (Bounds.ExpandBy(FVector(500.0)).IsInside(Camera) ? TEXT(" (inside/near level bounds)") : TEXT(" (OUTSIDE level bounds)")) : TEXT("")));
		}
		if (CourseActor && IsValid(CourseActor))
			Lines.Add(FString::Printf(TEXT("course actor (m): %.0f, %.0f, %.0f"), CourseActor->GetActorLocation().X / 100.0, CourseActor->GetActorLocation().Y / 100.0, CourseActor->GetActorLocation().Z / 100.0));
	}
	if (!LevelRejectedClass.IsEmpty())
		Lines.Add(FString::Printf(TEXT("allowlist refused: %s"), *LevelRejectedClass));
	else if (bLevelAllowlistPassed)
		Lines.Add(TEXT("allowlist: every actor and component accepted"));
	if (CourseActor && IsValid(CourseActor))
		Lines.Add(FString::Printf(TEXT("kit: authored level active %s, kit landmarks visible %d"), CourseActor->IsAuthoredLevelActiveForTesting() ? TEXT("yes") : TEXT("no"), CourseActor->GetVisibleHanLandmarkCountForTesting()));
	if (LevelEvents.Num() > 0)
	{
		Lines.Add(TEXT("level events:"));
		for (const FString &Event : LevelEvents)
			Lines.Add(TEXT("  ") + Event);
	}
	return FString::Join(Lines, TEXT("\n"));
}

void UCourseSubsystem::DestroyCourseScene()
{
	if (AuthoredLevel && IsValid(AuthoredLevel))
	{
		AuthoredLevel->SetShouldBeVisible(false);
		AuthoredLevel->SetShouldBeLoaded(false);
	}
	AuthoredLevel = nullptr;
	AuthoredLevelState = EAuthoredLevelState::None;
	AuthoredLevelFailure.Empty();
	if (CourseActor)
	{
		CourseActor->Destroy();
		CourseActor = nullptr;
	}
	ActorRouteKey.Empty();
}

void UCourseSubsystem::RejectAuthoredLevel(const FString &Category)
{
	// The built-in kit stays the visible course; only a redacted category is kept.
	UE_LOG(LogTemp, Warning, TEXT("Han authored level rejected: %s"), *Category);
	NoteLevel(FString::Printf(TEXT("rejected: %s; kit stays"), *Category));
	AuthoredLevelFailure = Category;
	AuthoredLevelState = EAuthoredLevelState::Rejected;
	if (AuthoredLevel && IsValid(AuthoredLevel))
	{
		AuthoredLevel->SetShouldBeVisible(false);
		AuthoredLevel->SetShouldBeLoaded(false);
	}
	AuthoredLevel = nullptr;
	// Handing back to the kit never touches the boat or camera; only visibility changes.
	if (CourseActor && IsValid(CourseActor))
		CourseActor->SetAuthoredLevelActive(false);
}

void UCourseSubsystem::BeginAuthoredLevelLoad()
{
	UWorld *World = GetWorld();
	const UGameInstance *GameInstance = World ? World->GetGameInstance() : nullptr;
	const UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (LevelStartSeconds == 0.0)
		LevelStartSeconds = FPlatformTime::Seconds();
	LevelEvents.Reset();
	LevelAssetPath.Empty();
	LevelRejectedClass.Empty();
	LevelPackageExists = -1;
	LevelActorCount = LevelPlainActorCount = LevelInstancedComponentCount = LevelInstanceTotal = 0;
	bLevelAllowlistPassed = false;
	LastLevelStreamingState.Empty();
	if (!Content || !Content->IsHanContentMounted())
	{
		LevelSkipReason = FString::Printf(TEXT("Han content is not mounted (%s)."), Content ? *Content->GetHanAvailabilityReason() : TEXT("content.unavailable"));
		NoteLevel(LevelSkipReason);
		return;
	}
	// The table is client-owned; the signed package never names the level.
	const std::optional<std::string> LevelPath = ContentRuntime::CourseLevelAssetPathForRoute(Content->GetSelectedRoute().RouteId);
	if (!LevelPath)
	{
		LevelSkipReason = FString::Printf(TEXT("route %s has no authored level (built-in course)."), UTF8_TO_TCHAR(Content->GetSelectedRoute().RouteId.c_str()));
		NoteLevel(LevelSkipReason);
		return;
	}
	LevelSkipReason.Empty();
	const FString PackageName = UTF8_TO_TCHAR(LevelPath->c_str());
	LevelAssetPath = PackageName;
	NoteLevel(FString::Printf(TEXT("requesting %s"), *PackageName));
	LevelPackageExists = FPackageName::DoesPackageExist(PackageName) ? 1 : 0;
	if (!LevelPackageExists)
	{
		RejectAuthoredLevel(TEXT("course.level_missing"));
		return;
	}
	ULevelStreamingDynamic::FLoadLevelInstanceParams Params(World, PackageName, FTransform(FRotator::ZeroRotator, AGrayBoxCourseActor::GetAuthoredLevelOriginCm()));
	Params.bInitiallyVisible = false;
	bool bSuccess = false;
	ULevelStreamingDynamic *Streaming = ULevelStreamingDynamic::LoadLevelInstance(Params, bSuccess);
	if (!bSuccess || !Streaming)
	{
		RejectAuthoredLevel(TEXT("course.level_load_failed"));
		return;
	}
	AuthoredLevel = Streaming;
	AuthoredLevelState = EAuthoredLevelState::Pending;
	LevelRequestedSeconds = FPlatformTime::Seconds();
	NoteLevel(TEXT("streaming requested (hidden until checked)"));
}

void UCourseSubsystem::PollAuthoredLevel()
{
	if (AuthoredLevelState == EAuthoredLevelState::Shown)
	{
		// A level that disappears after it was shown hands the course back to the kit.
		if (!AuthoredLevel || !IsValid(AuthoredLevel) || !AuthoredLevel->GetLoadedLevel())
			RejectAuthoredLevel(TEXT("course.level_lost"));
		return;
	}
	if (AuthoredLevelState != EAuthoredLevelState::Pending)
		return;
	if (!AuthoredLevel || !IsValid(AuthoredLevel))
	{
		RejectAuthoredLevel(TEXT("course.level_load_failed"));
		return;
	}
	const FString StreamingName = EnumToString(AuthoredLevel->GetLevelStreamingState());
	if (StreamingName != LastLevelStreamingState)
	{
		LastLevelStreamingState = StreamingName;
		NoteLevel(FString::Printf(TEXT("streaming state: %s"), *StreamingName));
	}
	if (AuthoredLevel->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
	{
		RejectAuthoredLevel(TEXT("course.level_load_failed"));
		return;
	}
	ULevel *Loaded = AuthoredLevel->GetLoadedLevel();
	if (!Loaded)
		return;
	LevelActorCount = LevelPlainActorCount = LevelInstancedComponentCount = LevelInstanceTotal = 0;
	for (const AActor *Actor : Loaded->Actors)
	{
		if (!Actor)
			continue;
		++LevelActorCount;
		LevelPlainActorCount += Actor->GetClass() == AActor::StaticClass() ? 1 : 0;
		for (const UActorComponent *Component : Actor->GetComponents())
		{
			if (const UInstancedStaticMeshComponent *Instanced = Cast<UInstancedStaticMeshComponent>(Component))
			{
				++LevelInstancedComponentCount;
				LevelInstanceTotal += Instanced->GetInstanceCount();
			}
		}
	}
	NoteLevel(FString::Printf(TEXT("loaded: %d actors, %d instances; checking allowlist"), LevelActorCount, LevelInstanceTotal));
	// The level is still hidden here. Every actor must be a native, allowlisted
	// engine class before it can be seen: no Blueprint or project code may ride in.
	for (const AActor *Actor : Loaded->Actors)
	{
		if (!Actor)
			continue;
		const UClass *Class = Actor->GetClass();
		const FString ClassPath = Class->GetClassPathName().ToString();
		if (!Class->HasAnyClassFlags(CLASS_Native) || !ContentRuntime::IsCourseLevelActorClassAllowed(std::string(TCHAR_TO_UTF8(*ClassPath))))
		{
			UE_LOG(LogTemp, Warning, TEXT("Han authored level actor class not allowed: %s"), *ClassPath);
			LevelRejectedClass = FString::Printf(TEXT("actor %s%s"), *ClassPath, Class->HasAnyClassFlags(CLASS_Native) ? TEXT("") : TEXT(" (not native)"));
			RejectAuthoredLevel(TEXT("course.level_class_rejected"));
			return;
		}
		if (!ContentRuntime::CourseLevelActorRequiresComponentCheck(std::string(TCHAR_TO_UTF8(*ClassPath))))
			continue;
		// A plain Actor may only carry native, allowlisted mesh components.
		for (const UActorComponent *Component : Actor->GetComponents())
		{
			const UClass *ComponentClass = Component ? Component->GetClass() : nullptr;
			const FString ComponentClassPath = ComponentClass ? ComponentClass->GetClassPathName().ToString() : FString();
			if (!ComponentClass || !ComponentClass->HasAnyClassFlags(CLASS_Native) || !ContentRuntime::IsCourseLevelComponentClassAllowed(std::string(TCHAR_TO_UTF8(*ComponentClassPath))))
			{
				UE_LOG(LogTemp, Warning, TEXT("Han authored level component class not allowed: %s"), *ComponentClassPath);
				LevelRejectedClass = FString::Printf(TEXT("component %s on %s"), *ComponentClassPath, *ClassPath);
				RejectAuthoredLevel(TEXT("course.level_class_rejected"));
				return;
			}
		}
	}
	bLevelAllowlistPassed = true;
	NoteLevel(TEXT("allowlist passed; making level visible"));
	if (AGrayBoxCourseActor::ReduceMotionRequested())
		AGrayBoxCourseActor::ApplyReducedMotionToLevelWater(*Loaded);
	AuthoredLevel->SetShouldBeVisible(true);
	AuthoredLevelState = EAuthoredLevelState::Shown;
	if (CourseActor)
		CourseActor->SetAuthoredLevelActive(true);
}

void UCourseSubsystem::EnsureCourseActor()
{
	if (IsRunningCommandlet())
		return;
	UWorld *World = GetWorld();
	if (!World || !SupportsWorldType(World->WorldType))
		return;
	const UGameInstance *GameInstance = World->GetGameInstance();
	const UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	const FString CurrentRouteKey = Content ? RouteKeyOf(Content->GetSelectedRoute()) : FString();
	// A selection change (allowed only while idle) rebuilds the scene from scratch.
	if (CourseActor && IsValid(CourseActor) && CurrentRouteKey != ActorRouteKey)
		DestroyCourseScene();
	if (!CourseActor || !IsValid(CourseActor))
	{
		// Deferred, because in a running world SpawnActor calls BeginPlay at once, and
		// BeginPlay builds the course from whatever route is configured by then. The
		// route must be set first or the actor locks in the Standard kit.
		CourseActor = World->SpawnActorDeferred<AGrayBoxCourseActor>(AGrayBoxCourseActor::StaticClass(), FTransform::Identity);
		if (!CourseActor)
			return;
		if (Content)
			CourseActor->ConfigureRoute(Content->GetSelectedRoute());
		CourseActor->FinishSpawning(FTransform::Identity);
		CourseActor->InitializeCourse();
		ActorRouteKey = CurrentRouteKey;
		BeginAuthoredLevelLoad();
	}
	PollAuthoredLevel();
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

bool UCourseSubsystem::CanToggleRestView() const
{
	return CourseActor && CourseActor->CanToggleRestView();
}

bool UCourseSubsystem::IsRestViewEnabled() const
{
	return CourseActor && CourseActor->IsRestViewEnabled();
}

void UCourseSubsystem::ToggleRestView()
{
	if (CourseActor)
		CourseActor->ToggleRestView();
}

AGrayBoxCourseActor *UCourseSubsystem::GetCourseActorForTesting() const
{
	return CourseActor;
}
