#include "AegisLab.h"
#include "AegisSquadPlanner.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
AAegisScenarioRunner* PlannerRunner(UWorld* World)
{
    if (World) for (TActorIterator<AAegisScenarioRunner> It(World); It; ++It)
        if (It->IsInteractive()) return *It;
    return nullptr;
}

class SAegisPlannerPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAegisPlannerPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AAegisPlayerController>, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Controller = Args._Controller;
        const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 24);
        const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 15);
        const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
        const FLinearColor Cyan(0.22f, 0.88f, 0.94f), Muted(0.55f, 0.69f, 0.78f);
        auto Preset = [&](const TCHAR* Label, const TCHAR* Instruction) -> TSharedRef<SWidget>
        {
            return SNew(SButton).ContentPadding(FMargin(14, 12))
                .OnClicked_Lambda([this, Text = FString(Instruction)]() {
                    Entry->SetText(FText::FromString(Text));
                    Submit(Text);
                    return FReply::Handled();
                })
                [SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont)];
        };
        ChildSlot
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.005f, 0.01f, 0.018f, 0.92f)).Padding(24)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [SNew(SBox).WidthOverride(820).HeightOverride(580)
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(0.025f, 0.055f, 0.08f)).Padding(28)
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text(FText::FromString(TEXT("AEGIS / SQUAD COPILOT"))).Font(TitleFont).ColorAndOpacity(Cyan)]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 18)
                            [SNew(STextBlock).Text(FText::FromString(TEXT("Give Aegis a goal. Read the plan, then return to combat.\nThe world stays paused here. In combat, Z / X / C take back command.")))
                                .Font(BodyFont).ColorAndOpacity(Muted)]
                            + SVerticalBox::Slot().AutoHeight()
                            [SAssignNew(Entry, SEditableTextBox).Font(BodyFont).MinDesiredWidth(700)
                                .ClearKeyboardFocusOnCommit(false)
                                .HintText(FText::FromString(TEXT("e.g. Secure the relay, then cover me. (English or Chinese)")))
                                .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type) {
                                    if (Type == ETextCommit::OnEnter) Submit(Text.ToString());
                                })]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 10)
                            [SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)[Preset(TEXT("Capture + cover"), TEXT("Secure the relay, then cover me."))]
                                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)[Preset(TEXT("Protect me"), TEXT("Stay close and protect me."))]
                                + SHorizontalBox::Slot().FillWidth(1)[Preset(TEXT("Attack + regroup"), TEXT("Attack the visible threat, then regroup."))]]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 14)
                            [SNew(SButton).ContentPadding(FMargin(14, 10))
                                .OnClicked_Lambda([this]() { Submit(Entry->GetText().ToString()); return FReply::Handled(); })
                                [SNew(STextBlock).Text(FText::FromString(TEXT("ASK AEGIS  /  Enter"))).Font(BodyFont)]]
                            + SVerticalBox::Slot().FillHeight(1)
                            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                .BorderBackgroundColor(FLinearColor(0.015f, 0.03f, 0.045f)).Padding(16)
                                [SNew(SVerticalBox)
                                    + SVerticalBox::Slot().AutoHeight()
                                    [SNew(STextBlock).Text_Lambda([this]() { return Status(); }).Font(BodyFont).ColorAndOpacity(Cyan).AutoWrapText(true)]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                                    [SNew(STextBlock).Text_Lambda([this]() { return Plan(); }).Font(BodyFont).ColorAndOpacity(FLinearColor::White).AutoWrapText(true)]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                                    [SNew(STextBlock).Text_Lambda([this]() { return Outcome(); }).Font(SmallFont).ColorAndOpacity(Muted).AutoWrapText(true)]]]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 8)
                            [SNew(SButton).ContentPadding(FMargin(16, 13))
                                .OnClicked_Lambda([this]() { if (Controller.IsValid()) Controller->ClosePlannerPanel(); return FReply::Handled(); })
                                [SNew(STextBlock).Text(FText::FromString(TEXT("RETURN TO COMBAT  /  Tab or Esc"))).Font(BodyFont)]]
                            + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text(FText::FromString(TEXT("AI selects tasks; sight, paths and combat remain checked by the game.")))
                                .Font(SmallFont).ColorAndOpacity(Muted)]
                        ]
                    ]
                ]
            ]
        ];
    }

    TSharedPtr<SWidget> InputWidget() const { return Entry; }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Tab)
        {
            if (Controller.IsValid()) Controller->ClosePlannerPanel();
            return FReply::Handled();
        }
        return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
    }

private:
    TWeakObjectPtr<AAegisPlayerController> Controller;
    TSharedPtr<SEditableTextBox> Entry;
    FString LocalMessage;
    AAegisSquadPlanner* Planner() const
    {
        auto* Runner = Controller.IsValid() ? PlannerRunner(Controller->GetWorld()) : nullptr;
        return Runner ? Runner->GetSquadPlanner() : nullptr;
    }
    void Submit(const FString& Text)
    {
        LocalMessage.Reset();
        if (Controller.IsValid() && Planner()) Controller->SubmitPlannerInstruction(Text);
        else LocalMessage = TEXT("Copilot is preparing. Deploy with a living companion to ask for a plan.");
    }
    FText Status() const
    {
        if (!LocalMessage.IsEmpty()) return FText::FromString(LocalMessage);
        const auto* Agent = Planner();
        return FText::FromString(Agent ? Agent->StatusLabel + TEXT("\n") + Agent->ProviderLabel +
            FString::Printf(TEXT("  |  last response %.0f ms"), Agent->LastLatencyMs) : TEXT("Copilot is preparing."));
    }
    FText Plan() const
    {
        const auto* Agent = Planner();
        return FText::FromString(Agent ? Agent->StepsLabel + TEXT("\n") + Agent->DecisionLabel : FString());
    }
    FText Outcome() const
    {
        const auto* Agent = Planner();
        return FText::FromString(Agent ? TEXT("Latest result: ") + Agent->LastOutcome : FString());
    }
};
}

void AAegisPlayerController::TogglePlannerPanel()
{
    if (bPlannerOpen) { ClosePlannerPanel(); return; }
    auto* Runner = PlannerRunner(GetWorld());
    if (!Runner || !Runner->GetSquadPlanner() || bMenuOpen || Runner->IsUpgradePending() ||
        Runner->GetTrial().finished() || !GetWorld()->GetGameViewport()) return;
    bPlannerOpen = true;
    SetPause(true);
    if (PlayerInput) PlayerInput->FlushPressedKeys();
    auto Panel = SNew(SAegisPlannerPanel).Controller(this);
    PlannerPanel = Panel;
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Panel, 100);
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(Panel->InputWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}
void AAegisPlayerController::ClosePlannerPanel()
{
    if (!bPlannerOpen && !PlannerPanel.IsValid()) return;
    bPlannerOpen = false;
    if (PlannerPanel.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
        GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(PlannerPanel.ToSharedRef());
    PlannerPanel.Reset();
    auto* Runner = PlannerRunner(GetWorld());
    SetPause(bMenuOpen || (Runner && Runner->IsUpgradePending()));
    if (PlayerInput) PlayerInput->FlushPressedKeys();
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}
bool AAegisPlayerController::SubmitPlannerInstruction(const FString& Instruction)
{
    auto* Runner = PlannerRunner(GetWorld());
    return Runner && Runner->GetSquadPlanner() && Runner->GetSquadPlanner()->RequestPlan(Instruction);
}
void AAegisPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
    ClosePlannerPanel();
    Super::EndPlay(Reason);
}
