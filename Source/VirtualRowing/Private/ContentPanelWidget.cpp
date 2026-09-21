#include "ContentPanelWidget.h"

#include "ContentSubsystem.h"
#include "CourseSubsystem.h"
#include "WorkoutHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 HeadingFontSize = 15;
	constexpr int32 LabelFontSize = 14;
	constexpr int32 DetailFontSize = 12;
	constexpr float PanelWidth = 520.0f;
	constexpr float DiagnosticsMaxHeight = 440.0f;
	constexpr float RefreshIntervalSeconds = 0.25f;

	const FLinearColor LiveColor(1.0f, 1.0f, 1.0f, 1.0f);
	const FLinearColor DimColor(0.55f, 0.55f, 0.55f, 1.0f);
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

void UContentPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	auto MakeText = [this](const FString &Content, int32 Size, const FName &Typeface)
	{
		UTextBlock *Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Content));
		Text->SetFont(FCoreStyle::GetDefaultFontStyle(Typeface, Size));
		Text->SetJustification(ETextJustify::Left);
		Text->SetColorAndOpacity(FSlateColor(LiveColor));
		Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Text;
	};
	auto MakeWrapping = [&](const FString &Content, int32 Size)
	{
		UTextBlock *Text = MakeText(Content, Size, "Regular");
		Text->SetAutoWrapText(true);
		// A long URL or path has no spaces; break it per character to stay in the panel.
		Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
		return Text;
	};

	UVerticalBox *Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	HanLevelText = MakeWrapping(TEXT(""), LabelFontSize);
	Column->AddChildToVerticalBox(HanLevelText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	Column->AddChildToVerticalBox(MakeText(TEXT("COURSE"), HeadingFontSize, "Bold"))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	UHorizontalBox *Courses = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto MakeCourseButton = [&](const TCHAR *Caption, TObjectPtr<UButton> &OutButton)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		OutButton->AddChild(MakeText(Caption, LabelFontSize, "Bold"));
		if (UButtonSlot *ButtonSlot = Cast<UButtonSlot>(OutButton->GetContent()->Slot))
			ButtonSlot->SetPadding(FMargin(10.0f, 6.0f));
		Courses->AddChildToHorizontalBox(OutButton)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	};
	MakeCourseButton(TEXT("Standard • 2 km"), StandardCourseButton);
	MakeCourseButton(TEXT("Han River • 5 km"), HanCourseButton);
	Column->AddChildToVerticalBox(Courses);
	CourseSelectionText = MakeWrapping(TEXT(""), LabelFontSize);
	Column->AddChildToVerticalBox(CourseSelectionText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	HanAvailabilityText = MakeWrapping(TEXT(""), LabelFontSize);
	HanAvailabilityText->SetColorAndOpacity(FSlateColor(DimColor));
	Column->AddChildToVerticalBox(HanAvailabilityText)->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	HanPackageBuildText = MakeWrapping(TEXT(""), DetailFontSize + 1);
	HanPackageBuildText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(HanPackageBuildText)->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));

	DownloadHanButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	DownloadHanButton->AddChild(MakeText(TEXT("Download Han River"), LabelFontSize, "Bold"));
	Column->AddChildToVerticalBox(DownloadHanButton)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	ContentLicensesButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	ContentLicensesButton->AddChild(MakeText(TEXT("Content Licenses / Credits"), LabelFontSize, "Bold"));
	Column->AddChildToVerticalBox(ContentLicensesButton)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	ContentLicensesText = MakeWrapping(TEXT(""), DetailFontSize + 1);
	ContentLicensesText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(ContentLicensesText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

	// Verbose view: bounded and scrollable so a long event log never grows the panel
	// off the screen.
	DetailsButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	DetailsButtonLabel = MakeText(TEXT("Show details"), LabelFontSize, "Bold");
	DetailsButton->AddChild(DetailsButtonLabel);
	Column->AddChildToVerticalBox(DetailsButton)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 4.0f));
	DiagnosticsText = MakeWrapping(TEXT(""), DetailFontSize);
	UScrollBox *DiagnosticsScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	DiagnosticsScroll->AddChild(DiagnosticsText);
	DiagnosticsBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	DiagnosticsBounds->SetVisibility(ESlateVisibility::Collapsed);
	DiagnosticsBounds->SetMaxDesiredHeight(DiagnosticsMaxHeight);
	DiagnosticsBounds->AddChild(DiagnosticsScroll);
	Column->AddChildToVerticalBox(DiagnosticsBounds);

	StandardCourseButton->OnClicked.AddDynamic(this, &UContentPanelWidget::HandleStandardCourseClicked);
	HanCourseButton->OnClicked.AddDynamic(this, &UContentPanelWidget::HandleHanCourseClicked);
	DownloadHanButton->OnClicked.AddDynamic(this, &UContentPanelWidget::HandleDownloadHanClicked);
	ContentLicensesButton->OnClicked.AddDynamic(this, &UContentPanelWidget::HandleContentLicensesClicked);
	DetailsButton->OnClicked.AddDynamic(this, &UContentPanelWidget::HandleDetailsClicked);

	USizeBox *Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Width->SetWidthOverride(PanelWidth);
	Width->AddChild(Column);
	UBorder *Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrushColor(UWorkoutHudWidget::MetricPanelBackgroundColor());
	Panel->SetPadding(FMargin(16.0f));
	Panel->AddChild(Width);
	// The root fills the viewport but stays out of hit testing, so only the panel's
	// own controls take clicks and the world behind it keeps its view and input.
	UBorder *Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Root->SetBrushColor(UWorkoutHudWidget::RootBackgroundColor());
	Root->SetHorizontalAlignment(HAlign_Right);
	Root->SetVerticalAlignment(VAlign_Top);
	Root->SetPadding(FMargin(0.0f, 24.0f, 32.0f, 0.0f));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Root->AddChild(Panel);
	WidgetTree->RootWidget = Root;
}

