#include "Misc/AutomationTest.h"

#include "CourseSubsystem.h"
#include "ContentSubsystem.h"
#include "GrayBoxCourseActor.h"
#include "WorkoutHudWidget.h"

#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FCoursePresentationSpec, "VirtualRowing.CoursePresentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
UWorld *World = nullptr;
AGrayBoxCourseActor *Course = nullptr;
END_DEFINE_SPEC(FCoursePresentationSpec)

void FCoursePresentationSpec::Define()
{
	BeforeEach([this]()
			   {
			World = UWorld::CreateWorld(EWorldType::Game, false);
			World->InitializeNewWorld(UWorld::InitializationValues()
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false));
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
		TestEqual(TEXT("visible edge covers every spline segment"), Course->GetCourseEdgeSegmentCountForTesting(), 32); });

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
