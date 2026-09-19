#include "WorkoutHudWidget.h"

#include "VirDebugLog.h"
#include "WorkoutSubsystem.h"
#include "CourseSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

#include "WorkoutRuntime/WorkoutDisplay.h"

namespace
{
	constexpr int32 MetricFontSize = 72;
	constexpr int32 LabelFontSize = 24;
	constexpr int32 BannerFontSize = 32;
	constexpr float MetricCellWidth = 420.0f;
	constexpr float LabelCellWidth = 280.0f;

	FText ToText(const std::string &Value)
	{
		return FText::FromString(UTF8_TO_TCHAR(Value.c_str()));
	}

	const FLinearColor LiveColor(1.0f, 1.0f, 1.0f, 1.0f);
	// Stale values are dimmed, but the banner and connection label carry the same
	// information in text: no essential state relies on color alone.
	const FLinearColor StaleColor(0.55f, 0.55f, 0.55f, 1.0f);
} // namespace

// Escape is handled before Slate routes it along the keyboard-focus path, because
// that path only reaches this widget while focus is inside it: after a click on
// empty HUD space the game viewport owns focus and the widget would never see the
// key. Escape only moves focus to the action that applies; it never activates it.
class FHudEscapeProcessor final : public IInputProcessor
{
  public:
	explicit FHudEscapeProcessor(UWorkoutHudWidget &InOwner)
		: Owner(&InOwner)
	{
	}

	virtual void Tick(const float, FSlateApplication &, TSharedRef<ICursor>) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication &, const FKeyEvent &InKeyEvent) override
	{
		if (InKeyEvent.GetKey() != EKeys::Escape || InKeyEvent.IsRepeat() || !Owner.IsValid())
			return false;
		Owner->FocusAction();
		VirDebugLog(TEXT("HUD: Escape moved focus to the applicable action"));
		return true;
	}

	virtual const TCHAR *GetDebugName() const override
	{
		return TEXT("VirtualRowingHudEscape");
	}

  private:
	TWeakObjectPtr<UWorkoutHudWidget> Owner;
};

void UWorkoutHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	auto MakeText = [this](const FString &Content, int32 Size, ETextJustify::Type Justification, const FName &Typeface)
	{
		UTextBlock *Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Content));
		Text->SetFont(FCoreStyle::GetDefaultFontStyle(Typeface, Size));
		Text->SetJustification(Justification);
		Text->SetColorAndOpacity(FSlateColor(LiveColor));
		return Text;
	};

	// One label + fixed-width, right-aligned value per row. Roboto's figures are
	// tabular, and the fixed cell keeps every value's position stable.
	UVerticalBox *Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	auto AddRow = [&](const TCHAR *Label, TObjectPtr<UTextBlock> &OutLabel, TObjectPtr<UTextBlock> &OutValue)
	{
		UHorizontalBox *Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		OutLabel = MakeText(Label, LabelFontSize, ETextJustify::Left, "Regular");
		USizeBox *LabelCell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabelCell->SetWidthOverride(LabelCellWidth);
		LabelCell->AddChild(OutLabel);
		Row->AddChildToHorizontalBox(LabelCell)->SetVerticalAlignment(VAlign_Center);
		OutValue = MakeText(TEXT("--"), MetricFontSize, ETextJustify::Right, "Bold");
		USizeBox *ValueCell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ValueCell->SetWidthOverride(MetricCellWidth);
		ValueCell->AddChild(OutValue);
		Row->AddChildToHorizontalBox(ValueCell);
		UVerticalBoxSlot *RowSlot = Column->AddChildToVerticalBox(Row);
		RowSlot->SetPadding(FMargin(0.0f, 6.0f));
	};

	// Banner and connection line sit in a fixed-height cell so the metrics below
	// never move when a banner appears or clears.
	BannerText = MakeText(TEXT(""), BannerFontSize, ETextJustify::Center, "Bold");
	USizeBox *BannerCell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BannerCell->SetHeightOverride(56.0f);
	BannerCell->AddChild(BannerText);
	Column->AddChildToVerticalBox(BannerCell);

	ConnectionText = MakeText(TEXT(""), LabelFontSize, ETextJustify::Center, "Regular");
	Column->AddChildToVerticalBox(ConnectionText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
	EstimatedStrokeText = MakeText(TEXT("Estimated stroke motion"), LabelFontSize, ETextJustify::Center, "Regular");
	EstimatedStrokeText->SetVisibility(ESlateVisibility::Hidden);
	Column->AddChildToVerticalBox(EstimatedStrokeText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	TObjectPtr<UTextBlock> Unused;
	AddRow(TEXT("DISTANCE (m)"), Unused, DistanceText);
	AddRow(TEXT("TIME"), Unused, ElapsedText);
	AddRow(TEXT("PACE (/500 m)"), Unused, PaceText);
	AddRow(TEXT("POWER (W)"), Unused, WattsText);
	AddRow(TEXT("STROKE RATE (spm)"), Unused, StrokeRateText);
	AddRow(TEXT("HEART RATE (bpm)"), HeartRateLabel, HeartRateText);

	auto MakeButton = [&](const TCHAR *Caption, TObjectPtr<UButton> &OutButton)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock *Label = MakeText(Caption, BannerFontSize, ETextJustify::Center, "Bold");
		OutButton->AddChild(Label);
		UWidget *Content = OutButton->GetContent();
		if (UButtonSlot *ButtonSlot = Cast<UButtonSlot>(Content ? Content->Slot : nullptr))
			ButtonSlot->SetPadding(FMargin(24.0f, 12.0f));
	};
	UHorizontalBox *Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	MakeButton(TEXT("End Session"), EndButton);
	MakeButton(TEXT("Start New"), StartNewButton);
	Actions->AddChildToHorizontalBox(EndButton)->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
	Actions->AddChildToHorizontalBox(StartNewButton);
	Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0.0f, 24.0f, 0.0f, 0.0f));

	EndButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleEndClicked);
	StartNewButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleStartNewClicked);

	UBorder *Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Root->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f));
	Root->SetHorizontalAlignment(HAlign_Center);
	Root->SetVerticalAlignment(VAlign_Center);
	Root->AddChild(Column);
	WidgetTree->RootWidget = Root;

	SetIsFocusable(true);
}

UWorkoutSubsystem *UWorkoutHudWidget::GetWorkoutSubsystem() const
{
	UGameInstance *GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UWorkoutSubsystem>() : nullptr;
}

void UWorkoutHudWidget::NativeTick(const FGeometry &MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem();
	if (!Subsystem)
		return;
	if (UGameInstance *GameInstance = GetGameInstance())
	{
		if (const UCourseSubsystem *Course = GameInstance->GetSubsystem<UCourseSubsystem>())
			EstimatedStrokeText->SetVisibility(AnimationLabelVisibility(Course->GetAnimationQuality()));
	}
	if (!bHasApplied || Subsystem->GetDisplayGeneration() != AppliedGeneration)
	{
		ApplyDisplay(*Subsystem);
		// Closes the software-latency measurement for the samples behind this display.
		if (UWorkoutSubsystem *Mutable = GetWorkoutSubsystem())
			Mutable->NoteDisplayApplied(AppliedGeneration);
	}
	if (bInitialFocusPending)
	{
		FocusAction();
		++InitialFocusAttempts;
		if (IsActionFocused())
		{
			bInitialFocusPending = false;
			VirDebugLog(FString::Printf(TEXT("HUD: initial focus landed after %d attempt(s)"), InitialFocusAttempts));
		}
		else if (InitialFocusAttempts >= 300)
		{
			bInitialFocusPending = false;
			VirDebugLog(TEXT("HUD: initial focus did not land after 300 frames"));
		}
	}
	ApplyFocusCue();
}

ESlateVisibility UWorkoutHudWidget::AnimationLabelVisibility(ECourseAnimationQuality Quality)
{
	return Quality == ECourseAnimationQuality::Estimated ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
}

bool UWorkoutHudWidget::IsActionFocused() const
{
	return EndButton->HasKeyboardFocus() || StartNewButton->HasKeyboardFocus();
}

void UWorkoutHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!EscapeProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		EscapeProcessor = MakeShared<FHudEscapeProcessor>(*this);
		FSlateApplication::Get().RegisterInputPreProcessor(EscapeProcessor);
	}
}

