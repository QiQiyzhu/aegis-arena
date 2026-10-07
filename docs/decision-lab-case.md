# 队友一直选 Follow，为什么仍然没有跟上？

更新：2026-09-17。十八局场景校准和最终 Development 包中的六局开发对照已完成。**同包 baseline 为 0/3 胜，improved 为 1/3；跟随失效等待减少，但队友承伤和动作切换增加。** 冻结后的独立 36 局 holdout 也已完成，单独记录在 [评估结果](decision-lab-results.md)，不与本页开发集混算。这次只改 Follow 的位置来源，不改 Utility 权重、治疗或敌人行为。完整评估边界见 [原生评估审计](decision-lab-evaluation-audit.md)。

## 先固定有区分能力的场景

开发集在 **Editor 的真实游戏进程**中运行，为 layout 0、seed 2001–2003，baseline 与 priority，敌人数分别为 1、2、3，共十八局。没有运行 improved 来选择难度。预先约定的规则是：取最大的敌人数 E，使至少一个策略的三局胜数为 1 或 2。

| 敌人数 | Baseline 胜利 | Priority 胜利 | 开发结论 |
|---|---:|---:|---|
| 1 | 3/3 | 3/3 | 此小样本已在胜率天花板。 |
| 2 | 1/3 | 1/3 | 符合规则，选择 **E=2**。 |
| 3 | 0/3 | 0/3 | 此小样本在胜率地板。 |

[全十八局校准分析](D:/AegisWork/Reports/decision-lab-20260917/calibration-analysis.json)核对了原始文件摘要、角色事件和终止条件。十八局都有实际伤害，不存在全局从未接敌的 timeout；但这不表示每局队友都参战。E2 中 baseline 是一胜、两次 timeout；priority 是一胜、一次 timeout、一次玩家死亡，胜出的 seed 分别为 2002 和 2001。相同 1/3 胜率不能说明行为相同，也不能作为独立泛化结果。

难度已固定为 E2。后续不根据 improved 或 holdout 的成绩再选择敌人数。

## 一段真实失败链：25.2–53.3 游戏秒

案例来自 **开发版 baseline、layout 0、E2、seed 2001**。[原始 episode](D:/AegisWork/Reports/decision-lab-20260917/calibration-v1-e2-baseline/episode-000.json) · [逐事件 trace](D:/AegisWork/Reports/decision-lab-20260917/calibration-v1-e2-baseline/decision-000.jsonl) · [运行摘要](D:/AegisWork/Reports/decision-lab-20260917/calibration-v1-e2-baseline/provenance.json)。下表的时间是本局 `timeSeconds`；观测和 BT 执行另有世界时间，不把不同时间轴直接相减。

| 本局时间 | 原始记录中的事实 | 能说明什么 |
|---|---|---|
| 24.8 秒 | `allyKnown=true`，盟友距离 **2.3875 m**，选择 Follow，最近 Follow 尝试成功。 | 队友此前有合法盟友观察；最后可见距离约 2.39 m。 |
| 25.0 秒 | `allyKnown=false`；Follow 分数 0.1，其余三项为 0，仍选 Follow。最近一次 BT 记录尚为成功。 | 感知与 BT 不是同一瞬间更新，不能仅凭这一行断言立即卡住。 |
| 25.2–53.1 秒 | **136 条 decision 记录**均为 `allyKnown=false`、无可见/记忆敌人、Follow 以 0.1 原始最高分被选择；最近 `attempted=Follow` 且 `attemptAccepted=false`。 | 想跟随的决定持续存在，但执行器缺少合法跟随对象，出现长期重复失败；此段不是迟滞保留了次优动作。 |
| 53.3 秒 | 重新观察到盟友，`allyKnown=true`、距离约 1.257 m，Support 分数约 0.5924 并被选择；BT 行仍是上一次失败的 Follow。 | 盟友观察恢复后，选择自然变化；没有手工改分数。不能把这一行算成 Support 已执行。 |
| 53.5 秒 | 最近 `attempted=Support` 且成功，玩家得到后续治疗。 | 观察、选择与执行的时间关系需要分别核对。 |
| 60 秒 | timeout，仍有一名敌人存活，玩家与队友均存活。 | 这局没有完成团队清敌目标；它也不是队友死亡造成的失败。 |

