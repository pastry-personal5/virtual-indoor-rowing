#include "WorkoutDevicePanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MessageFontSize = 30;
	constexpr int32 ButtonFontSize = 26;
	constexpr int32 JournalFontSize = 22;
	// Attached: the device actions shrink to a corner cluster so they never sit on the metrics.
	constexpr int32 CompactButtonFontSize = 14;
	constexpr int32 CompactJournalFontSize = 12;
	const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	// The unsaved warning must not rely on color alone; the text says it too.
	const FLinearColor WarningColor(1.0f, 0.55f, 0.2f, 1.0f);
	const FLinearColor FocusedFill(0.10f, 0.45f, 0.95f, 1.0f);
	const FLinearColor RestingFill(0.16f, 0.18f, 0.22f, 1.0f);

	bool IsBlocking(EWorkoutDevicePanelMode Mode)
	{
		return Mode != EWorkoutDevicePanelMode::Hidden && Mode != EWorkoutDevicePanelMode::Attached;
	}
} // namespace

void UWorkoutDevicePanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	auto MakeText = [this](const FString &Content, int32 Size, ETextJustify::Type Justification)
	{
		UTextBlock *Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Content));
		Text->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Text->SetJustification(Justification);
		Text->SetColorAndOpacity(FSlateColor(TextColor));
		return Text;
	};
	auto MakeButton = [&](const TCHAR *Caption, TObjectPtr<UButton> &OutButton, TObjectPtr<UTextBlock> *OutLabel = nullptr)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock *Label = MakeText(Caption, ButtonFontSize, ETextJustify::Center);
		OutButton->AddChild(Label);
		if (OutLabel)
			*OutLabel = Label;
		UWidget *Content = OutButton->GetContent();
		if (UButtonSlot *ButtonSlot = Cast<UButtonSlot>(Content ? Content->Slot : nullptr))
			ButtonSlot->SetPadding(FMargin(24.0f, 12.0f));
		return Label;
	};

	UVerticalBox *Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	MessageText = MakeText(TEXT(""), MessageFontSize, ETextJustify::Center);
	MessageText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(MessageText)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 16.0f));

	for (int32 Index = 0; Index < MaxCandidateButtons; ++Index)
	{
		TObjectPtr<UButton> Button;
		TObjectPtr<UTextBlock> Label;
		MakeButton(TEXT(""), Button, &Label);
		CandidateButtons.Add(Button);
		CandidateLabels.Add(Label);
		Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 4.0f));
	}
	CandidateButtons[0]->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCandidate0);
	CandidateButtons[1]->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCandidate1);
	CandidateButtons[2]->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCandidate2);
	CandidateButtons[3]->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCandidate3);
	CandidateButtons[4]->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCandidate4);

	UHorizontalBox *Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	MakeButton(TEXT("Connect to PM5"), ConnectButton);
	MakeButton(TEXT("Retry"), RetryButton);
	MakeButton(TEXT("Row without saving"), ConfirmButton);
	MakeButton(TEXT("Scan for a different PM5"), ScanButton, &ScanLabel);
	MakeButton(TEXT("Forget PM5"), ForgetButton);
	MakeButton(TEXT("Cancel"), CancelButton, &CancelLabel);
	for (UButton *Button : {ConnectButton.Get(), RetryButton.Get(), ConfirmButton.Get(), ScanButton.Get(), ForgetButton.Get(), CancelButton.Get()})
		Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(8.0f, 0.0f));
	Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));

	ConnectButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleConnect);
	RetryButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleRetry);
	ConfirmButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleConfirmWithoutSaving);
	ScanButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleScan);
	ForgetButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleForget);
	CancelButton->OnClicked.AddDynamic(this, &UWorkoutDevicePanelWidget::HandleCancel);

	JournalText = MakeText(TEXT(""), JournalFontSize, ETextJustify::Center);
	Column->AddChildToVerticalBox(JournalText)->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 8.0f));

	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RootBorder->SetHorizontalAlignment(HAlign_Center);
	RootBorder->AddChild(Column);
	WidgetTree->RootWidget = RootBorder;

	SetIsFocusable(true);
	// Hidden until the first Sync() applies the panel state. The subsystem calls Sync()
	// itself, so showing the panel never depends on this widget's own tick running.
	SetVisibility(ESlateVisibility::Hidden);
	AppliedFocus.Init(-1, MaxCandidateButtons + 6);
}

