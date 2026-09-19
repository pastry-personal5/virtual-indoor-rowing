#include "GrayBoxCourseActor.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr int32 SplinePointCount = 32;
	constexpr int32 MarkerCount = 8;
	constexpr double CourseRadiusX = 65'000.0;
	constexpr double CourseRadiusY = 18'000.0;

	UMaterialInterface *BasicMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
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
	InspectionCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InspectionCamera"));
	InspectionCamera->SetupAttachment(SceneRoot);
	InspectionCamera->SetFieldOfView(50.0f);
	InspectionCamera->bUsePawnControlRotation = false;
}

void AGrayBoxCourseActor::BeginPlay()
{
	Super::BeginPlay();
	InitializeCourse();
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
	if (UMaterialInterface *Material = BasicMaterial())
	{
		UMaterialInstanceDynamic *Dynamic = UMaterialInstanceDynamic::Create(Material, Component);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Dynamic);
	}
	AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void AGrayBoxCourseActor::InitializeCourse()
{
	if (bInitialized)
		return;
	bInitialized = true;

	UStaticMesh *Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh *Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!Cube || !Cylinder)
		return;

	CourseSpline->ClearSplinePoints(false);
	for (int32 Index = 0; Index < SplinePointCount; ++Index)
	{
		const double Angle = 2.0 * UE_PI * static_cast<double>(Index) / static_cast<double>(SplinePointCount);
		CourseSpline->AddSplinePoint(FVector(CourseRadiusX * FMath::Cos(Angle), CourseRadiusY * FMath::Sin(Angle), 20.0), ESplineCoordinateSpace::Local, false);
		CourseSpline->SetSplinePointType(Index, ESplinePointType::Curve, false);
	}
	CourseSpline->SetClosedLoop(true, true);

	UStaticMeshComponent *Water = MakeMesh(TEXT("Water"), Cube, SceneRoot, FVector(680.0, 205.0, 0.1), FLinearColor(0.03f, 0.20f, 0.35f));
	Water->SetRelativeLocation(FVector(0.0, 0.0, -10.0));
	EnvironmentMeshes.Add(Water);
	for (int32 Side : {-1, 1})
	{
		UStaticMeshComponent *Shore = MakeMesh(Side < 0 ? TEXT("ShoreNorth") : TEXT("ShoreSouth"), Cube, SceneRoot, FVector(680.0, 12.0, 0.35), FLinearColor(0.16f, 0.34f, 0.12f));
		Shore->SetRelativeLocation(FVector(0.0, Side * 21'000.0, 15.0));
		EnvironmentMeshes.Add(Shore);
	}

	for (int32 Index = 0; Index < MarkerCount; ++Index)
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
	const double Wrapped = FMath::Fmod(FMath::Max(0.0, WrappedDistanceMm), static_cast<double>(CourseLengthMm));
	const float SplineDistance = static_cast<float>(Wrapped / static_cast<double>(CourseLengthMm) * CourseSpline->GetSplineLength());
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
	const float HandsX = FMath::Lerp(SeatX + 72.0f, SeatX - 58.0f, static_cast<float>(Snapshot.ArmsPose));
	LeftArm->SetRelativeLocation(FVector(HandsX, -25.0, 148.0));
	RightArm->SetRelativeLocation(FVector(HandsX, 25.0, 148.0));
	const float OarYaw = FMath::Lerp(-34.0f, 42.0f, static_cast<float>(Snapshot.OarPose));
	LeftOar->SetRelativeLocation(FVector(HandsX, -155.0, 105.0));
	RightOar->SetRelativeLocation(FVector(HandsX, 155.0, 105.0));
	LeftOar->SetRelativeRotation(FRotator(0.0, OarYaw, 0.0));
	RightOar->SetRelativeRotation(FRotator(0.0, -OarYaw, 0.0));

	const FVector BoatLocation = CourseTransform.GetLocation();
	const FVector CameraLocation = BoatLocation + CourseTransform.GetUnitAxis(EAxis::Y) * 800.0 + FVector(0.0, 0.0, 250.0);
	FRotator CameraRotation = UKismetMathLibrary::FindLookAtRotation(CameraLocation, BoatLocation + FVector(0.0, 0.0, 90.0));
	CameraRotation.Roll = 0.0f;
	InspectionCamera->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
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
int32 AGrayBoxCourseActor::GetMarkerCountForTesting() const
{
	return MarkerLabels.Num();
}
bool AGrayBoxCourseActor::HasInputComponentForTesting() const
{
	return InputComponent != nullptr;
}
