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

namespace
{
	ContentRuntime::FRouteDefinition MakeTestHanRoute()
	{
		ContentRuntime::FRouteDefinition Route = ContentRuntime::BuiltInStandardRouteDefinition();
		Route.SchemaVersion = ContentRuntime::RouteDefinitionSchemaV2;
		Route.RouteId = "route.han-river.5k";
		Route.SemanticVersion = "2.0.0";
		Route.ContentSetId = "han-river-alpha-2";
		Route.LengthMm = 5'000'000;
		Route.bClosed = false;
		Route.Checkpoints = {
			{"sebit-lookback", 650'000},
			{"dongjak-span", 1'450'000},
			{"nodeulseom", 3'000'000},
			{"hangang-bridge", 3'650'000},
			{"wonhyo-finish", 5'000'000},
		};
		Route.PresentationPath = ContentRuntime::FRoutePresentationPath{};
		auto &Path = *Route.PresentationPath;
		Path.PathFormatVersion = ContentRuntime::PresentationPathFormatV1;
		Path.OwningRouteId = Route.RouteId;
		for (std::uint32_t Index = 0; Index < 40; ++Index)
		{
			const std::uint64_t DistanceMm = Route.LengthMm * Index / 39ULL;
			ContentRuntime::FRouteHermiteControlPoint Point;
			Point.PointId = "han-test-" + std::to_string(Index);
			Point.RouteDistanceMm = DistanceMm;
			Point.PositionMm = {static_cast<std::int64_t>(DistanceMm), 0, 0};
			Point.ArriveTangentMm = {static_cast<std::int64_t>(Route.LengthMm / 39), 0, 0};
			Point.LeaveTangentMm = Point.ArriveTangentMm;
			Path.ControlPoints.push_back(Point);
			Path.ArcLengthLookup.push_back({DistanceMm, FMath::Min(Index, 38U), Index == 39 ? 1'000'000U : 0U});
		}
		return Route;
	}
} // namespace

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

