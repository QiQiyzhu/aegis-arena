#include "AegisLab.h"
#include "AegisDecisionLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
FString DecisionActionName(aegis::Action Action)
{
    return FString(UTF8_TO_TCHAR(aegis::actionName(Action)));
}

FString DecisionAge(double Now, double RecordedAt)
{
    return RecordedAt < 0 ? TEXT("--") : FString::Printf(TEXT("%.1fs"), FMath::Max(0.0, Now - RecordedAt));
}

FString CompanionActivity(const AAegisAIController* AI, const FAegisDecisionTelemetry* T, double Now)
{
    if (!AI || !T || T->AcceptedBTLeafAt < 0 || Now - T->AcceptedBTLeafAt > 1.0)
        return TEXT("Waiting for an action");
    const FName Leaf = T->AcceptedBTLeaf;
    if (Leaf == TEXT("Attack")) return TEXT("Attacking a visible threat");
    if (Leaf == TEXT("Chase")) return TEXT("Approaching the last known threat");
    if (Leaf == TEXT("Follow")) return AI->GetMoveStatus() == EPathFollowingStatus::Moving
        ? (T->FollowTargetSource == TEXT("team_position_link") ? TEXT("Following your shared position") : TEXT("Following you"))
        : TEXT("Staying close to you");
    if (Leaf == TEXT("Support")) return AI->GetMoveStatus() == EPathFollowingStatus::Moving
        ? TEXT("Moving closer to support you") : TEXT("Supporting you");
    if (Leaf == TEXT("Retreat") || Leaf == TEXT("FindCover"))
    {
        if (T->QueryStatus == TEXT("pending")) return TEXT("Looking for a retreat position");
        if (!T->bQueryMoveAccepted) return TEXT("Reassessing a retreat position");
        return AI->GetMoveStatus() == EPathFollowingStatus::Moving
            ? TEXT("Retreating to the chosen position") : TEXT("Holding the chosen retreat position");
    }
    if (Leaf == TEXT("Recover")) return TEXT("Recovering health");
    if (Leaf == TEXT("AttackPosition")) return TEXT("Finding a firing position");
    if (Leaf == TEXT("Investigate")) return TEXT("Checking the last known threat");
    if (Leaf == TEXT("Patrol")) return TEXT("Searching the arena");
    return Leaf.ToString();
}
}

