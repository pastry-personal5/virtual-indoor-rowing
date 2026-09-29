#include "GrayBoxCourseActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 MarkerCount = 8;
	constexpr float OarInterpolationSpeed = 16.0f;
	constexpr float MaxOarInterpolationStepSeconds = 1.0f / 30.0f;
	constexpr int32 HullWakePoolSize = 12;
	constexpr int32 OarRipplePoolSize = 4;
	constexpr uint64 HullWakeLifetimeNs = 1'400'000'000ULL;
	constexpr uint64 OarRippleLifetimeNs = 650'000'000ULL;
	constexpr double HanRiverWidthCm = 42'000.0;
	constexpr double HanBankOffsetCm = 25'000.0;
	constexpr double HanCheckpointBuoyOffsetCm = 900.0;
	constexpr double HanControlPointBeaconOffsetCm = 650.0;
	TOptional<bool> GReduceMotionOverrideForTesting;

	float SmoothStep(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

} // namespace

AGrayBoxCourseActor::AGrayBoxCourseActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CourseRoot"));
	SetRootComponent(SceneRoot);
	CourseSpline = CreateDefaultSubobject<USplineComponent>(TEXT("CourseSpline"));
	CourseSpline->SetupAttachment(SceneRoot);
	BoatRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BoatRoot"));
	BoatRoot->SetupAttachment(SceneRoot);
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BoatVisualRoot"));
	VisualRoot->SetupAttachment(BoatRoot);
	InspectionCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InspectionCamera"));
	InspectionCamera->SetupAttachment(SceneRoot);
	InspectionCamera->SetFieldOfView(50.0f);
	InspectionCamera->bUsePawnControlRotation = false;
	InspectionCamera->SetActive(true);
	// /Engine/Maps/Entry deliberately has no authored lighting. The generated course
	// uses the engine's lit primitive material, so without a code-owned light every
	// surface is black even though the actor and camera are valid.
	CourseLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("CourseLight"));
	CourseLight->SetupAttachment(SceneRoot);
	CourseLight->SetMobility(EComponentMobility::Movable);
	CourseLight->SetRelativeRotation(FRotator(-55.0f, -35.0f, 0.0f));
	CourseLight->SetIntensity(8.0f);
	CourseLight->SetLightColor(FLinearColor(1.0f, 0.94f, 0.86f));
	CourseLight->SetCastShadows(false);

	// Keep the primitive assets as hard CDO references. Runtime LoadObject paths are
	// not a reliable cooking contract for a code-only actor in a Shipping package.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> OarFinder(TEXT("/Game/Boat/Meshes/SM_ScullOar.SM_ScullOar"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HullFinder(TEXT("/Game/Boat/Meshes/SM_ScullHull.SM_ScullHull"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RowerTorsoFinder(TEXT("/Game/Boat/Character/SM_RowerTorso.SM_RowerTorso"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RowerArmFinder(TEXT("/Game/Boat/Character/SM_RowerArm.SM_RowerArm"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RowerLegFinder(TEXT("/Game/Boat/Character/SM_RowerLeg.SM_RowerLeg"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RowerShoeFinder(TEXT("/Game/Boat/Character/SM_RowerShoe.SM_RowerShoe"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	PlaneMesh = PlaneFinder.Object;
	OarMesh = OarFinder.Object;
	HullMesh = HullFinder.Object;
	RowerTorsoMesh = RowerTorsoFinder.Object;
	RowerArmMesh = RowerArmFinder.Object;
	RowerLegMesh = RowerLegFinder.Object;
	RowerShoeMesh = RowerShoeFinder.Object;
	CourseMaterial = MaterialFinder.Object;
	// Water is authored as plain cooked material data by Scripts/build_water_material.py.
	// If it is missing, the flat primitive water stays.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterFinder(TEXT("/Game/Water/M_CourseWater.M_CourseWater"));
	WaterMaterial = WaterFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterInteractionFinder(TEXT("/Game/Water/M_WaterInteraction.M_WaterInteraction"));
	WaterInteractionMaterial = WaterInteractionFinder.Object;
}

void AGrayBoxCourseActor::BeginPlay()
{
	Super::BeginPlay();
	InitializeCourse();
}

void AGrayBoxCourseActor::ConfigureRoute(const ContentRuntime::FRouteDefinition &InRoute)
{
	if (bInitialized || InRoute.RouteId.empty() || InRoute.LengthMm == 0)
		return;
	Route = InRoute;
	bIsHanRiverRoute = Route.RouteId == "route.han-river.5k";
}

UStaticMeshComponent *AGrayBoxCourseActor::MakeMesh(const TCHAR *Name,
													UStaticMesh *Mesh,
													USceneComponent *Parent,
													const FVector &Scale,
													const FLinearColor &Color)
{
	UStaticMeshComponent *Component = NewObject<UStaticMeshComponent>(this, Name);
	Component->SetupAttachment(Parent);
	Component->SetStaticMesh(Mesh);
	Component->SetRelativeScale3D(Scale);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	if (CourseMaterial && Mesh != OarMesh.Get() && Mesh != HullMesh.Get() &&
		Mesh != RowerTorsoMesh.Get() && Mesh != RowerArmMesh.Get() &&
		Mesh != RowerLegMesh.Get() && Mesh != RowerShoeMesh.Get())
	{
		UMaterialInstanceDynamic *Dynamic = UMaterialInstanceDynamic::Create(CourseMaterial, Component);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Dynamic);
	}
	AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

UStaticMeshComponent *AGrayBoxCourseActor::MakeHanLandmark(const TCHAR *Name,
														   UStaticMesh *Mesh,
														   const FVector &Scale,
														   const FLinearColor &Color,
														   const FVector &Location)
{
	UStaticMeshComponent *Component = MakeMesh(Name, Mesh, SceneRoot, Scale, Color);
	Component->SetWorldLocation(Location);
	HanLandmarkMeshes.Add(Component);
	return Component;
}

