# Aegis Arena v2 Guard 阵位案例

2026-09-18。**最终同源码 Guard 夹具通过；两轮各六局包体观察完整保留。局部机制有效，完整行动存在明确取舍，没有证据支持整体策略或真人胜率提升。** 本文区分受控机制、脚本输入完整行动和真人体验，不把历史版本数据混入 v2。

## 问题与单一变化

需要检验的问题是：Guard 首个阵位虽然可达，武器射线却被低障碍挡住时，队友会不会忽略另一侧可达且能开火的阵位。

旧逻辑接受第一个可达阵位后返回；新逻辑仅在队友自身 Sight 确认当前目标时，额外要求候选点到目标的武器射线首先命中该目标，否则继续尝试另一侧。路径仍需完整、仍避开玩家火线，不瞬移，不增加隐藏敌人信息，不改伤害或敌人规则。开关 `-AegisGuardLegacy` 只关闭该筛选。源码入口为 [RepositionForTarget 和 MoveTactically](../Source/AegisArena/Private/AegisAIController.cpp)。

BT 仍承担观察与任务调度，Utility 评分仍会计算并写黑板；实际 `ExecuteAction` 在战术模式统一进入 `ExecuteTacticalTrial`，绕开历史 Action 分支及 EQS 请求。本轮不能称为 Utility 算法胜过旧 BT，也不能把 `DebugState` 当成 `Decision->Selected`。

## 已保留的开发失败

| 记录 | 原始事实 | 可以得出的结论 |
|---|---|---|
| [guard-baseline-01](D:/AegisWork/Reports/v2-20260918/guard-baseline-01/provenance.json) | `both_formation_slots_have_complete_paths` 失败，`baselineDefectObserved=false`；0 枪、0 位移 | 夹具路径前置条件未成立，不能把 0 枪当作已经复现 AI 缺陷 |
| [control-editor-01](D:/AegisWork/Reports/v2-20260918/control-editor-01/provenance.json) | 蓄能付款 12、开枪 1 次通过；两人各 52 的断言失败，原始账本只记录一名敌人受伤 52，整轮失败 | 发射接受不等于两目标命中；仅凭失败断言不能认定穿透机制损坏 |
| [control-editor-02](D:/AegisWork/Reports/v2-20260918/control-editor-02/provenance.json) | 新目录复测 36 个控制断言通过，源码 `c39113838a9a…`；显式放置角色、施加伤害并冻结 AI | 这是开发阶段受控输入／交易证据，不是自然战斗或最终包结论 |
| [guard-baseline-02](D:/AegisWork/Reports/v2-20260918/guard-baseline-02/provenance.json) | 12 个原生断言通过，记录旧机制 0 枪、0 位移；外层 gate 因未接受的引擎错误仍失败 | 局部现象已有记录，但整轮严格验收未通过，必须保留失败状态并继续定位 |
| [guard-baseline-03](D:/AegisWork/Reports/v2-20260918/guard-baseline-03/provenance.json) | 同时通过 12 个原生断言、Automation 1/1 和严格 wrapper；实际观察窗口内 0 枪、位移 0 cm，`baselineDefectObserved=true` | 在这个已满足导航与视线条件的局部夹具中，旧机制缺陷已复现；不能推广为所有 Guard 场景都失效 |
| [guard-improved-01](D:/AegisWork/Reports/v2-20260918/guard-improved-01/provenance.json) | 前六个几何／感知断言通过，18 秒在 stage 2 超时；23 次筛选、0 次另侧选择、0 枪，整轮失败 | 新机制当时尚未产生所要求的动作。`movementCm=0` 在此版本 timeout 路径为未更新的默认值，不能据此证明全程静止 |
| [guard-improved-03](D:/AegisWork/Reports/v2-20260918/guard-improved-03/provenance.json) | 原生 12 项通过、约 380.265 cm 位移、1 枪，但两条 Zen 旧进程锁警告使严格 wrapper 失败 | 原生行为成功不能覆盖外层失败；没有放宽警告，最终 improved04 在新缓存根重跑 |

`guard-baseline-01`／`control-editor-01` 使用源码 `d43fbfc884af…`，两个 `-02` 记录使用 `c39113838a9a…`；`guard-baseline-03` 与 `guard-improved-01` 则同为 `8a106cc7b167…`。历史 r1 对照 baseline04／improved02 使用 `df57a548…`，保留于[原独立复核](D:/AegisWork/Reports/v2-20260918/guard-case-review.json)；下文最终对照为 r3 的 baseline05／improved04，使用 `6dad1952…`。不能把全部开发记录说成同一冻结快照，新机制失败也不能因后来成功而移除。