void UWorkoutHudWidget::NativeDestruct()
{
	if (EscapeProcessor.IsValid() && FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(EscapeProcessor);
	EscapeProcessor.Reset();
	Super::NativeDestruct();
}

void UWorkoutHudWidget::ApplyFocusCue()
{
	// Keyboard focus must be visible: a focused action is drawn bright with a
	// contrasting fill, others are dark. Restyled only when focus actually changes.
	const bool bEndFocused = EndButton->HasKeyboardFocus();
	const bool bStartNewFocused = StartNewButton->HasKeyboardFocus();
	if (bFocusCueApplied && bEndFocused == bEndFocusedApplied && bStartNewFocused == bStartNewFocusedApplied)
		return;
	bFocusCueApplied = true;
	bEndFocusedApplied = bEndFocused;
	bStartNewFocusedApplied = bStartNewFocused;
	const FLinearColor FocusedFill(0.10f, 0.45f, 0.95f, 1.0f);
	const FLinearColor RestingFill(0.16f, 0.18f, 0.22f, 1.0f);
	EndButton->SetBackgroundColor(bEndFocused ? FocusedFill : RestingFill);
	StartNewButton->SetBackgroundColor(bStartNewFocused ? FocusedFill : RestingFill);
}

void UWorkoutHudWidget::ApplyDisplay(const UWorkoutSubsystem &Subsystem)
{
	const FWorkoutDisplay &Display = Subsystem.GetDisplay();
	const FSlateColor ValueColor(Display.bValuesStale ? StaleColor : LiveColor);

	BannerText->SetText(ToText(Display.Banner));
	ConnectionText->SetText(ToText(Display.ConnectionLabel));
	const auto SetValue = [&ValueColor](UTextBlock *Text, const std::string &Value)
	{
		Text->SetText(ToText(Value));
		Text->SetColorAndOpacity(ValueColor);
	};
	SetValue(DistanceText, Display.Distance);
	SetValue(ElapsedText, Display.Elapsed);
	SetValue(PaceText, Display.Pace);
	SetValue(WattsText, Display.Watts);
	SetValue(StrokeRateText, Display.StrokeRate);

	// Hidden (not collapsed) keeps every other row where it is.
	const ESlateVisibility HeartRateVisibility = Display.bHeartRateSupplied ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	HeartRateLabel->SetVisibility(HeartRateVisibility);
	HeartRateText->SetVisibility(HeartRateVisibility);
	SetValue(HeartRateText, Display.HeartRate);

	EndButton->SetIsEnabled(Display.bCanEnd);
	StartNewButton->SetIsEnabled(Display.bCanStartNew);

	const bool bActionChanged = !bHasApplied || bAppliedCanEnd != Display.bCanEnd;
	bHasApplied = true;
	AppliedGeneration = Subsystem.GetDisplayGeneration();
	bAppliedCanEnd = Display.bCanEnd;
	// A disabled button cannot keep keyboard focus, so move it to the action that
	// now applies.
	if (bActionChanged && (HasAnyUserFocus() || HasFocusedDescendants()))
		FocusAction();
}

void UWorkoutHudWidget::FocusAction()
{
	if (bAppliedCanEnd || !StartNewButton->GetIsEnabled())
		EndButton->SetKeyboardFocus();
	else
		StartNewButton->SetKeyboardFocus();
}

void UWorkoutHudWidget::FocusPrimaryAction(APlayerController *Controller)
{
	if (!Controller)
		return;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->SetInputMode(InputMode);
	Controller->SetShowMouseCursor(true);
	const UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem();
	if (Subsystem)
		bAppliedCanEnd = Subsystem->GetDisplay().bCanEnd;
	bInitialFocusPending = true;
	InitialFocusAttempts = 0;
	FocusAction();
}

void UWorkoutHudWidget::HandleEndClicked()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->EndSession();
}

void UWorkoutHudWidget::HandleStartNewClicked()
{
	if (UWorkoutSubsystem *Subsystem = GetWorkoutSubsystem())
		Subsystem->StartNewSession();
}
