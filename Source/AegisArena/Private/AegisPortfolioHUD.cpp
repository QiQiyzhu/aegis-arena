#include "AegisLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioMusic.h"
#include "CollisionQueryParams.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
FString AegisUI(const bool English, const TCHAR* Chinese, const TCHAR* EnglishText)
{
    return FString(English ? EnglishText : Chinese);
}

FString PortfolioClock(double Seconds)
{
    const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));
    return FString::Printf(TEXT("%02d:%02d"), Whole / 60, Whole % 60);
}

FString PortfolioRole(const AAegisAICharacter* Bot, bool English)
{
    if (Bot->bElite) return English ? TEXT("ELITE") : TEXT("精英");
    if (Bot->EnemyRole == EAegisEnemyRole::Flanker) return English ? TEXT("FLANKER") : TEXT("游击");
    if (Bot->EnemyRole == EAegisEnemyRole::Suppressor) return English ? TEXT("SUPPRESSOR") : TEXT("压制");
    return English ? TEXT("STRIKER") : TEXT("突击");
}

FString PortfolioCommand(const AAegisAIController* AI, bool English)
{
    if (!AI) return English ? TEXT("LINK OFFLINE") : TEXT("链路离线");
    if (AI->CompanionCommand == EAegisCompanionCommand::Focus) return English ? TEXT("FOCUS") : TEXT("集火");
    if (AI->CompanionCommand == EAegisCompanionCommand::Rally) return English ? TEXT("RALLY") : TEXT("集结");
    return English ? TEXT("GUARD") : TEXT("防守");
}

FString PortfolioCompanionState(const AAegisAIController* AI, bool English)
{
    if (!AI) return AegisUI(English, TEXT("正在连接行动员"), TEXT("Linking to operator"));
    const FString& State = AI->DebugState;
    if (State.IsEmpty()) return AegisUI(English, TEXT("链路已连接 · 等待指令"), TEXT("Link ready / awaiting command"));
    if (English) return State;
    struct FStateLabel { const TCHAR* Key; const TCHAR* Value; };
    static const FStateLabel Labels[] = {
        {TEXT("Guard: take rear flank"), TEXT("防守：占据后侧" )},
        {TEXT("Guard: clear your firing lane"), TEXT("防守：让开你的射击线")},
        {TEXT("Guard: follow on your flank"), TEXT("防守：跟随你的侧翼")},
        {TEXT("Guard: moving for a clear shot"), TEXT("防守：移动寻找清晰射击角度")},
        {TEXT("Focus: designated threat"), TEXT("集火：指定威胁")},
        {TEXT("Focus: search your marked position"), TEXT("集火：搜索标记位置")},
        {TEXT("Rally: move to signal"), TEXT("集结：前往信号点")},
        {TEXT("Rally: moving to your signal"), TEXT("集结：前往你的信号点")},
        {TEXT("Rally: holding your signal"), TEXT("集结：坚守信号点")},
        {TEXT("Covering: aiming at visible threat"), TEXT("掩护：瞄准可见威胁")},
        {TEXT("Search: last observed position"), TEXT("搜索：前往最后观测位置")},
        {TEXT("Advance: relay objective"), TEXT("推进：前往中继目标")},
        {TEXT("Fire: relocating after shot"), TEXT("开火：射击后重新转位")},
        {TEXT("Reposition: shot obstructed"), TEXT("转位：射击线被阻挡")},
        {TEXT("Staggered: regaining balance"), TEXT("受击：恢复平衡")},
        {TEXT("Stopped / session ended"), TEXT("停止 / 行动结束")},
        {TEXT("Dead / stopped"), TEXT("失效 / 已停止")},
        {TEXT("Patrol"), TEXT("巡逻")},
        {TEXT("Chase"), TEXT("接近目标")},
        {TEXT("Investigate"), TEXT("调查观测位置")},
        {TEXT("Attack"), TEXT("攻击目标")},
        {TEXT("AttackPosition"), TEXT("寻找射击位置")},
        {TEXT("FindCover"), TEXT("寻找掩体")},
        {TEXT("Retreat"), TEXT("撤离交火区")},
        {TEXT("Recover"), TEXT("恢复状态")},
        {TEXT("Follow"), TEXT("跟随行动员")},
        {TEXT("Support"), TEXT("支援队友")},
        {TEXT("Missing BT asset"), TEXT("行为系统暂不可用")},
    };
    for (const FStateLabel& Label : Labels)
        if (State.Equals(Label.Key, ESearchCase::IgnoreCase)) return Label.Value;
    // New internal states retain their raw names in telemetry. The player card
    // always shows a localized, known command rather than leaking debug text.
    return FString::Printf(TEXT("当前指令：%s"), *PortfolioCommand(AI, false));
}

FString PortfolioFeedback(const FString& Feedback, bool English)
{
    if (English || Feedback.IsEmpty()) return Feedback;
    FString Result = Feedback;
    struct FFeedbackLabel { const TCHAR* From; const TCHAR* To; };
    static const FFeedbackLabel Labels[] = {
        {TEXT("PRISM FALL | RMB charge 12 / Q pulse 35 / F overclock 35"), TEXT("棱镜坠落 | RMB 蓄力 12 / Q 脉冲 35 / F 超频 35")},
        {TEXT("60 energy: Q pulse 35 | E repair 40"), TEXT("60 能量：Q 脉冲 35 | E 修复 40")},
        {TEXT("Survey: hold G for 2.5s | damage cancels"), TEXT("档案扫描：按住 G 2.5 秒 | 受伤会中断")},
        {TEXT("Survey: stand within 1.8m of an unclaimed cache"), TEXT("档案扫描：靠近未领取的档案至 1.8 米内")},
        {TEXT("Survey: release G and try again"), TEXT("档案扫描：松开 G 后重试")},
        {TEXT("Survey: scan interrupted by damage"), TEXT("档案扫描：受伤中断，请重新按住 G")},
        {TEXT("Survey: return to the cache to scan"), TEXT("档案扫描：回到档案旁继续扫描")},
        {TEXT("Survey: finish or cancel the current scan first"), TEXT("档案扫描：先完成或取消当前扫描")},
        {TEXT("Survey supply: no healing needed"), TEXT("档案补给：暂无需要恢复的生命值")},
        {TEXT("Survey key: storage already full"), TEXT("中继密钥：已有一枚待使用的密钥")},
        {TEXT("Survey key: no data relays remain"), TEXT("中继密钥：已无可强化的数据中继")},
        {TEXT("Survey reward: SUPPLY | heal 20 / 15"), TEXT("扫描奖励：补给 | 为自己 / 队友恢复 20 / 15")},
        {TEXT("Survey reward: RELAY KEY | upload x1.25"), TEXT("扫描奖励：中继密钥 | 数据上传 x1.25")},
        {TEXT("Survey key stored | next data relay x1.25"), TEXT("密钥已储存 | 下次数据中继推进 x1.25")},
        {TEXT("Survey key linked | this data relay x1.25"), TEXT("密钥已连接 | 当前数据中继推进 x1.25")},
        {TEXT("Survey supply |"), TEXT("档案补给 |")},
        {TEXT("Prism shot"), TEXT("棱镜射击")},
        {TEXT("Charged shot needs"), TEXT("蓄力射击需要")},
        {TEXT("Pulse needs"), TEXT("脉冲需要")},
        {TEXT("Pulse -35 energy | disrupt and push enemies"), TEXT("脉冲 -35 能量 | 打断并击退敌人")},
        {TEXT("Repair is cooling down"), TEXT("修复冷却中")},
        {TEXT("No repair needed: ally must be within 5m and clear of cover"), TEXT("无需修复：队友需在 5 米内且无遮挡")},
        {TEXT("Repair needs"), TEXT("修复需要")},
        {TEXT("Repair -"), TEXT("修复 -")},
        {TEXT("YOU +"), TEXT("你 +")},
        {TEXT("ALLY +"), TEXT("队友 +")},
        {TEXT("Overclock: stand inside an active data relay"), TEXT("超频：请站在当前数据中继内")},
        {TEXT("This relay has already been overclocked"), TEXT("本中继已经超频")},
        {TEXT("Overclock needs"), TEXT("超频需要")},
        {TEXT("OVERCLOCK x2"), TEXT("超频 x2")},
        {TEXT("contest stops progress, timer keeps running"), TEXT("争夺会暂停进度，计时仍会继续")},
        {TEXT("Transfer route: NORTH > EAST"), TEXT("转移路线：北侧 > 东侧")},
        {TEXT("Transfer route: EAST > NORTH"), TEXT("转移路线：东侧 > 北侧")},
        {TEXT("Enemy defeated"), TEXT("敌人已击败")},
        {TEXT("Sector clear"), TEXT("区域清除")},
        {TEXT("choose an upgrade"), TEXT("选择一项升级")},
        {TEXT("cover and allies still block"), TEXT("掩体与队友仍会阻挡")},
        {TEXT("energy"), TEXT("能量")},
        {TEXT(" HP"), TEXT(" 点生命")},
        {TEXT("6s"), TEXT("6 秒")},
    };
    for (const FFeedbackLabel& Label : Labels) Result = Result.Replace(Label.From, Label.To, ESearchCase::IgnoreCase);
    return Result;
}

