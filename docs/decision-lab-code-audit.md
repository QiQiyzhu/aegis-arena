# Decision Lab 代码审计（2026-09-17）

本次先核对真实调用链，再添加被动 telemetry；没有调整效用权重、迟滞、攻击、治疗、导航或敌人参数。本文中的失败机制是待原生实验检验的假说，不是已证实的 0/30 失败原因。没有在此审计中启动 Unreal 或模型。

## 实际代码与两条运行路径

项目没有 `core/include/aegis/decision.hpp`，纯规则实现位于 [rules.hpp](../core/include/aegis/rules.hpp)：`Observation`（94 行）、`utility`（100 行）、`choose`（108 行）。`UAegisDecisionComponent` 的实现也不在单独的 decision-component 文件，而在 [AegisAIController.cpp](../Source/AegisArena/Private/AegisAIController.cpp) 的 `Evaluate`（32 行）。下述行号对应本次添加 telemetry 后的文件，后续编辑可能移动行号。

| 路径 | 实际决策和执行 |
|---|---|
| 既有 `StartInteractive` / objective trial | `SpawnConfiguredBot` 设置 `bTacticalTrial = bInteractive && bObjectiveTrial`；`ExecuteAction` 直接调用 `ExecuteTacticalTrial`，忽略传入的 BT action。Utility 分数仍会计算，但不能用其选中动作解释战术 Guard/Focus/Rally 的实际行为。 |
| 既有 `StartBatch → StartEpisode` | `bInteractive=false`，真人 pawn 被移除，生成两个友方 AI 加敌人。角色 0 是 scripted player，角色 1 才是 companion。所有角色使用真实 perception、BT、导航及 EQS。 |
| 新独立 Decision Lab（根代理接线） | 应固定 `bTacticalTrial=false`，手动玩家与 scripted-player 对照共用同一 spawn/config；不把已有 v1.3 objective/copilot 战斗混入比较。此项是集成约束，不是本审计已经跑通的结果。 |

既有批量链见 [AegisLab.cpp](../Source/AegisArena/Private/AegisLab.cpp)：`StartEpisode`（309 行）、`SpawnConfiguredBot`（372 行）、`Sample`（704 行）。普通敌人、精英及 scripted player 的行为分支不能简单统称为“同一个 Utility AI”：只有 `IsCompanion` 分支受 `UtilityAction` 授权；其他角色走 BT priority 分支。`Evaluate` 中名为 priority 的同伴策略仅为“可见则 Attack，否则 Follow”，不会主动选择 Support 或 Retreat。

实际 BT 资产创建逻辑在 [AegisEditorLibrary.cpp](../Source/AegisArenaEditor/Private/AegisEditorLibrary.cpp) 175–230 行。服务每 0.2 秒构造观察，根 sequence 在动作后等待 0.2 秒。Companion selector 的顺序是 Recover、Retreat、Support、Attack、Chase、Follow；例如 `Selected=Retreat` 也不保证 Retreat 获执行，条件不满足或任务失败时仍会往下回退。Enemy selector 是 Retreat、Recover、FindCover、Attack、AttackPosition、Investigate、Patrol。资产生成源码说明预期结构；本次没有打开编辑器重新验证当前二进制资产结构。

## 数据权限和可解释性边界

