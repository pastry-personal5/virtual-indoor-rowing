#include "WorkoutHudWidget.h"

#include "VirDebugLog.h"
#include "ContentSubsystem.h"
#include "WorkoutSubsystem.h"
#include "CourseSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

#include "WorkoutRuntime/WorkoutDisplay.h"

namespace
{
	constexpr int32 MetricFontSize = 48;
	constexpr int32 LabelFontSize = 18;
	constexpr int32 BannerFontSize = 24;
	constexpr int32 CourseFontSize = 18;
	constexpr float MetricCellWidth = 300.0f;
	constexpr float LabelCellWidth = 220.0f;
	constexpr float StatusPanelWidth = 420.0f;

	FText ToText(const std::string &Value)
	{
		return FText::FromString(UTF8_TO_TCHAR(Value.c_str()));
	}

	const FLinearColor LiveColor(1.0f, 1.0f, 1.0f, 1.0f);
	// Stale values are dimmed, but the banner and connection label carry the same
	// information in text: no essential state relies on color alone.
	const FLinearColor StaleColor(0.55f, 0.55f, 0.55f, 1.0f);
	const FLinearColor SelectedCourseFill(0.10f, 0.45f, 0.95f, 1.0f);
	const FLinearColor UnselectedCourseFill(0.16f, 0.18f, 0.22f, 1.0f);

	FString HanAvailabilityTextFor(const FString &Reason)
	{
		if (Reason == TEXT("content.han.safe_mode"))
			return TEXT("Han River unavailable: Safe Mode is on.");
		if (Reason == TEXT("content.han.expired"))
			return TEXT("Han River unavailable: refresh content while online.");
		if (Reason == TEXT("content.han.withdrawn"))
			return TEXT("Han River is currently unavailable.");
		return TEXT("Han River unavailable: download and restart to activate it.");
	}
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
		Text->SetShadowOffset(FVector2D(2.0f, 2.0f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
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
	MetricAccuracyText = MakeText(MetricAccuracyNotice(), LabelFontSize, ETextJustify::Center, "Regular");
	MetricAccuracyText->SetColorAndOpacity(FSlateColor(StaleColor));
	Column->AddChildToVerticalBox(MetricAccuracyText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
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

	UTextBlock *CourseHeading = MakeText(TEXT("COURSE"), LabelFontSize, ETextJustify::Left, "Bold");
	Column->AddChildToVerticalBox(CourseHeading)->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 4.0f));
	UHorizontalBox *Courses = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto MakeCourseButton = [&](const TCHAR *Caption, TObjectPtr<UButton> &OutButton)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		OutButton->AddChild(MakeText(Caption, CourseFontSize, ETextJustify::Center, "Bold"));
		if (UButtonSlot *ButtonSlot = Cast<UButtonSlot>(OutButton->GetContent()->Slot))
			ButtonSlot->SetPadding(FMargin(12.0f, 8.0f));
		Courses->AddChildToHorizontalBox(OutButton)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	};
	MakeCourseButton(TEXT("Standard • 2 km"), StandardCourseButton);
	MakeCourseButton(TEXT("Han River • 5 km"), HanCourseButton);
	Column->AddChildToVerticalBox(Courses);
	CourseSelectionText = MakeText(TEXT(""), LabelFontSize, ETextJustify::Left, "Regular");
	Column->AddChildToVerticalBox(CourseSelectionText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	HanAvailabilityText = MakeText(TEXT(""), LabelFontSize, ETextJustify::Left, "Regular");
	HanAvailabilityText->SetColorAndOpacity(FSlateColor(StaleColor));
	Column->AddChildToVerticalBox(HanAvailabilityText)->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	HanPackageBuildText = MakeText(TEXT(""), 14, ETextJustify::Left, "Regular");
	HanPackageBuildText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(HanPackageBuildText)->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	DownloadHanButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	DownloadHanButton->AddChild(MakeText(TEXT("Download Han River"), LabelFontSize, ETextJustify::Left, "Bold"));
	Column->AddChildToVerticalBox(DownloadHanButton)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	ContentLicensesButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	ContentLicensesButton->AddChild(MakeText(TEXT("Content Licenses / Credits"), LabelFontSize, ETextJustify::Left, "Bold"));
	Column->AddChildToVerticalBox(ContentLicensesButton)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	ContentLicensesText = MakeText(TEXT(""), 14, ETextJustify::Left, "Regular");
	ContentLicensesText->SetAutoWrapText(true);
	ContentLicensesText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(ContentLicensesText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	StandardCourseButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleStandardCourseClicked);
	HanCourseButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleHanCourseClicked);
	DownloadHanButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleDownloadHanClicked);
	ContentLicensesButton->OnClicked.AddDynamic(this, &UWorkoutHudWidget::HandleContentLicensesClicked);

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
	// The root fills the viewport. It must remain transparent so the gray-box world
	// and its camera render behind the HUD rather than being covered by this widget.
	Root->SetBrushColor(RootBackgroundColor());
	// Keep the presentation's focal area clear. The previous centered 756+ px card
	// hid the boat in common 1280 px test windows even after its root became clear.
	Root->SetHorizontalAlignment(HAlign_Fill);
	Root->SetVerticalAlignment(VAlign_Fill);
	UBorder *MetricPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	// Keep the metrics readable under exertion without turning the large center card
	// back into an apparent full-screen wall. Text shadows provide the contrast that
	// the former near-opaque fill supplied.
	MetricPanel->SetBrushColor(MetricPanelBackgroundColor());
	MetricPanel->SetPadding(FMargin(20.0f));
	MetricPanel->AddChild(Column);
	UOverlay *Layout = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot *MetricSlot = Layout->AddChildToOverlay(MetricPanel);
	MetricSlot->SetHorizontalAlignment(HAlign_Left);
	MetricSlot->SetVerticalAlignment(VAlign_Center);
	MetricSlot->SetPadding(FMargin(32.0f, 0.0f, 0.0f, 0.0f));

	// Right-hand diagnostics box: fixed width, wraps per character so a long path
	// or URL with no spaces still stays inside the box.
	ContentStatusText = MakeText(TEXT(""), 14, ETextJustify::Left, "Regular");
	ContentStatusText->SetAutoWrapText(true);
	ContentStatusText->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
	USizeBox *StatusWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	StatusWidth->SetWidthOverride(StatusPanelWidth);
	StatusWidth->AddChild(ContentStatusText);
	UBorder *StatusPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	StatusPanel->SetBrushColor(MetricPanelBackgroundColor());
	StatusPanel->SetPadding(FMargin(16.0f));
	StatusPanel->AddChild(StatusWidth);
	UOverlaySlot *StatusSlot = Layout->AddChildToOverlay(StatusPanel);
	StatusSlot->SetHorizontalAlignment(HAlign_Right);
	StatusSlot->SetVerticalAlignment(VAlign_Center);
	StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 32.0f, 0.0f));
	Root->AddChild(Layout);
	WidgetTree->RootWidget = Root;

	SetIsFocusable(true);
}

