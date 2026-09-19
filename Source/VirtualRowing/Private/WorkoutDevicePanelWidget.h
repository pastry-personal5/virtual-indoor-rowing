#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WorkoutSubsystem.h"

#include "WorkoutDevicePanelWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;

/**
 * Code-only device panel for the real-PM5 flow (docs/phase-1/07-milestone-7-real-pm5-app-wiring.md).
 * It renders FWorkoutDevicePanel and forwards clicks to UWorkoutSubsystem; it holds no
 * device or journal logic. While the flow is up it covers the HUD; once a PM5 is attached
 * it shrinks to a strip at the top with Change / Forget / Disconnect and the journal line.
 * It is not created for a simulator run.
 */
UCLASS()
class VIRTUALROWING_API UWorkoutDevicePanelWidget : public UUserWidget
{
	GENERATED_BODY()

  public:
	// The most candidates the panel offers at once; the connector's list is
	// nearest-first, so these are the nearest PMs.
	static constexpr int32 MaxCandidateButtons = 5;

  protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry &MyGeometry, float InDeltaTime) override;

  private:
	UFUNCTION()
	void HandleConnect();
	UFUNCTION()
	void HandleRetry();
	UFUNCTION()
	void HandleConfirmWithoutSaving();
	UFUNCTION()
	void HandleScan();
	UFUNCTION()
	void HandleForget();
	UFUNCTION()
	void HandleCancel();
	UFUNCTION()
	void HandleCandidate0();
	UFUNCTION()
	void HandleCandidate1();
	UFUNCTION()
	void HandleCandidate2();
	UFUNCTION()
	void HandleCandidate3();
	UFUNCTION()
	void HandleCandidate4();

	void SelectCandidate(int32 Index);
	UWorkoutSubsystem *GetWorkoutSubsystem() const;
	void ApplyPanel(const FWorkoutDevicePanel &Panel);
	void ApplyFocusCue();
	UButton *GetPrimaryButton(const FWorkoutDevicePanel &Panel) const;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> JournalText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ScanLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CancelLabel;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> CandidateLabels;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> CandidateButtons;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConnectButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ScanButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ForgetButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelButton;

	FWorkoutDevicePanel LastPanel;
	uint64 AppliedGeneration = 0;
	bool bHasApplied = false;
	// Keyboard focus is (re)placed whenever the mode, or the primary action within a
	// mode (Scanning: the scan button, then the nearest candidate), changes.
	TWeakObjectPtr<UButton> FocusedPrimary;
	int32 FocusAttempts = 0;
	// Last cue applied per button: -1 = none yet (so the resting fill is applied once), 0/1 = focused.
	TArray<int8> AppliedFocus;
};
