#include "AegisLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioMusic.h"
#include "CollisionQueryParams.h"
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
    const FLinearColor Navy(.018f, .035f, .060f, .95f), Steel(.038f, .073f, .100f, .96f);
    const FLinearColor Paper(.91f, .94f, .93f, .98f), Ink(.032f, .075f, .098f);
    const FLinearColor White(.94f, .97f, .95f), Muted(.57f, .68f, .72f), Slate(.24f, .35f, .39f);
    const FLinearColor Mint(.28f, .93f, .76f), Teal(.02f, .43f, .36f), Amber(1.f, .69f, .26f);
    const FLinearColor Red(1.f, .36f, .29f), Hairline(.19f, .30f, .35f, .85f);
    const FLinearColor Iris(.75f, .62f, 1.f);
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
        DrawText(Value, Color, OX + X*S, OY + Y*S, Font, Size*FontUnit*S, false);
    };
    auto Measure = [&](const FString& Value, float Size) {
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
    auto Panel = [&](float X, float Y, float W, float H, bool Light, FLinearColor Accent) {
        Box(X + 3, Y + 4, W, H, FLinearColor(0, 0, 0, .22f));
        Box(X, Y, W, H, Light ? Paper : Navy);
        Box(X, Y, 3, H, Accent);
        Line(X + 14, Y, X + W - 14, Y, Light ? FLinearColor(.99f, 1, .98f) : Hairline);
        Line(X + W - 10, Y + H, X + W, Y + H - 10, Accent);
    };
    auto Hovered = [&](float X, float Y, float W, float H) {
        return Mouse.X >= X && Mouse.X <= X+W && Mouse.Y >= Y && Mouse.Y <= Y+H;
    };
    auto Button = [&](FName Name, const FString& Label, float X, float Y, float W, float H,
                      bool Primary, bool Light = true) {
        const bool Hover = Hovered(X, Y, W, H);
        const FLinearColor Back = Primary ? (Hover ? Mint : Teal) :
            (Hover ? (Light ? FLinearColor(.77f, .86f, .83f) : Steel) : (Light ? FLinearColor(.82f, .87f, .86f) : Navy));
        Box(X, Y, W, H, Back);
        Line(X, Y, X + W, Y, Primary ? Teal : (Light ? FLinearColor(.68f, .77f, .75f) : Hairline));
        if (Hover) Box(X, Y + H - 2, W, 2, Primary ? White : Mint);
        float LabelSize = 18.f;
        while (LabelSize > 14.f && Measure(Label, LabelSize) > W - 28.f) LabelSize -= 1.f;
        Text(Fit(Label, W - 28.f, LabelSize), X + 14, Y + (H - LabelSize)*.5f - 1, LabelSize,
             Primary ? (Hover ? Ink : White) : (Light ? Ink : White));
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
            Box(P.X - 27, P.Y - 2, 54, 8, Navy);
            Health(Bot, P.X - 25, P.Y, 50, Color);
            if (Friendly) Text(AegisUI(English, TEXT("艾吉斯"), TEXT("AEGIS")), P.X - 22, P.Y - 18, 13, Mint);
            else if (Aimed || Warning)
            {
                const FString Label = Warning ? AegisUI(English, TEXT("开火"), TEXT("FIRING")) : PortfolioRole(Bot, English);
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

    // Discrete instruments leave the arena and all aiming lanes visible.
    Panel(24, 24, 208, 68, false, Mint);
    Text(AegisUI(English, TEXT("棱镜竞技场"), TEXT("AEGIS ARENA")), 40, 33, 23, White);
    Text(V2 ? AegisUI(English, TEXT("棱镜坠落  /  2.4 RC"), TEXT("P R I S M   F A L L  /  2.4 RC")) : AegisUI(English, TEXT("棱镜中继"), TEXT("P R I S M   R E L A Y")), 41, 65, 12, Mint);
    if (V2 && !Modal) Text(MusicMuted ? AegisUI(English, TEXT("M  音乐关闭"), TEXT("M  MUSIC OFF")) : AegisUI(English, TEXT("M  音乐开启"), TEXT("M  MUSIC ON")), 1100, 87, 12, Muted);
    if (!Menu && !Briefing && !Finished && !Upgrade)
        Button(TEXT("Menu"), AegisUI(English, TEXT("ESC  /  菜单"), TEXT("ESC  /  MENU")), 1100, 24, 156, 34, false, false);
#if !UE_BUILD_SHIPPING
    if (Debug) Text(AegisUI(English, TEXT("F1  诊断开启"), TEXT("F1  DIAGNOSTICS ON")), 1100, 66, 12, Mint);
#endif

    Panel(370, 20, 540, 120, false, Contested ? Amber : Mint);
    const FString StageNames[] = {AegisUI(English, TEXT("01  固守"), TEXT("01  SECURE")), AegisUI(English, TEXT("02  转移"), TEXT("02  TRANSFER")), AegisUI(English, TEXT("03  撤离"), TEXT("03  EXTRACT"))};
    for (int32 I = 0; I < 3; ++I)
    {
        const bool Current = Trial.wave == I + 1;
        const bool Done = Trial.wave > I + 1 || (Trial.wave == I + 1 && (Intermission || Trial.phase == aegis::TrialPhase::Won));
        const float X = 388.f + I*172.f;
        Text(StageNames[I], X, 31, 14, Done ? Mint : Current ? White : Muted);
        Box(X, 56, 150, 2, Done ? Mint : Current ? Amber : Hairline);
    }
    FString ObjectiveTitle;
    if (Briefing) ObjectiveTitle = AegisUI(English, TEXT("连接小队 · 固守中继 · 完成撤离"), TEXT("LINK UP. SECURE THE RELAYS. EXTRACT."));
    else if (Finished) ObjectiveTitle = Trial.phase == aegis::TrialPhase::Won ? AegisUI(English, TEXT("全部阶段完成"), TEXT("ALL STAGES COMPLETE")) : AegisUI(English, TEXT("行动结束"), TEXT("OPERATION ENDED"));
    else if (Upgrade) ObjectiveTitle = AegisUI(English, TEXT("阶段完成  /  选择升级"), TEXT("STAGE CLEAR  /  CHOOSE YOUR UPGRADE"));
    else if (Intermission) ObjectiveTitle = English ? FString::Printf(TEXT("NEXT STAGE IN %.0fs"), FMath::Max(0.0, Trial.transitionAt - Now)) : FString::Printf(TEXT("下一阶段将在 %.0fs 后开始"), FMath::Max(0.0, Trial.transitionAt - Now));
    else if (Operation.complete)
        ObjectiveTitle = AegisUI(English, TEXT("中继已上线 · 清除剩余敌人"), TEXT("RELAY ONLINE - CLEAR REMAINING HOSTILES"));
    else if (Contested)
        ObjectiveTitle = AegisUI(English, TEXT("中继争夺中 · 将敌人赶出区域"), TEXT("RELAY CONTESTED - PUSH HOSTILES OUT"));
    else if (Trial.wave == 1)
        ObjectiveTitle = AegisUI(English, TEXT("固守中继 · 保持占领区"), TEXT("SECURE THE RELAY - HOLD THE RING"));
    else if (Trial.wave == 2)
        ObjectiveTitle = English ? FString::Printf(TEXT("TRANSFER DATA - RELAY %d / 2"), Operation.relay+1)
                                : FString::Printf(TEXT("转移数据 · 中继 %d / 2"), Operation.relay+1);
    else
        ObjectiveTitle = AegisUI(English, TEXT("撤离 · 与队伍一起进入区域"), TEXT("EXTRACT - BRING YOUR TEAM TO THE RING"));
    Text(Fit(ObjectiveTitle, 504, 18), 388, 67, 18, Contested ? Amber : White);
    FString Occupancy;
    if (Briefing) Occupancy = AegisUI(English, TEXT("3 个阶段  /  2 次升级  /  共享储备"), TEXT("3 STAGES  /  2 UPGRADES  /  ONE SHARED RESERVE"));
    else if (Finished || Upgrade || Intermission) Occupancy = AegisUI(English, TEXT("射击 + 移动 + 协作"), TEXT("FIRE + MOVEMENT + TEAMWORK"));
    else if (Operation.complete) Occupancy = AegisUI(English, TEXT("目标已上线  /  清除剩余敌人"), TEXT("OBJECTIVE ONLINE  /  CLEAR REMAINING HOSTILES"));
    else if (Contested) Occupancy = AegisUI(English, TEXT("目标争夺中  /  清除环内敌人"), TEXT("CONTESTED  /  REMOVE HOSTILES FROM THE RING"));
    else if (Trial.wave == 3 && Enemies > 0) Occupancy = AegisUI(English, TEXT("撤离锁定  /  先清除敌人"), TEXT("EXTRACTION LOCKED  /  CLEAR HOSTILES FIRST"));
    else if (Trial.wave == 3 && !PlayerIn) Occupancy = AegisUI(English, TEXT("需要行动员进入撤离区"), TEXT("EXTRACTION REQUIRES YOU INSIDE THE RING"));
    else if (PlayerIn && CompanionIn) Occupancy = AegisUI(English, TEXT("双方在范围内  /  上传 x1.5"), TEXT("BOTH IN RANGE  /  UPLINK x1.5"));
    else if (PlayerIn || CompanionIn) Occupancy = AegisUI(English, TEXT("一名队友在范围内  /  上传 x1.0"), TEXT("ONE ALLY IN RANGE  /  UPLINK x1.0"));
    else Occupancy = AegisUI(English, TEXT("进入目标区  /  按 C 集结队友"), TEXT("ENTER THE RING  /  RALLY YOUR COMPANION WITH C"));
    if (V2 && !Modal && Portfolio->GetOverclockRemaining() > 0)
    {
        const FString OverclockReason = Contested ? AegisUI(English, TEXT("争夺中：不会推进"), TEXT("CONTESTED: NO PROGRESS"))
                                                  : AegisUI(English, TEXT("清除敌人后完成"), TEXT("CLEAR HOSTILES TO FINISH"));
        if (English)
            Occupancy = FString::Printf(TEXT("OVERCLOCK x2  %.1fs / %s"), Portfolio->GetOverclockRemaining(), *OverclockReason);
        else
            Occupancy = FString::Printf(TEXT("超频 x2  %.1f 秒 / %s"), Portfolio->GetOverclockRemaining(), *OverclockReason);
    }
    else if (SurveyEnabled && !Modal && Portfolio->IsSurveyBoostActive())
        Occupancy = Contested ? AegisUI(English, TEXT("密钥 x1.25 / 争夺中：不会推进"), TEXT("KEY x1.25 / CONTESTED: NO PROGRESS"))
                              : AegisUI(English, TEXT("密钥已连接 / 数据中继推进 x1.25"), TEXT("KEY LINKED / DATA UPLINK x1.25"));
    Text(Fit(Occupancy, 360, 12), 388, 96, 12, Contested ? Amber : Muted);
    const FString Status = Briefing ? TEXT("03:00") : PortfolioClock(aegis::Trial::duration - Trial.elapsed);
    Text(English ? FString::Printf(TEXT("%d LEFT  |  %s"), Enemies, *Status) : FString::Printf(TEXT("剩余 %d  |  %s"), Enemies, *Status), 761, 96, 13, White);
    const double ObjectiveProgress = Briefing ? 0 : (Operation.complete ? 1.0 : Operation.charge / Operation.requiredSeconds());
    Bar(388, 122, 504, 4, ObjectiveProgress, Contested ? Amber : Mint, Hairline);

    Panel(24, 605, 286, 90, false, PlayerAlive ? Mint : Red);
    Text(AegisUI(English, TEXT("行动员"), TEXT("OPERATOR")), 41, 616, 14, Muted);
    Line(41, 636, 291, 636, Hairline);
    const FString HealthValue = Player && Player->Health ? FString::Printf(TEXT("%.0f / %.0f"), Player->Health->Current, Player->Health->Maximum) : TEXT("--");
    Text(HealthValue, 291 - Measure(HealthValue, 22), 612, 22, PlayerAlive ? White : Red);
    Health(Player, 41, 646, 250, PlayerAlive && Player->Health->Current > Player->Health->Maximum*.3f ? Mint : Red);
    const float Dash = Player ? Player->GetDashCooldownRemaining() : 0;
    Text(AegisUI(English, TEXT("SPACE  闪避"), TEXT("SPACE  DASH")), 41, 665, 14, White);
    Text(Dash > 0 ? FString::Printf(TEXT("%.1fs"), Dash) : AegisUI(English, TEXT("就绪"), TEXT("READY")), 220, 665, 14, Dash > 0 ? Muted : Mint);

    Panel(389, 590, 502, 108, true, Teal);
    Text(AegisUI(English, TEXT("共享能量"), TEXT("SHARED ENERGY")), 407, 601, 14, Slate);
    Line(407, 624, 873, 624, FLinearColor(.70f, .79f, .77f));
    Text(Portfolio ? FString::Printf(TEXT("%03d"), Energy) : TEXT("---"), 787, 594, 34, Ink);
    Text(TEXT("/ 100"), 845, 608, 13, Slate);
    for (int32 I = 0; I < 20; ++I)
        Bar(407 + I*23.3f, 629, 19.3f, 5, FMath::Clamp((Energy - I*5) / 5.f, 0.f, 1.f), Teal, FLinearColor(.71f, .80f, .77f));
    const float PulseCooldown = Player ? Player->GetPulseCooldownRemaining() : 0;
    const float RepairCooldown = Portfolio ? Portfolio->GetRepairCooldown() : 0;
    auto SkillState = [&](float Cooldown, int32 Cost) -> FString {
        if (!Portfolio) return AegisUI(English, TEXT("离线"), TEXT("OFFLINE"));
        if (Cooldown > 0) return FString::Printf(TEXT("%.1fs"), Cooldown);
        return Energy >= Cost ? AegisUI(English, TEXT("可用"), TEXT("AFFORDABLE")) : AegisUI(English, TEXT("能量不足"), TEXT("LOW ENERGY"));
    };
    Text(AegisUI(English, TEXT("Q  脉冲  /  35"), TEXT("Q  PULSE  /  35")), 407, 644, 16, Ink);
    const int32 RepairQuote = Portfolio ? Portfolio->GetRepairQuote() : 40;
    Text(English ? FString::Printf(TEXT("E  REPAIR  /  %d"), RepairQuote) : FString::Printf(TEXT("E  修复  /  %d"), RepairQuote), 655, 644, 16, Ink);
    Text(SkillState(PulseCooldown, AAegisPortfolio::PulseCost), 407, 670, 13,
         Energy >= AAegisPortfolio::PulseCost && PulseCooldown <= 0 ? Teal : Slate);
    Text(RepairQuote == 0 ? AegisUI(English, TEXT("无伤口"), TEXT("NO WOUNDS")) : SkillState(RepairCooldown, RepairQuote), 655, 670, 13,
         Energy >= RepairQuote && RepairCooldown <= 0 && RepairQuote > 0 ? Teal : Slate);
    Text(AegisUI(English, TEXT("打断附近敌人"), TEXT("DISRUPT NEARBY")), 496, 670, 11, Slate);
    Text(AegisUI(English, TEXT("自己 + 附近队友"), TEXT("SELF + NEAR ALLY")), 744, 670, 11, Slate);

    Panel(970, 605, 286, 90, false, CompanionAlive ? Mint : Red);
    Text(AegisUI(English, TEXT("艾吉斯"), TEXT("AEGIS")), 987, 616, 14, Muted);
    Text(PortfolioCommand(AI, English), 1117, 616, 14, Mint);
    Line(987, 630, 1238, 630, Hairline);
    Health(Companion, 987, 643, 250, CompanionAlive ? Mint : Red);
    const FString CompanionState = !CompanionAlive ? AegisUI(English, TEXT("队友倒下"), TEXT("Companion down")) :
        Finished ? AegisUI(English, TEXT("行动结束 / 待命"), TEXT("Operation ended / standing by")) :
        Briefing ? AegisUI(English, TEXT("准备与你部署"), TEXT("Ready to deploy with you")) :
        Intermission ? AegisUI(English, TEXT("阶段完成 / 等待下一段"), TEXT("Stage clear / awaiting next sector")) :
        PortfolioCompanionState(AI, English);
    Text(Fit(CompanionState, 250, 14),
         987, 659, 14, CompanionAlive ? White : Red);
    Text(AegisUI(English, TEXT("Z 防守   X 集火   C 集结"), TEXT("Z GUARD     X FOCUS     C RALLY")), 987, 704, 12, Muted);
    Text(AegisUI(English, TEXT("WASD 移动  /  按住左键射击"), TEXT("WASD MOVE  /  HOLD LMB FIRE")), 24, 704, 12, Muted);

    if (V2 && !Modal)
    {
        const float Charge = Player ? Player->GetChargeFraction() : 0;
        const bool ShowSurvey = SurveyEnabled && NearestSurvey >= 0 &&
            (SurveyDistance < 500.f || Portfolio->GetSurveyNode() >= 0) && Trial.phase == aegis::TrialPhase::Active;
        if (ShowSurvey)
        {
            const bool Scanning = Portfolio->GetSurveyNode() >= 0;
            const bool Supply = Portfolio->IsSurveySupplySelected();
            Panel(24, 462, 286, 127, false, Iris);
            Text(Scanning ? AegisUI(English, TEXT("G  扫描中 / 保持按住"), TEXT("G  SCANNING / KEEP HELD"))
                          : AegisUI(English, TEXT("G  按住扫描 / 2.5 秒"), TEXT("G  HOLD TO SCAN / 2.5s")), 41, 473, 15, White);
            Bar(41, 499, 250, 4, Portfolio->GetSurveyProgress(), Iris, Hairline);
            const FString Reward = Supply ? AegisUI(English, TEXT("补给 / 自己 +20 · 队友 +15"), TEXT("SUPPLY / SELF +20 · ALLY +15"))
                                          : AegisUI(English, TEXT("中继密钥 / 数据上传 x1.25"), TEXT("RELAY KEY / DATA UPLINK x1.25"));
            Text(Fit(Reward, 250, 13), 41, 512, 13, Iris);
            Text(Supply ? AegisUI(English, TEXT("队友需在 5 米内且无遮挡"), TEXT("ALLY WITHIN 5m / CLEAR SIGHT"))
                        : AegisUI(English, TEXT("自动用于数据中继 / 撤离无增幅"), TEXT("AUTO LINKS / NO EXTRACTION BOOST")), 41, 532, 11, Muted);
            Line(41, 550, 291, 550, Hairline);
            Text(Scanning ? AegisUI(English, TEXT("受伤、离开或松键会中断"), TEXT("DAMAGE, MOVING OUT OR RELEASE CANCELS"))
                          : AegisUI(English, TEXT("H  切换奖励"), TEXT("H  SWITCH REWARD")), 41, 560, Scanning ? 10 : 13, White);
            if (!Scanning)
            {
                const FString Range = SurveyDistance <= 180.f ? AegisUI(English, TEXT("范围内"), TEXT("IN RANGE"))
                    : (English ? FString::Printf(TEXT("%.1fm / NEED 1.8m"), SurveyDistance / 100.f)
                               : FString::Printf(TEXT("%.1f 米 / 需 1.8 米"), SurveyDistance / 100.f));
                Text(Range, 291 - Measure(Range, 10), 563, 10, SurveyDistance <= 180.f ? Mint : Muted);
            }
        }
        else
        {
            Panel(24, 489, 286, 100, false, Mint);
            Text(AegisUI(English, TEXT("RMB  按住 + 松开 / 12"), TEXT("RMB  HOLD + RELEASE / 12")), 41, 498, 14, White);
            Bar(41, 519, 250, 3, Charge, Charge >= 1 ? Amber : Mint, Hairline);
            Text(Player && Player->IsCharging() ? (Charge >= 1 ? AegisUI(English, TEXT("就绪：松开释放穿透"), TEXT("READY: RELEASE TO PIERCE")) : AegisUI(English, TEXT("蓄力 / 0.7s"), TEXT("CHARGING / 0.7s"))) : AegisUI(English, TEXT("52 伤害 / 掩体可阻挡"), TEXT("52 DAMAGE / COVER BLOCKS")), 41, 526, 11, Charge >= 1 ? Amber : Muted);
            Line(41, 545, 291, 545, Hairline);
            Text(AegisUI(English, TEXT("F  中继超频 / 35"), TEXT("F  RELAY OVERCLOCK / 35")), 41, 551, 14, White);
            FString OverclockState;
            if (Trial.wave == 3)
                OverclockState = AegisUI(English, TEXT("仅限数据中继"), TEXT("DATA RELAYS ONLY"));
            else if (Portfolio->GetOverclockRemaining() > 0)
                OverclockState = English ? FString::Printf(TEXT("x2 UPLINK / %.1fs LEFT"), Portfolio->GetOverclockRemaining())
                                         : FString::Printf(TEXT("x2 上传 / %.1f 秒剩余"), Portfolio->GetOverclockRemaining());
            else if (Portfolio->IsOverclockUsed())
                OverclockState = AegisUI(English, TEXT("本中继已使用"), TEXT("USED ON THIS RELAY"));
            else if (PlayerIn && !Operation.complete)
                OverclockState = AegisUI(English, TEXT("6 秒增幅 / 每个中继一次"), TEXT("6s BOOST / ONE USE PER RELAY"));
            else
                OverclockState = AegisUI(English, TEXT("进入当前中继后使用"), TEXT("ENTER ACTIVE RELAY TO USE"));
            Text(OverclockState, 41, 573, 11, Muted);
        }
        // Static route and team positions are known; no hidden enemy markers.
        if (!Debug)
        {
            Panel(1092, 148, 164, 176, false, Mint);
            Text(AegisUI(English, TEXT("区域路线"), TEXT("FIELD ROUTE")), 1105, 158, 12, Muted);
            auto MapPoint = [&](const FVector& World) {
                const FVector D = World - Runner->GetActorLocation();
                return FVector2D(1174 + D.X * .037f, 240 - D.Y * .037f);
            };
            const FVector2D Nodes[] = {MapPoint(Runner->GetActorLocation()+FVector(-500,-1000,0)),
                MapPoint(Runner->GetActorLocation()+FVector(850,-850,0)), MapPoint(Runner->GetActorLocation()+FVector(0,1000,0)),
                MapPoint(Runner->GetActorLocation()+FVector(-1250,0,0))};
            const int32 First = Runner->IsNorthRouteFirst() ? 2 : 1, Second = 3-First;
            Line(Nodes[0].X,Nodes[0].Y,Nodes[First].X,Nodes[First].Y,Hairline);
            Line(Nodes[First].X,Nodes[First].Y,Nodes[Second].X,Nodes[Second].Y,Hairline);
            Line(Nodes[Second].X,Nodes[Second].Y,Nodes[3].X,Nodes[3].Y,Hairline);
            for (int32 I=0;I<4;++I)
            {
                Ring(Nodes[I].X, Nodes[I].Y, 5, 1, I == 0 ? Mint : Muted, 1.25f);
                Text(FString::Printf(TEXT("0%d"), I + 1), Nodes[I].X - 7, Nodes[I].Y - 18, 8, I == 0 ? Mint : Muted);
            }
            if (SurveyEnabled)
            {
                for (int32 Node = 0; Node < 2; ++Node)
                {
                    const FVector2D P = MapPoint(Portfolio->GetSurveyLocation(Node));
                    const bool Claimed = (SurveyMask & (1 << Node)) != 0;
                    const FLinearColor Color = Claimed ? Slate : Iris;
                    Ring(P.X, P.Y, 4, 1, Color, 1.25f);
                    if (Claimed) Line(P.X - 3, P.Y + 3, P.X + 3, P.Y - 3, Color, 1);
                    Text(FString::Printf(TEXT("S%d"), Node + 1), P.X + (Node == 0 ? -20 : 7), P.Y - 5, 8, Color);
                }
            }
            if (!Operation.complete) { const auto P=MapPoint(Runner->GetObjectiveLocation()); Ring(P.X,P.Y,7,1,Amber,2); }
            if (PlayerAlive) { const auto P=MapPoint(Player->GetActorLocation()); Box(P.X-2,P.Y-2,4,4,White); }
            if (CompanionAlive) { const auto P=MapPoint(Companion->GetActorLocation()); Box(P.X-2,P.Y-2,4,4,Mint); }
            Text(AegisUI(English, TEXT("你 / 队友 / 目标"), TEXT("YOU / ALLY / TARGET")),1105,SurveyEnabled ? 284 : 302,10,Muted);
            if (SurveyEnabled)
            {
                const FString SurveyStatus = Portfolio->HasSurveyKey() ? AegisUI(English, TEXT("密钥待用 / x1.25"), TEXT("KEY READY / x1.25"))
                    : (English ? FString::Printf(TEXT("CACHE %d/2 / G + H"), SurveysClaimed)
                               : FString::Printf(TEXT("档案 %d/2 / G + H"), SurveysClaimed));
                Text(SurveyStatus, 1105, 302, 10, Iris);
            }
        }
        if (Player && Player->IsCharging()) Ring(Mouse.X, Mouse.Y, 18, Charge, Charge >= 1 ? Amber : Mint, 3);
    }

    if (!Modal && Portfolio && !Portfolio->GetFeedback().IsEmpty() && Portfolio->GetFeedbackAge() < 3.f)
    {
        const FString Feedback = Fit(PortfolioFeedback(Portfolio->GetFeedback(), English), 460, 16);
        const float W = Measure(Feedback, 16) + 30;
        Box(640-W*.5f, 551, W, 29, Navy);
        Text(Feedback, 655-W*.5f, 557, 16, Mint);
    }
    else if (!Modal && PC && Now < PC->CommandFeedbackUntil)
    {
        const FString Feedback = Fit(PortfolioFeedback(PC->CommandFeedback, English), 460, 16);
        const float W = Measure(Feedback, 16) + 30;
        Box(640-W*.5f, 551, W, 29, Navy);
        Text(Feedback, 655-W*.5f, 557, 16, White);
    }

    if (Modal)
    {
        Box(0, 0, 1280, 720, FLinearColor(.005f, .014f, .024f, .66f));
        const float X = Debug ? 28.f : 136.f;
        const float W = Debug ? 856.f : 1008.f;
        const float Y = 164.f;
        if (Menu && PC && PC->bRestartConfirmation)
        {
            Panel(X, Y, W, 300, true, Amber);
            Text(AegisUI(English, TEXT("重新开始本局？"), TEXT("RESTART THIS OPERATION?")), X+28, Y+28, 31, Ink);
            Wrap(AegisUI(English, TEXT("本局的阶段进度、能量和升级将重置。取消后仍保持暂停。"),
                TEXT("This attempt's stages, energy and upgrades will reset. Cancel keeps the game paused.")), X+29, Y+90, W-58, 20, Slate, 3);
            const float BW = (W-68)/2;
            Button(TEXT("Menu"), AegisUI(English, TEXT("ESC  取消"), TEXT("ESC  CANCEL")), X+28, Y+210, BW, 48, true);
            Button(TEXT("Restart"), AegisUI(English, TEXT("R  确认重开"), TEXT("R  RESTART")), X+40+BW, Y+210, BW, 48, false);
        }
        else if (Menu)
        {
            const float ExtraHelp = SurveyEnabled ? 29.f : 0.f;
            Panel(X, Y, W, 375 + ExtraHelp, true, Teal);
            Text(AegisUI(English, TEXT("行动暂停"), TEXT("OPERATION PAUSED")), X+28, Y+23, 31, Ink);
            Text(PC && PC->bPausedForFocus ? AegisUI(English, TEXT("已因切换窗口暂停。准备好后按 ESC 继续。"), TEXT("Paused when you left the window. Press ESC when ready."))
                : AegisUI(English, TEXT("稳住节奏，下一步由你决定。"), TEXT("Take a breath. Your next move is a choice.")), X+29, Y+68, 18, Slate);
            Line(X+28, Y+109, X+W-28, Y+109, FLinearColor(.68f, .77f, .76f));
            Wrap(V2 ? AegisUI(English,
                               TEXT("按住 RMB 0.7 秒后松开：消耗 12 能量，造成 52 伤害并穿透两名敌人。Q：35 能量打断。E：按实际治疗量消耗 8-40，未受伤时不消耗。F：35 能量让中继增幅 6 秒；争夺会暂停进度，但不会暂停计时。击杀 +12，阶段完成 +25。"),
                               TEXT("Hold RMB for 0.7s then release: 12 energy, 52 damage, pierces two hostiles. Q: 35 to disrupt. E: 8-40 based on actual healing, zero when none. F: 35 for a 6s relay boost; contest pauses progress, not its timer. Kills +12; stage clear +25."))
                     : AegisUI(English,
                               TEXT("Q 消耗 35 能量，对附近敌人造成伤害并打断。E 消耗 40，为你和附近存活队友修复。击杀 +12，每个完成阶段 +25。"),
                               TEXT("Q spends 35 energy to damage and interrupt nearby enemies. E spends 40 to repair you and a nearby living companion. Kills earn 12; each cleared stage earns 25.")),
                 X+29, Y+131, W-58, 18, Ink, 4);
            Text(AegisUI(English, TEXT("WASD 移动     左键 射击     SPACE 闪避     Z / X / C 队伍指令"), TEXT("WASD  MOVE     LMB  FIRE     SPACE  DASH     Z / X / C  TEAM COMMANDS")), X+29, Y+225, 15, Slate);
            if (SurveyEnabled)
                Text(AegisUI(English, TEXT("G 在档案旁按住 2.5 秒 / H 选择补给或密钥 / 受伤会中断，松键后可重试"),
                            TEXT("G: HOLD 2.5s AT CACHE / H: SUPPLY OR KEY / DAMAGE CANCELS; RELEASE G TO RETRY")), X+29, Y+252, 13, Teal);
            if (V2) Text(MusicMuted ? AegisUI(English, TEXT("M  音乐关闭 / 按键恢复"), TEXT("M  MUSIC OFF / PRESS TO RESTORE")) : AegisUI(English, TEXT("M  音乐开启 / 按键静音"), TEXT("M  MUSIC ON / PRESS TO MUTE")), X+29, Y+252+ExtraHelp, 14, Teal);
            const float BW = (W-80)/3;
            Button(TEXT("Menu"), AegisUI(English, TEXT("ESC  继续"), TEXT("ESC  RESUME")), X+28, Y+286+ExtraHelp, BW, 48, true);
            Button(TEXT("Restart"), AegisUI(English, TEXT("R  重新开始"), TEXT("R  RESTART")), X+40+BW, Y+286+ExtraHelp, BW, 48, false);
            Button(TEXT("Quit"), AegisUI(English, TEXT("X  退出"), TEXT("X  QUIT")), X+52+BW*2, Y+286+ExtraHelp, BW, 48, false);
        }
        else if (Briefing)
        {
            // The full illustration belongs to the briefing only. Its 3:2
            // composition remains intact, including both figures and crystal.
            Panel(X, 150, W, 438, true, Teal);
            const float ArtW = W * .328f;
            const float ArtH = ArtW * 2.f / 3.f;
            Box(X, 150, ArtW, 438, Navy);
            if (BriefingIllustration)
            {
                const float TextureAspect = static_cast<float>(BriefingIllustration->GetSizeX()) /
                    FMath::Max(1.f, static_cast<float>(BriefingIllustration->GetSizeY()));
                const float PictureH = FMath::Min(ArtH, ArtW / TextureAspect);
                const float PictureW = PictureH * TextureAspect;
                DrawTexture(BriefingIllustration.Get(), OX + (X + (ArtW-PictureW)*.5f)*S,
                    OY + (150 + (ArtH-PictureH)*.5f)*S, PictureW*S, PictureH*S,
                    0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
            }
            Box(X, 150 + ArtH - 2, ArtW, 2, Iris);
            Line(X + ArtW, 150, X + ArtW, 588, FLinearColor(.59f, .67f, .66f));
            Text(AegisUI(English, TEXT("棱光档案 / 行动 2.4 RC"), TEXT("PRISM ARCHIVE / OPERATION 2.4 RC")), X+22, 394, 11, Iris);
            Text(V2 ? AegisUI(English, TEXT("棱镜坠落"), TEXT("PRISM FALL")) : AegisUI(English, TEXT("棱镜中继"), TEXT("PRISM RELAY")), X+21, 416, English ? 27 : 31, White);
            Wrap(AegisUI(English, TEXT("穿过棱光档案，与队友完成最后一次上传。"),
                         TEXT("Explore the archive. Complete the final uplink together.")),
                 X+23, 460, ArtW-46, 14, Muted, 3);
            Line(X+22, 528, X+ArtW-22, 528, Hairline);
            Text(SurveyEnabled ? AegisUI(English, TEXT("03 阶段 / 02 档案 / 01 队伍"), TEXT("03 STAGES / 02 ARCHIVES / 01 TEAM"))
                               : AegisUI(English, TEXT("03 阶段 / 02 升级 / 01 队伍"), TEXT("03 STAGES / 02 UPGRADES / 01 TEAM")), X+23, 545, 11, Mint);
            Text(AegisUI(English, TEXT("探索 · 协作 · 撤离"), TEXT("EXPLORE · COOPERATE · EXTRACT")), X+23, 565, 10, Muted);

            const float RX = X + ArtW + 24.f;
            const float RW = W - ArtW - 48.f;
            Text(AegisUI(English, TEXT("行动简报"), TEXT("MISSION BRIEFING")), RX, 172, 27, Ink);
            Text(AegisUI(English, TEXT("共享能量，规划路线，一起完成撤离。"), TEXT("Share energy. Choose your route. Extract together.")), RX+1, 211, 14, Slate);
            const FString Titles[] = {AegisUI(English, TEXT("固守中继"), TEXT("SECURE THE RELAY")), AegisUI(English, TEXT("转移与探索"), TEXT("TRANSFER & EXPLORE")), AegisUI(English, TEXT("完成撤离"), TEXT("REACH EXTRACTION"))};
            const FString Copy[] = {
                AegisUI(English, TEXT("固守第一座中继并清场。队友在环内可加快上传，阶段结束后选择升级。"), TEXT("Secure and clear the first relay. Rally your ally for faster uploads, then choose an upgrade.")),
                SurveyEnabled ? AegisUI(English, TEXT("连接两座中继。途中靠近档案，H 选择补给或密钥，按住 G 扫描 2.5 秒。"), TEXT("Link two relays. At a cache, H selects supply or a relay key; hold G for 2.5s to scan."))
                              : AegisUI(English, TEXT("连接两座中继。每个阶段结束后选择升级，塑造下一段玩法。"), TEXT("Link two relays. Choose an upgrade after each cleared stage to shape your next fight.")),
                AegisUI(English, TEXT("清除最后的敌人。由你亲自进入撤离区，队友不能替你完成撤离。"), TEXT("Clear the final hostiles. Step into extraction yourself; your companion cannot leave for you."))};
            for (int32 I=0; I<3; ++I)
            {
                const float RowY = 246.f + I*70.f;
                Text(FString::Printf(TEXT("0%d"), I+1), RX, RowY-2, 21, I == 2 ? FLinearColor(.58f, .35f, .12f) : Teal);
                Text(Titles[I], RX+42, RowY, 16, Ink);
                Wrap(Copy[I], RX+42, RowY+24, RW-42, 14, Slate, 2);
            }
            Line(RX, 449, RX+RW, 449, FLinearColor(.70f, .79f, .77f));
            Text(V2 ? AegisUI(English, TEXT("RMB 12 蓄力 / Q 35 打断 / E 8-40 修复 / F 35 超频"), TEXT("RMB 12 CHARGE / Q 35 DISRUPT / E 8-40 REPAIR / F 35 OVERCLOCK")) : AegisUI(English, TEXT("Q 35 打断 / E 40 修复 / 击杀 +12 / 阶段 +25"), TEXT("Q 35 DISRUPT / E 40 REPAIR / KILLS +12 / STAGE +25")), RX, 460, 12, Teal);
            if (V2) Text(Runner->IsNorthRouteFirst() ? AegisUI(English, TEXT("V 路线：北侧 > 东侧 / 转移前可更改"), TEXT("V ROUTE: NORTH > EAST / CHANGE BEFORE TRANSFER")) : AegisUI(English, TEXT("V 路线：东侧 > 北侧 / 转移前可更改"), TEXT("V ROUTE: EAST > NORTH / CHANGE BEFORE TRANSFER")), RX, 480, 12, Slate);
            Text(Runner->bPressureSelected ? AegisUI(English, TEXT("行动难度 / 压力已选"), TEXT("DIFFICULTY / PRESSURE SELECTED"))
                                           : AegisUI(English, TEXT("行动难度 / 引导已选"), TEXT("DIFFICULTY / GUIDED SELECTED")), RX, 501, 10, Slate);
            const float ModeW = (RW-210.f)*.5f;
            Button(TEXT("Guided"), AegisUI(English, TEXT("1  引导"), TEXT("1  GUIDED")), RX, 518, ModeW, 40, !Runner->bPressureSelected);
            Button(TEXT("Pressure"), AegisUI(English, TEXT("2  压力"), TEXT("2  PRESSURE")), RX+ModeW+12, 518, ModeW, 40, Runner->bPressureSelected);
            Button(TEXT("Deploy"), AegisUI(English, TEXT("ENTER  开始行动"), TEXT("ENTER  DEPLOY")), RX+RW-186, 518, 186, 40, true);
            Text(V2 ? AegisUI(English, TEXT("WASD 移动 / 按住左键射击 / SPACE 闪避 / ESC 菜单 / M 音乐"), TEXT("WASD MOVE / HOLD LMB FIRE / SPACE DASH / ESC MENU / M MUSIC")) : AegisUI(English, TEXT("WASD 移动 / 按住左键射击 / SPACE 闪避 / ESC 菜单"), TEXT("WASD MOVE / HOLD LMB FIRE / SPACE DASH / ESC MENU")), RX, 572, 12, Ink);
        }
        else if (Upgrade)
        {
            Text(AegisUI(English, TEXT("阶段完成"), TEXT("STAGE CLEAR")), X, 156, 15, Mint);
            Text(AegisUI(English, TEXT("选择下一项优势"), TEXT("Choose your next advantage.")), X, 184, 31, White);
            Text(AegisUI(English, TEXT("选择一项永久升级，已安装的选项不可重复选择。"), TEXT("One permanent upgrade. Already installed options cannot be selected again.")), X+1, 229, 16, Muted);
            const float CW = (W-24)/3;
            const FString Names[] = {V2 ? AegisUI(English, TEXT("棱镜分裂"), TEXT("PRISM SPLIT")) : AegisUI(English, TEXT("穿透弹"), TEXT("PIERCING ROUNDS")), AegisUI(English, TEXT("救援脉冲"), TEXT("RESCUE PULSE")), V2 ? AegisUI(English, TEXT("中继共生"), TEXT("RELAY SYMBIOSIS")) : AegisUI(English, TEXT("生命链接"), TEXT("LIFE LINK"))};
            const FString CardTags[] = {AegisUI(English, TEXT("角度 / 压力"), TEXT("ANGLE / PRESSURE")), AegisUI(English, TEXT("时机 / 恢复"), TEXT("TIMING / RECOVERY")), V2 ? AegisUI(English, TEXT("位置 / 恢复"), TEXT("POSITION / RECOVERY")) : AegisUI(English, TEXT("伤害 / 持续"), TEXT("DAMAGE / SUSTAIN"))};
            const FString Effects[] = {
                V2 ? AegisUI(English, TEXT("基础射击最多命中两名敌人，蓄力射击最多三名。掩体和队友仍会阻挡。"), TEXT("Basic shots hit up to two hostiles. Charged shots hit up to three. Walls and friendlies still stop both.")) : AegisUI(English, TEXT("射击可穿透一名敌人并命中第二名，掩体和队友仍会阻挡。"), TEXT("Your shots can pass through one hostile and hit a second. Walls and friendlies still block the shot.")),
                AegisUI(English, TEXT("Q 保留伤害与硬直，并最多为自己恢复 12、为附近队友恢复 20。"), TEXT("Q keeps its damage and stagger, and also heals you for up to 12 and a nearby ally for up to 20.")),
                V2 ? AegisUI(English, TEXT("只有目标正在推进且未被争夺时，区域内存活队友才会持续恢复。"), TEXT("Each living ally inside the relay heals 2 HP/s only while it is making actual objective progress.")) : AegisUI(English, TEXT("持续造成伤害可为两名队友恢复实际生命值。"), TEXT("Allied damage heals both living teammates for 10% of the damage actually dealt."))};
            const FString Choices[] = {
                AegisUI(English, TEXT("把两名敌人排成一线，提升火力并保留能量用于修复。"), TEXT("Align two hostiles. Stronger basic fire saves energy for repairs.")),
                AegisUI(English, TEXT("用 35 能量换取控制与恢复，队友需在脉冲范围且视线畅通。"), TEXT("35 energy for control and recovery. Ally needs pulse range and line of sight.")),
                V2 ? AegisUI(English, TEXT("争夺或完成时不会恢复；超频会缩短恢复窗口。不能复活。"), TEXT("No healing while contested or complete. Overclock shortens recovery. No revival.")) : AegisUI(English, TEXT("持续输出换取恢复，不消耗能量；不能复活。"), TEXT("Keep dealing damage to recover. No energy cost; no revival."))};
            for (int32 I=0; I<3; ++I)
            {
                const int32 Number = I+1;
                const bool Owned = Runner->HasUpgrade(Number);
                const float CX = X + I*(CW+12);
                const bool Hover = !Owned && Hovered(CX, 273, CW, 308);
                Panel(CX, 273, CW, 308, true, Owned ? Slate : Teal);
                if (Hover) Box(CX+3, 273, CW-3, 5, Teal);
                Text(FString::Printf(TEXT("0%d"), Number), CX+19, 287, 28, Owned ? Slate : Teal);
                Text(CardTags[I], CX+65, 295, 12, Slate);
                Text(Fit(Names[I], CW-38, 23), CX+19, 330, 23, Ink);
                Wrap(Effects[I], CX+19, 371, CW-38, 17, Ink, 4);
                Line(CX+19, 462, CX+CW-19, 462, FLinearColor(.70f, .79f, .77f));
                Wrap(Choices[I], CX+19, 476, CW-38, 14, Slate, 3);
                Box(CX+19, 543, CW-38, 25, Owned ? FLinearColor(.76f, .81f, .80f) : Teal);
                Text(Owned ? AegisUI(English, TEXT("已安装"), TEXT("INSTALLED")) : (English ? FString::Printf(TEXT("%d  INSTALL UPGRADE"), Number) : FString::Printf(TEXT("%d  安装升级"), Number)), CX+29, 547, 14, Owned ? Slate : White);
                if (!Owned)
                    AddHitBox(FVector2D(OX+CX*S, OY+273*S), FVector2D(CW*S, 308*S), FName(*FString::Printf(TEXT("Upgrade%d"), Number)), true);
            }
            if (V2 && Trial.wave == 1) Text(Runner->IsNorthRouteFirst() ? AegisUI(English, TEXT("V  转移路线：北侧 > 东侧"), TEXT("V  TRANSFER ROUTE: NORTH > EAST")) : AegisUI(English, TEXT("V  转移路线：东侧 > 北侧"), TEXT("V  TRANSFER ROUTE: EAST > NORTH")), X+1, 598, 16, Mint);
        }
        else if (Finished)
        {
            const bool Won = Trial.phase == aegis::TrialPhase::Won;
            Panel(X, 152, W, 429, true, Won ? Teal : FLinearColor(.72f, .23f, .12f));
            Text(Won ? AegisUI(English, TEXT("上传完成"), TEXT("UPLINK COMPLETE")) : AegisUI(English, TEXT("行动结束"), TEXT("OPERATION ENDED")), X+28, 173, 35, Ink);
            const FString Reason = Won ? AegisUI(English, TEXT("三个阶段均已完成，行动员成功撤离。"), TEXT("All three stages secured. Operator extracted.")) :
                Trial.elapsed >= aegis::Trial::duration ? AegisUI(English, TEXT("计时结束，未能完成撤离。"), TEXT("Time expired before extraction.")) :
                PlayerAlive ? AegisUI(English, TEXT("行动在撤离前被中断。"), TEXT("Operation interrupted before extraction.")) : AegisUI(English, TEXT("行动员已失去战斗能力。"), TEXT("Operator lost before extraction."));
            Text(Reason, X+29, 222, 19, Slate);
            const auto* Stats = Portfolio ? &Portfolio->GetStatistics() : nullptr;
            const int32 Stages = Stats ? Stats->StagesRewarded : (Won ? 3 : FMath::Max(0, Trial.wave-1));
            const float CW = (W-56)/3;
            auto Metric = [&](int32 Column, int32 Row, const FString& Label, const FString& Value, const FString& Note) {
                const float MX = X+28+Column*CW, MY = 269.f+Row*108.f;
                Text(Label, MX, MY, 13, Slate);
                Text(Value, MX, MY+23, 32, Ink);
                Text(Fit(Note, CW-20, 13), MX, MY+67, 13, Slate);
            };
            Metric(0, 0, AegisUI(English, TEXT("阶段完成"), TEXT("STAGES SECURED")), FString::Printf(TEXT("%d / 3"), Stages), AegisUI(English, TEXT("固守 / 转移 / 撤离"), TEXT("Secure / transfer / extract")));
            Metric(1, 0, AegisUI(English, TEXT("队伍输出"), TEXT("ALLIED DAMAGE")), FString::Printf(TEXT("%.0f"), Runner->TrialDamageDealt), AegisUI(English, TEXT("实际造成的伤害"), TEXT("Actual damage applied")));
            Metric(2, 0, AegisUI(English, TEXT("行动员承伤"), TEXT("OPERATOR DAMAGE TAKEN")), FString::Printf(TEXT("%.0f"), Runner->TrialDamageTaken), AegisUI(English, TEXT("治疗与恢复前的承伤"), TEXT("Before healing and recovery")));
            Metric(0, 1, AegisUI(English, TEXT("用时"), TEXT("ELAPSED")), PortfolioClock(Trial.elapsed), Runner->bPressureSelected ? AegisUI(English, TEXT("压力模式"), TEXT("Pressure operation")) : AegisUI(English, TEXT("引导模式"), TEXT("Guided operation")));
            FString EnergyNote;
            if (!Stats)
                EnergyNote = AegisUI(English, TEXT("资源记录不可用"), TEXT("Resource log unavailable"));
            else if (V2)
                EnergyNote = FString::Printf(TEXT("Q%d / E%d / RMB%d / F%d"), Stats->PulsesUsed, Stats->RepairsUsed, Stats->ChargedShots, Stats->Overclocks);
            else if (English)
                EnergyNote = FString::Printf(TEXT("%d pulse / %d repair"), Stats->PulsesUsed, Stats->RepairsUsed);
            else
                EnergyNote = FString::Printf(TEXT("%d 次脉冲 / %d 次修复"), Stats->PulsesUsed, Stats->RepairsUsed);
            FString RepairValue = TEXT("--");
            if (Stats)
                RepairValue = English ? FString::Printf(TEXT("%.0f HP"), Stats->PlayerHealing + Stats->CompanionHealing)
                                      : FString::Printf(TEXT("%.0f 点生命"), Stats->PlayerHealing + Stats->CompanionHealing);
            Metric(1, 1, AegisUI(English, TEXT("能量消耗"), TEXT("ENERGY SPENT")), Stats ? FString::FromInt(Stats->EnergySpent) : TEXT("--"), EnergyNote);
            Metric(2, 1, AegisUI(English, TEXT("修复恢复"), TEXT("REPAIR RECOVERY")), RepairValue, AegisUI(English, TEXT("两名队友通过 E 实际恢复"), TEXT("Actual E healing across both allies")));
            if (SurveyEnabled && Stats)
            {
                Line(X+28, 480, X+W-28, 480, FLinearColor(.70f, .79f, .77f));
                const FString SurveySummary = English ? FString::Printf(TEXT("ARCHIVES %d/2  /  KEYS %d  /  SUPPLIES %d  /  CACHE HEALING %.0f HP"),
                    SurveysClaimed, Stats->SurveyKeys, Stats->SurveySupplies, Stats->SurveyPlayerHealing + Stats->SurveyCompanionHealing)
                    : FString::Printf(TEXT("档案 %d/2  /  中继密钥 %d  /  补给 %d  /  档案实际恢复 %.0f 点生命"),
                    SurveysClaimed, Stats->SurveyKeys, Stats->SurveySupplies, Stats->SurveyPlayerHealing + Stats->SurveyCompanionHealing);
                Text(Fit(SurveySummary, W-56, 13), X+29, 491, 13, Teal);
            }
            Button(TEXT("Restart"), AegisUI(English, TEXT("R  新行动"), TEXT("R  NEW OPERATION")), X+28, 519, W-244, 40, true);
            Button(TEXT("Quit"), AegisUI(English, TEXT("X  退出"), TEXT("X  QUIT")), X+W-204, 519, 176, 40, false);
        }
    }

    // Paint navigation after the modal scrim so the language switch stays
    // legible and available on briefing, pause, upgrade and result screens.
    Button(TEXT("Language"), English ? TEXT("L  简体中文") : TEXT("L  ENGLISH"), 930, 24, 160, 34, false, false);
    if (Modal)
    {
        Button(TEXT("WindowMode"), AegisUI(English, TEXT("F11  窗口 / 全屏"), TEXT("F11  WINDOW / FULL")), 1100, 24, 156, 34, false, false);
        Button(TEXT("Music"), MusicMuted ? AegisUI(English, TEXT("M  音乐：关"), TEXT("M  MUSIC: OFF")) : AegisUI(English, TEXT("M  音乐：开"), TEXT("M  MUSIC: ON")), 1100, 65, 156, 30, false, false);
    }
    if (Modal && PC && Now < PC->CommandFeedbackUntil && !PC->CommandFeedback.IsEmpty())
        Text(Fit(PC->CommandFeedback, 326, 11), 930, 67, 11, Muted);

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