- `RefreshDecision`（controller 272 行）只枚举当前 sight 感知集合，敌人位置和血量读取受该集合约束；选择最近可见敌人。Batch 的 allyKnown 同样要求 ally 当前可见。只有战术 interactive companion 额外拥有显式 allied radio link。
- `RememberTarget`（177 行）记录合法刺激位置；`PerceptionChanged`（258 行）允许 damage 刺激提供历史位置，但不授予 sight。记忆持续 2.5 游戏秒，新的合法刺激或可见观察会刷新到期时间。无可见目标时，Observation 的 targetDistance 为 1000，不能把它当作真实敌人距离；导航仍可使用尚未到期的 LastKnown。
- `HasMemory` 和 `HasLOS` 是不同权限。Attack 在实际执行时再次检查 active sight；EQS threat context（992 行）只输出 LastKnown，不读取隐藏目标实时 transform。
- EQS 的导航与几何测试不是额外敌人感知。生成器在 querier 周围取网格，先过滤可达路径，再根据 threat 的历史点做 trace 和距离评分。Retreat 的遮挡测试只是 **评分**，不是硬过滤；成功结果不保证遮蔽、脱战或治疗机会。见 `Query`（editor library 87–131 行）。
- `RequestTacticalPoint`（controller 361 行）限制为单个在途查询并有 1 秒预算；`QueryFinished`（404 行）检查身份、存活、记忆与结果，再调用 MoveToLocation。查询成功、接受 move 请求、真实到达、持续占据掩体是四件不同的事。

## 首选单一失败假说：撤退切断输出，但恢复条件迟迟不成立

建议先复现 **Utility 低血撤退期间无法恢复为有效支援**，而不是先改多个分数。

1. 可见敌人时 `Attack = 0.45 + 0.3h`，`Retreat = 1.25(1-h)`。忽略其他动作竞争，从 Attack 转入 Retreat 需约 `h ≤ 0.4645`；由于 0.08 迟滞，从 Retreat 回到 Attack 需约 `h ≥ 0.5677`。这是直接按当前公式推导的条件，不是统计结果。
2. BT Recover 要求 `UtilityAction=Retreat && NeedsRecovery`；`NeedsRecovery` 又要求 `h<0.65 && !HasMemory`（controller 351 行）。持续可见或新的伤害刺激会维持记忆，因此挡住自愈入口。
3. `ExecuteAction(Retreat)`（871 行起的执行函数）只提交/维持 EQS 和移动，不开火、不治疗队友。即使到达查询点，只要仍在查询预算内，也可继续返回成功。Retreat 的 EQS 只偏好遮挡，因此可能无法消除维持记忆的接触。
4. 这会形成可证伪的反馈链：低血进入 Retreat → 无输出且不能 Recover → 血量无法跨过返回 Attack 的门槛。若合法感知中 ally 足够危险，Support 仍可能赢得竞争；因此不能宣称所有低血角色都锁死，必须用实际轨迹验证。

建议诊断窗口：真实受伤后连续至少 3 游戏秒 `AcceptedBTLeaf=Retreat`、低血、memory 持续为真，且该窗口没有己方输出/实际 support heal。检查查询是否接受、角色是否实际移动、是否失去 sight 后仍因 memory 保持而延迟 Recover。若窗口主要为 Support/Attack，或很快脱战恢复，应拒绝此假说并保留失败记录。该阈值是诊断分段规则，不是新的游戏参数。

旁支现象先只记录：Support 的分数不含接近队友所需时间，实际治疗却必须靠近到 250cm，并有 5 秒冷却；`Selected=Support` 或 BT Support 返回成功都可能只是移动，没有真正回血。Batch 又可能因视野方向失去 allyKnown。它们可能解释“支援过晚”，但不能在没有轨迹时与撤退机制一起修改。

## 旧 0/30 能说明什么

[原生评测](native-evaluation.md) 的 priority、utility 各 30 个 paired seeds 都为 0 胜。Utility 的结算时同伴死亡从 25 降至 6，但平均团队输出从 170.46 降至 139.40，玩家承伤从 106.68 升至 126.06。此模式与“自保但未保护团队”相容，却不足以证明上述撤退链。

例如 seed 1024：priority 团队输出 284、32 秒结束、同伴死亡；utility 输出 156、25 秒结束、同伴仍活着且玩家承伤 122。原始报告没有动作驻留、治疗事件、死亡时间及具体终止原因；damageTaken 可因治疗超过起始血量。双方 0 胜还可能是困难场景的胜率下限，不能仅据此认定 Utility 无效。

