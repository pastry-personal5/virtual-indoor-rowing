#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CourseRuntime/CoursePresentation.h"
#include "GrayBoxCourseActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class ULevel;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Code-only Phase 1 gray-box course and single-scull proxy. */
UCLASS()
class VIRTUALROWING_API AGrayBoxCourseActor : public AActor
{
	GENERATED_BODY()

  public:
	AGrayBoxCourseActor();
	virtual void BeginPlay() override;

	void InitializeCourse();
	// Called before the first initialization. The actor consumes only the stable
	// route definition, never a downloaded Unreal package path.
	void ConfigureRoute(const ContentRuntime::FRouteDefinition &Route);
	void ApplyPresentation(const FCoursePresentationSnapshot &Snapshot);

	FTransform GetCourseTransform(double WrappedDistanceMm) const;
	FVector GetCourseTangent(double WrappedDistanceMm) const;
	FTransform GetBoatTransformForTesting() const;
	FTransform GetSeatRelativeTransformForTesting() const;
	FTransform GetTorsoRelativeTransformForTesting() const;
	FTransform GetLeftOarRelativeTransformForTesting() const;
	FTransform GetCameraTransformForTesting() const;
	static float InterpolateOarMotion(float Current, float Target, float DeltaSeconds);
	float GetCameraFieldOfViewForTesting() const;
	static bool ReduceMotionRequested();
	// Presentation-only water animation. Reduced motion freezes the water's wave phase.
	static void ApplyWaterMotion(UMaterialInstanceDynamic &Water, bool bReduceMotion);
	static void ApplyReducedMotionToLevelWater(ULevel &Level);
	float GetWaterMotionScaleForTesting() const;
	float GetCourseLightIntensityForTesting() const;
	int32 GetMarkerCountForTesting() const;
	int32 GetCourseEdgeSegmentCountForTesting() const;
	int32 GetHanLandmarkCountForTesting() const;
	bool HasHanRiverEnvironmentForTesting() const;
	bool HasInputComponentForTesting() const;

	// The downloaded Han level covers only the first ~500 m of the route, so the
	// built-in kit stays as the whole-route baseline. While the level is shown the
	// kit's own overlapping landmarks, light and water surface yield to it. The
	// call is reversible and has no effect on any workout fact.
	void SetAuthoredLevelActive(bool bActive);
	bool IsAuthoredLevelActiveForTesting() const;
	int32 GetVisibleHanLandmarkCountForTesting() const;
	// Offset that places the authored level's origin at the route start.
	static FVector GetAuthoredLevelOriginCm();

  private:
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(Transient)
	TObjectPtr<USplineComponent> CourseSpline;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> BoatRoot;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Seat;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftArm;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RightArm;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftOar;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RightOar;
	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> InspectionCamera;
	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> CourseLight;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CourseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterSurface;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> EnvironmentMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HanLandmarkMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> CourseEdgeMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> MarkerLabels;

	bool bInitialized = false;
	float SmoothedCameraYaw = 0.0f;
	bool bHasCameraHeading = false;
	bool bIsHanRiverRoute = false;
	bool bAuthoredLevelActive = false;
	ContentRuntime::FRouteDefinition Route = ContentRuntime::BuiltInStandardRouteDefinition();
	bool bHasOarPresentation = false;
	float SmoothedHandsX = 0.0f;
	float SmoothedOarYaw = 0.0f;
	UStaticMeshComponent *MakeMesh(const TCHAR *Name, UStaticMesh *Mesh, USceneComponent *Parent, const FVector &Scale, const FLinearColor &Color);
	UStaticMeshComponent *MakeHanLandmark(const TCHAR *Name, UStaticMesh *Mesh, const FVector &Scale, const FLinearColor &Color, const FVector &Location);
	void BuildHanRiverEnvironment(UStaticMesh *Cube, UStaticMesh *Cylinder);
};