	It("builds an open, landmarked Han River presentation with visible checkpoints", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		TestTrue(TEXT("Han route receives dedicated presentation"), HanCourse->HasHanRiverEnvironmentForTesting());
		TestTrue(TEXT("Han route has bridges, banks, skyline, and sparse buoys"), HanCourse->GetHanLandmarkCountForTesting() >= 50);
		TestEqual(TEXT("Han route shows one label for every signed checkpoint"), HanCourse->GetMarkerCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size()));
		TestEqual(TEXT("every checkpoint has two distinct river buoys"), HanCourse->GetBuoyCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size() * 2));
		TestTrue(TEXT("orange and white checkpoint buoys remain visible"), HanCourse->AreRouteBuoysVisibleForTesting());
		TestEqual(TEXT("every signed Han control point has a visible beacon gate"), HanCourse->GetControlPointMarkerCountForTesting(), static_cast<int32>(HanRoute.PresentationPath->ControlPoints.size() * 2));
		TestTrue(TEXT("cyan Han control-point beacon gates remain visible"), HanCourse->AreControlPointMarkersVisibleForTesting());
		TestTrue(TEXT("bright directional arrow is attached at the boat bow"), !HanCourse->GetCheckpointArrowForwardForTesting().IsNearlyZero());
		TestFalse(TEXT("Han endpoint stays open rather than wrapping"), HanCourse->GetCourseTransform(0.0).GetLocation().Equals(HanCourse->GetCourseTransform(5'000'000.0).GetLocation(), 1.0));
		HanCourse->Destroy(); });

	It("keeps the boat anchor on every signed route checkpoint", [this]()
	   {
		auto VerifyCheckpoints = [this](AGrayBoxCourseActor &Actor,
									 const ContentRuntime::FRouteDefinition &Route,
									 const TCHAR *RouteName)
		{
			TestTrue(FString::Printf(TEXT("%s defines checkpoints"), RouteName), !Route.Checkpoints.empty());
			for (const ContentRuntime::FRouteCheckpoint &Checkpoint : Route.Checkpoints)
			{
				FCoursePresentationRuntime Runtime;
				Runtime.SelectRoute(Route);
				FCourseTelemetryInput Telemetry;
				Telemetry.bHasSession = true;
				Telemetry.bHasValidSample = true;
				Telemetry.AcceptedSampleGeneration = 1;
				Telemetry.MeasuredDistanceMm = Checkpoint.DistanceMm;
				Telemetry.SpeedMmPerS = 0;
				Telemetry.SampleMonotonicNs = 1'000'000'000ULL;
				Telemetry.bConnected = true;
				Telemetry.SessionState = ERowingSessionState::Active;
				Telemetry.WorkoutState = ERowingWorkoutState::Active;
				Telemetry.RowingState = ERowingState::Active;
				const FCoursePresentationSnapshot Snapshot = Runtime.Update(Telemetry, Telemetry.SampleMonotonicNs);
				Actor.ApplyPresentation(Snapshot);

				const FTransform Expected = Actor.GetCourseTransform(static_cast<double>(Checkpoint.DistanceMm));
				TestTrue(FString::Printf(TEXT("%s checkpoint %s keeps the boat on the path"),
					RouteName, UTF8_TO_TCHAR(Checkpoint.CheckpointId.c_str())),
					Actor.GetBoatTransformForTesting().GetLocation().Equals(Expected.GetLocation(), 0.01));
				TestTrue(FString::Printf(TEXT("%s checkpoint %s points the boat along the course"),
					RouteName, UTF8_TO_TCHAR(Checkpoint.CheckpointId.c_str())),
					Actor.GetBoatTransformForTesting().GetUnitAxis(EAxis::X).Equals(Expected.GetUnitAxis(EAxis::X), 0.0001));
			}
		};

		const ContentRuntime::FRouteDefinition StandardRoute = ContentRuntime::BuiltInStandardRouteDefinition();
		VerifyCheckpoints(*Course, StandardRoute, TEXT("Standard"));
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		const ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		VerifyCheckpoints(*HanCourse, HanRoute, TEXT("Han"));
		TestEqual(TEXT("each Han checkpoint has a visible marker"), HanCourse->GetMarkerCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size()));
		TestEqual(TEXT("two buoy objects are created for each Han checkpoint"), HanCourse->GetBuoyCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size() * 2));
		TestTrue(TEXT("both sides of every checkpoint gate remain visible"), HanCourse->AreRouteBuoysVisibleForTesting());
		TestEqual(TEXT("two beacon objects are created for each signed Han control point"), HanCourse->GetControlPointMarkerCountForTesting(), static_cast<int32>(HanRoute.PresentationPath->ControlPoints.size() * 2));
		TestTrue(TEXT("both sides of every control-point beacon gate remain visible"), HanCourse->AreControlPointMarkersVisibleForTesting());
		for (int32 CheckpointIndex = 0; CheckpointIndex < static_cast<int32>(HanRoute.Checkpoints.size()); ++CheckpointIndex)
		{
			const ContentRuntime::FRouteCheckpoint &Checkpoint = HanRoute.Checkpoints[CheckpointIndex];
			const FTransform PathTransform = HanCourse->GetCourseTransform(static_cast<double>(Checkpoint.DistanceMm));
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				const int32 Side = SideIndex == 0 ? -1 : 1;
				const FVector ExpectedBuoy = PathTransform.GetLocation() + PathTransform.GetUnitAxis(EAxis::Y) * (Side * 900.0) + FVector(0.0, 0.0, 140.0);
				TestTrue(FString::Printf(TEXT("checkpoint buoy %s side %d is separate and path-aligned"), UTF8_TO_TCHAR(Checkpoint.CheckpointId.c_str()), Side),
					HanCourse->GetMarkerLocationForTesting(CheckpointIndex * 2 + SideIndex).Equals(ExpectedBuoy, 0.01));
			}

			FCoursePresentationRuntime ArrowRuntime;
			ArrowRuntime.SelectRoute(HanRoute);
			FCourseTelemetryInput ArrowInput;
			ArrowInput.bHasSession = true;
			ArrowInput.bHasValidSample = true;
			ArrowInput.AcceptedSampleGeneration = CheckpointIndex + 1;
			ArrowInput.MeasuredDistanceMm = Checkpoint.DistanceMm;
			ArrowInput.SpeedMmPerS = 0;
			ArrowInput.SampleMonotonicNs = 1'000'000'000ULL;
			ArrowInput.bConnected = true;
			ArrowInput.SessionState = ERowingSessionState::Active;
			ArrowInput.WorkoutState = ERowingWorkoutState::Active;
			ArrowInput.RowingState = ERowingState::Active;
			const FCoursePresentationSnapshot ArrowSnapshot = ArrowRuntime.Update(ArrowInput, ArrowInput.SampleMonotonicNs);
			HanCourse->ApplyPresentation(ArrowSnapshot);
			const uint64 NextDistanceMm = CheckpointIndex + 1 < static_cast<int32>(HanRoute.Checkpoints.size())
										  ? HanRoute.Checkpoints[CheckpointIndex + 1].DistanceMm
										  : HanRoute.LengthMm;
			const FCoursePathSample NextPoint = EvaluateCoursePath(HanRoute, static_cast<double>(NextDistanceMm));
			const FVector Direction(NextPoint.PositionMm.X - ArrowSnapshot.PathPositionMm.X,
									 NextPoint.PositionMm.Y - ArrowSnapshot.PathPositionMm.Y,
									 NextPoint.PositionMm.Z - ArrowSnapshot.PathPositionMm.Z);
			TestTrue(FString::Printf(TEXT("bow arrow points from %s to the next checkpoint"), UTF8_TO_TCHAR(Checkpoint.CheckpointId.c_str())),
				HanCourse->GetCheckpointArrowForwardForTesting().Equals(Direction.GetSafeNormal(), 0.001));
		}
		for (int32 ControlPointIndex = 0; ControlPointIndex < static_cast<int32>(HanRoute.PresentationPath->ControlPoints.size()); ++ControlPointIndex)
		{
			const ContentRuntime::FRouteHermiteControlPoint &ControlPoint = HanRoute.PresentationPath->ControlPoints[ControlPointIndex];
			const FTransform PathTransform = HanCourse->GetCourseTransform(static_cast<double>(ControlPoint.RouteDistanceMm));
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				const int32 Side = SideIndex == 0 ? -1 : 1;
				const FVector ExpectedBeacon = PathTransform.GetLocation() + PathTransform.GetUnitAxis(EAxis::Y) * (Side * 650.0) + FVector(0.0, 0.0, 175.0);
				TestTrue(FString::Printf(TEXT("control-point beacon %s side %d is path-aligned"), UTF8_TO_TCHAR(ControlPoint.PointId.c_str()), Side),
					HanCourse->GetControlPointMarkerLocationForTesting(ControlPointIndex * 2 + SideIndex).Equals(ExpectedBeacon, 0.01));
			}
		}
		HanCourse->Destroy(); });

	It("preserves the world-authored Han level frame", [this]()
	   {
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
		auto &Path = *HanRoute.PresentationPath;
		Path.RouteLocalOriginMm = {1'000'000, 2'000'000, 3'000};
		Path.RouteLocalYawMicroradians = 1'570'796;
		Path.ControlPoints.front().PositionMm = {400'000, 0, 0};
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->InitializeCourse();
		const FTransform Transform = HanCourse->GetAuthoredLevelTransform();
		TestTrue(TEXT("world-authored level is not translated or rotated a second time"), Transform.Equals(FTransform::Identity, 0.0));
		HanCourse->Destroy(); });

	It("builds the Han kit when the actor is spawned into a world that has begun play", [this]()
	   {
		// In a running world SpawnActor calls BeginPlay immediately, which used to
		// build the Standard course before the route could be configured.
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
		AGrayBoxCourseActor *HanCourse = World->SpawnActorDeferred<AGrayBoxCourseActor>(AGrayBoxCourseActor::StaticClass(), FTransform::Identity);
		HanCourse->ConfigureRoute(HanRoute);
		HanCourse->FinishSpawning(FTransform::Identity);
		HanCourse->InitializeCourse();
		TestTrue(TEXT("Han route is honored after BeginPlay"), HanCourse->HasHanRiverEnvironmentForTesting());
		TestEqual(TEXT("Han checkpoint labels are present after BeginPlay"), HanCourse->GetMarkerCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size()));
		TestEqual(TEXT("paired checkpoint buoys are present after BeginPlay"), HanCourse->GetBuoyCountForTesting(), static_cast<int32>(HanRoute.Checkpoints.size() * 2));
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
		ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
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
		ContentRuntime::FRouteDefinition HanRoute = MakeTestHanRoute();
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

	It("adapts anchor, cosmetic root, and level camera snapshots on both courses", [this]()
	   {
		auto VerifySnapshot = [this](AGrayBoxCourseActor &Actor, const TCHAR *RouteName)
		{
			FCoursePresentationSnapshot Snapshot;
			Snapshot.PathPositionMm = {10'000.0, 20'000.0, 300.0};
			Snapshot.DampedYawRadians = PI / 2.0;
			Snapshot.HullRollDegrees = -1.2;
			Snapshot.HullPitchDegrees = 0.7;
			Snapshot.AmbientBobMm = 12.0;
			Snapshot.StrokeHeaveMm = 5.0;
			Snapshot.CameraPose.PositionMm = {1'000.0, 2'000.0, 3'000.0};
			Snapshot.CameraPose.LookAtMm = {5'000.0, 2'000.0, 3'000.0};
			Snapshot.CameraPose.FieldOfViewDegrees = 68.0;
			Actor.ApplyPresentation(Snapshot);

			const FTransform Anchor = Actor.GetBoatTransformForTesting();
			TestTrue(FString::Printf(TEXT("%s converts path millimetres once"), RouteName),
				Anchor.GetLocation().Equals(FVector(1'000.0, 2'000.0, 30.0), 0.01));
			FVector VisibleBoat;
			TestTrue(FString::Printf(TEXT("%s reports a visible hull world location"), RouteName),
				Actor.TryGetVisibleBoatWorldLocation(VisibleBoat));
			TestTrue(FString::Printf(TEXT("%s includes the visible hull bob and heave"), RouteName),
				VisibleBoat.Equals(Anchor.GetLocation() + FVector(0.0, 0.0, 1.7), 0.01));
			TestTrue(FString::Printf(TEXT("%s keeps the authoritative anchor level"), RouteName),
				FMath::IsNearlyZero(Anchor.Rotator().Pitch, 0.01) &&
				FMath::IsNearlyZero(Anchor.Rotator().Roll, 0.01) &&
				FMath::IsNearlyEqual(Anchor.Rotator().Yaw, 90.0, 0.01));
			const FTransform Visual = Actor.GetVisualRootRelativeTransformForTesting();
			TestTrue(FString::Printf(TEXT("%s confines bob and heave to the visual child"), RouteName),
				FMath::IsNearlyEqual(Visual.GetLocation().Z, 1.7, 0.01));
			TestTrue(FString::Printf(TEXT("%s confines roll and pitch to the visual child"), RouteName),
				FMath::IsNearlyEqual(Visual.Rotator().Pitch, 0.7, 0.01) &&
				FMath::IsNearlyEqual(Visual.Rotator().Roll, -1.2, 0.01));
			const FTransform Camera = Actor.GetCameraTransformForTesting();
			TestTrue(FString::Printf(TEXT("%s converts the numeric camera pose once"), RouteName),
				Camera.GetLocation().Equals(FVector(100.0, 200.0, 300.0), 0.01));
			TestTrue(FString::Printf(TEXT("%s camera horizon remains level"), RouteName),
				FMath::IsNearlyZero(Camera.Rotator().Roll, 0.01));
			TestTrue(FString::Printf(TEXT("%s applies the numeric FOV"), RouteName),
				FMath::IsNearlyEqual(Actor.GetCameraFieldOfViewForTesting(), 68.0, 0.01));
		};

		VerifySnapshot(*Course, TEXT("Standard"));
		AGrayBoxCourseActor *HanCourse = World->SpawnActor<AGrayBoxCourseActor>();
		HanCourse->ConfigureRoute(MakeTestHanRoute());
		HanCourse->InitializeCourse();
		VerifySnapshot(*HanCourse, TEXT("Han"));
		HanCourse->Destroy(); });

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
