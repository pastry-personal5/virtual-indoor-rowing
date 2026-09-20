#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CourseRuntime/CoursePresentation.h"

#include "WorkoutHudWidget.generated.h"

class APlayerController;
class UButton;
class UTextBlock;
class UWorkoutSubsystem;

/**
 * Code-only live HUD (FR-003): no authored asset and no Blueprint. It binds the
 * already-formatted values of FWorkoutDisplay and re-applies them only when the
 * subsystem's display generation changes, so a frame with no new snapshot costs
 * a comparison. Metric cells have fixed sizes so values never shift position.
 */
UCLASS()
class VIRTUALROWING_API UWorkoutHudWidget : public UUserWidget
{
	GENERATED_BODY()

  public:
	// Routes keyboard input to the HUD and focuses the action that applies now
	// (End Session while rowing, Start New once the session has ended).
	void FocusPrimaryAction(APlayerController *Controller);

	// Moves keyboard focus to the action that applies now. Public so the Escape
	// input processor can reach it; never activates the action.
	void FocusAction();
	static ESlateVisibility AnimationLabelVisibility(ECourseAnimationQuality Quality);
	static FLinearColor RootBackgroundColor();
	static FLinearColor MetricPanelBackgroundColor();
	static const TCHAR *MetricAccuracyNotice();

  protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry &MyGeometry, float InDeltaTime) override;

  private:
	UFUNCTION()
	void HandleEndClicked();
	UFUNCTION()
	void HandleStartNewClicked();
	UFUNCTION()
	void HandleStandardCourseClicked();
	UFUNCTION()
	void HandleHanCourseClicked();
	UFUNCTION()
	void HandleContentLicensesClicked();

	UWorkoutSubsystem *GetWorkoutSubsystem() const;
	void ApplyDisplay(const UWorkoutSubsystem &Subsystem);
	void SyncCourseSelection();
	bool IsActionFocused() const;
	void ApplyFocusCue();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BannerText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConnectionText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MetricAccuracyText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EstimatedStrokeText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DistanceText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ElapsedText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PaceText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WattsText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StrokeRateText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeartRateLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeartRateText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> EndButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> StartNewButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CourseSelectionText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HanAvailabilityText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> StandardCourseButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> HanCourseButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ContentLicensesButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ContentLicensesText;

	uint64 AppliedGeneration = 0;
	bool bHasApplied = false;
	bool bAppliedCanEnd = false;
	bool bEndFocusedApplied = false;
	bool bStartNewFocusedApplied = false;
	bool bFocusCueApplied = false;
	// Initial focus is retried each frame until Slate reports it landed, because it
	// cannot land before the widget has been arranged in the viewport.
	bool bInitialFocusPending = false;
	int32 InitialFocusAttempts = 0;
	TSharedPtr<class FHudEscapeProcessor> EscapeProcessor;
};