FString PortfolioGuardReason(const FString& Reason, bool English)
{
    struct FReasonLabel { const TCHAR* Key; const TCHAR* Chinese; const TCHAR* EnglishText; };
    static const FReasonLabel Labels[] = {
        {TEXT("awaiting_guard_observation"), TEXT("等待防守位置观测"), TEXT("Awaiting guard observation")},
        {TEXT("legacy_formation"), TEXT("基础跟随队形"), TEXT("Basic follow formation")},
        {TEXT("alternate_visible_firing_slot"), TEXT("已选择备选可见射击位"), TEXT("Alternate visible firing slot selected")},
        {TEXT("preferred_visible_firing_slot"), TEXT("已选择首选可见射击位"), TEXT("Preferred visible firing slot selected")},
        {TEXT("team_formation_without_visible_target"), TEXT("未见目标，保持队伍阵形"), TEXT("Team formation / no visible target")},
        {TEXT("no_reachable_visible_firing_slot"), TEXT("暂无可到达的可见射击位"), TEXT("No reachable visible firing slot")},
    };
    for (const FReasonLabel& Label : Labels)
        if (Reason == Label.Key) return AegisUI(English, Label.Chinese, Label.EnglishText);
    return AegisUI(English, TEXT("等待位置评估"), TEXT("Awaiting position evaluation"));
}

FString PortfolioTransaction(const FString& Transaction, bool English)
{
    if (English) return Transaction;
    FString Result = PortfolioFeedback(Transaction, false);
    // Transaction identifiers remain unchanged in the recorded evidence.
    Result = Result.Replace(TEXT("charged_shot:"), TEXT("蓄力射击："));
    Result = Result.Replace(TEXT("overclock:"), TEXT("中继超频："));
    Result = Result.Replace(TEXT("stage_1_complete:"), TEXT("固守完成："));
    Result = Result.Replace(TEXT("stage_2_complete:"), TEXT("转移完成："));
    Result = Result.Replace(TEXT("stage_3_complete:"), TEXT("撤离完成："));
    Result = Result.Replace(TEXT("enemy_defeated:"), TEXT("击败敌人："));
    Result = Result.Replace(TEXT("pulse:"), TEXT("脉冲："));
    Result = Result.Replace(TEXT("repair:"), TEXT("修复："));
    return Result;
}
}