Trace 中 `executed="Follow"` 会保留历史成功叶子的名字，必须同时读 `executedGameSeconds`、`attemptAccepted` 和新的 `attempted`。该失败区间的历史成功时间不更新，不能把静止的字符串当成持续成功执行。汇总 `companionActionSeconds` 则将失效状态计为 `waiting`，使用约 0.1 秒的左端采样，仍不是逐帧精确执行时间。

此局全程选择 Follow 约 **51.48 秒**，执行状态中的 waiting 约 **36.12 秒**；队友开枪 **3 次**、造成 **42** 实际伤害。玩家累计承伤 **134**，实际恢复约 **121.60**，其中队友 Support 直接记录的治疗贡献为 **88**。承伤可因治疗后再次受伤而超过初始生命；这些量解释发生了什么，不各自代表“保护得好”或“保护得差”。

## 只改一个机制：注册盟友的位置联系

失败假设很具体：Follow 已经被正确选中，执行分支却仅依赖 Sight 中的 `ObservedAlly`；视野暂时失去盟友后，它无法继续兑现跟随意图。已冻结的修复给 Decision Lab 的 companion 显式注册本局主玩家作为**同队位置联系**。只有 improved 的 Follow 分支、且当下没有 Sight 盟友时，才可用该注册对象的位置继续 `MoveToActor`，沿用超过 250 cm 发起跟随、200 cm 接受半径的现有规则。

位置联系不是任意角色查询。`SetFollowPositionLink` 只接受非 tactical companion 到另一个非 Neutral 同队角色的注册；敌人、非 companion、自己或换队后失效的对象均不能作为授权链接。对象以弱引用保存，并重复校验有效性与队伍；重新开局后重新注册。允许读取的是对象有效性、同队身份与位置，**不读取链接对象的 Health，不把它写回 `ObservedAlly`，不修改 Observation、Utility 分数或 Support 条件**。敌人的发现、记忆、攻击授权也不变。baseline 可以保留注册记录但不采样或使用链接位置，priority 保留原规则。入口见 [AI Controller](../Source/AegisArena/Private/AegisAIController.cpp) 与 [Decision Lab 注册](../Source/AegisArena/Private/AegisDecisionLab.cpp)；最终包的实际运行证据在下文单列。

因此，三种策略的地图、角色数、生命和伤害相同，但 improved 获得了明确授权的同队位置信息。这是**增加同队位置通信的执行机制实验**，不能称为相同信息预算下的纯评分算法提升。

选择持续的显式位置联系，是因为本案例最后可见盟友距离只有约 2.39 m。仅保存那个固定旧位置，可能已经接近当前导航到达范围；一次走到旧点未必能恢复之后的跟随。这是基于轨迹和现有到达阈值的**设计推断**，不是“旧位置方案已经被实测证伪”。本轮只实现和测试位置联系这一种机制。

新的 trace 在 `allyPositionLink` 下单列 `registered`、`executionSource`（`sight` / `team_position_link` / `none`）、`executionSampleGameSeconds`，以及链接位置的 `sampleGameSeconds`、`position` 和以米计的 `distance`，保留原 Sight observation。执行来源及其时间描述的是**最近一次 Follow 尝试**，不代表当前所有 BT 动作；baseline 的注册可为 true，但位置采样时间为 -1，且不输出位置或距离。这样可以核对 Follow 使用了哪一种授权来源，不把链接位置伪装成视野内信息。

## 看结果之前先写预期

本轮预期是：**在已失去 Sight 盟友、仍选择 Follow 的时段，improved 的跟随失败和 waiting 应减少，并有真实移动或到达反馈。** 这是执行可用性的预期，不预定哪一个 seed 必须获胜。现有 Follow 叶子返回成功并不单独证明寻路请求成功或已经到达，仍须核对真实位置变化和距离。

