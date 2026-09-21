#include "GrayBoxCourseActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 SplinePointCount = 32;
	constexpr int32 MarkerCount = 8;
	constexpr double CourseRadiusX = 65'000.0;
	constexpr double CourseRadiusY = 18'000.0;
	constexpr double CameraBehindCm = 1'400.0;
	constexpr double CameraStarboardCm = 900.0;
	constexpr double CameraElevationCm = 650.0;
	constexpr double CameraLookAheadCm = 700.0;
	// Han River chase view: centred on the boat's axis, 12 m astern and 1 m above the
	// boat's waterline, level and looking along the (smoothed) heading so the boat is
	// seen moving away through the river's bends. Tuning values, not derived geometry.
	constexpr double HanCameraBehindCm = 1'200.0;
	constexpr double HanCameraHeightCm = 100.0;
	constexpr float HanCameraFieldOfView = 78.0f;
	// Heading lag so bends read as a camera swing rather than a rigid turn.
	constexpr float HanCameraHeadingInterpSpeed = 2.5f;
	constexpr float OarInterpolationSpeed = 16.0f;
	constexpr float MaxOarInterpolationStepSeconds = 1.0f / 30.0f;
	constexpr double HanRiverWidthCm = 42'000.0;
	constexpr double HanBankOffsetCm = 25'000.0;

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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	CourseMaterial = MaterialFinder.Object;
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
	if (CourseMaterial)
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
	AddBridge(TEXT("Hangang"), 3'650'000.0, 230.0f);
	AddBridge(TEXT("Wonhyo"), 5'000'000.0, 220.0f);

	const TArray<double> BeatDistances = {0.0, 650'000.0, 1'450'000.0, 3'000'000.0, 3'650'000.0, 5'000'000.0};
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
	for (double DistanceMm : {650'000.0, 3'000'000.0, 5'000'000.0})
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
	const int32 PointCount = Route.bClosed ? SplinePointCount : SplinePointCount + 1;
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		if (Route.bClosed)
		{
			const double Angle = 2.0 * UE_PI * static_cast<double>(Index) / static_cast<double>(SplinePointCount);
			CourseSpline->AddSplinePoint(FVector(CourseRadiusX * FMath::Cos(Angle), CourseRadiusY * FMath::Sin(Angle), 20.0), ESplineCoordinateSpace::Local, false);
			CourseSpline->SetSplinePointType(Index, ESplinePointType::Curve, false);
		}
		else
		{
			const double Alpha = static_cast<double>(Index) / static_cast<double>(PointCount - 1);
			const double X = FMath::Lerp(-250'000.0, 250'000.0, Alpha);
			const double Y = 8'000.0 * FMath::Sin(Alpha * 2.0 * UE_PI) + 2'000.0 * FMath::Sin(Alpha * 8.0 * UE_PI);
			CourseSpline->AddSplinePoint(FVector(X, Y, 20.0), ESplineCoordinateSpace::Local, false);
			CourseSpline->SetSplinePointType(Index, ESplinePointType::Curve, false);
		}
	}
	CourseSpline->SetClosedLoop(Route.bClosed, true);
	const int32 EdgeCount = Route.bClosed ? PointCount : PointCount - 1;
	for (int32 Index = 0; Index < EdgeCount; ++Index)
	{
		const int32 NextIndex = Route.bClosed ? (Index + 1) % PointCount : Index + 1;
		USplineMeshComponent *CourseEdge = NewObject<USplineMeshComponent>(this, *FString::Printf(TEXT("CourseEdge%d"), Index));
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

	UStaticMeshComponent *Water = MakeMesh(TEXT("Water"), Cube, SceneRoot, bIsHanRiverRoute ? FVector(5'400.0, HanRiverWidthCm / 100.0, 0.1) : FVector(680.0, 205.0, 0.1), bIsHanRiverRoute ? FLinearColor(0.025f, 0.10f, 0.18f) : FLinearColor(0.03f, 0.20f, 0.35f));
	Water->SetRelativeLocation(FVector(0.0, 0.0, -10.0));
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

	const int32 RouteMarkerCount = bIsHanRiverRoute ? 0 : (Route.bClosed ? MarkerCount : FMath::FloorToInt(static_cast<double>(Route.LengthMm) / 250'000.0));
	for (int32 Index = 0; Index < RouteMarkerCount; ++Index)
	{
		const int32 MarkerMetres = (Index + 1) * 250;
		const double DistanceMm = static_cast<double>(MarkerMetres) * 1000.0;
		const FTransform CourseTransform = GetCourseTransform(DistanceMm);
		UStaticMeshComponent *Marker = MakeMesh(*FString::Printf(TEXT("Marker%d"), Index), Cylinder, SceneRoot, FVector(0.18, 0.18, 2.5), FLinearColor(0.95f, 0.75f, 0.08f));
		const FVector Right = CourseTransform.GetUnitAxis(EAxis::Y);
		Marker->SetWorldLocation(CourseTransform.GetLocation() + Right * 350.0 + FVector(0.0, 0.0, 125.0));
		EnvironmentMeshes.Add(Marker);

		UTextRenderComponent *Label = NewObject<UTextRenderComponent>(this, *FString::Printf(TEXT("MarkerLabel%d"), Index));
		Label->SetupAttachment(SceneRoot);
		Label->SetText(FText::FromString(FString::Printf(TEXT("%d m"), MarkerMetres)));
		Label->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		Label->SetWorldSize(90.0f);
		Label->SetTextRenderColor(FColor::White);
		AddInstanceComponent(Label);
		Label->RegisterComponent();
		Label->SetWorldLocation(Marker->GetComponentLocation() + FVector(0.0, 0.0, 200.0));
		MarkerLabels.Add(Label);
	}

	Hull = MakeMesh(TEXT("Hull"), Cube, BoatRoot, FVector(4.8, 0.28, 0.18), FLinearColor(0.85f, 0.88f, 0.92f));
	Hull->SetRelativeLocation(FVector(0.0, 0.0, 45.0));
	Seat = MakeMesh(TEXT("SlidingSeat"), Cube, BoatRoot, FVector(0.35, 0.42, 0.09), FLinearColor(0.08f, 0.08f, 0.09f));
	Torso = MakeMesh(TEXT("Torso"), Cube, BoatRoot, FVector(0.22, 0.32, 0.65), FLinearColor(0.85f, 0.42f, 0.12f));
	LeftArm = MakeMesh(TEXT("LeftArm"), Cube, BoatRoot, FVector(0.55, 0.07, 0.07), FLinearColor(0.84f, 0.64f, 0.45f));
	RightArm = MakeMesh(TEXT("RightArm"), Cube, BoatRoot, FVector(0.55, 0.07, 0.07), FLinearColor(0.84f, 0.64f, 0.45f));
	LeftOar = MakeMesh(TEXT("PortOar"), Cube, BoatRoot, FVector(1.8, 0.04, 0.04), FLinearColor(0.80f, 0.12f, 0.10f));
	RightOar = MakeMesh(TEXT("StarboardOar"), Cube, BoatRoot, FVector(1.8, 0.04, 0.04), FLinearColor(0.80f, 0.12f, 0.10f));
	ApplyPresentation({});
}

FTransform AGrayBoxCourseActor::GetCourseTransform(double WrappedDistanceMm) const
{
	if (!CourseSpline || CourseSpline->GetSplineLength() <= 0.0f)
		return FTransform::Identity;
	const double RouteDistance = Route.bClosed
									 ? FMath::Fmod(FMath::Max(0.0, WrappedDistanceMm), static_cast<double>(Route.LengthMm))
									 : FMath::Clamp(WrappedDistanceMm, 0.0, static_cast<double>(Route.LengthMm));
	const float SplineDistance = static_cast<float>(RouteDistance / static_cast<double>(Route.LengthMm) * CourseSpline->GetSplineLength());
	const FVector Location = CourseSpline->GetLocationAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World);
	const FVector Tangent = CourseSpline->GetDirectionAtDistanceAlongSpline(SplineDistance, ESplineCoordinateSpace::World).GetSafeNormal();
	return FTransform(Tangent.Rotation(), Location);
}

FVector AGrayBoxCourseActor::GetCourseTangent(double WrappedDistanceMm) const
{
	return GetCourseTransform(WrappedDistanceMm).GetUnitAxis(EAxis::X);
}

void AGrayBoxCourseActor::ApplyPresentation(const FCoursePresentationSnapshot &Snapshot)
{
	if (!bInitialized)
		return;
	const FTransform CourseTransform = GetCourseTransform(Snapshot.WrappedCourseDistanceMm);
	BoatRoot->SetWorldTransform(CourseTransform);

	const float SeatX = FMath::Lerp(-65.0f, 55.0f, static_cast<float>(Snapshot.SeatPose));
	Seat->SetRelativeLocation(FVector(SeatX, 0.0, 72.0));
	Torso->SetRelativeLocation(FVector(SeatX - 5.0f, 0.0, 135.0));
	Torso->SetRelativeRotation(FRotator(FMath::Lerp(22.0f, -14.0f, static_cast<float>(Snapshot.TorsoPose)), 0.0, 0.0));
	const float TargetHandsX = FMath::Lerp(SeatX + 72.0f, SeatX - 58.0f, static_cast<float>(Snapshot.ArmsPose));
	const float TargetOarYaw = FMath::Lerp(-34.0f, 42.0f, static_cast<float>(Snapshot.OarPose));
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
	LeftArm->SetRelativeLocation(FVector(TargetHandsX, -25.0, 148.0));
	RightArm->SetRelativeLocation(FVector(TargetHandsX, 25.0, 148.0));
	LeftOar->SetRelativeLocation(FVector(SmoothedHandsX, -155.0, 105.0));
	RightOar->SetRelativeLocation(FVector(SmoothedHandsX, 155.0, 105.0));
	LeftOar->SetRelativeRotation(FRotator(0.0, SmoothedOarYaw, 0.0));
	RightOar->SetRelativeRotation(FRotator(0.0, -SmoothedOarYaw, 0.0));

	const FVector BoatLocation = CourseTransform.GetLocation();
	const FVector Forward = CourseTransform.GetUnitAxis(EAxis::X);
	if (bIsHanRiverRoute)
	{
		const float TargetYaw = Forward.Rotation().Yaw;
		if (!bHasCameraHeading || ReduceMotionRequested())
		{
			SmoothedCameraYaw = TargetYaw;
			bHasCameraHeading = true;
		}
		else
		{
			const float DeltaYaw = FMath::FindDeltaAngleDegrees(SmoothedCameraYaw, TargetYaw);
			SmoothedCameraYaw = FRotator::NormalizeAxis(SmoothedCameraYaw + FMath::FInterpTo(0.0f, DeltaYaw, FMath::Max(WorldDeltaSeconds, 0.0f), HanCameraHeadingInterpSpeed));
		}
		const FRotator CameraRotation(0.0, SmoothedCameraYaw, 0.0);
		const FVector CameraLocation = BoatLocation - CameraRotation.Vector() * HanCameraBehindCm + FVector(0.0, 0.0, HanCameraHeightCm);
		InspectionCamera->SetFieldOfView(HanCameraFieldOfView);
		InspectionCamera->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
		return;
	}
	const FVector Starboard = CourseTransform.GetUnitAxis(EAxis::Y);
	const FVector CameraLocation = BoatLocation - Forward * CameraBehindCm + Starboard * CameraStarboardCm + FVector(0.0, 0.0, CameraElevationCm);
	const FVector CameraFocus = BoatLocation + Forward * CameraLookAheadCm + FVector(0.0, 0.0, 90.0);
	FRotator CameraRotation = UKismetMathLibrary::FindLookAtRotation(CameraLocation, CameraFocus);
	CameraRotation.Roll = 0.0f;
	InspectionCamera->SetFieldOfView(50.0f);
	InspectionCamera->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
}

bool AGrayBoxCourseActor::ReduceMotionRequested()
{
	// Launch-flag stand-in for the platform Reduce Motion preference, which needs an
	// Apple adapter outside game code and is not wired yet.
	static const bool bRequested = FParse::Param(FCommandLine::Get(), TEXT("ReduceMotion"));
	return bRequested;
}

float AGrayBoxCourseActor::InterpolateOarMotion(float Current, float Target, float DeltaSeconds)
{
	const float BoundedDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxOarInterpolationStepSeconds);
	return FMath::FInterpTo(Current, Target, BoundedDeltaSeconds, OarInterpolationSpeed);
}

FTransform AGrayBoxCourseActor::GetBoatTransformForTesting() const
{
	return BoatRoot->GetComponentTransform();
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
int32 AGrayBoxCourseActor::GetCourseEdgeSegmentCountForTesting() const
{
	return CourseEdgeMeshes.Num();
}
int32 AGrayBoxCourseActor::GetHanLandmarkCountForTesting() const
{
	return HanLandmarkMeshes.Num();
}
FVector AGrayBoxCourseActor::GetAuthoredLevelOriginCm()
{
	// The Han spline starts at local X = -250,000 cm; the authored level's own
	// origin is its route start.
	return FVector(-250'000.0, 0.0, 0.0);
}

void AGrayBoxCourseActor::SetAuthoredLevelActive(bool bActive)
{
	if (!bIsHanRiverRoute || !bInitialized || bAuthoredLevelActive == bActive)
		return;
	bAuthoredLevelActive = bActive;
	// Authored footprint: the first 500 m plus the bridge approach behind the start.
	const double FootprintEndX = GetAuthoredLevelOriginCm().X + 50'000.0;
	for (UStaticMeshComponent *Landmark : HanLandmarkMeshes)
	{
		if (Landmark && Landmark->GetComponentLocation().X < FootprintEndX)
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
