#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ContentRuntime/ContentManifest.h"

#include "ContentSubsystem.generated.h"

/**
 * Game-thread adapter for signed, data-only route content. It owns no official
 * workout facts: mounting or selecting a route only changes presentation.
 */
UCLASS()
class VIRTUALROWING_API UContentSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

  public:
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;

	const ContentRuntime::FRouteDefinition &GetSelectedRoute() const;
	FString GetHanAvailabilityReason() const;
	bool IsHanAvailable() const;
	// True once the verified Han IoStore is mounted in this process. Selecting the
	// route alone never implies its authored level can be loaded.
	bool IsHanContentMounted() const;
	/** Returns the signed notice for the mounted package, for the Content Licenses/Credits view. */
	FString GetActivePackageNotice() const;
	FString GetContentLicensesCreditsText() const;
	bool SelectRouteById(const FString &RouteId);
	static bool CanSelectRoute(const FString &RouteId, bool bHanAvailable, bool bWorkoutActive);

	// Content page actions must call this guard before any download, activation,
	// withdrawal, or mount operation. It deliberately refuses an active workout.
	bool CanOperateContent() const;

	// The catalog endpoint is configured by the internal release channel. Refresh
	// is started at boot when configured; downloading remains an explicit Content
	// page action and never starts itself.
	bool BeginCatalogRefresh();
	bool BeginHanDownload();
	FString GetContentOperationStatus() const;

	// Automation seam: an extracted set whose Pak/IoStore pair fails to mount
	// leaves Standard selected and records a redacted stable error category.
	bool MountInstalledContentForTesting(const FString &InstallPath);

  private:
	struct FImpl;
	// UHT emits this subsystem's default constructor in generated code, where a
	// TUniquePtr deleter would require FImpl to be complete. TSharedPtr keeps
	// deletion in ContentSubsystem.cpp, matching UWorkoutSubsystem's pimpl.
	TSharedPtr<FImpl> Impl;

	bool TryMountInstalledContent(const FString &InstallPath, FString &OutFailureCategory);
	void HandleCatalogResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void HandleDownloadResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	bool IsWorkoutActive() const;
};