void AAegisDebugHUD::DrawPortfolio(AAegisScenarioRunner* Runner)
{
    if (!Canvas || !Runner || !GetWorld()) return;
    auto* PC = Cast<AAegisPlayerController>(GetOwningPlayerController());
    const bool English = PC && PC->bEnglishUI;
    auto* Player = PC ? Cast<AAegisPlayerCharacter>(PC->GetPawn()) : nullptr;
    auto* Companion = Runner->GetCompanion();
    auto* AI = Companion ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
    auto* Portfolio = AAegisPortfolio::Find(GetWorld());
    const bool V2 = Portfolio && Portfolio->IsV2();
    const bool SurveyEnabled = Portfolio && Portfolio->IsV23();
    bool MusicMuted = false;
    if (V2) for (TActorIterator<AAegisPortfolioMusic> It(GetWorld()); It; ++It) { MusicMuted = It->IsMuted(); break; }
    const auto& Trial = Runner->GetTrial();
    const auto& Operation = Runner->GetOperation();
    const bool Briefing = Trial.phase == aegis::TrialPhase::Briefing;
    const bool Intermission = Trial.phase == aegis::TrialPhase::Intermission;
    const bool Finished = Trial.finished();
    const bool Upgrade = Runner->IsUpgradePending();
    const bool Menu = PC && PC->bMenuOpen;
    const bool Modal = Briefing || Finished || Upgrade || Menu;
    bool Debug = false;
#if !UE_BUILD_SHIPPING
    Debug = PC && PC->bShowDiagnostics;
#endif
    const double Now = GetWorld()->GetTimeSeconds();
    const int32 Enemies = Runner->LivingEnemies();
    const int32 Energy = Portfolio ? Portfolio->GetEnergy() : 0;
    const int32 SurveyMask = SurveyEnabled ? Portfolio->GetSurveyClaimedMask() : 0;
    const int32 SurveysClaimed = (SurveyMask & 1 ? 1 : 0) + (SurveyMask & 2 ? 1 : 0);
    int32 NearestSurvey = -1;
    float SurveyDistance = TNumericLimits<float>::Max();
    if (SurveyEnabled && Player)
        for (int32 Node = 0; Node < 2; ++Node)
        {
            if (SurveyMask & (1 << Node)) continue;
            const float Distance = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Portfolio->GetSurveyLocation(Node)));
            if (Distance < SurveyDistance) { SurveyDistance = Distance; NearestSurvey = Node; }
        }
    const bool PlayerAlive = Player && Player->Health && Player->Health->IsAlive();
    const bool CompanionAlive = Companion && Companion->Health && Companion->Health->IsAlive();
    const bool PlayerIn = Runner->IsPlayerInObjective();
    const bool CompanionIn = Runner->IsCompanionInObjective();
    const bool Contested = Runner->IsObjectiveContested();
    const float S = FMath::Min(Canvas->SizeX / 1280.f, Canvas->SizeY / 720.f);
    const float OX = (Canvas->SizeX - 1280.f * S) * .5f;
    const float OY = (Canvas->SizeY - 720.f * S) * .5f;
    const FLinearColor Navy(.009f, .022f, .043f, .96f), Steel(.022f, .052f, .086f, .98f);
    const FLinearColor Paper(.025f, .055f, .086f, .98f), Ink(.90f, .96f, 1.f);
    const FLinearColor White(.90f, .96f, 1.f), Muted(.47f, .64f, .73f), Slate(.38f, .54f, .64f);
    const FLinearColor Mint(.22f, 1.f, .74f), Teal(.14f, .82f, 1.f), Amber(1.f, .64f, .20f);
    const FLinearColor Red(1.f, .30f, .20f), Hairline(.14f, .27f, .37f, .90f);
    const FLinearColor Iris(.69f, .51f, 1.f);
    UFont* Font = InterfaceFont ? InterfaceFont.Get() : GEngine->GetMediumFont();
    const float FontUnit = 1.f / FMath::Max(1.f, Font->GetMaxCharHeight());
    float MouseX = -1, MouseY = -1;
    if (PC) PC->GetMousePosition(MouseX, MouseY);
    const FVector2D Mouse((MouseX - OX) / S, (MouseY - OY) / S);

    auto Box = [&](float X, float Y, float W, float H, FLinearColor Color) {
        DrawRect(Color, OX + X*S, OY + Y*S, W*S, H*S);
    };
    auto Line = [&](float X1, float Y1, float X2, float Y2, FLinearColor Color, float Width = 1.f) {
        DrawLine(OX + X1*S, OY + Y1*S, OX + X2*S, OY + Y2*S, Color, Width*S);
    };
    auto Text = [&](const FString& Value, float X, float Y, float Size, FLinearColor Color) {
        Size = FMath::Max(Size, 16.f); // Keep essential labels readable at the supported 720p layout.
        DrawText(Value, Color, OX + X*S, OY + Y*S, Font, Size*FontUnit*S, false);
    };
    auto Measure = [&](const FString& Value, float Size) {
        Size = FMath::Max(Size, 16.f);
        float W = 0, H = 0;
        GetTextSize(Value, W, H, Font, Size*FontUnit);
        return W;
    };
    auto Fit = [&](FString Value, float Width, float Size) {
        if (Measure(Value, Size) <= Width) return Value;
        while (!Value.IsEmpty() && Measure(Value + TEXT("..."), Size) > Width) Value.LeftChopInline(1);
        return Value + TEXT("...");
    };
    auto Wrap = [&](const FString& Value, float X, float Y, float Width, float Size,
                    FLinearColor Color, int32 MaximumLines = 5) {
        struct FWrapToken { FString Value; bool SpaceBefore; };
        TArray<FWrapToken> Tokens;
        FString Word;
        bool SpaceBefore = false;
        auto FlushWord = [&]()
        {
            if (!Word.IsEmpty())
            {
                Tokens.Add({MoveTemp(Word), SpaceBefore});
                Word.Reset();
                SpaceBefore = false;
            }
        };
        // Keep Latin words intact, while allowing Chinese clauses containing
        // key names and numbers to wrap naturally as well as plain Chinese.
        for (const TCHAR Character : Value)
        {
            if (FChar::IsWhitespace(Character))
            {
                FlushWord();
                SpaceBefore = true;
            }
            else if (Character >= 0x2e80)
            {
                FlushWord();
                FString Glyph;
                Glyph.AppendChar(Character);
                const bool ClosingPunctuation = FString(TEXT("，。；：！？、）》」』】％")).Contains(Glyph);
                if (ClosingPunctuation && !Tokens.IsEmpty()) Tokens.Last().Value += Glyph;
                else Tokens.Add({MoveTemp(Glyph), SpaceBefore});
                SpaceBefore = false;
            }
            else Word.AppendChar(Character);
        }
        FlushWord();
        FString Row;
        int32 Index = 0;
        for (const FWrapToken& Token : Tokens)
        {
            const FString Candidate = Row + (!Row.IsEmpty() && Token.SpaceBefore ? TEXT(" ") : TEXT("")) + Token.Value;
            if (!Row.IsEmpty() && Measure(Candidate, Size) > Width)
            {
                if (Index + 1 >= MaximumLines)
                {
                    Text(Fit(Row + TEXT("..."), Width, Size), X, Y + Index*(Size + 4), Size, Color);
                    return;
                }
                Text(Row, X, Y + Index*(Size + 4), Size, Color);
                Row = Token.Value;
                ++Index;
            }
            else Row = Candidate;
        }
        if (!Row.IsEmpty()) Text(Fit(Row, Width, Size), X, Y + Index*(Size + 4), Size, Color);
    };
    auto Bar = [&](float X, float Y, float W, float H, double Fraction, FLinearColor Fill,
                   FLinearColor Background) {
        Box(X, Y, W, H, Background);
        Box(X, Y, W*FMath::Clamp(static_cast<float>(Fraction), 0.f, 1.f), H, Fill);
    };
    auto Cut = [&](float X, float Y, float W, float H, FLinearColor Color) {
        const float C = FMath::Min(12.f, H*.22f);
        const FVector2D P[] = {{X+C,Y},{X+W,Y},{X+W,Y+H-C},{X+W-C,Y+H},{X,Y+H},{X,Y+C}};
        for (int32 I=1; I<5; ++I)
        {
            FCanvasTriangleItem Item(FVector2D(OX+P[0].X*S,OY+P[0].Y*S), FVector2D(OX+P[I].X*S,OY+P[I].Y*S),
                FVector2D(OX+P[I+1].X*S,OY+P[I+1].Y*S), GWhiteTexture);
            Item.SetColor(Color); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item);
        }
    };
    auto Panel = [&](float X, float Y, float W, float H, bool Light, FLinearColor Accent) {
        Cut(X+3,Y+4,W,H,FLinearColor(0,0,0,.30f));
        Cut(X,Y,W,H,Light ? Paper : Navy);
        Line(X+12,Y,X+W,Y,Hairline);
        Line(X,Y+12,X+12,Y,Accent,1.5f);
        Line(X+W-12,Y+H,X+W,Y+H-12,Accent,1.5f);
        Box(X+18,Y,42,2,Accent);
        Line(X,Y+H,X+W-12,Y+H,Hairline);
    };
    auto Hovered = [&](float X, float Y, float W, float H) {
        return Mouse.X >= X && Mouse.X <= X+W && Mouse.Y >= Y && Mouse.Y <= Y+H;
    };
    auto Button = [&](FName Name, const FString& Label, float X, float Y, float W, float H,
                      bool Primary, bool Light = true) {
        const bool Hover = Hovered(X, Y, W, H);
        const FLinearColor Back = Primary ? (Hover ? FLinearColor(.55f,.96f,1.f) : Teal) : (Hover ? Steel : Navy);
        Cut(X,Y,W,H,Back);
        Line(X+10,Y,X+W,Y,Primary ? Teal : Hairline);
        if (Hover) Box(X+10,Y+H-2,W-20,2,Primary ? White : Teal);
        float LabelSize = 18.f;
        while (LabelSize > 14.f && Measure(Label, LabelSize) > W - 28.f) LabelSize -= 1.f;
        Text(Fit(Label, W - 28.f, LabelSize), X + 14, Y + (H - LabelSize)*.5f - 1, LabelSize,
             Primary ? Navy : White);
        AddHitBox(FVector2D(OX + X*S, OY + Y*S), FVector2D(W*S, H*S), Name, true);
    };
    auto Ring = [&](float X, float Y, float Radius, double Fraction, FLinearColor Color, float Width = 2.f) {
        const int32 Segments = 40;
        const double End = FMath::Clamp(Fraction, 0.0, 1.0)*Segments;
        for (int32 I = 0; I < FMath::CeilToInt(End); ++I)
        {
            const float A = -HALF_PI + 2.f*PI*I/Segments;
            const float B = -HALF_PI + 2.f*PI*FMath::Min(static_cast<double>(I + 1), End)/Segments;
            Line(X + FMath::Cos(A)*Radius, Y + FMath::Sin(A)*Radius,
                 X + FMath::Cos(B)*Radius, Y + FMath::Sin(B)*Radius, Color, Width);
        }
    };
    auto Screen = [&](const FVector& Position) {
        const FVector P = Project(Position);
        return FVector((P.X - OX) / S, (P.Y - OY) / S, P.Z);
    };
    auto Health = [&](const AAegisCharacter* Character, float X, float Y, float W, FLinearColor Color) {
        if (!Character || !Character->Health) return;
        Bar(X, Y, W, 5, Character->Health->Current / FMath::Max(1.f, Character->Health->Maximum), Color, Hairline);
    };

    // Public objective and visible combatants only. HUD queries never drive AI or attacks.
    if (!Modal && Trial.phase == aegis::TrialPhase::Active)
    {
        const FVector Objective = Screen(Runner->GetObjectiveLocation() + FVector(0, 0, 70));
        const float Right = Debug ? 875.f : 1220.f;
        const float X = FMath::Clamp(static_cast<float>(Objective.X), 62.f, Right);
        const float Y = FMath::Clamp(static_cast<float>(Objective.Y), 163.f, 548.f);
        const bool Offscreen = Objective.Z <= 0 || FMath::Abs(Objective.X-X) > 1 || FMath::Abs(Objective.Y-Y) > 1;
        const FLinearColor ObjectiveColor = Contested ? Amber : Mint;
        const double Progress = Operation.complete ? 1.0 : Operation.charge / Operation.requiredSeconds();
        if (!Operation.complete)
        {
            const float Radius = 17.f + .6f*FMath::Sin(static_cast<float>(Now)*2.f);
            Ring(X, Y, Radius, 1, Hairline, 1);
            Ring(X, Y, Radius, Progress, ObjectiveColor, 3);
            Line(X - 5, Y, X + 5, Y, ObjectiveColor, 1.5f);
            Line(X, Y - 5, X, Y + 5, ObjectiveColor, 1.5f);
            if (Offscreen)
            {
                FVector2D Direction(Objective.X - X, Objective.Y - Y);
                Direction.Normalize();
                const FVector2D Tip = FVector2D(X, Y) + Direction*27.f;
                const FVector2D Base = Tip - Direction*8.f;
                const FVector2D Side(-Direction.Y*5.f, Direction.X*5.f);
                Line(Tip.X, Tip.Y, Base.X + Side.X, Base.Y + Side.Y, ObjectiveColor, 2);
                Line(Tip.X, Tip.Y, Base.X - Side.X, Base.Y - Side.Y, ObjectiveColor, 2);
            }
            const double Distance = Player ? FVector::Dist2D(Player->GetActorLocation(), Runner->GetObjectiveLocation()) / 100.0 : 0;
            const FString Label = FString::Printf(TEXT("%s  %.0fm"), Trial.wave == 3 ? *AegisUI(English, TEXT("撤离"), TEXT("EXTRACT")) : *AegisUI(English, TEXT("中继"), TEXT("RELAY")), Distance);
            const float Width = Measure(Label, 15) + 18;
            Box(X - Width*.5f, Y + 25, Width, 24, Navy);
            Text(Label, X - Width*.5f + 9, Y + 28, 15, ObjectiveColor);
        }

        // The nearby archive is a public map feature. Its marker never reveals
        // enemies and disappears after claiming it, keeping the view quiet.
        if (NearestSurvey >= 0 && SurveyDistance < 700.f)
        {
            const FVector P = Screen(Portfolio->GetSurveyLocation(NearestSurvey) + FVector(0, 0, 75));
            if (P.Z > 0 && P.X > 330 && P.X < Right - 24 && P.Y > 164 && P.Y < 530)
            {
                Ring(P.X, P.Y, 13, 1, Iris, 1.5f);
                const bool InRange = SurveyDistance <= 180.f;
                const FString Label = English ? FString::Printf(TEXT("G  CACHE %d / %.1fm"), NearestSurvey + 1, SurveyDistance / 100.f)
                                              : FString::Printf(TEXT("G  档案 %d / %.1f 米"), NearestSurvey + 1, SurveyDistance / 100.f);
                const float Width = Measure(Label, 12) + 18;
                Box(P.X - Width*.5f, P.Y + 20, Width, 23, Navy);
                Text(Label, P.X - Width*.5f + 9, P.Y + 24, 12, InRange ? White : Iris);
            }
        }

        // The operator always knows their own position. A single camera-to-self
        // visibility trace adds a locator only while geometry hides the body;
        // it neither senses hostiles nor changes collision or target selection.
        if (PlayerAlive && PC)
        {
            const FVector SelfLocation = Player->GetActorLocation() + FVector(0, 0, 25);
            const FVector Self = Screen(SelfLocation);
            if (Self.Z > 0 && Self.X > 48 && Self.X < Right && Self.Y > 205 && Self.Y < 568)
            {
                FVector CameraLocation;
                FRotator CameraRotation;
                PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
                FHitResult Occluder;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(AegisPortfolioSelfVisibility), false, Player);
                const bool Occluded = GetWorld()->LineTraceSingleByChannel(
                    Occluder, CameraLocation, SelfLocation, ECC_Visibility, Query);
                if (Occluded)
                {
                    const float SX = static_cast<float>(Self.X), SY = static_cast<float>(Self.Y);
                    Ring(SX, SY, 15, 1, Navy, 4);
                    Ring(SX, SY, 15, 1, Mint, 1.5f);
                    Box(SX-2, SY-2, 4, 4, White);
                    Line(SX, SY-17, SX, SY-29, Mint, 1.5f);
                    Box(SX-23, SY-53, 46, 23, Navy);
                    Box(SX-23, SY-32, 46, 2, Mint);
                    const FString You = AegisUI(English, TEXT("你"), TEXT("YOU"));
                    Text(You, SX-Measure(You, 15)*.5f, SY-50, 15, White);
                }
            }
        }

        for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        {
            auto* Bot = *It;
            if (!IsValid(Bot) || !Bot->Health || !Bot->Health->IsAlive()) continue;
            const bool Friendly = Bot == Companion;
            if (!Friendly && (Bot->Team != EAegisTeam::Enemy || !PC || !PC->LineOfSightTo(Bot))) continue;
            const FVector P = Screen(Bot->GetActorLocation() + FVector(0, 0, 115));
            if (P.Z <= 0 || P.X < 48 || P.X > Right || P.Y < 158 || P.Y > 556) continue;
            const bool Aimed = !Friendly && Player && FVector::DistSquared2D(Player->GetAimPoint(), Bot->GetActorLocation()) < FMath::Square(105.f);
            const bool Warning = !Friendly && Bot->GetShotWindupRemaining() > 0;
            const FLinearColor Color = Friendly ? Mint : (Warning ? Amber : Red);
            if (Warning) { Line(P.X-7,P.Y-27,P.X,P.Y-34,Amber,2); Line(P.X,P.Y-34,P.X+7,P.Y-27,Amber,2); }
            Box(P.X - 27, P.Y - 2, 54, 8, Navy);
            Health(Bot, P.X - 25, P.Y, 50, Color);
            if (Friendly) Text(AegisUI(English, TEXT("艾吉斯"), TEXT("AEGIS")), P.X - 22, P.Y - 18, 13, Mint);
            else if (Aimed || Warning)
            {
                const FString Label = Warning ? AegisUI(English, TEXT("即将开火"), TEXT("INCOMING")) : PortfolioRole(Bot, English);
                Text(Label, P.X - Measure(Label, 13)*.5f, P.Y - 18, 13, Color);
            }
            if (Aimed)
            {
                const FVector Center = Screen(Bot->GetActorLocation());
                for (int32 Side : {-1, 1})
                {
                    Line(Center.X + Side*23, Center.Y - 13, Center.X + Side*23, Center.Y + 13, White, 1.5f);
                    Line(Center.X + Side*23, Center.Y - 13, Center.X + Side*15, Center.Y - 13, White, 1.5f);
                    Line(Center.X + Side*23, Center.Y + 13, Center.X + Side*15, Center.Y + 13, White, 1.5f);
                }
            }
        }
        if (Mouse.X > 18 && Mouse.X < Right && Mouse.Y > 156 && Mouse.Y < 578)
        {
            const bool Hit = Player && Player->Combat && Player->Combat->SecondsSinceLastHit() < .13f;
            const FLinearColor Color = Hit ? Amber : White;
            Ring(Mouse.X, Mouse.Y, Hit ? 10 : 6, 1, Color, 1);
            if (Hit)
            {
                Line(Mouse.X-13, Mouse.Y-13, Mouse.X-8, Mouse.Y-8, Color, 2);
                Line(Mouse.X+13, Mouse.Y-13, Mouse.X+8, Mouse.Y-8, Color, 2);
                Line(Mouse.X-13, Mouse.Y+13, Mouse.X-8, Mouse.Y+8, Color, 2);
                Line(Mouse.X+13, Mouse.Y+13, Mouse.X+8, Mouse.Y+8, Color, 2);
            }
        }
    }

    // v2.5: a single tactical instrument hierarchy. Shape reinforces every color.
    auto Glyph = [&](int32 Kind, float X, float Y, float R, FLinearColor C) {
        if (Kind == 0) { // operator / charged round: solid-ended diamond
            Line(X,Y-R,X+R,Y,C,2); Line(X+R,Y,X,Y+R,C,2);
            Line(X,Y+R,X-R,Y,C,2); Line(X-R,Y,X,Y-R,C,2); Box(X-2,Y-2,4,4,C);
        } else if (Kind == 1) { // ally / repair: open shield and cross
            Line(X-R,Y-R,X-R,Y+R*.4f,C,2); Line(X+R,Y-R,X+R,Y+R*.4f,C,2);
            Line(X-R,Y+R*.4f,X,Y+R,C,2); Line(X,Y+R,X+R,Y+R*.4f,C,2);
            Line(X-4,Y,X+4,Y,C,2); Line(X,Y-4,X,Y+4,C,2);
        } else if (Kind == 2) { // threat / pulse: directional chevron
            Line(X-R,Y+R*.7f,X,Y-R,C,2); Line(X,Y-R,X+R,Y+R*.7f,C,2);
            Line(X-R,Y+R*.7f,X+R,Y+R*.7f,C,2); Line(X,Y-3,X,Y+2,C,2); Box(X-1,Y+5,2,2,C);
        } else { // uplink / archive: segmented hexagon
            for(int32 I=0; I<6; ++I) {
                float A=PI/3*I, B=A+PI/3*.80f;
                Line(X+FMath::Cos(A)*R,Y+FMath::Sin(A)*R,X+FMath::Cos(B)*R,Y+FMath::Sin(B)*R,C,2);
            }
            Line(X,Y+4,X,Y-4,C,2); Line(X-3,Y-1,X,Y-4,C,1); Line(X+3,Y-1,X,Y-4,C,1);
        }
    };
    auto StageStrip = [&](float X, float Y, float W) {
        const FString Titles[]={AegisUI(English,TEXT("固守"),TEXT("SECURE")),AegisUI(English,TEXT("转移"),TEXT("TRANSFER")),AegisUI(English,TEXT("撤离"),TEXT("EXTRACT"))};
        for(int32 I=0; I<3; ++I) {
            bool Done=Trial.wave>I+1 || (Trial.wave==I+1 && (Intermission || Trial.phase==aegis::TrialPhase::Won));
            bool Current=Trial.wave==I+1 && !Finished;
            float PX=X+I*W/3;
            Box(PX,Y,W/3-12,3,Done ? Mint : Current ? Teal : Hairline);
            Text(FString::Printf(TEXT("0%d  %s"),I+1,*Titles[I]),PX,Y+9,11,Done ? Mint : Current ? White : Muted);
        }
    };
    auto TacticalMap = [&](float X, float Y, float W, float H, bool Large) {
        Box(X,Y,W,H,FLinearColor(.008f,.024f,.045f,.88f));
        for(int32 I=1;I<6;++I) { Line(X+I*W/6,Y,X+I*W/6,Y+H,Hairline*.5f); Line(X,Y+I*H/6,X+W,Y+I*H/6,Hairline*.5f); }
        auto MP=[&](FVector P) { FVector D=P-Runner->GetActorLocation(); return FVector2D(X+W*.5f+D.X*W/4200,Y+H*.5f-D.Y*H/3400); };
        FVector2D N[]={MP(Runner->GetActorLocation()+FVector(-500,-1000,0)), MP(Runner->GetActorLocation()+FVector(850,-850,0)),
            MP(Runner->GetActorLocation()+FVector(0,1000,0)),MP(Runner->GetActorLocation()+FVector(-1250,0,0))};
        int32 First=Runner->IsNorthRouteFirst()?2:1, Second=3-First;
        const int32 Order[]={0,First,Second,3};
        for(int32 I=0;I<3;++I) {
            auto A=N[Order[I]], B=N[Order[I+1]];
            for(int32 J=0;J<12;++J) { auto U=FMath::Lerp(A,B,J/12.f), V=FMath::Lerp(A,B,(J+.55f)/12.f); Line(U.X,U.Y,V.X,V.Y,Teal*.6f,1); }
        }
        // Public stage positions; this schematic deliberately contains no enemy radar.
        for(int32 I=0;I<4;++I) {
            Glyph(I==3?0:3,N[I].X,N[I].Y,Large?12.f:5.f,I==3?Amber:Teal);
            if(Large) Text(I==3?AegisUI(English,TEXT("撤离"),TEXT("EXTRACT")):FString::Printf(TEXT("R%d"),I+1),N[I].X+17,N[I].Y-7,12,White);
        }
        if(SurveyEnabled) for(int32 I=0;I<2;++I) {
            auto P=MP(Portfolio->GetSurveyLocation(I)); Glyph(3,P.X,P.Y,Large?8.f:4.f,SurveyMask&(1<<I)?Slate:Iris);
            if(Large) Text(FString::Printf(TEXT("S%d"),I+1),P.X+12,P.Y-7,11,Iris);
        }
        if(!Large && PlayerAlive) { auto P=MP(Player->GetActorLocation()); Glyph(0,P.X,P.Y,4,White); }
        if(!Large && CompanionAlive) { auto P=MP(Companion->GetActorLocation()); Line(P.X-4,P.Y-3,P.X-4,P.Y+3,Mint,2); Line(P.X+4,P.Y-3,P.X+4,P.Y+3,Mint,2); }
        if(Large) {
            Text(TEXT("N"),X+W*.5f-4,Y+6,11,Muted);
            Text(AegisUI(English,TEXT("任务节点图 / 非敌方雷达"),TEXT("MISSION NODES / NO ENEMY RADAR")),X+12,Y+H-22,10,Muted);
        }
    };
    const float PulseCooldown=Player?Player->GetPulseCooldownRemaining():0;
    const float RepairCooldown=Portfolio?Portfolio->GetRepairCooldown():0;
    const int32 RepairQuote=Portfolio?Portfolio->GetRepairQuote():40;
    const float Charge=Player?Player->GetChargeFraction():0;
    const float Dash=Player?Player->GetDashCooldownRemaining():0;
    const FString Status=Briefing?TEXT("03:00"):PortfolioClock(aegis::Trial::duration-Trial.elapsed);
    FString ObjectiveTitle=Operation.complete?AegisUI(English,TEXT("清除剩余敌人"),TEXT("CLEAR REMAINING HOSTILES")):
        Contested?AegisUI(English,TEXT("将敌人赶出中继区"),TEXT("PUSH HOSTILES OUT OF THE RING")):
        Trial.wave==3?AegisUI(English,TEXT("进入撤离区"),TEXT("REACH EXTRACTION")):
        Trial.wave==2?AegisUI(English,TEXT("连接下一座中继"),TEXT("LINK THE NEXT RELAY")):
        AegisUI(English,TEXT("固守中继圆环"),TEXT("HOLD THE RELAY RING"));
    FString Occupancy=Contested?AegisUI(English,TEXT("争夺中 · 上传暂停"),TEXT("CONTESTED / UPLINK PAUSED")):
        Operation.complete?AegisUI(English,TEXT("上传完成 · 清敌后推进"),TEXT("UPLINK COMPLETE / CLEAR TO ADVANCE")):
        PlayerIn&&CompanionIn?AegisUI(English,TEXT("队伍协作 · 上传 x1.5"),TEXT("TEAM LINK / UPLINK x1.5")):
        PlayerIn||CompanionIn?AegisUI(English,TEXT("单人占领 · 上传 x1.0"),TEXT("SOLO LINK / UPLINK x1.0")):
        AegisUI(English,TEXT("C 集结队友 · 进入圆环推进"),TEXT("C RALLY ALLY / ENTER RING TO ADVANCE"));
    if(Trial.wave==3) Occupancy=Enemies>0?AegisUI(English,TEXT("先清除敌人，再由你亲自撤离"),TEXT("CLEAR HOSTILES, THEN EXTRACT YOURSELF")):
        AegisUI(English,TEXT("撤离必须由行动员亲自完成"),TEXT("THE OPERATOR MUST ENTER EXTRACTION"));
    if(Portfolio && Portfolio->GetOverclockRemaining()>0) Occupancy=FString::Printf(TEXT("%s / x2 / %.1fs"),*AegisUI(English,TEXT("中继超频"),TEXT("OVERCLOCK")),Portfolio->GetOverclockRemaining());
    else if(Portfolio && Portfolio->IsSurveyBoostActive()) Occupancy=AegisUI(English,TEXT("密钥已绑定 · 中继 x1.25"),TEXT("ARCHIVE KEY LINKED / UPLINK x1.25"));

    if(!Modal) {
        Panel(24,20,228,76,false,Teal);
        Glyph(0,49,49,11,Teal); Text(TEXT("AEGIS / ARENA"),70,34,18,White);
        Text(AegisUI(English,TEXT("棱镜坠落 · 战术链路 2.5"),TEXT("PRISM FALL / TACTICAL LINK 2.5")),40,71,10,Muted);
        Panel(276,20,704,107,false,Contested?Amber:Teal);
        Text(ObjectiveTitle,294,33,22,White); Text(Status,885,30,27,aegis::Trial::duration-Trial.elapsed<30?Amber:White);
        Text(Fit(Occupancy,480,12),295,65,12,Contested?Amber:Muted);
        Text(FString::Printf(TEXT("%02d %s"),Enemies,*AegisUI(English,TEXT("敌人"),TEXT("HOSTILES"))),850,67,12,Amber);
        const double Progress=Operation.complete?1.:Operation.charge/Operation.requiredSeconds();
        Bar(295,91,662,3,Progress,Contested?Amber:Teal,Hairline);
        for(int32 I=1;I<3;++I) Box(295+I*662.f/3,88,1,9,Slate);
        Text(FString::Printf(TEXT("%02d / 03"),FMath::Max(1,Trial.wave)),295,101,10,Teal);
        Text(AegisUI(English,TEXT("固守  >  转移  >  撤离"),TEXT("SECURE  >  TRANSFER  >  EXTRACT")),382,101,10,Muted);
        Button(TEXT("Language"),English?TEXT("L  中文"):TEXT("L  EN"),1004,20,108,34,false,false);
        Button(TEXT("Menu"),AegisUI(English,TEXT("ESC 菜单"),TEXT("ESC MENU")),1122,20,134,34,false,false);
        if(!Debug) {
            Panel(1060,147,196,189,false,Slate);
            Text(AegisUI(English,TEXT("任务路线"),TEXT("MISSION ROUTE")),1075,157,11,Muted);
            TacticalMap(1074,181,168,118,false);
            Text(FString::Printf(TEXT("%s %d/2"),*AegisUI(English,TEXT("档案"),TEXT("ARCHIVES")),SurveysClaimed),1075,314,11,Iris);
            if(Portfolio && Portfolio->HasSurveyKey()) Text(TEXT("KEY x1.25"),1167,314,10,Iris);
        }
        Panel(24,586,246,108,false,PlayerAlive?Teal:Red);
        Glyph(0,49,611,10,Teal); Text(AegisUI(English,TEXT("行动员"),TEXT("OPERATOR")),69,599,12,Muted);
        const FString HP=Player&&Player->Health?FString::Printf(TEXT("%.0f"),Player->Health->Current):TEXT("--");
        Text(HP,69,617,30,PlayerAlive?White:Red);
        Text(Player&&Player->Health?FString::Printf(TEXT("/ %.0f"),Player->Health->Maximum):TEXT(""),130,633,12,Muted);
        Health(Player,42,656,210,Teal);
        Text(TEXT("SPACE"),42,672,11,White); Text(Dash>0?FString::Printf(TEXT("%.1fs"),Dash):AegisUI(English,TEXT("闪避就绪"),TEXT("DASH READY")),137,672,11,Dash>0?Muted:Teal);
        if(PlayerAlive && Player->Health->Current<Player->Health->Maximum*.3f) { Line(24,586,270,586,Red,3); Text(AegisUI(English,TEXT("生命危险 · E 修复"),TEXT("CRITICAL / E REPAIR")),28,565,13,Red); }

        Panel(286,565,708,129,false,Teal);
        Text(AegisUI(English,TEXT("共享储备"),TEXT("SHARED RESERVE")),304,577,12,Muted);
        Text(FString::Printf(TEXT("%03d"),Energy),450,573,22,White); Text(TEXT("/ 100"),503,583,10,Muted);
        for(int32 I=0;I<20;++I) Box(562+I*20.3f,582,16,4,Energy>=I*5+1?Teal:Hairline);
        auto Skill=[&](int32 Index,int32 Icon,const FString& Key,const FString& Name,int32 Cost,const FString& State,bool Ready,FLinearColor Color,float Fraction=0.f) {
            float X=304+Index*170;
            Cut(X,603,160,77,Steel); Glyph(Icon,X+19,623,8,Ready?Color:Slate);
            Text(Key,X+38,612,14,White); Text(FString::FromInt(Cost),X+129,614,12,Energy>=Cost?Color:Red);
            Text(Name,X+12,637,13,Ready?White:Muted); Text(Fit(State,136,10),X+12,658,10,Ready?Color:Muted);
            if(Fraction>0) Bar(X+12,675,136,2,Fraction,Color,Hairline);
        };
        auto State=[&](float CD,int32 Cost) { return CD>0?FString::Printf(TEXT("%.1fs %s"),CD,*AegisUI(English,TEXT("冷却"),TEXT("COOLDOWN"))):Energy<Cost?AegisUI(English,TEXT("能量不足"),TEXT("NEED ENERGY")):AegisUI(English,TEXT("就绪"),TEXT("READY")); };
        Skill(0,0,TEXT("RMB"),AegisUI(English,TEXT("棱镜蓄能"),TEXT("PRISM CHARGE")),12,Player&&Player->IsCharging()?(Charge>=1?AegisUI(English,TEXT("松开释放"),TEXT("RELEASE TO FIRE")):AegisUI(English,TEXT("蓄能中 / 0.7s"),TEXT("CHARGING / 0.7s"))):State(0,12),Energy>=12,Teal,Charge);
        Skill(1,2,TEXT("Q"),AegisUI(English,TEXT("打断脉冲"),TEXT("DISRUPT")),35,State(PulseCooldown,35),PulseCooldown<=0&&Energy>=35,Teal);
        Skill(2,1,TEXT("E"),AegisUI(English,TEXT("队伍修复"),TEXT("TEAM REPAIR")),RepairQuote,RepairQuote==0?AegisUI(English,TEXT("无需治疗"),TEXT("NO WOUNDS")):State(RepairCooldown,RepairQuote),RepairQuote>0&&RepairCooldown<=0&&Energy>=RepairQuote,Mint);
        bool ClockReady=Portfolio && !Portfolio->IsOverclockUsed() && PlayerIn && Trial.wave<3 && !Operation.complete && Energy>=35;
        FString ClockState=Portfolio&&Portfolio->GetOverclockRemaining()>0?FString::Printf(TEXT("x2 / %.1fs"),Portfolio->GetOverclockRemaining()):Trial.wave==3?AegisUI(English,TEXT("仅限数据中继"),TEXT("DATA RELAYS ONLY")):
            Portfolio&&Portfolio->IsOverclockUsed()?AegisUI(English,TEXT("本中继已使用"),TEXT("USED THIS RELAY")):!PlayerIn||Operation.complete?AegisUI(English,TEXT("进入当前中继"),TEXT("ENTER ACTIVE RELAY")):State(0,35);
        Skill(3,3,TEXT("F"),AegisUI(English,TEXT("中继超频"),TEXT("OVERCLOCK")),35,ClockState,ClockReady,Amber);

        Panel(1010,586,246,108,false,CompanionAlive?Mint:Red); Glyph(1,1034,611,10,Mint);
        Text(TEXT("AEGIS / 02"),1056,599,12,Muted); Text(PortfolioCommand(AI,English),1056,620,21,Mint);
        Health(Companion,1027,656,211,CompanionAlive?Mint:Red);
        Text(AegisUI(English,TEXT("Z 防守 / X 集火 / C 集结"),TEXT("Z GUARD / X FOCUS / C RALLY")),1027,672,10,White);
        Text(Fit(CompanionAlive?PortfolioCompanionState(AI,English):AegisUI(English,TEXT("队友倒下"),TEXT("COMPANION DOWN")),232,11),1015,565,11,CompanionAlive?Muted:Red);
        Text(AegisUI(English,TEXT("WASD 移动 / 鼠标瞄准 / 左键射击"),TEXT("WASD MOVE / MOUSE AIM / LMB FIRE")),25,704,10,Muted);
        if(SurveyEnabled && NearestSurvey>=0 && (SurveyDistance<500 || Portfolio->GetSurveyNode()>=0)) {
            bool Scanning=Portfolio->GetSurveyNode()>=0;
            Panel(24,427,246,126,false,Iris); Glyph(3,47,452,10,Iris);
            Text(Scanning?AegisUI(English,TEXT("正在读取档案"),TEXT("READING ARCHIVE")):AegisUI(English,TEXT("附近的档案"),TEXT("ARCHIVE NEARBY")),68,440,15,White);
            Text(Scanning?AegisUI(English,TEXT("保持按住 G"),TEXT("KEEP G HELD")):FString::Printf(TEXT("G / %.1fm"),SurveyDistance/100),68,461,11,Iris);
            Bar(42,486,210,3,Portfolio->GetSurveyProgress(),Iris,Hairline);
            Text(Portfolio->IsSurveySupplySelected()?AegisUI(English,TEXT("H  补给 / 恢复生命"),TEXT("H  SUPPLY / RECOVER HP")):AegisUI(English,TEXT("H  密钥 / 中继 x1.25"),TEXT("H  KEY / RELAY x1.25")),42,500,12,Iris);
            Text(Fit(Scanning?AegisUI(English,TEXT("受伤、离开或松键会中断"),TEXT("DAMAGE, RANGE OR RELEASE CANCELS")):AegisUI(English,TEXT("靠近至 1.8 米 / 扫描 2.5 秒"),TEXT("WITHIN 1.8m / SCAN FOR 2.5s")),210,10),42,524,10,Muted);
        }
        if(Player && Player->IsCharging()) { Ring(Mouse.X,Mouse.Y,20,Charge,Teal,3); if(Charge>=1) Glyph(0,Mouse.X,Mouse.Y,10,White); }
        FString Feedback;
        if(Portfolio && Portfolio->GetFeedbackAge()<2.5f) Feedback=PortfolioFeedback(Portfolio->GetFeedback(),English);
        else if(PC && Now<PC->CommandFeedbackUntil) Feedback=PortfolioFeedback(PC->CommandFeedback,English);
        if(!Feedback.IsEmpty()) { Feedback=Fit(Feedback,620,13); float W=Measure(Feedback,13)+32; Cut(640-W*.5f,524,W,28,Navy); Text(Feedback,656-W*.5f,531,13,Teal); }
    }

    if(Modal) {
        Box(0,0,1280,720,FLinearColor(.002f,.008f,.017f,.82f));
        for(int32 I=0;I<12;++I) Line(28+I*112,106,28+I*112,670,FLinearColor(.06f,.14f,.19f,.35f));
        Glyph(0,43,44,13,Teal); Text(TEXT("AEGIS"),68,23,28,White);
        Text(TEXT("P R I S M   F A L L  /  2.5"),70,61,10,Muted);
        Text(AegisUI(English,TEXT("战术指挥终端"),TEXT("TACTICAL COMMAND INTERFACE")),252,35,12,Teal);
        Button(TEXT("Language"),English?TEXT("L  简体中文"):TEXT("L  ENGLISH"),638,24,144,34,false,false);
        Button(TEXT("Music"),MusicMuted?AegisUI(English,TEXT("M 音乐关"),TEXT("M MUSIC OFF")):AegisUI(English,TEXT("M 音乐开"),TEXT("M MUSIC ON")),792,24,142,34,false,false);
        Button(TEXT("WindowMode"),AegisUI(English,TEXT("F11 窗口/全屏"),TEXT("F11 DISPLAY")),944,24,164,34,false,false);
        Button(TEXT("Effects"),PC&&PC->bReducedEffects?AegisUI(English,TEXT("K 特效精简"),TEXT("K FX LOW")):AegisUI(English,TEXT("K 特效标准"),TEXT("K FX FULL")),1118,24,138,34,false,false);
        Line(24,88,1256,88,Hairline);
        Text(AegisUI(English,TEXT("离线单人行动 / 所有决定由你掌控"),TEXT("OFFLINE SINGLE PLAYER / YOUR SQUAD, YOUR DECISIONS")),27,698,11,Muted);
        if(PC && Now<PC->CommandFeedbackUntil) Text(Fit(PC->CommandFeedback,570,11),638,68,11,Muted);

        if(Menu && PC && PC->bRestartConfirmation) {
            Panel(248,204,784,310,true,Amber); Glyph(2,298,252,18,Amber);
            Text(AegisUI(English,TEXT("重新开始行动？"),TEXT("RESTART THE OPERATION?")),339,231,29,White);
            Text(TEXT("SESSION RESET / 01"),339,274,11,Amber);
            Wrap(AegisUI(English,TEXT("当前的阶段进度、能量与升级将重置。取消后保持暂停，你可以继续原来的行动。"),TEXT("Your stages, reserve and upgrades will reset. Cancel keeps this attempt paused so you can continue.")),282,320,714,19,Muted,3);
            Button(TEXT("Menu"),AegisUI(English,TEXT("ESC  保留本局"),TEXT("ESC  KEEP THIS ATTEMPT")),282,429,350,52,true);
            Button(TEXT("Restart"),AegisUI(English,TEXT("R  确认重开"),TEXT("R  CONFIRM RESTART")),646,429,350,52,false);
        } else if(Menu) {
            Panel(92,142,432,482,true,Teal); Text(TEXT("PAUSE / TACTICAL HOLD"),120,166,12,Teal);
            Text(AegisUI(English,TEXT("行动暂停"),TEXT("OPERATION PAUSED")),120,199,32,White);
            Wrap(PC&&PC->bPausedForFocus?AegisUI(English,TEXT("已因切换窗口暂停。准备好后继续。"),TEXT("Paused when you left the window. Continue when ready.")):AegisUI(English,TEXT("稳住节奏。下一步由你决定。"),TEXT("Take a breath. Choose your next move.")),121,252,373,18,Muted,3);
            StageStrip(121,335,373);
            Button(TEXT("Menu"),AegisUI(English,TEXT("ESC  继续行动"),TEXT("ESC  RESUME OPERATION")),120,401,376,55,true);
            Button(TEXT("Restart"),AegisUI(English,TEXT("R  重新开始"),TEXT("R  RESTART")),120,471,376,47,false);
            Button(TEXT("Quit"),AegisUI(English,TEXT("X  退出到桌面"),TEXT("X  QUIT TO DESKTOP")),120,533,376,47,false);
            Panel(548,142,640,482,false,Slate); Text(AegisUI(English,TEXT("装备与小队指令"),TEXT("LOADOUT & SQUAD COMMANDS")),576,170,22,White);
            struct FHelp { const TCHAR* Key; const TCHAR* Zh; const TCHAR* En; int32 Shape; FLinearColor Color; };
            const FHelp Help[]={{TEXT("RMB / 12"),TEXT("按住 0.7 秒再松开，穿透射击"),TEXT("Hold 0.7s, then release a piercing shot"),0,Teal},
                {TEXT("Q / 35"),TEXT("打断附近敌人的攻击"),TEXT("Interrupt nearby hostiles"),2,Teal},
                {TEXT("E / 8–40"),TEXT("修复自己与队友，按实际治疗消耗"),TEXT("Repair allies; cost follows actual healing"),1,Mint},
                {TEXT("F / 35"),TEXT("当前中继增幅 6 秒，每座一次"),TEXT("Boost this relay for 6s; one use per relay"),3,Amber}};
            for(int32 I=0;I<4;++I) { float Y=221+I*68; Glyph(Help[I].Shape,590,Y+15,11,Help[I].Color); Text(Help[I].Key,617,Y,13,Help[I].Color); Text(Help[I].En?AegisUI(English,Help[I].Zh,Help[I].En):TEXT(""),617,Y+25,15,White); Line(576,Y+57,1158,Y+57,Hairline); }
            Text(AegisUI(English,TEXT("WASD 移动 / 左键射击 / SPACE 闪避"),TEXT("WASD MOVE / LMB FIRE / SPACE DASH")),577,507,14,White);
            Text(AegisUI(English,TEXT("Z 防守 / X 集火 / C 集结"),TEXT("Z GUARD / X FOCUS / C RALLY")),577,538,14,Mint);
            Text(AegisUI(English,TEXT("G 扫描档案 / H 切换补给或密钥"),TEXT("G SCAN ARCHIVE / H SELECT REWARD")),577,572,14,Iris);
        } else if(Briefing) {
            Panel(36,120,482,547,false,Teal); Text(TEXT("OPERATION / 01"),62,145,12,Teal);
            Text(AegisUI(English,TEXT("棱镜坠落"),TEXT("PRISM FALL")),61,173,40,White);
            Wrap(AegisUI(English,TEXT("在失联的晶体设施中，重建最后一条上传链路。"),TEXT("Restore the last uplink inside a silent crystal facility.")),63,228,422,18,Muted,3);
            TacticalMap(62,299,428,273,true);
            Glyph(0,76,607,9,Teal); Text(AegisUI(English,TEXT("你 / 行动员"),TEXT("YOU / OPERATOR")),95,600,12,White);
            Glyph(1,275,607,9,Mint); Text(AegisUI(English,TEXT("艾吉斯 / AI 队友"),TEXT("AEGIS / AI ALLY")),294,600,12,White);
            Text(AegisUI(English,TEXT("3 阶段 · 2 档案 · 1 支队伍"),TEXT("3 STAGES / 2 ARCHIVES / 1 SQUAD")),63,637,11,Muted);
            Panel(542,120,702,547,true,Slate); Text(AegisUI(English,TEXT("重建信号。一起撤离。"),TEXT("RESTORE THE SIGNAL.")),569,147,30,White);
            Text(AegisUI(English,TEXT("共享能量，协作站位，完成最后一次传输。"),TEXT("Share energy. Hold together. Complete the last transfer.")),570,191,15,Muted);
            const FString Titles[]={AegisUI(English,TEXT("固守中继"),TEXT("SECURE THE RELAY")),AegisUI(English,TEXT("转移与探索"),TEXT("TRANSFER & EXPLORE")),AegisUI(English,TEXT("清场后撤离"),TEXT("CLEAR & EXTRACT"))};
            const FString Copy[]={AegisUI(English,TEXT("站在中继圆环内推进。队友一起占领更快，清敌后选择升级。"),TEXT("Stand in the ring to upload. Your ally speeds it up. Clear hostiles, then upgrade.")),
                AegisUI(English,TEXT("连接两座中继。档案可选：H 选奖励，按住 G 扫描。"),TEXT("Link two relays. Archives are optional: H selects a reward, hold G to scan.")),
                AegisUI(English,TEXT("击败剩余敌人，由你亲自进入撤离区。队友无法代替你撤离。"),TEXT("Defeat the remaining hostiles. Enter extraction yourself; your ally cannot finish for you."))};
            for(int32 I=0;I<3;++I) { float Y=244+I*82; Text(FString::Printf(TEXT("0%d"),I+1),570,Y,27,I==2?Amber:Teal); Text(Titles[I],624,Y+1,19,White); Wrap(Copy[I],624,Y+31,585,15,Muted,2); }
            Line(570,491,1216,491,Hairline);
            Text(Runner->IsNorthRouteFirst()?AegisUI(English,TEXT("V  路线：北侧 > 东侧"),TEXT("V  ROUTE: NORTH > EAST")):AegisUI(English,TEXT("V  路线：东侧 > 北侧"),TEXT("V  ROUTE: EAST > NORTH")),571,509,13,Teal);
            Text(AegisUI(English,TEXT("选择行动强度"),TEXT("CHOOSE INTENSITY")),571,542,11,Muted);
            Button(TEXT("Guided"),AegisUI(English,TEXT("1  引导"),TEXT("1  GUIDED")),570,568,151,49,!Runner->bPressureSelected);
            Button(TEXT("Pressure"),AegisUI(English,TEXT("2  压力"),TEXT("2  PRESSURE")),733,568,151,49,Runner->bPressureSelected);
            Button(TEXT("Deploy"),AegisUI(English,TEXT("ENTER  开始行动"),TEXT("ENTER  DEPLOY")),902,558,314,59,true);
            Text(AegisUI(English,TEXT("WASD 移动 / 左键射击 / SPACE 闪避 / ESC 菜单"),TEXT("WASD MOVE / LMB FIRE / SPACE DASH / ESC MENU")),571,638,12,Muted);
        } else if(Upgrade) {
            Text(AegisUI(English,TEXT("阶段完成 / 装备改装"),TEXT("STAGE CLEAR / LOADOUT UPGRADE")),54,131,13,Mint);
            Text(AegisUI(English,TEXT("选择下一项优势"),TEXT("CHOOSE YOUR NEXT ADVANTAGE")),53,165,33,White);
            Text(AegisUI(English,TEXT("时间暂停。选择一项永久升级，已安装项不能重复选择。"),TEXT("Time is paused. Choose one permanent upgrade; installed options cannot be selected again.")),55,215,16,Muted);
            const FLinearColor Colors[]={Teal,Mint,Amber};
            const FString Names[]={AegisUI(English,TEXT("棱镜分裂"),TEXT("PRISM SPLIT")),AegisUI(English,TEXT("救援脉冲"),TEXT("RESCUE PULSE")),AegisUI(English,TEXT("中继共生"),TEXT("RELAY SYMBIOSIS"))};
            const FString Effects[]={AegisUI(English,TEXT("基础射击最多命中两名敌人，蓄力射击最多三名。掩体和队友仍然阻挡。"),TEXT("Basic shots hit up to two hostiles; charged shots hit three. Cover and allies still block.")),
                AegisUI(English,TEXT("Q 同时恢复自己 12 点、附近无遮挡队友 20 点生命。不能复活。"),TEXT("Q also restores 12 HP to you and 20 to a nearby ally with clear sight. No revival.")),
                AegisUI(English,TEXT("双方共同推进中继时持续恢复生命。争夺或完成时停止，不能复活。"),TEXT("Recover while both allies advance a relay. Stops during contest or completion. No revival."))};
            const FString ModuleTags[]={AegisUI(English,TEXT("射击 / 穿透"),TEXT("OFFENSE / PIERCE")),AegisUI(English,TEXT("生存 / 支援"),TEXT("SURVIVAL / SUPPORT")),AegisUI(English,TEXT("协作 / 持续"),TEXT("TEAMWORK / SUSTAIN"))};
            for(int32 I=0;I<3;++I) { float X=54+I*397; bool Owned=Runner->HasUpgrade(I+1); FLinearColor C=Owned?Slate:Colors[I]; Panel(X,273,378,332,true,C);
                Glyph(I==0?0:I==1?1:3,X+43,320,21,C); Text(ModuleTags[I],X+87,303,12,C); Text(FString::Printf(TEXT("MODULE / 0%d"),I+1),X+87,328,10,Muted);
                Text(Names[I],X+24,366,24,Owned?Muted:White); Wrap(Effects[I],X+24,410,330,17,Muted,4);
                if(!Owned) Button(FName(*FString::Printf(TEXT("Upgrade%d"),I+1)),FString::Printf(TEXT("%d  %s"),I+1,*AegisUI(English,TEXT("安装升级"),TEXT("INSTALL MODULE"))),X+24,537,330,44,true);
                else { Cut(X+24,537,330,44,Navy); Text(AegisUI(English,TEXT("已安装"),TEXT("INSTALLED")),X+42,550,16,Slate); }
            }
            if(Trial.wave==1) Text(Runner->IsNorthRouteFirst()?AegisUI(English,TEXT("V  转移路线：北侧 > 东侧"),TEXT("V  TRANSFER ROUTE: NORTH > EAST")):AegisUI(English,TEXT("V  转移路线：东侧 > 北侧"),TEXT("V  TRANSFER ROUTE: EAST > NORTH")),55,637,15,Teal);
        } else if(Finished) {
            bool Won=Trial.phase==aegis::TrialPhase::Won; FLinearColor Outcome=Won?Mint:Amber;
            const auto* Stats=Portfolio?&Portfolio->GetStatistics():nullptr;
            Panel(54,141,373,487,false,Outcome); Ring(240,267,61,1,Hairline,1); Ring(240,267,70,1,Outcome,2); Glyph(Won?3:2,240,267,30,Outcome);
            Text(Won?TEXT("UPLINK / COMPLETE"):TEXT("SIGNAL / LOST"),91,370,14,Outcome);
            Text(Won?AegisUI(English,TEXT("成功撤离"),TEXT("EXTRACTED")):AegisUI(English,TEXT("行动结束"),TEXT("RUN ENDED")),90,408,34,White);
            Wrap(Won?AegisUI(English,TEXT("三阶段上传完成，行动员已撤离。"),TEXT("Three stages secured. The operator has extracted.")):AegisUI(English,TEXT("本次链路未能完成。调整路线与升级，再试一次。"),TEXT("The uplink did not complete. Adapt your route and upgrades, then try again.")),92,470,292,17,Muted,4);
            Panel(451,141,775,487,true,Slate); Text(AegisUI(English,TEXT("行动复盘"),TEXT("OPERATION DEBRIEF")),479,167,25,White); StageStrip(480,213,718);
            auto Metric=[&](int32 Col,int32 Row,const FString& Label,const FString& Value,FLinearColor Color) { float X=480+Col*239, Y=278+Row*108; Text(Label,X,Y,12,Muted); Text(Value,X,Y+26,34,Color); };
            Metric(0,0,AegisUI(English,TEXT("阶段完成"),TEXT("STAGES SECURED")),FString::Printf(TEXT("%d / 3"),Stats?Stats->StagesRewarded:0),Outcome);
            Metric(1,0,AegisUI(English,TEXT("用时"),TEXT("ELAPSED")),PortfolioClock(Trial.elapsed),White);
            Metric(2,0,AegisUI(English,TEXT("发现档案"),TEXT("ARCHIVES CLAIMED")),FString::Printf(TEXT("%d / 2"),SurveysClaimed),Iris);
            Metric(0,1,AegisUI(English,TEXT("队伍实际伤害"),TEXT("ALLIED DAMAGE")),FString::Printf(TEXT("%.0f"),Runner->TrialDamageDealt),White);
            Metric(1,1,AegisUI(English,TEXT("消耗能量"),TEXT("ENERGY SPENT")),Stats?FString::FromInt(Stats->EnergySpent):TEXT("--"),Teal);
            Metric(2,1,AegisUI(English,TEXT("E 实际恢复生命"),TEXT("ACTUAL E HEALING")),Stats?FString::Printf(TEXT("%.0f"),Stats->PlayerHealing+Stats->CompanionHealing):TEXT("--"),Mint);
            Text(CompanionAlive?AegisUI(English,TEXT("艾吉斯链路完整 / 队友存活"),TEXT("AEGIS LINK INTACT / COMPANION SURVIVED")):AegisUI(English,TEXT("艾吉斯链路离线 / 队友倒下"),TEXT("AEGIS LINK OFFLINE / COMPANION DOWN")),480,499,13,CompanionAlive?Mint:Amber);
            Button(TEXT("Restart"),AegisUI(English,TEXT("R  新行动"),TEXT("R  NEW OPERATION")),480,550,472,49,true);
            Button(TEXT("Quit"),AegisUI(English,TEXT("X  退出"),TEXT("X  QUIT")),966,550,231,49,false);
        }
    }