void UContentPanelWidget::HandleStandardCourseClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->SelectRouteById(TEXT("route.standard.2k"));
}

void UContentPanelWidget::HandleHanCourseClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->SelectRouteById(TEXT("route.han-river.5k"));
}

void UContentPanelWidget::HandleDownloadHanClicked()
{
	if (UGameInstance *GameInstance = GetGameInstance())
		if (UContentSubsystem *Content = GameInstance->GetSubsystem<UContentSubsystem>())
			Content->BeginHanDownload();
}

void UContentPanelWidget::HandleContentLicensesClicked()
{
	UGameInstance *GameInstance = GetGameInstance();
	UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (!Content || !ContentLicensesText)
		return;
	ContentLicensesText->SetText(FText::FromString(Content->GetContentLicensesCreditsText()));
	ContentLicensesText->SetVisibility(ContentLicensesText->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UContentPanelWidget::HandleDetailsClicked()
{
	const bool bShow = DiagnosticsBounds->GetVisibility() == ESlateVisibility::Collapsed;
	DiagnosticsBounds->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	DetailsButtonLabel->SetText(FText::FromString(bShow ? TEXT("Hide details") : TEXT("Show details")));
	// Refresh at once instead of waiting for the next interval.
	SecondsSinceDiagnostics = RefreshIntervalSeconds;
}

void UContentPanelWidget::NativeTick(const FGeometry &MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	SecondsSinceDiagnostics += InDeltaTime;
	Sync();
}

void UContentPanelWidget::Sync()
{
	UGameInstance *GameInstance = GetGameInstance();
	UContentSubsystem *Content = GameInstance ? GameInstance->GetSubsystem<UContentSubsystem>() : nullptr;
	if (!Content || !StandardCourseButton || !HanCourseButton || !DownloadHanButton)
		return;
	const UWorld *World = GetWorld();
	const UCourseSubsystem *Course = World ? World->GetSubsystem<UCourseSubsystem>() : nullptr;

	const bool bCanSelect = Content->CanOperateContent();
	const bool bHanAvailable = Content->IsHanAvailable();
	const bool bHanUpdate = Content->IsHanUpdateAvailable();
	const FString SelectedRoute = UTF8_TO_TCHAR(Content->GetSelectedRoute().RouteId.c_str());
	StandardCourseButton->SetIsEnabled(bCanSelect);
	HanCourseButton->SetIsEnabled(bCanSelect && bHanAvailable);
	DownloadHanButton->SetIsEnabled(bCanSelect && ((!bHanAvailable && Content->GetContentOperationStatus() == TEXT("content.catalog_ready")) || bHanUpdate));
	DownloadHanButton->SetVisibility(bHanAvailable && !bHanUpdate ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	StandardCourseButton->SetBackgroundColor(SelectedRoute == TEXT("route.standard.2k") ? SelectedCourseFill : UnselectedCourseFill);
	HanCourseButton->SetBackgroundColor(SelectedRoute == TEXT("route.han-river.5k") ? SelectedCourseFill : UnselectedCourseFill);
	CourseSelectionText->SetText(FText::FromString(SelectedRoute == TEXT("route.han-river.5k") ? TEXT("Selected: Han River • 5 km • Open") : TEXT("Selected: Standard • 2 km • Closed")));
	HanAvailabilityText->SetText(FText::FromString(bHanAvailable ? TEXT("") : HanAvailabilityTextFor(Content->GetHanAvailabilityReason())));
	HanAvailabilityText->SetVisibility(bHanAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	HanLevelText->SetText(FText::FromString(TEXT("Han level: ") + (Course ? Course->GetAuthoredLevelSummary() : FString(TEXT("no course subsystem")))));

	const int64 IssuedAt = Content->GetActivePackageIssuedAtUnixSeconds();
	const bool bShowBuild = IssuedAt > 0 && Content->IsHanContentMounted();
	if (bShowBuild)
		HanPackageBuildText->SetText(FText::FromString(TEXT("Han package built: ") + FDateTime::FromUnixTimestamp(IssuedAt).ToString(TEXT("%Y-%m-%d %H:%M:%S UTC"))));
	HanPackageBuildText->SetVisibility(bShowBuild ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (SecondsSinceDiagnostics >= RefreshIntervalSeconds && DiagnosticsBounds->GetVisibility() != ESlateVisibility::Collapsed)
	{
		SecondsSinceDiagnostics = 0.0f;
		DiagnosticsText->SetText(FText::FromString(TEXT("Course level\n") + (Course ? Course->GetAuthoredLevelDiagnosticsText() : FString(TEXT("no course subsystem"))) +
												   TEXT("\n\nContent status\n") + Content->GetContentOperationStatus() + TEXT("\n") + Content->GetDiagnosticsText()));
	}
}