位置联系可能让队友更早回到玩家身边，也可能使其更早暴露于敌人、降低自保或改变输出。预注册比较同时保留团队胜负、玩家承伤、队友观察到的存活与死亡时刻、双方输出、治疗、动作切换和短反转。不会因为某个指标变好就隐藏其他指标，也不会把 Follow 更频繁成功等同于整队更强。

若新轨迹显示位置联系存在但移动持续被拒绝，则失败可能还在导航或执行层；若 Follow 等待减少但胜率不变，只能报告机制兑现改善。没有证据时不归因给模型理解、学习或更复杂的推理——此实验没有调用语言模型。

## 最终包的六局开发对照

六局来自**同一份独立 Development 包**：UE 5.8.2，E2、layout 0、seed 2001–2003，每局独立进程，固定 1/60 游戏步长、NullRHI。它们验证真实引擎行为，不是渲染帧率或真人体验测试。[严格分析结果](D:/AegisWork/Reports/decision-lab-20260917/final-case-analysis.json)逐目录复核了文件摘要、原生日志、事件与汇总、完整配对及一致的源码、资产、实际 payload.exe、wrapper 和引擎版本；六局均通过。绑定信息可查 [seed 2001 的 provenance](D:/AegisWork/Reports/decision-lab-20260917/final-case-2001-baseline/provenance.json)。