旧记录源摘要为 `58eeb1037ce8338801598d8f9eaa47fec6cd41003cd20bef665d72206fb894e9`，内容摘要为 `91547daf2b2a7e81c88d18d0e71cabf738018f7b74f942b8ff0443443ddc74c2`。当时四敌包含一个 160HP、20 ranged damage 的精英；`AAegisAICharacter::BeginPlay` 仍可见该设定。当前代码还含后来修复的 possession 后 perception-listener 更新、记忆及 EQS cancellation 改动。不能把旧快照的 60 局和本次新 runner 的普通敌人、默认 100HP/14 damage 配置混为同一实验，亦不能把 NullRHI 结果当渲染性能或真人体验证据。

## 本次新增 telemetry 的精确含义

只读 API：`AAegisAIController::GetDecisionTelemetry()` 返回 `const FAegisDecisionTelemetry&`，声明在 [AegisAIController.h](../Source/AegisArena/Public/AegisAIController.h)。

| 字段 | 来源与限制 |
|---|---|
| Observation / Scores / Previous / RawWinner / Selected | 复制真正送入策略的观察和真正算出的分数；不做额外 actor 扫描。RawWinner 保留 max_element 首项平分顺序。 |
| DecisionSequence / GameSeconds / SelectionReason | 决策序号与游戏时间；原因是 priority、hysteresis 或 raw_winner。第一条决策不计作切换。 |
| SwitchCount / ShortReversalCount | 后续 Selected 改变次数；短反转定义为连续 A→B→A，两次切换间隔不超过 1 游戏秒。不是所有抖动的完整指标。 |
| BTLeaf / BTLeafSucceeded / BTExecutedAt | 最近实际尝试的 BT 任务及返回结果，不把失败尝试计为成功执行。 |
| AcceptedBTLeaf / AcceptedBTLeafAt | 最近返回 true 的任务及最近接受时间；一般每 0.2 秒更新。此时间用于新鲜度，不是动作开始时间，不等于命中、抵达或实际治疗。 |
| QueryId / QueryDecisionSequence / QuerySubmittedBTLeaf | 查询身份、提交时对应决策和任务。异步查询应关联这组字段，不能归因于完成时刚好选中的动作。 |
| QueryStatus / QueryReturnedItems | pending、accepted、move_rejected、failed、failed_empty、state_rejected、aborted、cancelled 等状态；SingleResult 返回项数不能冒充全部候选数。 |
| QueryGeneratedItems / QueryValidItems | 无候选调试数据时为 -1；只在现有 `-AegisQueryDiagnostics` 前 50 个查询保留的数据中填写。不能显示成“0 个候选”，也不能重算假分数。 |
| QueryAcceptedPoint / QueryAcceptedAt / bQueryMoveAccepted | 最近查询选中点、接收时间和当时导航请求是否接受；是带时间的历史事件，不证明当前仍在该点或已完成导航。 |
| SupportHealCount / SupportHealAmount / LastSupportHealAt | 只在 Health->Heal 的实际返回量大于 0 时增加；既不把意图当治疗，也不把满血施法计为有效支援。 |
| bImprovedPolicy | 默认 false，目前只记录配置，不启用任何策略变化。 |

Telemetry 的决策记录放在原有 DecisionCpuSeconds 计量之后；EQS 候选保留仍需显式开启，因为它改变内存和查询成本。新旧代码总耗时不能仅凭这个安排宣称完全可比。根 runner 应基于带时间的样本积分 accepted-action 驻留，另记真实伤害/治疗/死亡/终止事件，不能从一次快照推算整个 episode。

下一步只跑冻结代码的 baseline，先确认或否定单机制；随后只对已复现机制做一个改动，在同配置、配对 seeds 和独立保留 seeds 上比较。保留原困难场景作为压力对照，不根据少数胜负选择性删局；每批结果绑定自己的源码、内容、参数与终止条件。本文没有声称新 baseline、改进版或任何新原生测试已经通过。