void UWorkoutHudWidget::HandleContentLicensesClicked()
{
	UGameInstance *GameInstance = GetGameInstance();
	UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (!Content || !ContentLicensesText)
		return;
	ContentLicensesText->SetText(FText::FromString(Content->GetContentLicensesCreditsText()));
	ContentLicensesText->SetVisibility(ContentLicensesText->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	if (UWorld *World = GetWorld())
	{
		if (const UCourseSubsystem *Course = World->GetSubsystem<UCourseSubsystem>())
			EstimatedStrokeText->SetVisibility(AnimationLabelVisibility(Course->GetAnimationQuality()));
	}
	SyncCourseSelection();
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

FLinearColor UWorkoutHudWidget::RootBackgroundColor()
{
	return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

FLinearColor UWorkoutHudWidget::MetricPanelBackgroundColor()
{
	return FLinearColor(0.02f, 0.03f, 0.05f, 0.38f);
}

const TCHAR *UWorkoutHudWidget::MetricAccuracyNotice()
{
	return TEXT("Metric-display accuracy is preliminary and may be limited.");
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

void UWorkoutHudWidget::SyncCourseSelection()
{
	UGameInstance *GameInstance = GetGameInstance();
	UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (!Content || !StandardCourseButton || !HanCourseButton || !DownloadHanButton)
		return;
	const bool bCanSelect = Content->CanOperateContent();
	const bool bHanAvailable = Content->IsHanAvailable();
	const FString SelectedRoute = UTF8_TO_TCHAR(Content->GetSelectedRoute().RouteId.c_str());
	StandardCourseButton->SetIsEnabled(bCanSelect);
	HanCourseButton->SetIsEnabled(bCanSelect && bHanAvailable);
	DownloadHanButton->SetIsEnabled(bCanSelect && !bHanAvailable && Content->GetContentOperationStatus() == TEXT("content.catalog_ready"));
	DownloadHanButton->SetVisibility(bHanAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	StandardCourseButton->SetBackgroundColor(SelectedRoute == TEXT("route.standard.2k") ? SelectedCourseFill : UnselectedCourseFill);
	HanCourseButton->SetBackgroundColor(SelectedRoute == TEXT("route.han-river.5k") ? SelectedCourseFill : UnselectedCourseFill);
	CourseSelectionText->SetText(FText::FromString(SelectedRoute == TEXT("route.han-river.5k") ? TEXT("Selected: Han River • 5 km • Open") : TEXT("Selected: Standard • 2 km • Closed")));
	HanAvailabilityText->SetText(FText::FromString(bHanAvailable ? TEXT("") : HanAvailabilityTextFor(Content->GetHanAvailabilityReason())));
	if (HanPackageBuildText)
	{
		const int64 IssuedAt = Content->GetActivePackageIssuedAtUnixSeconds();
		const bool bShow = IssuedAt > 0 && Content->IsHanContentMounted();
		if (bShow)
			HanPackageBuildText->SetText(FText::FromString(TEXT("Han package built: ") + FDateTime::FromUnixTimestamp(IssuedAt).ToString(TEXT("%Y-%m-%d %H:%M:%S UTC"))));
		HanPackageBuildText->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ContentStatusText)
		ContentStatusText->SetText(FText::FromString(TEXT("Content status\n") + Content->GetContentOperationStatus()));
	HanAvailabilityText->SetVisibility(bHanAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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

void UWorkoutHudWidget::HandleStandardCourseClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->SelectRouteById(TEXT("route.standard.2k"));
}

void UWorkoutHudWidget::HandleHanCourseClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->SelectRouteById(TEXT("route.han-river.5k"));
}

void UWorkoutHudWidget::HandleDownloadHanClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->BeginHanDownload();
}
