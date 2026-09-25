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
	// The telemetry input is a read-only copy of the latest workout snapshot. It
	// drives only the route-local rest camera; it never changes workout facts.
	void ApplyPresentation(const FCoursePresentationSnapshot &Snapshot,
						   const FCourseTelemetryInput &Telemetry,
						   uint64 NowMonotonicNs);

	FTransform GetCourseTransform(double WrappedDistanceMm) const;
	FVector GetCourseTangent(double WrappedDistanceMm) const;
	FTransform GetBoatTransformForTesting() const;
	FTransform GetSeatRelativeTransformForTesting() const;
	FTransform GetTorsoRelativeTransformForTesting() const;
	FTransform GetLeftOarRelativeTransformForTesting() const;
	int32 GetOarWaterContactCountForTesting() const;
	int32 GetActiveWaterEffectCountForTesting() const;
	FTransform GetCameraTransformForTesting() const;
	static float InterpolateOarMotion(float Current, float Target, float DeltaSeconds);
	float GetCameraFieldOfViewForTesting() const;
	bool CanToggleRestView() const;
	bool IsRestViewEnabled() const;
	void ToggleRestView();
	static void SetReduceMotionForTesting(TOptional<bool> bRequested);
	static bool ReduceMotionRequested();
	static bool WaterHiddenForBenchmark();
	static bool WaterEffectsHiddenForBenchmark();
	// Presentation-only water animation. Reduced motion freezes the water's wave phase.
	static void ApplyWaterMotion(UMaterialInstanceDynamic &Water, bool bReduceMotion);
	static void ApplyPresentationOptionsToLevelWater(ULevel &Level);
	static void ApplyPresentationOptionsToLevelWater(ULevel &Level, bool bReduceMotion, bool bHideWater);
	float GetWaterMotionScaleForTesting() const;
	float GetCourseLightIntensityForTesting() const;
	int32 GetMarkerCountForTesting() const;
	int32 GetCourseEdgeSegmentCountForTesting() const;
	bool AreCourseEdgesAttachedForTesting() const;
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
	TObjectPtr<UStaticMeshComponent> LeftThigh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RightThigh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftShin;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RightShin;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftShoe;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RightShoe;
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
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> OarMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> HullMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RowerTorsoMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RowerArmMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RowerLegMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RowerShoeMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CourseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterInteractionMaterial;
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
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WaterEffectMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterEffectMaterials;

	bool bInitialized = false;
	float SmoothedCameraYaw = 0.0f;
	bool bHasCameraHeading = false;
	FVector PreviousCameraBoatLocation = FVector::ZeroVector;
	bool bIsHanRiverRoute = false;
	bool bAuthoredLevelActive = false;
	bool bHasRowerCharacterMeshes = false;
	struct FRestCameraKeyframe
	{
		float BehindCm = 0.0f;
		float StarboardCm = 0.0f;
		float HeightCm = 0.0f;
		float FieldOfView = 0.0f;
		float LookAheadCm = 0.0f;
	};
	struct FRouteCameraMetadata
	{
		bool bRestViewEnabled = false;
		float RestDwellSeconds = 5.0f;
		float OutboundSeconds = 10.0f;
		float ReturnSeconds = 4.0f;
		FRestCameraKeyframe Chase;
		FRestCameraKeyframe Midpoint;
		FRestCameraKeyframe Reveal;
	};
	enum class ERestCameraState : uint8
	{
		Chase,
		Dwell,
		Outbound,
		Hold,
		Returning
	};
	struct FCameraPose
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float FieldOfView = 50.0f;
	};
	FRouteCameraMetadata CameraMetadata;
	ERestCameraState RestCameraState = ERestCameraState::Chase;
	uint64 RestCameraStateStartedNs = 0;
	FCameraPose ReturnStartPose;
	bool bRestViewRequested = false;
	bool bRestCameraEligible = false;
	ContentRuntime::FRouteDefinition Route = ContentRuntime::BuiltInStandardRouteDefinition();
	bool bHasOarPresentation = false;
	bool bHasOarPoseHistory = false;
	bool bOarDrivePhase = false;
	float PreviousOarPose = 0.0f;
	bool bOarContactsArmed = false;
	bool bLeftBladeSubmerged = false;
	bool bRightBladeSubmerged = false;
	int32 OarWaterContactCount = 0;
	struct FWaterEffectState
	{
		uint64 StartedNs = 0;
		bool bActive = false;
	};
	TArray<FWaterEffectState> WaterEffectStates;
	bool bHasWakeAnchor = false;
	FVector PreviousWakeAnchor = FVector::ZeroVector;
	float SmoothedHandsX = 0.0f;
	float SmoothedOarYaw = 0.0f;
	static float OarBladePitch(float OarPose, bool bDrivePhase);
	void InitializeWaterEffects();
	void ClearWaterEffects();
	void SpawnWaterEffect(bool bOarRipple, const FVector &Position, uint64 NowMonotonicNs);
	void UpdateWaterEffects(uint64 NowMonotonicNs);
	void UpdateHullWake(bool bFreshStroke, uint64 NowMonotonicNs);
	void UpdateOarWaterContacts(bool bFreshStroke, uint64 NowMonotonicNs);
	UStaticMeshComponent *MakeMesh(const TCHAR *Name, UStaticMesh *Mesh, USceneComponent *Parent, const FVector &Scale, const FLinearColor &Color);
	UStaticMeshComponent *MakeHanLandmark(const TCHAR *Name, UStaticMesh *Mesh, const FVector &Scale, const FLinearColor &Color, const FVector &Location);
	void BuildHanRiverEnvironment(UStaticMesh *Cube, UStaticMesh *Cylinder);
	static FRouteCameraMetadata CameraMetadataForRoute(const std::string &RouteId);
	static bool IsRestCameraEligible(const FCourseTelemetryInput &Telemetry);
	FCameraPose BuildChaseCameraPose(const FTransform &CourseTransform, float DeltaSeconds);
	FCameraPose BuildRestCameraPose(const FTransform &CourseTransform, const FRestCameraKeyframe &Keyframe) const;
	static FCameraPose InterpolateCameraPose(const FCameraPose &Start, const FCameraPose &End, float Alpha);
	void ApplyCameraPose(const FCameraPose &Pose);
	void ApplyRouteCamera(const FTransform &CourseTransform, const FCourseTelemetryInput &Telemetry, uint64 NowMonotonicNs, float DeltaSeconds);
};