`guard-baseline-02` 的 13 条启动期 `Condition failed` 不能豁免。UE 源码的 `UnifiedErrorTests.cpp` 中 `FUnifiedErrorTest_CreateErrorMessage`／`CreateErrorMessageWithContext` 包含本地化文本与英文硬编码的比较；当次日志实际输出中文“空错误”，而 `LowLevelTestAdapter.h` 把 CHECK 失败统一记录为这句消息。只在新运行命令加 `-culture=en`，03 才通过原有严格错误 gate。日志没有逐条调用栈，不能声称已将 13 条各自精确归因。

`guard-improved-01` 暴露的是**候选资格射线与实际枪口的高度模型不同**。最终原生诊断确认：NavMesh 投影 Z=10，实际胶囊半高 88，旧资格枪口为 Z=128；地面原始点直接加半高与枪口偏移得到 Z=118，而真实枪口是 Z=120.150。低掩体顶部 Z=125，过高的资格射线会误判首侧可开火。早期 Z=130 是按半高 90 的估算，已由原生记录的 128 更正，不作为实测值保留。

修正后的 `GetGuardCandidateMuzzle` 对候选点下方做物理地面射线，要求可行走地面，再加胶囊半高、当前行走胶囊与地面的间隙和真实武器的 +30 cm 偏移。该夹具中得到 **Z=120.150，与真实枪口一致**；资格测试不再把 Recast 导航面当作物理站立面。修正仍只用于 opt-in Guard 筛选，不改掩体、伤害、敌人、Focus 或 legacy 分支；夹具前置检查也使用同一物理高度方法。

历史 [EQS 查询失败与修正](eqs-debugging.md)、[旧两策略各 0/30](native-evaluation.md)、[v1.4 的改进与负例](decision-lab-results.md)、[v1.5 录制器失败及撤回的猜测](portfolio-recording-case.md)全部沿原路径保留，版本、场景和指标不混池。后续每次失败也必须新增目录进入最终索引，不能补跑一个成功结果替换原局。

## 同构建局部机制证据

最终对照为 [guard-baseline-05](D:/AegisWork/Reports/v2-20260918/guard-baseline-05/provenance.json) 与 [guard-improved-04](D:/AegisWork/Reports/v2-20260918/guard-improved-04/provenance.json)。两者均通过严格 wrapper，各自 Automation 为 1 成功、0 失败、0 警告，12 个原生断言全部通过；每次运行前后源码、内容与记录的运行模块哈希均保持一致。独立只读复核重新执行原严格 validator，检查所有原件哈希、几何与包装脚本，记录于 [guard-case-review-r3.json](D:/AegisWork/Reports/v2-20260918/guard-case-review-r3.json)。

- 源码：`6dad19526dbc6b093a841ccf46f040e48148e37700d3e5a2184d6f66d42b3cc2`。
- 内容：`9667582db87935a68d8823eca0801b1c47ade897f12a1c848f926a4e02e88fd5`。
- Editor 运行模块 `UnrealEditor-AegisArena.dll`：`f62b695ef0e44401b86466f49717b6f2290cdd93dc3a0b2185784f1e803f1217`。

二进制绑定范围也需限定：逐次 provenance 记录 Editor launcher 与上述 runtime DLL，未逐次保存 fixture 所在 Editor DLL 的前后哈希，不能补写成历史绑定。improved04 使用新 `guard-r3-clean` 缓存根，避免旧 Zen 锁；源码、记录的模块、几何和游戏命令一致，但不能声称操作系统／缓存环境完全相同。没有豁免 improved03 的警告。

| 同场景检查 | Legacy | Improved |
|---|---|---|
| 自身 Sight 有效、两侧完整路径、首侧挡枪／另侧通畅 | 全部成立 | 全部成立 |
| 新资格检查／拒绝／另侧选择次数 | 0／0／0 | 4／2／2 |
| 起终点实际位移 | 0 cm，位置未变 | 380.264013 cm，向另一侧 |
| 正常前摇／实际射击次数 | 未观察到／0 | 已观察到／1 |
| 遮挡后隐藏目标不触发筛选或开火 | 两项通过 | 两项通过 |
| 原生断言、Automation 与严格 wrapper | 全部通过 | 全部通过 |

两者起点均为 `(-180,240,90.150)`；improved 的最终位置为 `(-150.863,-139.146,90.150)`，`movementMeasured=true`。380.264 cm 是夹具 Complete 时、包含后续遮挡阶段的起终点平面净位移，不是累计路径长度，也不是首次射击时的精确位移。交战阶段单独通过“位移至少 120 cm，Y 向另一侧超过 120 cm”的断言，然后观察到真实前摇和射击；该阶段未保存精确位移值。最终 `slotReason=team_formation_without_visible_target` 描述遮挡后的状态，不能覆盖此前的另侧选择事件。

