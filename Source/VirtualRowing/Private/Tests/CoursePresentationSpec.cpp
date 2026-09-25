#include "Misc/AutomationTest.h"

#include "CourseSubsystem.h"
#include "ContentSubsystem.h"
#include "GrayBoxCourseActor.h"
#include "WorkoutHudWidget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FCoursePresentationSpec, "VirtualRowing.CoursePresentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
UWorld *World = nullptr;
AGrayBoxCourseActor *Course = nullptr;
virtual bool RunTest(const FString &Parameters) override
{
	// Automation can retain a spec from an old dylib after native hot reload.
	// Reject before queuing BeforeEach/It; returning from BeforeEach alone
	// would still execute the cases with an invalid Course pointer.
	if (AGrayBoxCourseActor::StaticClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		AddError(TEXT("CoursePresentation references a replaced class after hot reload. Restart Unreal Editor before running native automation."));
		return false;
	}
	return FAutomationSpecBase::RunTest(Parameters);
}
END_DEFINE_SPEC(FCoursePresentationSpec)

void FCoursePresentationSpec::Define()
{
	BeforeEach([this]()
			   {
			// CreateWorld initializes the new world itself; a second InitializeNewWorld
			// would spawn a duplicate WorldSettings and hit a fatal name collision.
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			Course = World->SpawnActor<AGrayBoxCourseActor>();
			Course->InitializeCourse(); });

	AfterEach([this]()
			  {
			Course = nullptr;
			if (World)
			{
				World->DestroyWorld(false);
				World = nullptr;
			} });

	It("maps the normalized 2 km domain continuously onto the closed spline", [this]()
	   {
		const FTransform Start = Course->GetCourseTransform(0.0);
		const FTransform Finish = Course->GetCourseTransform(2'000'000.0);
		TestTrue(TEXT("finish wraps to start"), Start.GetLocation().Equals(Finish.GetLocation(), 0.1));
		TestTrue(TEXT("finish tangent wraps"), Course->GetCourseTangent(0.0).Equals(Course->GetCourseTangent(2'000'000.0), 0.001));
		TestFalse(TEXT("250 m advances"), Start.GetLocation().Equals(Course->GetCourseTransform(250'000.0).GetLocation(), 1.0));
		TestFalse(TEXT("1,000 m advances"), Start.GetLocation().Equals(Course->GetCourseTransform(1'000'000.0).GetLocation(), 1.0));
		TestEqual(TEXT("markers every 250 m"), Course->GetMarkerCountForTesting(), 8);
		TestEqual(TEXT("visible edge covers every spline segment"), Course->GetCourseEdgeSegmentCountForTesting(), 32);
		TestTrue(TEXT("course edges remain attached to their root"), Course->AreCourseEdgesAttachedForTesting()); });

	It("builds an open, landmarked Han River presentation without route markers", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		HanRoute.RouteId = "route.han-river.5k";
		HanRoute.SemanticVersion = "1.0.0";
		HanRoute.ContentSetId = "han-river-alpha-1";
		HanRoute.LengthMm = 5'000'000;
		HanRoute.bClosed = false;
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		TestTrue(TEXT("Han route receives dedicated presentation"), HanCourse->HasHanRiverEnvironmentForTesting());
		TestTrue(TEXT("Han route has bridges, banks, skyline, and sparse buoys"), HanCourse->GetHanLandmarkCountForTesting() >= 50);
		TestEqual(TEXT("Han route avoids intrusive distance-marker labels"), HanCourse->GetMarkerCountForTesting(), 0);
		TestFalse(TEXT("Han endpoint stays open rather than wrapping"), HanCourse->GetCourseTransform(0.0).GetLocation().Equals(HanCourse->GetCourseTransform(5'000'000.0).GetLocation(), 1.0));
		HanCourse->Destroy(); });

	It("builds the Han kit when the actor is spawned into a world that has begun play", [this]()
	   {
		// In a running world SpawnActor calls BeginPlay immediately, which used to
		// build the Standard course before the route could be configured.
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		HanRoute.RouteId = "route.han-river.5k";
		HanRoute.SemanticVersion = "1.0.1";
		HanRoute.ContentSetId = "han-river-alpha-1";
		HanRoute.LengthMm = 5'000'000;
		HanRoute.bClosed = false;
		AGrayBoxCourseActor *HanCourse = World->SpawnActorDeferred<AGrayBoxCourseActor>(AGrayBoxCourseActor::StaticClass(), FTransform::Identity);
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->FinishSpawning(FTransform::Identity);
		HanCourse->InitializeCourse();
		TestTrue(TEXT("Han route is honored after BeginPlay"), HanCourse->HasHanRiverEnvironmentForTesting());
		TestEqual(TEXT("no Standard distance markers"), HanCourse->GetMarkerCountForTesting(), 0);
		HanCourse->SetAuthoredLevelActive(true);
		TestTrue(TEXT("authored level can take over"), HanCourse->IsAuthoredLevelActiveForTesting());
		HanCourse->Destroy(); });

	It("animates the kit water and freezes it only for reduced motion", [this]()
	   {
		const UMaterialInterface *WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Water/M_CourseWater.M_CourseWater"));
		if (!TestNotNull(TEXT("the cooked water material exists"), WaterMaterial))
			return;
		TestTrue(TEXT("the kit water runs at the material's own motion by default"), Course->GetWaterMotionScaleForTesting() > 0.0f);
		UMaterialInstanceDynamic *Probe = UMaterialInstanceDynamic::Create(const_cast<UMaterialInterface *>(WaterMaterial), nullptr);
		AGrayBoxCourseActor::ApplyWaterMotion(*Probe, false);
		float Scale = -1.0f;
		Probe->GetScalarParameterValue(TEXT("MotionScale"), Scale);
		TestTrue(TEXT("normal motion leaves the default"), Scale > 0.0f);
		AGrayBoxCourseActor::ApplyWaterMotion(*Probe, true);
		Probe->GetScalarParameterValue(TEXT("MotionScale"), Scale);
		TestEqual(TEXT("reduced motion freezes the wave phase"), Scale, 0.0f); });

	It("hides only Han water components for the Shipping GPU comparison", [this]()
	   {
		UStaticMesh *Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UStaticMesh *Hull = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Boat/Meshes/SM_ScullHull.SM_ScullHull"));
		UMaterialInterface *HanWater = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Phase2/HanRiver/Materials/M_Han_Water.M_Han_Water"));
		// A same-named material outside the Han package must remain visible.
		UMaterial *OtherNamedWater = NewObject<UMaterial>(World, TEXT("M_Han_Water"));
		UMaterialInterface *Other = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!TestNotNull(TEXT("cube mesh for water visibility test"), Cube) ||
			!TestNotNull(TEXT("multi-slot hull mesh for mixed-material visibility test"), Hull) ||
			!TestNotNull(TEXT("authored Han water material"), HanWater) ||
			!TestNotNull(TEXT("same-named non-Han material"), OtherNamedWater) ||
			!TestNotNull(TEXT("non-water material for water visibility test"), Other))
			return;
		auto AddComponent = [this](UStaticMesh *MeshAsset, UMaterialInterface *Material)
		{
			AActor *Owner = World->SpawnActor<AActor>();
			UStaticMeshComponent *Mesh = NewObject<UStaticMeshComponent>(Owner);
			Owner->SetRootComponent(Mesh);
			Mesh->SetStaticMesh(MeshAsset);
			Mesh->SetMaterial(0, Material);
			Owner->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
			return Mesh;
		};
		UStaticMeshComponent *Water = AddComponent(Cube, HanWater);
		UStaticMeshComponent *Impostor = AddComponent(Cube, OtherNamedWater);
		UStaticMeshComponent *Bank = AddComponent(Cube, Other);
		UStaticMeshComponent *Mixed = AddComponent(Hull, HanWater);
		if (!TestTrue(TEXT("mixed geometry has multiple material slots"), Mixed->GetNumMaterials() > 1))
			return;
		Mixed->SetMaterial(1, Other);
		AGrayBoxCourseActor::ApplyPresentationOptionsToLevelWater(*World->PersistentLevel, false, false);
		TestTrue(TEXT("normal presentation keeps Han water visible"), Water->IsVisible());
		AGrayBoxCourseActor::ApplyPresentationOptionsToLevelWater(*World->PersistentLevel, true, false);
		UMaterialInstanceDynamic *ReducedWater = Cast<UMaterialInstanceDynamic>(Water->GetMaterial(0));
		if (!TestNotNull(TEXT("reduced motion creates a Han water instance"), ReducedWater))
			return;
		float MotionScale = -1.0f;
		ReducedWater->GetScalarParameterValue(TEXT("MotionScale"), MotionScale);
		TestEqual(TEXT("reduced motion freezes authored Han water"), MotionScale, 0.0f);
		TestTrue(TEXT("reduced motion keeps Han water visible"), Water->IsVisible());
		AGrayBoxCourseActor::ApplyPresentationOptionsToLevelWater(*World->PersistentLevel, false, true);
		TestFalse(TEXT("benchmark flag hides the Han water component"), Water->IsVisible());
		TestTrue(TEXT("benchmark flag preserves mixed-material geometry"), Mixed->IsVisible());
		TestTrue(TEXT("benchmark flag preserves a same-named material in another package"), Impostor->IsVisible());
		TestTrue(TEXT("benchmark flag preserves non-water geometry"), Bank->IsVisible()); });

	It("yields the kit's start-area landmarks to the authored level and restores them", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		HanRoute.RouteId = "route.han-river.5k";
		HanRoute.SemanticVersion = "1.0.0";
		HanRoute.ContentSetId = "han-river-alpha-1";
		HanRoute.LengthMm = 5'000'000;
		HanRoute.bClosed = false;
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		const int32 AllLandmarks = HanCourse->GetVisibleHanLandmarkCountForTesting();
		HanCourse->SetAuthoredLevelActive(true);
		TestTrue(TEXT("overlap is hidden"), HanCourse->IsAuthoredLevelActiveForTesting());
		TestTrue(TEXT("start-area landmarks yield"), HanCourse->GetVisibleHanLandmarkCountForTesting() < AllLandmarks);
		TestTrue(TEXT("the rest of the route keeps the kit"), HanCourse->GetVisibleHanLandmarkCountForTesting() > 0);
		HanCourse->SetAuthoredLevelActive(false);
		TestEqual(TEXT("fallback restores every landmark"), HanCourse->GetVisibleHanLandmarkCountForTesting(), AllLandmarks);
		Course->SetAuthoredLevelActive(true);
		TestFalse(TEXT("the Standard route ignores the authored level"), Course->IsAuthoredLevelActiveForTesting());
		HanCourse->Destroy(); });

	It("hands over between the kit and the authored level without moving the boat or camera", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		HanRoute.RouteId = "route.han-river.5k";
		HanRoute.SemanticVersion = "1.0.0";
		HanRoute.ContentSetId = "han-river-alpha-1";
		HanRoute.LengthMm = 5'000'000;
		HanRoute.bClosed = false;
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		FCoursePresentationSnapshot Snapshot;
		Snapshot.WrappedCourseDistanceMm = 100'000.0;
		HanCourse->ApplyPresentation(Snapshot);
		const FTransform Boat = HanCourse->GetBoatTransformForTesting();
		const FTransform Camera = HanCourse->GetCameraTransformForTesting();
		HanCourse->SetAuthoredLevelActive(true);
		TestTrue(TEXT("boat is unmoved by handover to the level"), HanCourse->GetBoatTransformForTesting().Equals(Boat, 0.0));
		TestTrue(TEXT("camera is unmoved by handover to the level"), HanCourse->GetCameraTransformForTesting().Equals(Camera, 0.0));
		HanCourse->SetAuthoredLevelActive(false);
		TestTrue(TEXT("boat is unmoved by fallback to the kit"), HanCourse->GetBoatTransformForTesting().Equals(Boat, 0.0));
		TestTrue(TEXT("camera is unmoved by fallback to the kit"), HanCourse->GetCameraTransformForTesting().Equals(Camera, 0.0));
		HanCourse->Destroy(); });

	It("chases the Han boat from 12 m astern and 1 m up, level and along its heading", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		HanRoute.RouteId = "route.han-river.5k";
		HanRoute.SemanticVersion = "1.0.0";
		HanRoute.ContentSetId = "han-river-alpha-1";
		HanRoute.LengthMm = 5'000'000;
		HanRoute.bClosed = false;
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		FCoursePresentationSnapshot Snapshot;
		Snapshot.WrappedCourseDistanceMm = 100'000.0;
		HanCourse->ApplyPresentation(Snapshot);
		const FTransform Boat = HanCourse->GetBoatTransformForTesting();
		const FTransform Camera = HanCourse->GetCameraTransformForTesting();
		const FVector Offset = Camera.GetLocation() - Boat.GetLocation();
		const FVector Forward = Boat.GetUnitAxis(EAxis::X);
		TestTrue(TEXT("camera is 12 m behind the boat"), FMath::IsNearlyEqual(static_cast<float>(-FVector::DotProduct(Offset, Forward)), 1'200.0f, 1.0f));
		TestTrue(TEXT("camera is 1 m above the boat"), FMath::IsNearlyEqual(static_cast<float>(Offset.Z), 100.0f, 1.0f));
		TestTrue(TEXT("camera is on the boat centerline"), FMath::IsNearlyZero(static_cast<float>(FVector::DotProduct(Offset, Boat.GetUnitAxis(EAxis::Y))), 1.0f));
		TestTrue(TEXT("camera is level"), FMath::IsNearlyZero(Camera.Rotator().Pitch, 0.01) && FMath::IsNearlyZero(Camera.Rotator().Roll, 0.01));
		TestTrue(TEXT("camera looks along the boat heading"), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Camera.Rotator().Yaw, Forward.Rotation().Yaw), 0.1));
		HanCourse->Destroy(); });

	It("uses only a fresh telemetry-backed Han rest for the cinematic reveal and resets its dwell", [this]()
	   {
		auto ConfigureHan = [this]()
		{
			AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
			ContentRuntime::FRouteDefinition HanRoute = ContentRuntime::BuiltInStandardRouteDefinition();
			HanRoute.RouteId = "route.han-river.5k";
			HanRoute.LengthMm = 5'000'000;
			HanRoute.bClosed = false;
			HanCourse->ConfigureRoute(HanRoute);
			HanCourse->InitializeCourse();
			return HanCourse;
		};
		auto RestTelemetry = []()
		{
			FCourseTelemetryInput Telemetry;
			Telemetry.bHasSession = true;
			Telemetry.bHasValidSample = true;
			Telemetry.bConnected = true;
			Telemetry.SessionState = ERowingSessionState::Active;
			Telemetry.WorkoutState = ERowingWorkoutState::Resting;
			return Telemetry;
		};
		auto OffsetMatches = [this](const AGrayBoxCourseActor &HanCourse, float BehindCm, float StarboardCm, float HeightCm)
		{
			const FTransform Boat = HanCourse.GetBoatTransformForTesting();
			const FVector Offset = HanCourse.GetCameraTransformForTesting().GetLocation() - Boat.GetLocation();
			return FMath::IsNearlyEqual(FVector::DotProduct(Offset, Boat.GetUnitAxis(EAxis::X)), -BehindCm, 1.0f) &&
				FMath::IsNearlyEqual(FVector::DotProduct(Offset, Boat.GetUnitAxis(EAxis::Y)), StarboardCm, 1.0f) &&
				FMath::IsNearlyEqual(Offset.Z, HeightCm, 1.0f);
		};

		AGrayBoxCourseActor::SetReduceMotionForTesting(false);
		AGrayBoxCourseActor *HanCourse = ConfigureHan();
		FCoursePresentationSnapshot Snapshot;
		Snapshot.WrappedCourseDistanceMm = 100'000.0;
		FCourseTelemetryInput Telemetry = RestTelemetry();
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 1'000'000'000ULL);
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 6'000'000'000ULL);
		TestTrue(TEXT("five-second dwell leaves the chase framing in place"), OffsetMatches(*HanCourse, 1'200.0f, 0.0f, 100.0f));
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 11'000'000'000ULL);
		TestTrue(TEXT("outbound midpoint is 18 m astern, 6 m starboard, and 5 m above"), OffsetMatches(*HanCourse, 1'800.0f, 600.0f, 500.0f));
		TestTrue(TEXT("midpoint widens to 84 degrees"), FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 84.0f));
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 16'000'000'000ULL);
		const FTransform Reveal = HanCourse->GetCameraTransformForTesting();
		TestTrue(TEXT("reveal is 32 m astern, 18 m starboard, and 14 m above"), OffsetMatches(*HanCourse, 3'200.0f, 1'800.0f, 1'400.0f));
		TestTrue(TEXT("reveal widens to 92 degrees with a level horizon"), FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 92.0f) && FMath::IsNearlyZero(Reveal.Rotator().Roll, 0.01f));
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 24'000'000'000ULL);
		TestTrue(TEXT("wide reveal holds for the rest interval"), HanCourse->GetCameraTransformForTesting().Equals(Reveal, 0.01f));

		Telemetry.WorkoutState = ERowingWorkoutState::Active;
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 24'000'000'000ULL);
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 26'000'000'000ULL);
		TestTrue(TEXT("four-second return restores FOV smoothly"), FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 85.0f, 0.1f));
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 28'000'000'000ULL);
		TestTrue(TEXT("return restores the normal Han chase"), OffsetMatches(*HanCourse, 1'200.0f, 0.0f, 100.0f) && FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 78.0f));

		Telemetry = RestTelemetry();
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 29'000'000'000ULL);
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 33'999'000'000ULL);
		TestTrue(TEXT("a new rest does not inherit its prior dwell"), FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 78.0f));
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 34'000'000'000ULL);
		HanCourse->ApplyPresentation(Snapshot, Telemetry, 39'000'000'000ULL);
		TestTrue(TEXT("a new rest reveals only after its fresh dwell"), FMath::IsNearlyEqual(HanCourse->GetCameraFieldOfViewForTesting(), 84.0f));
		HanCourse->Destroy();

		AGrayBoxCourseActor *SuppressedCourse = ConfigureHan();
		for (const TCHAR *Reason : {TEXT("stale"), TEXT("frozen"), TEXT("reconnecting")})
		{
			FCourseTelemetryInput Suppressed = RestTelemetry();
			if (FCString::Strcmp(Reason, TEXT("stale")) == 0)
				Suppressed.bStale = true;
			else if (FCString::Strcmp(Reason, TEXT("frozen")) == 0)
				Suppressed.bFrozen = true;
			else
				Suppressed.bConnected = false;
			SuppressedCourse->ApplyPresentation(Snapshot, Suppressed, 1'000'000'000ULL);
			SuppressedCourse->ApplyPresentation(Snapshot, Suppressed, 12'000'000'000ULL);
			TestTrue(FString::Printf(TEXT("%s rest cannot start an automatic view"), Reason), FMath::IsNearlyEqual(SuppressedCourse->GetCameraFieldOfViewForTesting(), 78.0f));
		}
		SuppressedCourse->Destroy();

		AGrayBoxCourseActor *EarlyCancellationCourse = ConfigureHan();
		Telemetry = RestTelemetry();
		EarlyCancellationCourse->ApplyPresentation(Snapshot, Telemetry, 1'000'000'000ULL);
		Telemetry.WorkoutState = ERowingWorkoutState::Active;
		EarlyCancellationCourse->ApplyPresentation(Snapshot, Telemetry, 3'000'000'000ULL);
		TestTrue(TEXT("rest ending before dwell cancellation leaves the chase untouched"), OffsetMatches(*EarlyCancellationCourse, 1'200.0f, 0.0f, 100.0f) && FMath::IsNearlyEqual(EarlyCancellationCourse->GetCameraFieldOfViewForTesting(), 78.0f));
		EarlyCancellationCourse->Destroy();

		Course->ApplyPresentation(Snapshot, RestTelemetry(), 1'000'000'000ULL);
		Course->ApplyPresentation(Snapshot, RestTelemetry(), 20'000'000'000ULL);
		TestTrue(TEXT("Standard remains on its existing camera during rest"), FMath::IsNearlyEqual(Course->GetCameraFieldOfViewForTesting(), 50.0f));
		AGrayBoxCourseActor *OtherRouteCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition OtherRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		OtherRoute.RouteId = "route.example.5k";
		OtherRoute.LengthMm = 5'000'000;
		OtherRoute.bClosed = false;
		OtherRouteCourse->ConfigureRoute(OtherRoute);
		OtherRouteCourse->InitializeCourse();
		OtherRouteCourse->ApplyPresentation(Snapshot, RestTelemetry(), 1'000'000'000ULL);
		OtherRouteCourse->ApplyPresentation(Snapshot, RestTelemetry(), 6'000'000'000ULL);
		OtherRouteCourse->ApplyPresentation(Snapshot, RestTelemetry(), 11'000'000'000ULL);
		TestTrue(TEXT("every non-Standard route receives camera cutscene one"), FMath::IsNearlyEqual(OtherRouteCourse->GetCameraFieldOfViewForTesting(), 84.0f));
		OtherRouteCourse->Destroy();

		AGrayBoxCourseActor *ReducedMotionCourse = ConfigureHan();
		AGrayBoxCourseActor::SetReduceMotionForTesting(true);
		ReducedMotionCourse->ApplyPresentation(Snapshot, RestTelemetry(), 1'000'000'000ULL);
		TestTrue(TEXT("reduced motion exposes a rest-only static-view action"), ReducedMotionCourse->CanToggleRestView() && !ReducedMotionCourse->IsRestViewEnabled());
		ReducedMotionCourse->ToggleRestView();
		ReducedMotionCourse->ApplyPresentation(Snapshot, RestTelemetry(), 2'000'000'000ULL);
		TestTrue(TEXT("reduced motion uses the static wide view without an automatic move"), ReducedMotionCourse->IsRestViewEnabled() && FMath::IsNearlyEqual(ReducedMotionCourse->GetCameraFieldOfViewForTesting(), 92.0f));
		Telemetry = RestTelemetry();
		Telemetry.WorkoutState = ERowingWorkoutState::Active;
		ReducedMotionCourse->ApplyPresentation(Snapshot, Telemetry, 3'000'000'000ULL);
		TestTrue(TEXT("active rowing restores reduced-motion chase"), !ReducedMotionCourse->CanToggleRestView() && !ReducedMotionCourse->IsRestViewEnabled() && FMath::IsNearlyEqual(ReducedMotionCourse->GetCameraFieldOfViewForTesting(), 78.0f));
		AGrayBoxCourseActor::SetReduceMotionForTesting({});
		ReducedMotionCourse->Destroy(); });

	It("applies catch drive finish recovery and disconnect-return proxy transforms", [this]()
	   {
		FCoursePresentationSnapshot Snapshot;
		Course->ApplyPresentation(Snapshot);
		const FTransform CatchSeat = Course->GetSeatRelativeTransformForTesting();
		Snapshot.StrokePose = 0.5;
		Snapshot.SeatPose = 1.0;
		Snapshot.TorsoPose = 0.5;
		Snapshot.ArmsPose = 0.0;
		Snapshot.OarPose = 0.5;
		Course->ApplyPresentation(Snapshot);
		TestTrue(TEXT("seat moves first"), Course->GetSeatRelativeTransformForTesting().GetLocation().X > CatchSeat.GetLocation().X);
		const FRotator MidTorso = Course->GetTorsoRelativeTransformForTesting().Rotator();
		Snapshot.SeatPose = 1.0;
		Snapshot.TorsoPose = 1.0;
		Snapshot.ArmsPose = 1.0;
		Snapshot.OarPose = 1.0;
		Course->ApplyPresentation(Snapshot);
		TestFalse(TEXT("torso continues after seat"), Course->GetTorsoRelativeTransformForTesting().Rotator().Equals(MidTorso, 0.1));
		Snapshot = {};
		Course->ApplyPresentation(Snapshot);
		TestTrue(TEXT("return reaches catch"), Course->GetSeatRelativeTransformForTesting().Equals(CatchSeat, 0.1)); });

	It("dips the visible blade only during a fresh drive and clears it for recovery or stale input", [this]()
	   {
		TestNotNull(TEXT("cooked-in interaction material exists"), LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Water/M_WaterInteraction.M_WaterInteraction")));
		FCoursePresentationSnapshot Snapshot;
		Snapshot.AnimationQuality = ECourseAnimationQuality::Primary;
		FCourseTelemetryInput Telemetry;
		Telemetry.bHasSession = true;
		Telemetry.bHasValidSample = true;
		Telemetry.bConnected = true;
		Telemetry.SessionState = ERowingSessionState::Active;
		Telemetry.WorkoutState = ERowingWorkoutState::Active;
		Telemetry.RowingState = ERowingState::Active;
		Telemetry.StrokeState = ERowingStrokeState::Drive;
		Snapshot.OarPose = 0.0;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'000'000'000ULL);
		const auto BladeZ = [this]()
		{
			return Course->GetLeftOarRelativeTransformForTesting().TransformPosition(FVector(220.0, 0.0, 0.0)).Z;
		};
		const double FeatheredZ = BladeZ();
		Snapshot.OarPose = 0.5;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'300'000'000ULL);
		TestTrue(TEXT("drive lowers the blade tip through the boat-relative waterline"), BladeZ() < -30.0);
		TestEqual(TEXT("one entry per rendered blade"), Course->GetOarWaterContactCountForTesting(), 2);
		TestEqual(TEXT("one visible ripple per blade entry"), Course->GetActiveWaterEffectCountForTesting(), 2);
		Course->ApplyPresentation(Snapshot, Telemetry, 1'350'000'000ULL);
		TestEqual(TEXT("steady submerged blade does not duplicate entry"), Course->GetOarWaterContactCountForTesting(), 2);
		Telemetry.StrokeState = ERowingStrokeState::Recovery;
		Course->ApplyPresentation(Snapshot, Telemetry, 2'000'000'000ULL);
		TestTrue(TEXT("recovery lifts the blade clear"), FMath::IsNearlyEqual(BladeZ(), FeatheredZ, 0.1));
		TestEqual(TEXT("one exit per rendered blade"), Course->GetOarWaterContactCountForTesting(), 4);
		Telemetry.StrokeState = ERowingStrokeState::Drive;
		Course->ApplyPresentation(Snapshot, Telemetry, 3'000'000'000ULL);
		TestEqual(TEXT("next drive creates one new entry per blade"), Course->GetOarWaterContactCountForTesting(), 6);
		Telemetry.bStale = true;
		Course->ApplyPresentation(Snapshot, Telemetry, 3'100'000'000ULL);
		TestTrue(TEXT("stale input feathers the blade without another drive"), FMath::IsNearlyEqual(BladeZ(), FeatheredZ, 0.1));
		TestEqual(TEXT("stale feathering creates no new pulse"), Course->GetOarWaterContactCountForTesting(), 6);
		TestTrue(TEXT("existing ripples expire during stale input"), Course->GetActiveWaterEffectCountForTesting() <= 2);
		Telemetry.bStale = false;
		Course->ApplyPresentation(Snapshot, Telemetry, 3'200'000'000ULL);
		TestEqual(TEXT("reconnect does not backfill a blade entry"), Course->GetOarWaterContactCountForTesting(), 6);
		Telemetry.StrokeState = ERowingStrokeState::Recovery;
		Course->ApplyPresentation(Snapshot, Telemetry, 3'400'000'000ULL);
		TestEqual(TEXT("a new visible exit is counted after reconnect"), Course->GetOarWaterContactCountForTesting(), 8);
		AGrayBoxCourseActor::SetReduceMotionForTesting(true);
		Telemetry.StrokeState = ERowingStrokeState::Drive;
		Course->ApplyPresentation(Snapshot, Telemetry, 3'600'000'000ULL);
		TestEqual(TEXT("reduced motion suppresses new water contacts"), Course->GetOarWaterContactCountForTesting(), 8);
		TestEqual(TEXT("reduced motion clears water effects"), Course->GetActiveWaterEffectCountForTesting(), 0);
		AGrayBoxCourseActor::SetReduceMotionForTesting({}); });

	It("emits bounded hull trail samples without bridging a skipped distance or stale interval", [this]()
	   {
		FCoursePresentationSnapshot Snapshot;
		Snapshot.AnimationQuality = ECourseAnimationQuality::Primary;
		FCourseTelemetryInput Telemetry;
		Telemetry.bHasSession = true;
		Telemetry.bHasValidSample = true;
		Telemetry.bConnected = true;
		Telemetry.SessionState = ERowingSessionState::Active;
		Telemetry.WorkoutState = ERowingWorkoutState::Active;
		Telemetry.RowingState = ERowingState::Active;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'000'000'000ULL);
		TestEqual(TEXT("first fresh pose only arms wake anchor"), Course->GetActiveWaterEffectCountForTesting(), 0);
		Snapshot.WrappedCourseDistanceMm = 1'000;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'100'000'000ULL);
		TestEqual(TEXT("short visible travel produces one hull sample"), Course->GetActiveWaterEffectCountForTesting(), 1);
		Snapshot.WrappedCourseDistanceMm = 300'000;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'200'000'000ULL);
		TestEqual(TEXT("skipped distance does not fill a trail"), Course->GetActiveWaterEffectCountForTesting(), 1);
		Telemetry.bStale = true;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'300'000'000ULL);
		Telemetry.bStale = false;
		Snapshot.WrappedCourseDistanceMm = 300'500;
		Course->ApplyPresentation(Snapshot, Telemetry, 1'400'000'000ULL);
		TestEqual(TEXT("reconnect only rearms wake anchor"), Course->GetActiveWaterEffectCountForTesting(), 1);
		Course->ApplyPresentation(Snapshot, Telemetry, 3'000'000'000ULL);
		TestEqual(TEXT("trail expires within bounded lifetime"), Course->GetActiveWaterEffectCountForTesting(), 0); });

	It("uses a rigid elevated follow camera with no input component", [this]()
	   {
		FCoursePresentationSnapshot Snapshot;
		Snapshot.WrappedCourseDistanceMm = 750'000.0;
		Course->ApplyPresentation(Snapshot);
		const FTransform Boat = Course->GetBoatTransformForTesting();
		const FVector Offset = Course->GetCameraTransformForTesting().GetLocation() - Boat.GetLocation();
		TestTrue(TEXT("camera follows behind"), FMath::IsNearlyEqual(FVector::DotProduct(Offset, Boat.GetUnitAxis(EAxis::X)), -1'400.0, 0.1));
		TestTrue(TEXT("camera has starboard offset"), FMath::IsNearlyEqual(FVector::DotProduct(Offset, Boat.GetUnitAxis(EAxis::Y)), 900.0, 0.1));
		TestTrue(TEXT("camera elevation keeps route and boat visible"), FMath::IsNearlyEqual(Offset.Z, 650.0, 0.1));
		TestTrue(TEXT("level horizon"), FMath::IsNearlyZero(Course->GetCameraTransformForTesting().Rotator().Roll, 0.01));
		TestTrue(TEXT("50 degree field of view"), FMath::IsNearlyEqual(Course->GetCameraFieldOfViewForTesting(), 50.0f));
		TestTrue(TEXT("code-owned course light illuminates the empty Entry map"), Course->GetCourseLightIntensityForTesting() > 0.0f);
		TestFalse(TEXT("course actor binds no input"), Course->HasInputComponentForTesting()); });

	It("restricts spawning and the fallback label to their intended states", [this]()
	   {
		TestTrue(TEXT("game supported"), UCourseSubsystem::SupportsWorldType(EWorldType::Game));
		TestTrue(TEXT("PIE supported"), UCourseSubsystem::SupportsWorldType(EWorldType::PIE));
		TestFalse(TEXT("editor unsupported"), UCourseSubsystem::SupportsWorldType(EWorldType::Editor));
		TestFalse(TEXT("preview unsupported"), UCourseSubsystem::SupportsWorldType(EWorldType::EditorPreview));
		TestTrue(TEXT("full-screen HUD root is transparent"), FMath::IsNearlyZero(UWorkoutHudWidget::RootBackgroundColor().A));
		const float PanelAlpha = UWorkoutHudWidget::MetricPanelBackgroundColor().A;
		TestTrue(TEXT("metric panel remains visibly translucent"), PanelAlpha > 0.0f && PanelAlpha < 0.5f);
		TestTrue(TEXT("estimated visible"), UWorkoutHudWidget::AnimationLabelVisibility(ECourseAnimationQuality::Estimated) == ESlateVisibility::HitTestInvisible);
		TestTrue(TEXT("primary hidden"), UWorkoutHudWidget::AnimationLabelVisibility(ECourseAnimationQuality::Primary) == ESlateVisibility::Hidden);
		TestTrue(TEXT("unavailable hidden"), UWorkoutHudWidget::AnimationLabelVisibility(ECourseAnimationQuality::Unavailable) == ESlateVisibility::Hidden); });

	It("shows a persistent preliminary metric-accuracy disclosure", [this]()
	   { TestEqual(TEXT("metric accuracy notice"), FString(UWorkoutHudWidget::MetricAccuracyNotice()), FString(TEXT("Metric-display accuracy is preliminary and may be limited."))); });

	It("smooths oar transforms without allowing a stalled frame to snap", [this]()
	   {
		const float FirstStep = AGrayBoxCourseActor::InterpolateOarMotion(-34.0f, 42.0f, 1.0f / 60.0f);
		TestTrue(TEXT("oar advances"), FirstStep > -34.0f);
		TestTrue(TEXT("oar does not reach target in one normal frame"), FirstStep < 42.0f);
		const float StalledStep = AGrayBoxCourseActor::InterpolateOarMotion(-34.0f, 42.0f, 1.0f);
		TestTrue(TEXT("stalled frame remains bounded away from target"), StalledStep < 42.0f); });

	It("cannot mutate authoritative workout facts", [this]()
	   {
		const uint64 OfficialDistance = 2'250'000;
		FCoursePresentationSnapshot Presentation;
		Presentation.MeasuredDistanceMm = OfficialDistance;
		Presentation.PresentedDistanceMm = 2'251'000.0;
		Presentation.WrappedCourseDistanceMm = 251'000.0;
		Course->ApplyPresentation(Presentation);
		TestEqual(TEXT("local official distance unchanged"), OfficialDistance, 2'250'000ULL);
		TestEqual(TEXT("presentation input remains cumulative"), Presentation.MeasuredDistanceMm, OfficialDistance); });

	It("allows peer course selection only while the route is available and no workout is active", [this]()
	   {
		TestTrue(TEXT("Standard is selectable while idle"), UContentSubsystem::CanSelectRoute(TEXT("route.standard.2k"), false, false));
		TestFalse(TEXT("Han is unavailable before validated content activates"), UContentSubsystem::CanSelectRoute(TEXT("route.han-river.5k"), false, false));
		TestTrue(TEXT("Han is selectable after validated content activates"), UContentSubsystem::CanSelectRoute(TEXT("route.han-river.5k"), true, false));
		TestFalse(TEXT("Standard selection is blocked during a workout"), UContentSubsystem::CanSelectRoute(TEXT("route.standard.2k"), true, true));
		TestFalse(TEXT("unknown route is not selectable"), UContentSubsystem::CanSelectRoute(TEXT("route.unknown"), true, false)); });
}

#endif // WITH_DEV_AUTOMATION_TESTS