UWorkoutSubsystem *UWorkoutDevicePanelWidget::GetWorkoutSubsystem() const
{
	UGameInstance *GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UWorkoutSubsystem>() : nullptr;
}

UButton *UWorkoutDevicePanelWidget::GetPrimaryButton(const FWorkoutDevicePanel &Panel) const
{
	switch (Panel.Mode)
	{
	case EWorkoutDevicePanelMode::Idle:
		return ConnectButton;
	// Retry, never "Row without saving": the unsafe choice is not the default one.
	case EWorkoutDevicePanelMode::JournalDecision:
	case EWorkoutDevicePanelMode::Problem:
		return RetryButton;
	case EWorkoutDevicePanelMode::Starting:
		return ScanButton;
	case EWorkoutDevicePanelMode::Scanning:
		return Panel.Candidates.IsEmpty() ? ScanButton.Get() : CandidateButtons[0].Get();
	default:
		return nullptr;
	}
}

void UWorkoutDevicePanelWidget::Sync()
{
	UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem();
	if (!Subsystem)
		return;
	if (!bHasApplied || Subsystem->GetDevicePanelGeneration() != AppliedGeneration)
	{
		ApplyPanel(Subsystem->GetDevicePanel());
		AppliedGeneration = Subsystem->GetDevicePanelGeneration();
		bHasApplied = true;
	}
	// The focus request is retried while the panel is up, because it cannot land
	// before the widget has been arranged in the viewport.
	if (IsBlocking(LastPanel.Mode) && FocusAttempts < 300)
	{
		if (UButton *Primary = GetPrimaryButton(LastPanel))
		{
			if (!Primary->HasKeyboardFocus() && FocusedPrimary.Get() != Primary)
			{
				Primary->SetKeyboardFocus();
				++FocusAttempts;
			}
			else if (Primary->HasKeyboardFocus())
			{
				FocusedPrimary = Primary;
			}
		}
	}
	ApplyFocusCue();
}

void UWorkoutDevicePanelWidget::ApplyFocusCue()
{
	// Keyboard focus must be visible: a focused action is drawn bright with a
	// contrasting fill, others are dark.
	UButton *Buttons[] = {ConnectButton, RetryButton, ConfirmButton, ScanButton, ForgetButton, CancelButton, CandidateButtons[0], CandidateButtons[1], CandidateButtons[2], CandidateButtons[3], CandidateButtons[4]};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buttons); ++Index)
	{
		const bool bFocused = Buttons[Index]->HasKeyboardFocus();
		if (AppliedFocus.IsValidIndex(Index) && AppliedFocus[Index] == (bFocused ? 1 : 0))
			continue;
		if (AppliedFocus.IsValidIndex(Index))
			AppliedFocus[Index] = bFocused ? 1 : 0;
		Buttons[Index]->SetBackgroundColor(bFocused ? FocusedFill : RestingFill);
	}
}