夹具限制：这是 NullRHI 的受控 PIE 世界，没有渲染画面；固定领队和初始目标，准备时暂停脑逻辑，伤害关闭，随后真实导航、前摇与射击执行。加入不透明掩体后还明确移动隐藏目标以验证权限边界。baseline 在 3 秒交战观察窗口结束检查；improved 等首次射击再检查，本次发生在总游戏时间约 4.05 秒，因此不能从 0／1 枪推算同暴露时间射速或伤害提升。每种模式只有一次最终局部复测，支持这个阵位机制修正，不支持一般胜率结论。

## 两轮完整行动协议与输出口径

开发种子 1101 用于校准输入与场景，不进入独立观察。第一轮预先冻结 **2203、3307、4409**，每个种子执行 legacy 与 improved，共六局。其开发证据为 1920×1080，但评估命令使用 1280×720；驾驶器的固定像素筛选范围会随分辨率产生不同的归一化范围。这是执行配置不一致，第一轮全部原件保留。

随后回到 1101 检查执行配置，第二轮运行前另行冻结从未用于前轮观察的 **5501、6607、7703**，统一使用已校准的 1920×1080。新旧两轮的源码、资产和包体完全相同，只修正评估启动分辨率并使用新种子，没有修改 AI、驾驶器或战斗参数。第二轮冻结记录绑定第一轮 evaluation 的 SHA-256，并明确排除已暴露的旧种子；不是从旧结果中挑选成功种子。

- [第一轮冻结](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-01/freeze.json)、[全部六局结果](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-01/evaluation.json)、[逐来源输出分析](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-01-output-analysis.json)。
- [第二轮冻结](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-02/freeze.json)、[全部六局结果](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-02/evaluation.json)、[逐来源输出分析](D:/AegisWork/Reports/v2-20260918/evaluation-frozen-02-output-analysis.json)。
- 两轮源码与内容均为上文的 `6dad1952…` / `9667582d…`；r3 游戏载荷 SHA-256 为 `b1fc5c4fb2dca50f3e5bc937a20e83669827d3550769b679e489061eb5bcb87c`，启动器与全部 cooked 容器绑定见各自 freeze。

每轮六局进程和严格 gate 全部通过，所有自然 Won／Lost 均保留。使用相同 30 Hz 固定游戏步长，仍渲染但不保存全部截图；不代表实时渲染性能。输入走正常玩家控制，不冻结 AI、不注入伤害、不强制推进或传送；同种子不保证 UE 调度和物理位级确定性。两轮不能合并成一个同配置样本或总体胜率估计。

“输出”指敌方实际损失的 HP，含普通射击、脉冲和蓄能，致死一击只计剩余生命，不计过量伤害。C++ `AAegisPortfolio::OnDamage` 只有在来源为本局 Player 或 Companion 且受害者为 Enemy 时才写出对应 `enemyVictim=true` 事件。[分析脚本](../scripts/analyze_v2_output.py) 在绑定源码契约和原始 trace 哈希后，按精确原生角色身份分解并与原总额对账；陌生来源记为 unknown，不重新分摊。两轮实际 unknown 均为 0。射击次数不等于伤害，任务秒输出率含赶路和占点，也不等于纯战斗 DPS。

## 第一轮六局保留的负例

下表所有箭头均为 legacy → improved；两策略的原始结果均保留，数值按实际报告四舍五入。

| Seed | 结果／任务秒 | 玩家实际输出 | 队友实际输出 | 玩家承伤 | 队友承伤／存活 |
|---|---|---|---|---|---|
| 2203 | Lost 180.0 → Lost 180.0 | 204 → 204 | 14 → 14 | 0 → 0 | 0／活 → 0／活 |
| 3307 | Lost 180.0 → Lost 180.0 | 204 → 204 | 14 → 14 | 0 → 0 | 0／活 → 0／活 |
| 4409 | Lost 97.7 → Won 94.1 | 1030 → 1180 | 158 → 80 | 228 → 28 | 120／倒下 → 14／活 |

这轮原始数量为 legacy 0/3 Won、improved 1/3 Won。2203／3307 两策略均未完成第一个阶段，团队实际输出只有 218，存在明显的驾驶器执行覆盖不足。2203 独立核对发现：`video11.333–183.933` 的 1727 条 Active 样本沿 Cover D 北侧保持 Y=`-425.967`，X 只在约 `-828.579` 至 `-783.944` 间往返；路径完整，索引反复在 1／2 间切换，实际与记住的按键一致。它不是空路径停发输入，而是碰撞边缘的跟点／切角问题。样本没有保存全部路径点和目标筛选原因，不能进一步把它完全归因于分辨率。

**旧种子的卡角尚未修复。** 第二轮只统一执行分辨率，没有修改该驾驶行为，也没有在旧种子上证明消除卡角。第一轮不能因为第二轮通过而删除，第二轮顺利完成也不能被写成“分辨率修复了导航”。另一个[清敌后空路径停滞的录制工具案例](portfolio-v2-recording-case.md)使用有限胶囊恢复，触发条件和本处完整路径卡角不同，不能混为同一故障。