void AAegisDebugHUD::DrawDecisionLab(AAegisDecisionLab* Lab)
{
    if (!Canvas || !Lab) return;
    auto* PC = Cast<AAegisPlayerController>(GetOwningPlayerController());
    auto* AI = Lab->GetCompanionController();
    const FAegisDecisionTelemetry* T = AI ? &AI->GetDecisionTelemetry() : nullptr;
    const double Now = GetWorld()->GetTimeSeconds();
    const bool Briefing = Lab->Phase == TEXT("Briefing");
    const bool Active = Lab->Phase == TEXT("Active");
    const bool Finished = !Briefing && !Active;
    const bool Paused = PC && PC->IsPaused();
    bool Debug = false;
#if !UE_BUILD_SHIPPING
    Debug = PC && PC->bShowDiagnostics;
#endif
    const float S = FMath::Min(Canvas->SizeX / 1280.f, Canvas->SizeY / 720.f);
    const float OX = (Canvas->SizeX - 1280.f * S) * .5f;
    const float OY = (Canvas->SizeY - 720.f * S) * .5f;
    const FLinearColor Ink(.015f, .025f, .038f, .96f), Panel(.027f, .05f, .07f, .97f);
    const FLinearColor White(.91f, .95f, .98f), Muted(.57f, .67f, .73f);
    const FLinearColor Cyan(.22f, .88f, .94f), Green(.38f, .91f, .66f);
    const FLinearColor Gold(1.f, .73f, .32f), Red(1.f, .42f, .35f);
    UFont* Font = InterfaceFont ? InterfaceFont.Get() : GEngine->GetMediumFont();
    const float FontScale = 14.f / FMath::Max(1.f, Font->GetMaxCharHeight());
    auto Box = [&](float X, float Y, float W, float H, FLinearColor C) {
        DrawRect(C, OX + X*S, OY + Y*S, W*S, H*S);
    };
    auto Text = [&](const FString& Value, float X, float Y, FLinearColor C, float Size = 1.f) {
        DrawText(Value, C, OX + X*S, OY + Y*S, Font, FontScale*Size*S, false);
    };
    auto Fit = [&](FString Value, float Width, float Size = 1.f) {
        float TW = 0, TH = 0;
        GetTextSize(Value, TW, TH, Font, FontScale*Size);
        if (TW <= Width) return Value;
        do {
            Value.LeftChopInline(1);
            GetTextSize(Value + TEXT("..."), TW, TH, Font, FontScale*Size);
        } while (!Value.IsEmpty() && TW > Width);
        return Value + TEXT("...");
    };
    auto Bar = [&](float X, float Y, float W, double Fraction, FLinearColor C) {
        Box(X, Y, W, 5, FLinearColor(.1f, .17f, .21f));
        Box(X, Y, W*FMath::Clamp(static_cast<float>(Fraction), 0.f, 1.f), 5, C);
    };
    auto Button = [&](FName Name, const FString& Label, float X, float Y, float W, FLinearColor Color) {
        float MX = -1, MY = -1;
        if (PC) PC->GetMousePosition(MX, MY);
        const bool Hover = MX >= OX+X*S && MX <= OX+(X+W)*S && MY >= OY+Y*S && MY <= OY+(Y+42)*S;
        Box(X, Y, W, 42, Hover ? FLinearColor(.1f, .25f, .28f) : FLinearColor(.05f, .1f, .14f));
        Box(X, Y, 3, 42, Color);
        Text(Fit(Label, W-24), X+12, Y+12, Color);
        AddHitBox(FVector2D(OX+X*S, OY+Y*S), FVector2D(W*S, 42*S), Name, true);
    };

    const bool Priority = Lab->Policy == TEXT("priority");
    const bool Baseline = Lab->Policy == TEXT("baseline");
    const FString PolicyName = Priority ? TEXT("PRIORITY") : (Baseline ? TEXT("BASELINE") : TEXT("IMPROVED"));
    const int32 Cleared = FMath::Clamp(Lab->EnemyDeaths, 0, Lab->EnemyCount);
    const int32 RemainingSeconds = FMath::Max(0, FMath::CeilToInt(Lab->Duration - Lab->GetElapsed()));

    Box(24, 22, 278, 68, Ink); Box(24, 22, 3, 68, Cyan);
    Text(TEXT("AEGIS / DECISION LAB"), 40, 34, White, 1.28f);
    Text(TEXT("ONE TEAM. FOUR CHOICES."), 40, 66, Muted, .85f);
    Box(318, 22, 556, 68, Ink);
    Text(FString::Printf(TEXT("CLEAR THE ARENA   %d / %d"), Cleared, Lab->EnemyCount), 336, 34, White, 1.2f);
    Text(FString::Printf(TEXT("%02ds LEFT   /   %s"), RemainingSeconds,
        Paused ? TEXT("SIMULATION PAUSED") : (Active ? TEXT("LIVE") : (Briefing ? TEXT("READY WHEN YOU ARE") : TEXT("ATTEMPT COMPLETE")))),
        336, 66, Paused ? Gold : Muted, .85f);
    Box(890, 22, 366, 68, Ink);
    Text(PolicyName + FString::Printf(TEXT("  /  SEED %d"), Lab->Seed), 906, 34, Cyan);
    Text(Debug ? TEXT("F1  CLOSE DIAGNOSTICS") : TEXT("F1  INSPECT THE AI"), 906, 66, Muted, .85f);

    Box(24, 624, 254, 72, Ink); Box(294, 624, 580, 72, Ink); Box(890, 624, 366, 72, Ink);
    if (Lab->Player && Lab->Player->Health)
    {
        const auto* H = Lab->Player->Health.Get();
        Text(FString::Printf(TEXT("YOU   %.0f / %.0f HP"), H->Current, H->Maximum), 40, 634, Cyan);
        Bar(40, 656, 222, H->Current/FMath::Max(1.f, H->Maximum), Cyan);
        Text(TEXT("WASD MOVE / HOLD LMB FIRE"), 40, 674, Muted, .8f);
    }
    if (Lab->Companion && Lab->Companion->Health)
    {
        const auto* H = Lab->Companion->Health.Get();
        Text(FString::Printf(TEXT("AEGIS   %.0f / %.0f HP"), H->Current, H->Maximum), 310, 634, Green);
        Bar(310, 656, 548, H->Current/FMath::Max(1.f, H->Maximum), Green);
        FString Activity = !H->IsAlive() ? TEXT("Companion down") : (Paused ? TEXT("Paused") :
            (Briefing ? TEXT("Ready to follow, attack, support or retreat") :
                (Finished ? TEXT("Standing down") : CompanionActivity(AI, T, Now))));
        if (Active && !Paused && T && T->LastSupportHealAt >= 0 && Now-T->LastSupportHealAt < 2.0)
            Activity = FString::Printf(TEXT("Support applied / +%.0f HP restored this attempt"), T->SupportHealAmount);
        Text(Fit(Activity, 546), 310, 673, Green);
    }
    // The paused overlay owns the sole Menu hitbox. A duplicate name makes
    // Canvas discard one button and emits a warning on every paused frame.
    if (!Paused) Button(TEXT("Menu"), TEXT("ESC / P  MENU"), 904, 637, 338, Gold);
    else Text(TEXT("ESC / P RESUME   /   X QUIT"), 906, 650, Muted, .85f);
    Text(TEXT("MOUSE AIM   /   SPACE DASH   /   R SAME-SEED RESET"), 24, 704, Muted, .75f);

    // Main-view feedback comes only from the accepted leaf and actual applied
    // healing. No candidate score is presented as an executed action.
    if (Active && !Paused && PC)
    {
        float MX = 0, MY = 0;
        if (PC->GetMousePosition(MX, MY) && !GetHitBoxAtCoordinates(FVector2D(MX, MY), true))
        {
            const bool InDebugPanel = Debug && MX >= OX+890*S && MX <= OX+1256*S && MY >= OY+102*S && MY <= OY+612*S;
            if (!InDebugPanel)
            {
                DrawLine(MX-10*S, MY, MX-4*S, MY, Cyan, 1.5f*S);
                DrawLine(MX+4*S, MY, MX+10*S, MY, Cyan, 1.5f*S);
                DrawLine(MX, MY-10*S, MX, MY-4*S, Cyan, 1.5f*S);
                DrawLine(MX, MY+4*S, MX, MY+10*S, Cyan, 1.5f*S);
            }
        }
    }

    if (Briefing || Finished || Paused)
    {
        const float X = Debug ? 138.f : 330.f;
        Box(X, 124, 620, 450, Ink); Box(X, 124, 3, 450, Cyan);
        const float L = X+24;
        if (Paused)
        {
            Text(TEXT("PAUSED / TAKE YOUR TIME"), L, 152, White, 1.45f);
            Text(TEXT("The fight and decision snapshots are frozen."), L, 196, Muted);
            Text(TEXT("F1 opens or closes the companion diagnostics."), L, 224, Muted);
            Button(TEXT("Menu"), TEXT("ESC / P  RESUME"), L, 286, 572, Cyan);
            Button(TEXT("Restart"), TEXT("R  RESET THE SAME SEED"), L, 348, 572, White);
            Button(TEXT("Quit"), TEXT("X  QUIT TO DESKTOP"), L, 410, 572, Gold);
        }
        else if (Briefing)
        {
            Text(TEXT("READ THE FIGHT."), L, 150, White, 1.65f);
            Text(TEXT("Clear the enemies before the 60-second timer ends."), L, 196, White);
            Text(TEXT("Follow: regroup. Attack: engage a seen threat."), L, 230, Muted);
            Text(TEXT("Support: approach and heal. Retreat: seek safety."), L, 254, Muted);
            Text(TEXT("Choose a policy; R repeats the same starting scene."), L, 294, Cyan);
            Button(TEXT("Guided"), TEXT("1  BASELINE"), L, 330, 179, Baseline ? Cyan : Muted);
            Button(TEXT("Pressure"), TEXT("2  IMPROVED"), L+193, 330, 186, !Baseline && !Priority ? Cyan : Muted);
            Button(TEXT("Upgrade3"), TEXT("3  PRIORITY"), L+393, 330, 179, Priority ? Cyan : Muted);
            Text(Priority ? TEXT("Visible threat: attack. Otherwise: follow.") :
                (Baseline ? TEXT("Four Utility scores with a small switch margin.") :
                    TEXT("Follow uses a registered ally position link when sight is lost.")), L, 391, Muted, .9f);
            if (!Priority && !Baseline)
                Text(TEXT("Position only: no linked health or enemy information."), L, 412, Muted, .8f);
            Button(TEXT("Deploy"), TEXT("ENTER  START"), L, 435, 572, Cyan);
            Text(TEXT("WASD + mouse / hold LMB / Space dash / F1 diagnostics"), L, 518, Muted, .85f);
        }
        else
        {
            const bool Won = Lab->Phase == TEXT("Won");
            Text(Won ? TEXT("ARENA CLEAR") : (Lab->Phase == TEXT("Timeout") ? TEXT("TIME IS UP") : TEXT("PLAYER DOWN")),
                L, 152, Won ? Green : Gold, 1.65f);
            Text(FString::Printf(TEXT("%.1fs elapsed   /   %d of %d enemies cleared"), Lab->GetElapsed(), Cleared, Lab->EnemyCount), L, 204, White);
            Text(FString::Printf(TEXT("Damage: you %.0f   /   Aegis %.0f   /   taken %.0f"),
                Lab->PlayerDamageDealt, Lab->CompanionDamageDealt, Lab->PlayerDamageTaken), L, 240, Muted);
            if (T) Text(FString::Printf(TEXT("Aegis restored %.0f HP   /   %d decision switches"), T->SupportHealAmount, T->SwitchCount), L, 274, Green);
            Text(TEXT("Reset to compare another policy from the same seed."), L, 326, Muted);
            Button(TEXT("Restart"), TEXT("R  RESET THE SAME SEED"), L, 382, 572, Cyan);
            Button(TEXT("Quit"), TEXT("QUIT TO DESKTOP"), L, 448, 572, Gold);
        }
    }

#if !UE_BUILD_SHIPPING
    if (!Debug) return;
    Box(890, 102, 366, 510, Panel); Box(890, 102, 3, 510, Cyan);
    Text(TEXT("COMPANION / DECISION INSPECTOR"), 906, 114, White);
    if (!T || T->DecisionSequence == 0)
    {
        Text(TEXT("No evaluated observation yet."), 906, 150, Muted);
        Text(TEXT("Start the attempt to inspect live decisions."), 906, 176, Muted, .85f);
        return;
    }
    const auto& O = T->Observation;
    const FString Mode = T->bTacticalTrial ? TEXT("TACTICAL / SCORES NOT CONTROLLING") :
        (!T->bUtilityPolicy ? TEXT("PRIORITY / SCORES ARE REFERENCE ONLY") : TEXT("UTILITY -> BT -> ACTION"));
    Text(Mode, 906, 141, T->bTacticalTrial || !T->bUtilityPolicy ? Gold : Cyan, .85f);
    Text(FString::Printf(TEXT("OBS #%d   age %s%s"), T->DecisionSequence, *DecisionAge(Now, T->GameSeconds), Paused ? TEXT(" / frozen") : TEXT("")), 906, 164, White, .9f);
    Text(FString::Printf(TEXT("self %.0f%%   ally sight %s"), O.health*100,
        O.allyKnown ? *FString::Printf(TEXT("%.0f%% / %.1fm"), O.allyHealth*100, O.allyDistance) : TEXT("unknown")), 906, 183, Muted, .85f);
    const FString Link = T->FollowLinkSampleGameSeconds >= 0
        ? FString::Printf(TEXT("position link %.1fm / age %s"), T->FollowLinkDistance, *DecisionAge(Now, T->FollowLinkSampleGameSeconds))
        : (T->FollowLinkAvailable ? TEXT("position link registered / inactive") : TEXT("position link not registered"));
    Text(Fit(Link, 334, .85f), 906, 202, T->FollowLinkSampleGameSeconds >= 0 ? Cyan : Muted, .85f);
    Text(FString::Printf(TEXT("sight %s   memory %s   support %s"), O.targetVisible ? TEXT("yes") : TEXT("no"),
        O.targetRemembered ? TEXT("yes") : TEXT("no"), O.supportReady ? TEXT("ready") : TEXT("cooldown")), 906, 221, Muted, .85f);
    Text(O.targetVisible ? FString::Printf(TEXT("visible target range %.1fm"), O.targetDistance) :
        TEXT("target range unknown / no current sight"), 906, 240, Muted, .85f);
    Text(TEXT("CANDIDATE UTILITY     * SELECTED"), 906, 259, White, .85f);
    double MaxScore = 1.0;
    for (const double Score : T->Scores) MaxScore = FMath::Max(MaxScore, Score);
    for (int32 I = 0; I < 4; ++I)
    {
        const auto Action = static_cast<aegis::Action>(I);
        const bool Selected = T->Selected == Action;
        const float Y = 281.f + I*21.f;
        Text((Selected ? TEXT("* ") : TEXT("  ")) + DecisionActionName(Action), 906, Y, Selected ? Cyan : Muted, .9f);
        Bar(997, Y+7, 155, T->Scores[I]/MaxScore, Selected ? Cyan : Muted);
        Text(FString::Printf(TEXT("%.3f"), T->Scores[I]), 1170, Y, Selected ? White : Muted, .9f);
    }
    Text(FString::Printf(TEXT("selected %s / raw best %s"), *DecisionActionName(T->Selected), *DecisionActionName(T->RawWinner)), 906, 371, White, .9f);
    FString Reason;
    if (T->SelectionReason == TEXT("priority")) Reason = O.targetVisible ? TEXT("Reason: visible threat -> attack") : TEXT("Reason: no visible threat -> follow");
    else if (T->SelectionReason == TEXT("hysteresis")) Reason = TEXT("Reason: held previous within 0.08 margin");
    else Reason = T->Previous == T->Selected ? TEXT("Reason: current action keeps highest score") :
        FString::Printf(TEXT("Reason: %s -> %s / highest score"), *DecisionActionName(T->Previous), *DecisionActionName(T->Selected));
    Text(Reason, 906, 392, Cyan, .85f);
    Text(FString::Printf(TEXT("switches %d / A-B-A within 1s: %d"), T->SwitchCount, T->ShortReversalCount), 906, 414, Muted, .85f);
    Text(FString::Printf(TEXT("BT #%d / observation #%d"), T->BTExecutionSequence, T->BTDecisionSequence), 906, 438, White, .9f);
    Text(Fit(FString::Printf(TEXT("last %s: %s"), *T->BTLeaf.ToString(), T->BTLeafSucceeded ? TEXT("accepted") : TEXT("failed")), 334, .9f), 906, 459, Muted, .9f);
    const FString AcceptedSource = T->AcceptedBTLeaf == TEXT("Follow")
        ? (T->FollowTargetSource == TEXT("team_position_link") ? TEXT(" / position link") :
            (T->FollowTargetSource == TEXT("sight") ? TEXT(" / sight") : TEXT(""))) : TEXT("");
    Text(Fit(FString::Printf(TEXT("accepted %s%s / %s ago"), *T->AcceptedBTLeaf.ToString(), *AcceptedSource,
        *DecisionAge(Now, T->AcceptedBTLeafAt)), 334, .8f), 906, 480, Muted, .8f);
    Text(Fit(FString::Printf(TEXT("EQS #%d %s"), T->QueryId, *T->QueryName), 334, .9f), 906, 504, White, .9f);
    const double QueryStamp = T->QueryCompletedAt >= 0 ? T->QueryCompletedAt : T->QuerySubmittedAt;
    Text(Fit(FString::Printf(TEXT("%s / %s age %s"), *T->QueryStatus,
        T->QueryCompletedAt >= 0 ? TEXT("result") : TEXT("request"), *DecisionAge(Now, QueryStamp)), 334, .85f), 906, 525, Cyan, .85f);
    Text(Fit(FString::Printf(TEXT("from %s / obs #%d"), *T->QuerySubmittedBTLeaf.ToString(), T->QueryDecisionSequence), 334, .85f), 906, 546, Muted, .85f);
    Text(T->QueryGeneratedItems >= 0 ? FString::Printf(TEXT("candidates %d valid / %d generated"), T->QueryValidItems, T->QueryGeneratedItems) :
        TEXT("candidate data not retained"), 906, 567, Muted, .85f);
    Text(T->bQueryMoveAccepted ? Fit(FString::Printf(TEXT("move accepted (%.0f, %.0f) / not arrival"), T->QueryAcceptedPoint.X, T->QueryAcceptedPoint.Y), 334, .8f) :
        TEXT("no accepted query destination"), 906, 588, Muted, .8f);

    // This marker belongs to the displayed controller/query snapshot, not a
    // fresh target lookup or a speculative position computed by the HUD.
    if (T->bQueryMoveAccepted && T->QueryStatus == TEXT("accepted") && !Briefing && !Finished)
    {
        const FVector Point = Project(T->QueryAcceptedPoint + FVector(0, 0, 15));
        if (Point.Z > 0 && Point.X > OX+24*S && Point.X < OX+870*S && Point.Y > OY+100*S && Point.Y < OY+612*S)
        {
            const float R = 8*S;
            DrawLine(Point.X-R, Point.Y, Point.X, Point.Y-R, Gold, 2*S);
            DrawLine(Point.X, Point.Y-R, Point.X+R, Point.Y, Gold, 2*S);
            DrawLine(Point.X+R, Point.Y, Point.X, Point.Y+R, Gold, 2*S);
            DrawLine(Point.X, Point.Y+R, Point.X-R, Point.Y, Gold, 2*S);
            DrawText(FString::Printf(TEXT("Q%d accepted move"), T->QueryId), Gold, Point.X+12*S, Point.Y-8*S, Font, FontScale*.85f*S);
        }
    }
#endif
}