#if !UE_BUILD_SHIPPING
    // Uplink deliberately uses the tactical controller. Reference utility scores
    // are exposed as inactive evidence, never labelled as the active selector.
    if (Debug)
    {
        const float X = 918, W = 338;
        Panel(X, 112, W, 470, false, Amber);
        Text(AegisUI(English, TEXT("实时系统诊断"), TEXT("LIVE SYSTEM DIAGNOSTICS")), X+16, 126, 17, White);
        Text(AegisUI(English, TEXT("F1 关闭 / 战术控制器"), TEXT("F1 CLOSE  /  TACTICAL CONTROLLER")), X+16, 153, 12, Amber);
        Line(X+16, 177, X+W-16, 177, Hairline);
        Text(AegisUI(English, TEXT("效用评分未参与当前控制"), TEXT("Utility scores not controlling")), X+16, 189, 16, Amber);
        Text(AegisUI(English, TEXT("战术移动未使用 EQS"), TEXT("EQS not used by tactical movement")), X+16, 215, 14, Muted);
        if (AI)
        {
            const auto& T = AI->GetDecisionTelemetry();
            Text(AegisUI(English, TEXT("当前战术状态"), TEXT("CURRENT TACTICAL STATE")), X+16, 245, 12, Muted);
            Wrap(PortfolioCompanionState(AI, English), X+16, 267, W-32, 16, White, 2);
            const FString Perception = English ? FString::Printf(TEXT("Sight %s / memory %s / windup %s"),
                AI->bTargetVisible ? TEXT("yes") : TEXT("no"), AI->HasTargetMemory() ? TEXT("yes") : TEXT("no"),
                AI->bWindingUpShot ? TEXT("yes") : TEXT("no")) : FString::Printf(TEXT("视野 %s / 记忆 %s / 蓄势 %s"),
                AI->bTargetVisible ? TEXT("有") : TEXT("无"), AI->HasTargetMemory() ? TEXT("有") : TEXT("无"),
                AI->bWindingUpShot ? TEXT("是") : TEXT("否"));
            Text(Perception, X+16, 313, 13, Muted);
            const FString Movement = English ? FString::Printf(TEXT("Moves %d / shots %d / navigation %s"), AI->TacticalMoveRequests, AI->TacticalShots,
                AI->GetMoveStatus() == EPathFollowingStatus::Moving ? TEXT("moving") : TEXT("idle")) : FString::Printf(TEXT("移动 %d / 射击 %d / 导航 %s"), AI->TacticalMoveRequests, AI->TacticalShots,
                AI->GetMoveStatus() == EPathFollowingStatus::Moving ? TEXT("移动中") : TEXT("空闲"));
            Text(Movement, X+16, 337, 13, White);
            if (V2)
            {
                Text(English ? FString::Printf(TEXT("Guard slots: %d checked / %d rejected / %d alt"), AI->GuardSlotChecks, AI->GuardSlotRejections, AI->GuardAlternateSelections)
                             : FString::Printf(TEXT("防守位：检查 %d / 排除 %d / 备选 %d"), AI->GuardSlotChecks, AI->GuardSlotRejections, AI->GuardAlternateSelections), X+16, 359, 11, Muted);
                Text(Fit(PortfolioGuardReason(AI->GuardSlotReason, English), W-32, 11), X+16, 377, 11, Amber);
            }
            else Text(English ? FString::Printf(TEXT("Inactive F %.2f  A %.2f  S %.2f  R %.2f"), T.Scores[0], T.Scores[1], T.Scores[2], T.Scores[3])
                              : FString::Printf(TEXT("未启用 F %.2f  A %.2f  S %.2f  R %.2f"), T.Scores[0], T.Scores[1], T.Scores[2], T.Scores[3]), X+16, 361, 12, Muted);
        }
        else Text(AegisUI(English, TEXT("队友控制器不可用"), TEXT("Companion controller unavailable")), X+16, 269, 15, Muted);
        Line(X+16, 393, X+W-16, 393, Hairline);
        Text(AegisUI(English, TEXT("能量流动 / 实际收支"), TEXT("ENERGY FLOW / ACTUAL TRANSACTIONS")), X+16, 405, 12, Amber);
        if (Portfolio)
        {
            const auto& Stats = Portfolio->GetStatistics();
            Text(English ? FString::Printf(TEXT("Reserve %d / earned %d / spent %d"), Energy, Stats.EnergyEarned, Stats.EnergySpent)
                         : FString::Printf(TEXT("储备 %d / 获得 %d / 消耗 %d"), Energy, Stats.EnergyEarned, Stats.EnergySpent), X+16, 430, 14, White);
            Text(English ? FString::Printf(TEXT("Overflow %d / pulse %d / repair %d"), Stats.EnergyOverflow, Stats.PulsesUsed, Stats.RepairsUsed)
                         : FString::Printf(TEXT("溢出 %d / 脉冲 %d / 修复 %d"), Stats.EnergyOverflow, Stats.PulsesUsed, Stats.RepairsUsed), X+16, 454, 14, Muted);
            Text(English ? FString::Printf(TEXT("E healing: self %.0f / ally %.0f"), Stats.PlayerHealing, Stats.CompanionHealing)
                         : FString::Printf(TEXT("E 恢复：自己 %.0f / 队友 %.0f"), Stats.PlayerHealing, Stats.CompanionHealing), X+16, 478, 14, White);
            Wrap(Stats.LastTransaction.IsEmpty() ? AegisUI(English, TEXT("暂无收支记录"), TEXT("No transaction yet")) : PortfolioTransaction(Stats.LastTransaction, English), X+16, 506, W-32, 13, Muted, 2);
            Text(AegisUI(English, TEXT("来源：运行时伤害与资源事件"), TEXT("Source: runtime damage + economy events")), X+16, 556, 12, Muted);
        }
    }
#endif
}