4409 有实际战斗区分：legacy 玩家生命降至 0、队友也倒下，仅完成 2 阶段；improved 完成 3 阶段且存活。但赢局的队友实际输出反而少 **78 HP**。这一例支持结果存在变化，不能用一次获胜证明队友输出提高或总体策略更强。

## 第二轮同分辨率的完整结果

第二轮六局均 Won、完成 3 阶段、队友存活；每局清敌后团队总实际输出都是 1260，所以总量本身没有区分度。以下仍按 legacy → improved 展示全部三对：

| Seed | 任务秒 | 玩家实际输出 | 队友实际输出 | 队友输出份额 | 玩家承伤 | 队友承伤 |
|---|---|---|---|---|---|---|
| 5501 | 90.9 → 100.9 | 1162 → 1124 | 98 → 136 | 7.78% → 10.79% | 62 → 76 | 70 → 70 |
| 6607 | 92.2 → 92.2 | 1204 → 1204 | 56 → 56 | 4.44% → 4.44% | 118 → 118 | 20 → 20 |
| 7703 | 87.1 → 86.7 | 1176 → 1152 | 84 → 108 | 6.67% → 8.57% | 0 → 104 | 14 → 56 |

- 5501：队友多输出 **38 HP**，但整局慢 **10.0 秒**，玩家多承伤 **14 HP**。
- 6607：两者在这些指标上相同。improved 仍记录过资格检查和另侧选择，不能据汇总一致推断机制完全没有执行。
- 7703：队友多输出 **24 HP**，只快 **0.4 秒**，同时玩家多承伤 **104 HP**、队友多承伤 **42 HP**。这不是全面改善。

第二轮只显示这三个脚本输入种子的实际取舍；双方都 3/3 Won，不构成胜率提升证据。配对差统一为 improved−legacy，输出、耗时和承伤分别解释，不合成一个掩盖代价的总分。不能单凭汇总数字认定承伤变化具体由某一次换位造成，若要解释个别事件还需追踪对应时间段。

动作稳定性的原始计数如下，同样为 legacy → improved；前后两批分开标注，不合并求平均。数值来自各轮 evaluation 原件。

| 批次／Seed | 状态切换次数 | 小于 1 秒的 A→B→A 回返 | 移动请求次数 |
|---|---|---|---|
| 第一轮／2203 | 33 → 33 | 13 → 13 | 10 → 11 |
| 第一轮／3307 | 32 → 32 | 13 → 13 | 10 → 10 |
| 第一轮／4409 | 81 → 72 | 11 → 10 | 65 → 67 |
| 第二轮／5501 | 91 → 78 | 19 → 11 | 67 → 80 |
| 第二轮／6607 | 47 → 47 | 9 → 9 | 68 → 68 |
| 第二轮／7703 | 72 → 65 | 9 → 8 | 68 → 75 |

`companionStateChanges` 与短回返包括正常瞄准／开火／重新定位，不能称作 Utility 抖动。5501 的切换与短回返减少，但移动请求更多、任务更慢；7703 的切换减少也没有转化为更低承伤。移动请求被接受不代表实际走完，次数减少也可能只是缺少行动机会，不能单独当成更稳定或更聪明的结论。各局终止时间不同，这些是整局计数，不是等长暴露下的频率。

每局原件还保留资格拒绝、技能支出、E／共生实际治疗及溢出。队友存活是各局终止时状态，不是相同暴露时间的生存率。全部记录均不是训练、真人试玩或人工可用性研究。

## 可用于讲解的结论

“我把 Guard 的单一失败拆成可复现夹具：首侧可达却挡枪，另一侧可达且能开火。新机制只用自身视野和真实站立枪口检查既有阵位，在同构建夹具中真实移动并开火；隐藏目标不再授权筛选。完整行动测试没有显示全面提升：有的局队友多输出却更慢、承伤更多，另有旧种子暴露了脚本驾驶器卡角。我保留失败、区分工具与策略证据，并把下一步范围限制为执行覆盖与具体协同事件。”

最终 r3 Guard 独立复核 SHA-256：`604244f162f88d82cf69cbcdbc309b7edf206ce1cdd93cbae3587e296789e3b8`。第二轮冻结为 `dc21a655eee2850f5d90b27eecbe5a811791b788c11104ac026087e7e270d37a`，evaluation 为 `2f286cee044136445ec74515341848add6606f8a0a8ab2b98609252aae8b53d2`，输出分解为 `cab5625fd5c7b64d6e80552825b0ec3d89d3762b6a4555bc52402022cf1cd664`；第一轮被绑定的 evaluation 原件仍为 `64b7768c994c80d47bf95f020ebba05a5151ed78182d1cc39bb480ba938cf95e`。