void AGrayBoxCourseActor::BuildHanRiverEnvironment(UStaticMesh *Cube, UStaticMesh *Cylinder)
{
	// This is deliberately an original, low-detail presentation kit: it communicates
	// the six authored beats without copying landmark architecture or adding collision,
	// navigation, current, or any path that could affect official workout facts.
	const FLinearColor BridgeConcrete(0.20f, 0.25f, 0.31f);
	const FLinearColor WarmLight(0.93f, 0.53f, 0.22f);
	const FLinearColor TreeCanopy(0.05f, 0.16f, 0.12f);
	const FLinearColor Skyline(0.11f, 0.17f, 0.25f);

	auto AddBridge = [this, Cube, Cylinder, &BridgeConcrete, &WarmLight](const TCHAR *Prefix, double DistanceMm, float HalfSpanScale)
	{
		const FTransform Beat = GetCourseTransform(DistanceMm);
		const FVector Centre = Beat.GetLocation();
		const FVector Right = Beat.GetUnitAxis(EAxis::Y);
		MakeHanLandmark(*FString::Printf(TEXT("%sDeck"), Prefix), Cube, FVector(5.0f, HalfSpanScale, 0.22f), BridgeConcrete, Centre + FVector(0.0, 0.0, 1'150.0));
		for (int32 Side : {-1, 1})
		{
			MakeHanLandmark(*FString::Printf(TEXT("%sPier%d"), Prefix, Side), Cube, FVector(1.5f, 2.2f, 10.0f), BridgeConcrete, Centre + Right * (Side * 13'000.0) + FVector(0.0, 0.0, 450.0));
			for (int32 LightIndex = 0; LightIndex < 3; ++LightIndex)
			{
				const float Along = static_cast<float>(LightIndex - 1) * 700.0f;
				MakeHanLandmark(*FString::Printf(TEXT("%sLight%d%d"), Prefix, Side, LightIndex), Cylinder, FVector(0.15f, 0.15f, 2.0f), WarmLight, Centre + Right * (Side * 10'500.0) + Beat.GetUnitAxis(EAxis::X) * Along + FVector(0.0, 0.0, 1'250.0));
			}
		}
	};

	AddBridge(TEXT("Banpo"), 0.0, 210.0f);
	AddBridge(TEXT("Dongjak"), 1'450'000.0, 190.0f);
	AddBridge(TEXT("Finish"), static_cast<double>(Route.LengthMm), 220.0f);

	const TArray<double> BeatDistances = {0.0, 650'000.0, 1'450'000.0, static_cast<double>(Route.LengthMm)};
	for (int32 BeatIndex = 0; BeatIndex < BeatDistances.Num(); ++BeatIndex)
	{
		const FTransform Beat = GetCourseTransform(BeatDistances[BeatIndex]);
		const FVector Right = Beat.GetUnitAxis(EAxis::Y);
		const FVector Forward = Beat.GetUnitAxis(EAxis::X);
		for (int32 Side : {-1, 1})
		{
			const FVector Bank = Beat.GetLocation() + Right * (Side * HanBankOffsetCm);
			MakeHanLandmark(*FString::Printf(TEXT("BankTree%d%d"), BeatIndex, Side), Cylinder, FVector(9.0f, 9.0f, 3.5f), TreeCanopy, Bank + Forward * (Side * 1'600.0) + FVector(0.0, 0.0, 500.0));
			MakeHanLandmark(*FString::Printf(TEXT("Skyline%d%d"), BeatIndex, Side), Cube, FVector(7.0f, 7.0f, 24.0f + BeatIndex * 2.0f), Skyline, Bank + Forward * (Side * 5'000.0) + FVector(0.0, 0.0, 1'200.0));
		}
	}

	// Some Sevit is suggested with three original glowing volumes. They remain
	// deliberately abstract rather than functioning as an architectural replica.
	const FTransform Sebit = GetCourseTransform(650'000.0);
	const FVector SebitRight = Sebit.GetUnitAxis(EAxis::Y);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeHanLandmark(*FString::Printf(TEXT("SebitVolume%d"), Index), Cube, FVector(7.0f + Index, 5.0f, 3.0f + Index), WarmLight, Sebit.GetLocation() - SebitRight * (17'000.0 + Index * 2'200.0) + FVector(Index * 700.0, 0.0, 450.0));
	}

	// Sparse non-collision buoys provide scale without occupying the rowing line.
	for (double DistanceMm : {650'000.0, static_cast<double>(Route.LengthMm)})
	{
		const FTransform Beat = GetCourseTransform(DistanceMm);
		MakeHanLandmark(*FString::Printf(TEXT("RiverBuoy%.0f"), DistanceMm), Cylinder, FVector(0.35f, 0.35f, 1.4f), WarmLight, Beat.GetLocation() + Beat.GetUnitAxis(EAxis::Y) * 5'500.0 + FVector(0.0, 0.0, 120.0));
	}
}

void AGrayBoxCourseActor::InitializeCourse()
{
	if (bInitialized)
		return;

	UStaticMesh *Cube = CubeMesh;
	UStaticMesh *Cylinder = CylinderMesh;
	if (!Cube || !Cylinder)
		return;
	bInitialized = true;
	if (bIsHanRiverRoute)
	{
		CourseLight->SetRelativeRotation(FRotator(-34.0f, 42.0f, 0.0f));
		CourseLight->SetIntensity(3.2f);
		CourseLight->SetLightColor(FLinearColor(0.38f, 0.51f, 0.78f));
	}

	CourseSpline->ClearSplinePoints(false);
	const int32 AuthoredPointCount = Route.PresentationPath ? static_cast<int32>(Route.PresentationPath->ControlPoints.size()) : 0;
	const int32 PointCount = Route.bClosed ? FMath::Max(0, AuthoredPointCount - 1) : AuthoredPointCount;
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const double DistanceMm = static_cast<double>(Route.PresentationPath->ControlPoints[Index].RouteDistanceMm);
		const FCoursePathSample Sample = EvaluateCoursePath(Route, DistanceMm);
		CourseSpline->AddSplinePoint(FVector(Sample.PositionMm.X, Sample.PositionMm.Y, Sample.PositionMm.Z) / 10.0, ESplineCoordinateSpace::Local, false);
		CourseSpline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}
	CourseSpline->SetClosedLoop(Route.bClosed, true);
	const int32 EdgeCount = Route.bClosed ? PointCount : PointCount - 1;
	for (int32 Index = 0; Index < EdgeCount; ++Index)
	{
		const int32 NextIndex = Route.bClosed ? (Index + 1) % PointCount : Index + 1;
		USplineMeshComponent *CourseEdge = NewObject<USplineMeshComponent>(this, *FString::Printf(TEXT("CourseEdge%d"), Index));
		// Dynamic spline edges inherit a movable course root. A static child cannot
		// attach to that root, leaving the visual edge out of the hierarchy.
		CourseEdge->SetMobility(EComponentMobility::Movable);
		CourseEdge->SetupAttachment(SceneRoot);
		CourseEdge->SetStaticMesh(Cube);
		CourseEdge->SetForwardAxis(ESplineMeshAxis::X, false);
		CourseEdge->SetStartAndEnd(
			CourseSpline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local),
			CourseSpline->GetTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local),
			CourseSpline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local),
			CourseSpline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local),
			false);
		CourseEdge->SetStartScale(FVector2D(0.30f, 0.10f), false);
		CourseEdge->SetEndScale(FVector2D(0.30f, 0.10f), false);
		CourseEdge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CourseEdge->SetCastShadow(false);
		if (CourseMaterial)
		{
			UMaterialInstanceDynamic *Dynamic = UMaterialInstanceDynamic::Create(CourseMaterial, CourseEdge);
			Dynamic->SetVectorParameterValue(TEXT("Color"), bIsHanRiverRoute ? FLinearColor(0.08f, 0.19f, 0.28f) : FLinearColor(0.92f, 0.82f, 0.18f));
			CourseEdge->SetMaterial(0, Dynamic);
		}
		CourseEdge->SetVisibility(!bIsHanRiverRoute);
		AddInstanceComponent(CourseEdge);
		CourseEdge->RegisterComponent();
		CourseEdge->UpdateMesh();
		CourseEdgeMeshes.Add(CourseEdge);
	}

	const FLinearColor WaterColor = bIsHanRiverRoute ? FLinearColor(0.025f, 0.10f, 0.18f) : FLinearColor(0.03f, 0.20f, 0.35f);
	UStaticMeshComponent *Water = MakeMesh(TEXT("Water"), Cube, SceneRoot, bIsHanRiverRoute ? FVector(5'400.0, HanRiverWidthCm / 100.0, 0.1) : FVector(680.0, 205.0, 0.1), WaterColor);
	if (WaterMaterial)
	{
		// Ambient, world-anchored animation only: it reads no workout fact, so it can
		// neither imply speed nor synthesize distance.
		UMaterialInstanceDynamic *Dynamic = UMaterialInstanceDynamic::Create(WaterMaterial, Water);
		Dynamic->SetVectorParameterValue(TEXT("DeepColor"), WaterColor);
		ApplyWaterMotion(*Dynamic, ReduceMotionRequested());
		Water->SetMaterial(0, Dynamic);
	}
	Water->SetRelativeLocation(FVector(0.0, 0.0, -10.0));
	Water->SetVisibility(!WaterHiddenForBenchmark());
	WaterSurface = Water;
	EnvironmentMeshes.Add(Water);
	for (int32 Side : {-1, 1})
	{
		UStaticMeshComponent *Shore = MakeMesh(Side < 0 ? TEXT("ShoreNorth") : TEXT("ShoreSouth"), Cube, SceneRoot, bIsHanRiverRoute ? FVector(5'400.0, 40.0, 0.35) : FVector(680.0, 12.0, 0.35), bIsHanRiverRoute ? FLinearColor(0.06f, 0.12f, 0.10f) : FLinearColor(0.16f, 0.34f, 0.12f));
		Shore->SetRelativeLocation(FVector(0.0, Side * (bIsHanRiverRoute ? HanBankOffsetCm : 21'000.0), 15.0));
		EnvironmentMeshes.Add(Shore);
	}
	if (bIsHanRiverRoute)
		BuildHanRiverEnvironment(Cube, Cylinder);

	const int32 RouteMarkerCount = bIsHanRiverRoute ? static_cast<int32>(Route.Checkpoints.size()) : (Route.bClosed ? MarkerCount : FMath::FloorToInt(static_cast<double>(Route.LengthMm) / 250'000.0));
	for (int32 Index = 0; Index < RouteMarkerCount; ++Index)
	{
		const bool bRouteCheckpoint = bIsHanRiverRoute;
		const uint64 DistanceMm = bRouteCheckpoint ? Route.Checkpoints[Index].DistanceMm : static_cast<uint64>((Index + 1) * 250'000);
		const FTransform CourseTransform = GetCourseTransform(DistanceMm);
		const FVector Right = CourseTransform.GetUnitAxis(EAxis::Y);
		FVector LabelLocation;
		if (bRouteCheckpoint)
		{
			const FString CheckpointId = UTF8_TO_TCHAR(Route.Checkpoints[Index].CheckpointId.c_str());
			const FLinearColor BuoyOrange(1.0f, 0.12f, 0.015f);
			const FLinearColor BuoyWhite(1.0f, 0.92f, 0.78f);
			for (int32 Side : {-1, 1})
			{
				const FString BuoyName = FString::Printf(TEXT("Checkpoint_%s_Buoy%d"), *CheckpointId, Side);
				const FVector BuoyLocation = CourseTransform.GetLocation() + Right * (Side * HanCheckpointBuoyOffsetCm) + FVector(0.0, 0.0, 140.0);
				UStaticMeshComponent *Buoy = MakeMesh(*BuoyName, Cylinder, SceneRoot, FVector(0.95, 0.95, 2.8), BuoyOrange);
				Buoy->SetWorldLocation(BuoyLocation);
				EnvironmentMeshes.Add(Buoy);
				RouteMarkers.Add(Buoy);

				for (int32 Band = 0; Band < 2; ++Band)
				{
					const FString BandName = FString::Printf(TEXT("Checkpoint_%s_Buoy%d_Band%d"), *CheckpointId, Side, Band);
					UStaticMeshComponent *Stripe = MakeMesh(*BandName, Cylinder, SceneRoot, FVector(1.0, 1.0, 0.18), BuoyWhite);
					Stripe->SetWorldLocation(BuoyLocation + FVector(0.0, 0.0, Band == 0 ? -35.0 : 35.0));
					EnvironmentMeshes.Add(Stripe);
				}
			}
			LabelLocation = CourseTransform.GetLocation() + FVector(0.0, 0.0, 520.0);
		}
		else
		{
			const FVector MarkerLocation = CourseTransform.GetLocation() + Right * 350.0 + FVector(0.0, 0.0, 125.0);
			UStaticMeshComponent *Marker = MakeMesh(*FString::Printf(TEXT("Marker%d"), Index), Cylinder, SceneRoot, FVector(0.18, 0.18, 2.5), FLinearColor(0.95f, 0.75f, 0.08f));
			Marker->SetWorldLocation(MarkerLocation);
			EnvironmentMeshes.Add(Marker);
			RouteMarkers.Add(Marker);
			LabelLocation = MarkerLocation + FVector(0.0, 0.0, 200.0);
		}

		UTextRenderComponent *Label = NewObject<UTextRenderComponent>(this, *FString::Printf(TEXT("MarkerLabel%d"), Index));
		Label->SetupAttachment(SceneRoot);
		if (bRouteCheckpoint)
		{
			FString CheckpointName = UTF8_TO_TCHAR(Route.Checkpoints[Index].CheckpointId.c_str());
			CheckpointName.ReplaceInline(TEXT("-"), TEXT(" "));
			CheckpointName = CheckpointName.ToUpper();
			Label->SetText(FText::FromString(CheckpointName));
		}
		else
			Label->SetText(FText::FromString(FString::Printf(TEXT("%d m"), (Index + 1) * 250)));
		Label->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		Label->SetWorldSize(bRouteCheckpoint ? 175.0f : 90.0f);
		Label->SetTextRenderColor(bRouteCheckpoint ? FColor(255, 186, 92) : FColor::White);
		AddInstanceComponent(Label);
		Label->RegisterComponent();
		Label->SetWorldLocation(LabelLocation);
		MarkerLabels.Add(Label);
	}
	// The signed curve is a reviewable course contract, not merely an invisible
	// interpolation aid. Paired cyan beacon gates make every control point
	// legible in the world without putting an obstacle on the boat's waterline.
	if (bIsHanRiverRoute && Route.PresentationPath)
	{
		for (int32 Index = 0; Index < static_cast<int32>(Route.PresentationPath->ControlPoints.size()); ++Index)
		{
			const ContentRuntime::FRouteHermiteControlPoint &ControlPoint = Route.PresentationPath->ControlPoints[Index];
			const FTransform ControlTransform = GetCourseTransform(static_cast<double>(ControlPoint.RouteDistanceMm));
			const FVector Right = ControlTransform.GetUnitAxis(EAxis::Y);
			for (int32 Side : {-1, 1})
			{
				const FVector BeaconLocation = ControlTransform.GetLocation() + Right * (Side * HanControlPointBeaconOffsetCm) + FVector(0.0, 0.0, 175.0);
				UStaticMeshComponent *Beacon = MakeMesh(*FString::Printf(TEXT("RouteControl_%02d_Beacon%d"), Index + 1, Side), Cylinder, SceneRoot, FVector(0.65, 0.65, 3.5), FLinearColor(0.05f, 0.92f, 1.0f));
				Beacon->SetWorldLocation(BeaconLocation);
				EnvironmentMeshes.Add(Beacon);
				ControlPointMarkers.Add(Beacon);

				UStaticMeshComponent *Cap = MakeMesh(*FString::Printf(TEXT("RouteControl_%02d_Beacon%d_Cap"), Index + 1, Side), Cylinder, SceneRoot, FVector(0.72, 0.72, 0.25), FLinearColor::White);
				Cap->SetWorldLocation(BeaconLocation + FVector(0.0, 0.0, 105.0));
				EnvironmentMeshes.Add(Cap);
			}

			UTextRenderComponent *ControlLabel = NewObject<UTextRenderComponent>(this, *FString::Printf(TEXT("RouteControlLabel%d"), Index + 1));
			ControlLabel->SetupAttachment(SceneRoot);
			ControlLabel->SetText(FText::FromString(FString::Printf(TEXT("CP %02d"), Index + 1)));
			ControlLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
			ControlLabel->SetWorldSize(115.0f);
			ControlLabel->SetTextRenderColor(FColor(82, 234, 255));
			AddInstanceComponent(ControlLabel);
			ControlLabel->RegisterComponent();
			ControlLabel->SetWorldLocation(ControlTransform.GetLocation() + FVector(0.0, 0.0, 580.0));
			ControlPointLabels.Add(ControlLabel);
		}
	}

	Hull = MakeMesh(TEXT("Hull"), HullMesh ? HullMesh.Get() : Cube, VisualRoot, HullMesh ? FVector::OneVector : FVector(4.8, 0.28, 0.18), FLinearColor(0.85f, 0.88f, 0.92f));
	if (!HullMesh)
		Hull->SetRelativeLocation(FVector(0.0, 0.0, 45.0));
	if (bIsHanRiverRoute)
	{
		CheckpointArrowRoot = NewObject<USceneComponent>(this, TEXT("CheckpointArrowRoot"));
		CheckpointArrowRoot->SetupAttachment(BoatRoot);
		CheckpointArrowRoot->SetRelativeLocation(FVector(280.0, 0.0, 115.0));
		AddInstanceComponent(CheckpointArrowRoot);
		CheckpointArrowRoot->RegisterComponent();
		CheckpointArrowShaft = MakeMesh(TEXT("CheckpointArrowShaft"), Cube, CheckpointArrowRoot, FVector(0.95, 0.20, 0.16), FLinearColor(0.95f, 0.95f, 0.04f));
		CheckpointArrowShaft->SetRelativeLocation(FVector::ZeroVector);
		CheckpointArrowHeadPort = MakeMesh(TEXT("CheckpointArrowHeadPort"), Cube, CheckpointArrowRoot, FVector(0.55, 0.20, 0.16), FLinearColor::White);
		CheckpointArrowHeadPort->SetRelativeLocation(FVector(55.0, -20.0, 0.0));
		CheckpointArrowHeadPort->SetRelativeRotation(FRotator(0.0, 45.0, 0.0));
		CheckpointArrowHeadStarboard = MakeMesh(TEXT("CheckpointArrowHeadStarboard"), Cube, CheckpointArrowRoot, FVector(0.55, 0.20, 0.16), FLinearColor::White);
		CheckpointArrowHeadStarboard->SetRelativeLocation(FVector(55.0, 20.0, 0.0));
		CheckpointArrowHeadStarboard->SetRelativeRotation(FRotator(0.0, -45.0, 0.0));
	}
	bHasRowerCharacterMeshes = RowerTorsoMesh && RowerArmMesh && RowerLegMesh && RowerShoeMesh;
	Seat = MakeMesh(TEXT("SlidingSeat"), Cube, VisualRoot, FVector(0.35, 0.42, 0.09), FLinearColor(0.08f, 0.08f, 0.09f));
	Torso = MakeMesh(TEXT("Torso"), bHasRowerCharacterMeshes ? RowerTorsoMesh.Get() : Cube, VisualRoot, bHasRowerCharacterMeshes ? FVector::OneVector : FVector(0.22, 0.32, 0.65), FLinearColor(0.85f, 0.42f, 0.12f));
	LeftArm = MakeMesh(TEXT("LeftArm"), bHasRowerCharacterMeshes ? RowerArmMesh.Get() : Cube, VisualRoot, bHasRowerCharacterMeshes ? FVector::OneVector : FVector(0.55, 0.07, 0.07), FLinearColor(0.84f, 0.64f, 0.45f));
	RightArm = MakeMesh(TEXT("RightArm"), bHasRowerCharacterMeshes ? RowerArmMesh.Get() : Cube, VisualRoot, bHasRowerCharacterMeshes ? FVector::OneVector : FVector(0.55, 0.07, 0.07), FLinearColor(0.84f, 0.64f, 0.45f));
	if (bHasRowerCharacterMeshes)
	{
		LeftThigh = MakeMesh(TEXT("LeftThigh"), RowerLegMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
		RightThigh = MakeMesh(TEXT("RightThigh"), RowerLegMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
		LeftShin = MakeMesh(TEXT("LeftShin"), RowerLegMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
		RightShin = MakeMesh(TEXT("RightShin"), RowerLegMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
		LeftShoe = MakeMesh(TEXT("LeftShoe"), RowerShoeMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
		RightShoe = MakeMesh(TEXT("RightShoe"), RowerShoeMesh, VisualRoot, FVector::OneVector, FLinearColor::White);
	}
	UStaticMesh *OarGeometry = OarMesh ? OarMesh.Get() : Cube;
	const FVector OarScale = OarMesh ? FVector::OneVector : FVector(1.8, 0.04, 0.04);
	LeftOar = MakeMesh(TEXT("PortOar"), OarGeometry, VisualRoot, OarScale, FLinearColor(0.80f, 0.12f, 0.10f));
	RightOar = MakeMesh(TEXT("StarboardOar"), OarGeometry, VisualRoot, OarScale, FLinearColor(0.80f, 0.12f, 0.10f));
	InitializeWaterEffects();
	ApplyPresentation({});
}

FTransform AGrayBoxCourseActor::GetCourseTransform(double WrappedDistanceMm) const
{
	if (!Route.PresentationPath)
		return FTransform::Identity;
	const double RouteDistance = Route.bClosed
									 ? FMath::Fmod(FMath::Max(0.0, WrappedDistanceMm), static_cast<double>(Route.LengthMm))
									 : FMath::Clamp(WrappedDistanceMm, 0.0, static_cast<double>(Route.LengthMm));
	const FCoursePathSample Sample = EvaluateCoursePath(Route, RouteDistance);
	const FVector Location = FVector(Sample.PositionMm.X, Sample.PositionMm.Y, Sample.PositionMm.Z) / 10.0;
	const FVector Tangent(Sample.UnitTangent.X, Sample.UnitTangent.Y, Sample.UnitTangent.Z);
	return FTransform(Tangent.Rotation(), Location);
}

FVector AGrayBoxCourseActor::GetCourseTangent(double WrappedDistanceMm) const
{
	return GetCourseTransform(WrappedDistanceMm).GetUnitAxis(EAxis::X);
}

bool AGrayBoxCourseActor::TryGetVisibleBoatWorldLocation(FVector &OutLocation) const
{
	if (!Hull)
		return false;
	OutLocation = Hull->GetComponentLocation();
	return true;
}

void AGrayBoxCourseActor::ApplyPresentation(const FCoursePresentationSnapshot &Snapshot)
{
	ApplyPresentation(Snapshot, FCourseTelemetryInput{}, 0);
}

void AGrayBoxCourseActor::ApplyPresentation(const FCoursePresentationSnapshot &Snapshot,
											const FCourseTelemetryInput &Telemetry,
											uint64 NowMonotonicNs)
{
	if (!bInitialized)
		return;
	const FVector AnchorLocation(Snapshot.PathPositionMm.X / 10.0, Snapshot.PathPositionMm.Y / 10.0, Snapshot.PathPositionMm.Z / 10.0);
	const FTransform CourseTransform(FRotator(0.0, FMath::RadiansToDegrees(Snapshot.DampedYawRadians), 0.0), AnchorLocation);
	BoatRoot->SetWorldTransform(CourseTransform);
	UpdateCheckpointArrow(Snapshot);
	VisualRoot->SetRelativeLocation(FVector(0.0, 0.0, (Snapshot.AmbientBobMm + Snapshot.StrokeHeaveMm) / 10.0));
	VisualRoot->SetRelativeRotation(FRotator(Snapshot.HullPitchDegrees, 0.0, Snapshot.HullRollDegrees));

	const float SeatX = FMath::Lerp(-65.0f, 55.0f, static_cast<float>(Snapshot.SeatPose));
	Seat->SetRelativeLocation(FVector(SeatX, 0.0, 72.0));
	Torso->SetRelativeLocation(FVector(SeatX - 5.0f, 0.0, 135.0));
	Torso->SetRelativeRotation(FRotator(FMath::Lerp(22.0f, -14.0f, static_cast<float>(Snapshot.TorsoPose)), 0.0, 0.0));
	const float TargetHandsX = FMath::Lerp(SeatX + 72.0f, SeatX - 58.0f, static_cast<float>(Snapshot.ArmsPose));
	const float TargetOarYaw = FMath::Lerp(-34.0f, 42.0f, static_cast<float>(Snapshot.OarPose));
	const float OarPose = FMath::Clamp(static_cast<float>(Snapshot.OarPose), 0.0f, 1.0f);
	const bool bFreshStroke = Telemetry.bHasSession && Telemetry.bHasValidSample && Telemetry.bConnected &&
							  !Telemetry.bFrozen && !Telemetry.bStale && Telemetry.SessionState == ERowingSessionState::Active &&
							  Telemetry.WorkoutState == ERowingWorkoutState::Active && Telemetry.RowingState == ERowingState::Active &&
							  Snapshot.AnimationQuality != ECourseAnimationQuality::Unavailable;
	if (!bFreshStroke)
	{
		bHasOarPoseHistory = false;
		bOarDrivePhase = false;
	}
	else
	{
		if (Telemetry.StrokeState == ERowingStrokeState::Drive)
			bOarDrivePhase = true;
		else if (Telemetry.StrokeState != ERowingStrokeState::Unknown)
			bOarDrivePhase = false;
		else if (bHasOarPoseHistory && !FMath::IsNearlyEqual(OarPose, PreviousOarPose, 0.001f))
			bOarDrivePhase = OarPose > PreviousOarPose;
		bHasOarPoseHistory = true;
		PreviousOarPose = OarPose;
	}
	const float WorldDeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	if (!bHasOarPresentation)
	{
		bHasOarPresentation = true;
		SmoothedHandsX = TargetHandsX;
		SmoothedOarYaw = TargetOarYaw;
	}
	else
	{
		SmoothedHandsX = InterpolateOarMotion(SmoothedHandsX, TargetHandsX, WorldDeltaSeconds);
		SmoothedOarYaw = InterpolateOarMotion(SmoothedOarYaw, TargetOarYaw, WorldDeltaSeconds);
	}
	if (bHasRowerCharacterMeshes)
	{
		// The procedural segments use local +X as a 100 cm bone. All poses are
		// cosmetic and consume the existing presentation snapshot only.
		auto PoseSegment = [](UStaticMeshComponent *Component, const FVector &Start, const FVector &End)
		{
			const FVector Delta = End - Start;
			Component->SetRelativeLocation(Start);
			Component->SetRelativeRotation(Delta.Rotation());
			Component->SetRelativeScale3D(FVector(Delta.Size() / 100.0, 1.0, 1.0));
		};
		for (int32 Side : {-1, 1})
		{
			UStaticMeshComponent *Arm = Side < 0 ? LeftArm.Get() : RightArm.Get();
			UStaticMeshComponent *Thigh = Side < 0 ? LeftThigh.Get() : RightThigh.Get();
			UStaticMeshComponent *Shin = Side < 0 ? LeftShin.Get() : RightShin.Get();
			UStaticMeshComponent *Shoe = Side < 0 ? LeftShoe.Get() : RightShoe.Get();
			const FVector Shoulder = Torso->GetRelativeLocation() +
									 Torso->GetRelativeRotation().RotateVector(FVector(0.0, Side * 20.0, 18.0));
			const FVector Hand(TargetHandsX, Side * 25.0, 148.0);
			PoseSegment(Arm, Shoulder, Hand);
			const FVector Hip(SeatX + 10.0f, Side * 13.0, 80.0);
			const FVector Foot(140.0, Side * 13.0, 65.0);
			const FVector Knee(FMath::Lerp(Hip.X, Foot.X, 0.52), Side * 13.0, 95.0 + (55.0 - SeatX) * 0.25);
			PoseSegment(Thigh, Hip, Knee);
			PoseSegment(Shin, Knee, Foot);
			Shoe->SetRelativeLocation(Foot);
		}
	}
	else
	{
		LeftArm->SetRelativeLocation(FVector(TargetHandsX, -25.0, 148.0));
		RightArm->SetRelativeLocation(FVector(TargetHandsX, 25.0, 148.0));
	}
	if (OarMesh)
	{
		// The mesh origin is the oarlock and its +X points outboard. Mirror it
		// across the hull while retaining the existing smoothed stroke sweep.
		// During a fresh drive the blade tip dips below the flat waterline; it
		// feathers clear for recovery and whenever presentation input is stale.
		const float BladePitch = OarBladePitch(OarPose, bFreshStroke && bOarDrivePhase);
		LeftOar->SetRelativeLocation(FVector(SmoothedHandsX, -95.0, 105.0));
		RightOar->SetRelativeLocation(FVector(SmoothedHandsX, 95.0, 105.0));
		LeftOar->SetRelativeRotation(FRotator(BladePitch, -90.0 - SmoothedOarYaw, 0.0));
		RightOar->SetRelativeRotation(FRotator(BladePitch, 90.0 + SmoothedOarYaw, 0.0));
	}
	else
	{
		LeftOar->SetRelativeLocation(FVector(SmoothedHandsX, -155.0, 105.0));
		RightOar->SetRelativeLocation(FVector(SmoothedHandsX, 155.0, 105.0));
		LeftOar->SetRelativeRotation(FRotator(0.0, SmoothedOarYaw, 0.0));
		RightOar->SetRelativeRotation(FRotator(0.0, -SmoothedOarYaw, 0.0));
	}
	UpdateHullWake(bFreshStroke, NowMonotonicNs);
	UpdateOarWaterContacts(bFreshStroke, NowMonotonicNs);
	UpdateWaterEffects(NowMonotonicNs);

	const FVector CameraLocation(Snapshot.CameraPose.PositionMm.X / 10.0, Snapshot.CameraPose.PositionMm.Y / 10.0, Snapshot.CameraPose.PositionMm.Z / 10.0);
	const FVector CameraFocus(Snapshot.CameraPose.LookAtMm.X / 10.0, Snapshot.CameraPose.LookAtMm.Y / 10.0, Snapshot.CameraPose.LookAtMm.Z / 10.0);
	FRotator CameraRotation = UKismetMathLibrary::FindLookAtRotation(CameraLocation, CameraFocus);
	CameraRotation.Roll = 0.0f;
	InspectionCamera->SetFieldOfView(Snapshot.CameraPose.FieldOfViewDegrees);
	InspectionCamera->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
}

void AGrayBoxCourseActor::UpdateCheckpointArrow(const FCoursePresentationSnapshot &Snapshot)
{
	if (!CheckpointArrowRoot || !Route.PresentationPath)
		return;

	std::uint64_t TargetDistanceMm = Route.LengthMm;
	for (const ContentRuntime::FRouteCheckpoint &Checkpoint : Route.Checkpoints)
	{
		if (Checkpoint.DistanceMm > Snapshot.MeasuredDistanceMm)
		{
			TargetDistanceMm = Checkpoint.DistanceMm;
			break;
		}
	}

	const FCoursePathSample Target = EvaluateCoursePath(Route, static_cast<double>(TargetDistanceMm));
	const double DeltaX = Target.PositionMm.X - Snapshot.PathPositionMm.X;
	const double DeltaY = Target.PositionMm.Y - Snapshot.PathPositionMm.Y;
	double DirectionYaw = std::atan2(Target.UnitTangent.Y, Target.UnitTangent.X);
	if (std::hypot(DeltaX, DeltaY) > 1.0)
		DirectionYaw = std::atan2(DeltaY, DeltaX);
	const double RelativeYaw = FMath::RadiansToDegrees(DirectionYaw - Snapshot.DampedYawRadians);
	CheckpointArrowRoot->SetRelativeRotation(FRotator(0.0, RelativeYaw, 0.0));
}

void AGrayBoxCourseActor::ApplyWaterMotion(UMaterialInstanceDynamic &Water, bool bReduceMotion)
{
	// Only the reduced-motion case overrides the material's own calm default.
	if (bReduceMotion)
		Water.SetScalarParameterValue(TEXT("MotionScale"), 0.0f);
}

void AGrayBoxCourseActor::ApplyPresentationOptionsToLevelWater(ULevel &Level)
{
	ApplyPresentationOptionsToLevelWater(Level, ReduceMotionRequested(), WaterHiddenForBenchmark());
}

void AGrayBoxCourseActor::ApplyPresentationOptionsToLevelWater(ULevel &Level, bool bReduceMotion, bool bHideWater)
{
	if (!bReduceMotion && !bHideWater)
		return;
	for (const AActor *Actor : Level.Actors)
	{
		if (!Actor)
			continue;
		for (UActorComponent *Component : Actor->GetComponents())
		{
			UPrimitiveComponent *Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive)
				continue;
			const int32 MaterialCount = Primitive->GetNumMaterials();
			bool bAllSlotsAreWater = MaterialCount > 0;
			for (int32 Slot = 0; Slot < MaterialCount; ++Slot)
			{
				const UMaterialInterface *Material = Primitive->GetMaterial(Slot);
				const UMaterial *Base = Material ? Material->GetMaterial() : nullptr;
				const bool bWaterSlot = Base && Base->GetPathName() == TEXT("/Game/Phase2/HanRiver/Materials/M_Han_Water.M_Han_Water");
				bAllSlotsAreWater &= bWaterSlot;
				if (bWaterSlot && bReduceMotion && !bHideWater)
					ApplyWaterMotion(*Primitive->CreateDynamicMaterialInstance(Slot, nullptr), true);
			}
			if (bHideWater && bAllSlotsAreWater)
				Primitive->SetVisibility(false);
		}
	}
}

float AGrayBoxCourseActor::GetWaterMotionScaleForTesting() const
{
	float Scale = -1.0f;
	if (const UMaterialInstanceDynamic *Dynamic = WaterSurface ? Cast<UMaterialInstanceDynamic>(WaterSurface->GetMaterial(0)) : nullptr)
		Dynamic->GetScalarParameterValue(TEXT("MotionScale"), Scale);
	return Scale;
}

bool AGrayBoxCourseActor::ReduceMotionRequested()
{
	// Launch-flag stand-in for the platform Reduce Motion preference, which needs an
	// Apple adapter outside game code and is not wired yet.
	if (GReduceMotionOverrideForTesting.IsSet())
		return GReduceMotionOverrideForTesting.GetValue();
	static const bool bRequested = FParse::Param(FCommandLine::Get(), TEXT("ReduceMotion"));
	return bRequested;
}

bool AGrayBoxCourseActor::WaterHiddenForBenchmark()
{
	static const bool bRequested = FParse::Param(FCommandLine::Get(), TEXT("HideWaterForBenchmark"));
	return bRequested;
}

bool AGrayBoxCourseActor::WaterEffectsHiddenForBenchmark()
{
	static const bool bRequested = WaterHiddenForBenchmark() || FParse::Param(FCommandLine::Get(), TEXT("HideWaterEffectsForBenchmark"));
	return bRequested;
}

void AGrayBoxCourseActor::SetReduceMotionForTesting(TOptional<bool> bRequested)
{
	GReduceMotionOverrideForTesting = bRequested;
}

float AGrayBoxCourseActor::InterpolateOarMotion(float Current, float Target, float DeltaSeconds)
{
	const float BoundedDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxOarInterpolationStepSeconds);
	return FMath::FInterpTo(Current, Target, BoundedDeltaSeconds, OarInterpolationSpeed);
}

float AGrayBoxCourseActor::OarBladePitch(float OarPose, bool bDrivePhase)
{
	if (!bDrivePhase)
		return -30.0f;
	const float Entry = SmoothStep((OarPose - 0.03f) / 0.15f);
	const float Exit = 1.0f - SmoothStep((OarPose - 0.77f) / 0.18f);
	return -30.0f - 14.0f * Entry * Exit;
}

void AGrayBoxCourseActor::InitializeWaterEffects()
{
	if (!PlaneMesh || !WaterInteractionMaterial || WaterEffectsHiddenForBenchmark())
		return;
	const int32 PoolSize = HullWakePoolSize + OarRipplePoolSize;
	WaterEffectStates.SetNum(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		UStaticMeshComponent *Effect = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("WaterEffect%d"), Index));
		Effect->SetupAttachment(SceneRoot);
		Effect->SetStaticMesh(PlaneMesh);
		Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Effect->SetCastShadow(false);
		Effect->SetVisibility(false);
		UMaterialInstanceDynamic *Material = UMaterialInstanceDynamic::Create(WaterInteractionMaterial, Effect);
		Material->SetScalarParameterValue(TEXT("EffectKind"), Index < HullWakePoolSize ? 0.0f : 1.0f);
		Effect->SetMaterial(0, Material);
		AddInstanceComponent(Effect);
		Effect->RegisterComponent();
		WaterEffectMeshes.Add(Effect);
		WaterEffectMaterials.Add(Material);
	}
}

void AGrayBoxCourseActor::ClearWaterEffects()
{
	bHasWakeAnchor = false;
	bOarContactsArmed = false;
	for (int32 Index = 0; Index < WaterEffectStates.Num(); ++Index)
	{
		WaterEffectStates[Index].bActive = false;
		WaterEffectMeshes[Index]->SetVisibility(false);
	}
}

void AGrayBoxCourseActor::SpawnWaterEffect(bool bOarRipple, const FVector &Position, uint64 NowMonotonicNs)
{
	if (NowMonotonicNs == 0 || WaterEffectMeshes.IsEmpty() || ReduceMotionRequested())
		return;
	const int32 Begin = bOarRipple ? HullWakePoolSize : 0;
	const int32 End = bOarRipple ? HullWakePoolSize + OarRipplePoolSize : HullWakePoolSize;
	int32 Chosen = Begin;
	for (int32 Index = Begin; Index < End; ++Index)
	{
		if (!WaterEffectStates[Index].bActive)
		{
			Chosen = Index;
			break;
		}
		if (WaterEffectStates[Index].StartedNs < WaterEffectStates[Chosen].StartedNs)
			Chosen = Index;
	}
	FWaterEffectState &State = WaterEffectStates[Chosen];
	State = {NowMonotonicNs, true};
	UStaticMeshComponent *Effect = WaterEffectMeshes[Chosen];
	Effect->SetWorldLocation(Position);
	Effect->SetWorldRotation(bOarRipple ? FRotator::ZeroRotator : BoatRoot->GetComponentRotation());
	Effect->SetVisibility(true);
}

void AGrayBoxCourseActor::UpdateWaterEffects(uint64 NowMonotonicNs)
{
	if (ReduceMotionRequested() || WaterEffectsHiddenForBenchmark())
	{
		ClearWaterEffects();
		return;
	}
	for (int32 Index = 0; Index < WaterEffectStates.Num(); ++Index)
	{
		FWaterEffectState &State = WaterEffectStates[Index];
		if (!State.bActive)
			continue;
		const bool bRipple = Index >= HullWakePoolSize;
		const uint64 LifetimeNs = bRipple ? OarRippleLifetimeNs : HullWakeLifetimeNs;
		if (NowMonotonicNs < State.StartedNs || NowMonotonicNs - State.StartedNs >= LifetimeNs)
		{
			State.bActive = false;
			WaterEffectMeshes[Index]->SetVisibility(false);
			continue;
		}
		const float Age = static_cast<float>(NowMonotonicNs - State.StartedNs) / static_cast<float>(LifetimeNs);
		const float SizeCm = bRipple ? FMath::Lerp(24.0f, 95.0f, Age) : FMath::Lerp(110.0f, 190.0f, Age);
		WaterEffectMeshes[Index]->SetWorldScale3D(FVector(SizeCm / 100.0f, (bRipple ? SizeCm : 42.0f) / 100.0f, 1.0f));
		WaterEffectMaterials[Index]->SetScalarParameterValue(TEXT("Age"), Age);
		WaterEffectMaterials[Index]->SetScalarParameterValue(TEXT("Opacity"), (bRipple ? 0.20f : 0.12f) * FMath::Square(1.0f - Age));
	}
}

void AGrayBoxCourseActor::UpdateHullWake(bool bFreshStroke, uint64 NowMonotonicNs)
{
	if (!bFreshStroke || ReduceMotionRequested() || WaterEffectsHiddenForBenchmark() || NowMonotonicNs == 0)
	{
		bHasWakeAnchor = false;
		return;
	}
	const FVector Stern = BoatRoot->GetComponentTransform().TransformPosition(FVector(-350.0, 0.0, 0.0));
	if (bHasWakeAnchor)
	{
		const double TravelCm = FVector::Dist2D(Stern, PreviousWakeAnchor);
		// One sample per update, never a bridge across a skipped distance or reconnect.
		if (TravelCm >= 55.0 && TravelCm <= 250.0)
		{
			const double WaterlineZ = bAuthoredLevelActive ? -10.0 : -5.0;
			SpawnWaterEffect(false, FVector(Stern.X, Stern.Y, WaterlineZ + 1.0), NowMonotonicNs);
		}
		if (TravelCm < 55.0)
			return;
	}
	PreviousWakeAnchor = Stern;
	bHasWakeAnchor = true;
}

void AGrayBoxCourseActor::UpdateOarWaterContacts(bool bFreshStroke, uint64 NowMonotonicNs)
{
	// Contacts are presentation events from the rendered blade, never PM5 facts.
	// A missing mesh has no recognizable blade. Re-arm after stale input or a
	// reconnect without replaying the crossing that happened during the gap.
	if (!bFreshStroke || ReduceMotionRequested() || WaterEffectsHiddenForBenchmark() || !OarMesh)
	{
		bOarContactsArmed = false;
		return;
	}
	const double WaterlineZ = bAuthoredLevelActive ? -10.0 : -5.0;
	const FVector LeftTip = LeftOar->GetComponentTransform().TransformPosition(FVector(205.0, 0.0, 0.0));
	const FVector RightTip = RightOar->GetComponentTransform().TransformPosition(FVector(205.0, 0.0, 0.0));
	const bool bLeftNowSubmerged = LeftTip.Z < WaterlineZ;
	const bool bRightNowSubmerged = RightTip.Z < WaterlineZ;
	if (bOarContactsArmed)
	{
		if (bLeftNowSubmerged != bLeftBladeSubmerged)
		{
			++OarWaterContactCount;
			SpawnWaterEffect(true, FVector(LeftTip.X, LeftTip.Y, WaterlineZ + 1.0), NowMonotonicNs);
		}
		if (bRightNowSubmerged != bRightBladeSubmerged)
		{
			++OarWaterContactCount;
			SpawnWaterEffect(true, FVector(RightTip.X, RightTip.Y, WaterlineZ + 1.0), NowMonotonicNs);
		}
	}
	bLeftBladeSubmerged = bLeftNowSubmerged;
	bRightBladeSubmerged = bRightNowSubmerged;
	bOarContactsArmed = true;
}

int32 AGrayBoxCourseActor::GetOarWaterContactCountForTesting() const
{
	return OarWaterContactCount;
}

int32 AGrayBoxCourseActor::GetActiveWaterEffectCountForTesting() const
{
	int32 Count = 0;
	for (const FWaterEffectState &State : WaterEffectStates)
		Count += State.bActive ? 1 : 0;
	return Count;
}

FTransform AGrayBoxCourseActor::GetBoatTransformForTesting() const
{
	return BoatRoot->GetComponentTransform();
}
FTransform AGrayBoxCourseActor::GetVisualRootRelativeTransformForTesting() const
{
	return VisualRoot ? VisualRoot->GetRelativeTransform() : FTransform::Identity;
}
FTransform AGrayBoxCourseActor::GetSeatRelativeTransformForTesting() const
{
	return Seat ? Seat->GetRelativeTransform() : FTransform::Identity;
}
FTransform AGrayBoxCourseActor::GetTorsoRelativeTransformForTesting() const
{
	return Torso ? Torso->GetRelativeTransform() : FTransform::Identity;
}
FTransform AGrayBoxCourseActor::GetLeftOarRelativeTransformForTesting() const
{
	return LeftOar ? LeftOar->GetRelativeTransform() : FTransform::Identity;
}
FTransform AGrayBoxCourseActor::GetCameraTransformForTesting() const
{
	return InspectionCamera->GetComponentTransform();
}
float AGrayBoxCourseActor::GetCameraFieldOfViewForTesting() const
{
	return InspectionCamera->FieldOfView;
}
float AGrayBoxCourseActor::GetCourseLightIntensityForTesting() const
{
	return CourseLight ? CourseLight->Intensity : 0.0f;
}
int32 AGrayBoxCourseActor::GetMarkerCountForTesting() const
{
	return MarkerLabels.Num();
}
int32 AGrayBoxCourseActor::GetBuoyCountForTesting() const
{
	return bIsHanRiverRoute ? RouteMarkers.Num() : 0;
}
bool AGrayBoxCourseActor::AreRouteBuoysVisibleForTesting() const
{
	if (!bIsHanRiverRoute || RouteMarkers.Num() == 0)
		return false;
	for (const UStaticMeshComponent *Buoy : RouteMarkers)
	{
		if (!Buoy || !Buoy->IsVisible())
			return false;
	}
	return true;
}
FVector AGrayBoxCourseActor::GetMarkerLocationForTesting(int32 Index) const
{
	return RouteMarkers.IsValidIndex(Index) && RouteMarkers[Index] ? RouteMarkers[Index]->GetComponentLocation() : FVector::ZeroVector;
}
int32 AGrayBoxCourseActor::GetControlPointMarkerCountForTesting() const
{
	return bIsHanRiverRoute ? ControlPointMarkers.Num() : 0;
}
bool AGrayBoxCourseActor::AreControlPointMarkersVisibleForTesting() const
{
	if (!bIsHanRiverRoute || ControlPointMarkers.Num() == 0)
		return false;
	for (const UStaticMeshComponent *Marker : ControlPointMarkers)
	{
		if (!Marker || !Marker->IsVisible())
			return false;
	}
	return true;
}
FVector AGrayBoxCourseActor::GetControlPointMarkerLocationForTesting(int32 Index) const
{
	return ControlPointMarkers.IsValidIndex(Index) && ControlPointMarkers[Index] ? ControlPointMarkers[Index]->GetComponentLocation() : FVector::ZeroVector;
}
FVector AGrayBoxCourseActor::GetCheckpointArrowForwardForTesting() const
{
	return CheckpointArrowShaft ? CheckpointArrowShaft->GetForwardVector() : FVector::ZeroVector;
}
int32 AGrayBoxCourseActor::GetCourseEdgeSegmentCountForTesting() const
{
	return CourseEdgeMeshes.Num();
}
bool AGrayBoxCourseActor::AreCourseEdgesAttachedForTesting() const
{
	for (const USplineMeshComponent *Edge : CourseEdgeMeshes)
	{
		if (!Edge || Edge->GetAttachParent() != SceneRoot)
			return false;
	}
	return true;
}
int32 AGrayBoxCourseActor::GetHanLandmarkCountForTesting() const
{
	return HanLandmarkMeshes.Num();
}
FTransform AGrayBoxCourseActor::GetAuthoredLevelTransform() const
{
	return FTransform::Identity;
}

void AGrayBoxCourseActor::SetAuthoredLevelActive(bool bActive)
{
	if (!bIsHanRiverRoute || !bInitialized || bAuthoredLevelActive == bActive)
		return;
	bAuthoredLevelActive = bActive;
	ClearWaterEffects();
	for (UStaticMeshComponent *Landmark : HanLandmarkMeshes)
	{
		if (Landmark)
			Landmark->SetVisibility(!bActive);
	}
	if (CourseLight)
		CourseLight->SetVisibility(!bActive);
	if (WaterSurface)
		WaterSurface->SetRelativeLocation(FVector(0.0, 0.0, bActive ? -80.0 : -10.0));
}

bool AGrayBoxCourseActor::IsAuthoredLevelActiveForTesting() const
{
	return bAuthoredLevelActive;
}

int32 AGrayBoxCourseActor::GetVisibleHanLandmarkCountForTesting() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent *Landmark : HanLandmarkMeshes)
		Count += (Landmark && Landmark->IsVisible()) ? 1 : 0;
	return Count;
}

bool AGrayBoxCourseActor::HasHanRiverEnvironmentForTesting() const
{
	return bIsHanRiverRoute && HanLandmarkMeshes.Num() > 0;
}
bool AGrayBoxCourseActor::HasInputComponentForTesting() const
{
	return InputComponent != nullptr;
}