- 源码 SHA-256：`8193689c4841b7fe17f41956d338d9b6d1ea2df8474d3e8fab5959f989307c3e`。
- 资产 SHA-256：`4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。
- 实际游戏 payload.exe SHA-256：`7e74169799b6ade5ca8803aeea831c6e251c1c32679b3ba205ead7ec8143ed0e`。

开发原始十八局来自较早源码的 Editor 游戏进程，保持独立，仅用于选场景和定位失败，没有与最终包结果合并。最终包 baseline 的胜数与早期校准不同，不能用旧的 1/3 替代本次实际的 0/3。UE 相同项目 seed 不保证调度和导航轨迹位级一致；本次机制比较只使用同一个最终包内的配对结果。

| Seed | Baseline 结果 | Improved 结果 | 玩家承伤（前→后） | 队友伤害输出（前→后） | Waiting 秒（前→后） |
|---|---|---|---:|---:|---:|
| 2001 | 60 秒 timeout，剩 1 敌 | **24.70 秒清敌** | 134→56 | 42→84 | 36.12→0.12 |
| 2002 | 34.40 秒结束，玩家于 34.32 秒死亡，剩 1 敌 | 60 秒 timeout，剩 1 敌 | **122→126** | 42→70 | 10.52→0.12 |
| 2003 | 60 秒 timeout，剩 1 敌 | 60 秒 timeout，剩 1 敌 | 112→70 | **86→70** | 23.50→0.12 |

下面的连续量均为三局均值，并非按每秒归一化。不同终止时刻会改变伤害、治疗和存活观察的暴露时长。

| 指标 | Baseline | Improved |
|---|---:|---:|
| 团队胜利 | 0/3 | 1/3 |
| 玩家死亡 / timeout | 1 / 2 | 0 / 2 |
| 队友终局存活 | 3/3 | 3/3 |
| 玩家累计承伤 | 122.67 | 84.00 |
| 队友累计承伤 | 23.33 | **65.33** |
| 玩家伤害输出 | 122.67 | **105.33** |
| 队友伤害输出 | 56.67 | 74.67 |
| 队友开枪次数 | 4.33 | 5.33 |
| 玩家实际恢复 | 80.93 | 55.57 |
| 队友 Support 直接治疗量 | 66.00 | **36.67** |
| 执行失效 waiting 秒 | 23.38 | 0.12 |
| 选择 Follow 的驻留秒 | 39.72 | 19.22 |
| 动作切换次数 | 9.00 | **12.00** |
| 一秒内短反转次数 | 2.33 | 1.00 |
| 队友观察到的存活秒 | 51.47 | 48.23 |

队友两组都没有死亡，improved 的平均存活观察更短主要受 seed 2001 提前清敌结束影响，不能称为生存变差。相反，队友承伤增加、玩家输出下降和切换次数增加都是实际观察，不能隐藏。seed 2003 的队友输出下降、切换从 12 增至 22；seed 2002 玩家总承伤增加 4，但存活到了 60 秒。治疗总量下降也混合了伤害需求和局长变化，不能单独判断治疗能力。

## 执行链是否真的改变了

最终包 [baseline 2001 trace](D:/AegisWork/Reports/decision-lab-20260917/final-case-2001-baseline/decision-000.jsonl)复现了原始失败窗口。25.2–53.1 秒的 136 条记录仍然选择 Follow、缺少 Sight 盟友、最近 Follow 尝试失败；新增 `selfPosition` 在所有行都为 `(552.5033, 377.7979, 90.15)` cm，`navigationStatus=0`（Idle）。因此这段停滞同时有执行反馈和真实位置证据，不只是显示字符串没有变化。

同包 [improved 2001 trace](D:/AegisWork/Reports/decision-lab-20260917/final-case-2001-improved/decision-000.jsonl)在 24.70 秒已清敌。第一次 Follow 在首轮 Sight 观测之前就使用位置联系，第一条 decision 已记录移动；轨迹从开局便分叉。**这支持位置联系会改变真实行为，但不能把获胜完全归因于消除了后来那段 25.2–53.3 秒窗口。** 两局不再拥有相同的后续战斗历史。

为避免把 BT 成功误认成移动，再核对 [improved 2003 trace](D:/AegisWork/Reports/decision-lab-20260917/final-case-2003-improved/decision-000.jsonl) 中一个未获胜案例。下列每行均为 `allyKnown=false`、选择 Follow、最近 Follow 成功且来源 `team_position_link`：

| 本局时间 | 队友 XY，cm | 导航状态 | 链接距离，m |
|---|---|---|---:|
| 1.5 秒 | (-1378.089, 177.667) | Idle | 3.2282 |
| 1.7 秒 | (-1407.344, 188.764) | Moving | 3.6579 |
| 1.9 秒 | (-1484.965, 218.394) | Moving | 2.8798 |
| 2.1 秒 | (-1498.002, 223.496) | Idle | 1.8479 |

队友实际净位移 **128.37 cm**，并进入接近范围；视野内盟友仍未知。这为链接提供了实际移动证据。链接距离按自己的采样时间记录，不能与 decision 时刻的位置当作完全同步测量；距离变化也同时包含玩家移动。另一个 [improved 2002 trace](D:/AegisWork/Reports/decision-lab-20260917/final-case-2002-improved/decision-000.jsonl) 在 8.6–9.2 秒保持同样的未知盟友/Follow/link 条件，队友净移动 **197.73 cm**，同样不是只有成功标记。

三局所有“盟友未知且选择 Follow”的 decision 快照中，最近 Follow 失败行分别从 baseline 的 **167、43、90** 降为 improved 的 **0、0、0**。这是快照计数，可能多次记录同一个 BT 尝试，且两组时长与情境数量不同，不能当成 300 次独立试验。improved 2001 的 2–3 秒也有链接 Follow 成功但自身未动的记录，当时距离已在约 0.68–1.75 m 内：**Idle 本身不等于失败，accepted 本身也不等于移动。**

这六局支持“位置联系减少 Follow 执行失效，并出现真实跟随移动”的开发预期；它们仍是开发对照，不能宣称未见数据上的效果或总体优越。随后已冻结源码、最终包 payload.exe、协议、E2 和执行顺序，使用同一最终包完成 layout 0 seeds **9001–9006**、layout 1 seeds **9101–9106**，跨 baseline/improved/priority 共 **36 局** holdout。每个 seed 轮换策略顺序，每局独立启动进程，所有单元均保留。layout 1 只是同一地图内预定出生通道旋转，不是跨地图泛化。

**Holdout 结果已全量核对，见 [独立结果报告](decision-lab-results.md)。** 它保留了布局差异和反例，没有用开发胜率替代独立结果。旧 [v1.0 两策略各 0/30](native-evaluation.md) 继续展示，它与当前 Decision Lab 的场景、源码和协议不同，不与新数据混算。