void UWorkoutDevicePanelWidget::ApplyPanel(const FWorkoutDevicePanel &Panel)
{
	const bool bModeChanged = Panel.Mode != LastPanel.Mode || !bHasApplied;
	LastPanel = Panel;
	if (bModeChanged)
	{
		FocusedPrimary = nullptr;
		FocusAttempts = 0;
	}

	const bool bBlocking = IsBlocking(Panel.Mode);
	const bool bAttached = Panel.Mode == EWorkoutDevicePanelMode::Attached;
	// Blocking modes cover the HUD and take clicks; the attached cluster lets clicks
	// through everywhere except its own buttons.
	SetVisibility(Panel.Mode == EWorkoutDevicePanelMode::Hidden ? ESlateVisibility::Hidden : (bBlocking ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible));
	RootBorder->SetVisibility(bBlocking ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
	RootBorder->SetBrushColor(bBlocking ? FLinearColor(0.02f, 0.03f, 0.05f, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	// Attached: a small cluster in the bottom-right corner, clear of the centered metrics,
	// instead of a full-width strip laid over the top of the HUD.
	RootBorder->SetHorizontalAlignment(bBlocking ? HAlign_Center : HAlign_Right);
	RootBorder->SetVerticalAlignment(bBlocking ? VAlign_Center : VAlign_Bottom);
	RootBorder->SetPadding(bBlocking ? FMargin(0.0f) : FMargin(16.0f));
	for (UButton *Button : {ScanButton.Get(), ForgetButton.Get(), CancelButton.Get()})
	{
		if (UTextBlock *Label = Cast<UTextBlock>(Button->GetContent()))
			Label->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", bAttached ? CompactButtonFontSize : ButtonFontSize));
		if (UButtonSlot *ButtonSlot = Cast<UButtonSlot>(Button->GetContent() ? Button->GetContent()->Slot : nullptr))
			ButtonSlot->SetPadding(bAttached ? FMargin(10.0f, 4.0f) : FMargin(24.0f, 12.0f));
	}
	JournalText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", bAttached ? CompactJournalFontSize : JournalFontSize));
	JournalText->SetJustification(bAttached ? ETextJustify::Right : ETextJustify::Center);

	MessageText->SetText(FText::FromString(Panel.Message));
	MessageText->SetVisibility(bBlocking ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	JournalText->SetText(FText::FromString(Panel.JournalLine));
	JournalText->SetColorAndOpacity(FSlateColor(Panel.bJournalNotSaved ? WarningColor : TextColor));
	JournalText->SetVisibility(Panel.JournalLine.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	for (int32 Index = 0; Index < MaxCandidateButtons; ++Index)
	{
		const bool bShown = Panel.Mode == EWorkoutDevicePanelMode::Scanning && Panel.Candidates.IsValidIndex(Index);
		CandidateButtons[Index]->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bShown)
			CandidateLabels[Index]->SetText(FText::FromString(Panel.Candidates[Index]));
	}

	const auto Show = [](UButton *Button, bool bShown)
	{
		Button->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	};
	const bool bJournalDecision = Panel.Mode == EWorkoutDevicePanelMode::JournalDecision;
	const bool bProblem = Panel.Mode == EWorkoutDevicePanelMode::Problem;
	const bool bStarting = Panel.Mode == EWorkoutDevicePanelMode::Starting;
	const bool bScanning = Panel.Mode == EWorkoutDevicePanelMode::Scanning;
	Show(ConnectButton, Panel.Mode == EWorkoutDevicePanelMode::Idle);
	Show(RetryButton, bJournalDecision || bProblem);
	Show(ConfirmButton, bJournalDecision);
	Show(ScanButton, bStarting || bScanning || bAttached);
	Show(ForgetButton, bScanning || bAttached);
	Show(CancelButton, bJournalDecision || bStarting || bScanning || bProblem || bAttached);
	ScanLabel->SetText(FText::FromString(bAttached ? TEXT("Change PM5") : (bScanning ? TEXT("Scan again") : TEXT("Scan for a different PM5"))));
	CancelLabel->SetText(FText::FromString(bAttached ? TEXT("Disconnect") : TEXT("Cancel")));
}

void UWorkoutDevicePanelWidget::HandleConnect()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->BeginConnect();
}

void UWorkoutDevicePanelWidget::HandleRetry()
{
	UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem();
	if (!Subsystem)
		return;
	if (LastPanel.Mode == EWorkoutDevicePanelMode::JournalDecision)
		Subsystem->BeginConnect();
	else
		Subsystem->ScanForDevices();
}

void UWorkoutDevicePanelWidget::HandleConfirmWithoutSaving()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->ConfirmRowWithoutSaving();
}

void UWorkoutDevicePanelWidget::HandleScan()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->ScanForDevices();
}

void UWorkoutDevicePanelWidget::HandleForget()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->ForgetDevice();
}

void UWorkoutDevicePanelWidget::HandleCancel()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->CancelConnect();
}

void UWorkoutDevicePanelWidget::SelectCandidate(int32 Index)
{
	// The token of the PM5 the user saw at this position, not whatever the list has re-sorted into.
	if (!LastPanel.CandidateTokens.IsValidIndex(Index))
		return;
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->SelectDeviceByToken(LastPanel.CandidateTokens[Index]);
}
void UWorkoutDevicePanelWidget::HandleCandidate0()
{
	SelectCandidate(0);
}
void UWorkoutDevicePanelWidget::HandleCandidate1()
{
	SelectCandidate(1);
}
void UWorkoutDevicePanelWidget::HandleCandidate2()
{
	SelectCandidate(2);
}
void UWorkoutDevicePanelWidget::HandleCandidate3()
{
	SelectCandidate(3);
}
void UWorkoutDevicePanelWidget::HandleCandidate4()
{
	SelectCandidate(4);
}
