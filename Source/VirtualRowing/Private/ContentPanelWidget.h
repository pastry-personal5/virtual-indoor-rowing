#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "ContentPanelWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Code-only right-hand panel for the downloadable Han River course: Han level
 * state, course selection, package build time, download, licenses, and a verbose
 * content/level diagnostics view. It reads the content and course subsystems and
 * never touches workout facts, so it can be shown or hidden without effect on a row.
 */
UCLASS()
class VIRTUALROWING_API UContentPanelWidget : public UUserWidget
{
	GENERATED_BODY()

  protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry &MyGeometry, float InDeltaTime) override;

  private:
	UFUNCTION()
	void HandleStandardCourseClicked();
	UFUNCTION()
	void HandleHanCourseClicked();
	UFUNCTION()
	void HandleDownloadHanClicked();
	UFUNCTION()
	void HandleContentLicensesClicked();
	UFUNCTION()
	void HandleDetailsClicked();

	void Sync();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HanLevelText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CourseSelectionText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HanAvailabilityText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HanPackageBuildText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DiagnosticsText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> StandardCourseButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> HanCourseButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> DownloadHanButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ContentLicensesButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ContentLicensesText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> DetailsButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailsButtonLabel;
	UPROPERTY(Transient)
	TObjectPtr<class USizeBox> DiagnosticsBounds;

	// The diagnostics view queries local data, so it refreshes a few times a second.
	float SecondsSinceDiagnostics = 1.0f;
};
