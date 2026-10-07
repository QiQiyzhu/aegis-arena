# Acceptance status

## v2.3 扫描探索与双语界面 · 2026-09-22

组合构建、8 项 UE Core、原生扫描探针 25 条断言通过。新 survey Development 包构建成功，3116 张 1080p/30fps 原生帧覆盖双缓存、密钥增幅、实际补给治疗、三阶段获胜和中英切换。当前源码、资产和包哈希与录制前后记录一致；录制为正常输入接口的脚本驱动，没有冻结 AI 或伤害 fixture。

恢复时发现旧视频编码存在一帧音频偏移和 PNG 默认时钟导致的帧量化，已修复并通过 27 项视频/EDL/音频测试。最终成片 **115.87 秒／49,230,305 字节**已通过独立完整解码（3476 帧）、抽帧、无削波及六点零延迟音频检查，并复制到桌面；视频 SHA-256 为 `61778af2d600a7661b843f1ab3225d8e84f99159624d537a345d0f806ccdbb1f`。桌面试玩快捷方式已指向新包。准确范围、文件与证据见 [v2.3 交付记录](portfolio-v2.3-delivery.md)。

## v2.1 PRISM FALL 视听升级 · 2026-09-18

已完成新原生包、桌面 **Aegis Arena v2.1**、133.87秒／18.72MB MP4、11页PDF／DOCX、32.68MB投递附件ZIP与160.19MB游戏ZIP。[新版交付](portfolio-v2.1-delivery.md) · [验收](../evidence/portfolio-v2.1/acceptance-final-01.json)。旧v1.5／v2.0投递目录已按要求删除，其他历史资料与失败证据保留。

源码 `4512e5e07bad285542e35911f7915d0d84beaae134aa7195861d67659343d3ea`，最终 r2 包通过控制36、音乐生命周期15、Lab输入35项原生检查；3656张连续帧、成片完整解码、5张编码帧及11页文档视觉检查通过。新增27组件受限的射击表现池、7段原创音乐、17个原创音效与M音乐控制。原生Master Submix有播放／静音／恢复差异；视频声轨为真实事件重混，范围分别披露。

完整演示三阶段获胜、106.8秒、团队输出1260（玩家1020／队友240）、承伤238／42、队友存活；状态变化122、短回返18。自然失败的无截图回归（80秒、2阶段、输出1016）和原始连续射击后冲刺索引越界均保留。索引修复只统一循环游标，不修改AI、伤害或难度；不根据一次录像宣称胜率提升。历史AI案例继续标v2.0，没有RL训练或本轮AI重评声明。新投递ZIP完整CRC与逐文件SHA检查通过，工作已落盘，可跨重启继续。

## v2.0 PRISM FALL · 2026-09-18

原生 Windows Development 包、桌面启动器、133.87 秒／16.66 MB 的实机 MP4、10 页 PDF 与可编辑 DOCX 已完成。[交付索引](portfolio-v2-delivery.md) · [最终验收](../evidence/portfolio-v2/acceptance.json)。桌面 **Aegis Arena v2.0** 以种子1101启动晶体遗迹场景；Enter 开始，Esc 再 X 退出，R 同种子重开。另附独立的 Decision Lab 入口。

最终源码 `6dad19526dbc6b093a841ccf46f040e48148e37700d3e5a2184d6f66d42b3cc2`，内容 `9667582db87935a68d8823eca0801b1c47ade897f12a1c848f926a4e02e88fd5`。r3 包通过36项受控输入、35项Lab输入，Guard局部前后各12项；3656张原生帧、成片及文档逐页检查均通过。正式演示完成3阶段，用时106.8游戏秒，团队输出1260，玩家／队友承伤238／42且队友存活；是脚本正常输入的一局，不是人工体验或FPS测试。

Guard仅新增自身Sight授权的既有阵位枪线筛选。最终三对新种子均完成任务，队友全部存活，但5501新组慢10秒、玩家多伤14，7703玩家／队友多伤104／42；6607表内报告指标一致。状态切换减少不等于协同改善。旧六局、完整路径卡角和录制器空路径失败均保留；没有将新分辨率配置称为卡角修复，也不合并两轮胜率。[完整输出与稳定性表](portfolio-v2-ai-case.md) · [工具修复案例](portfolio-v2-recording-case.md)。

无RL训练或普遍AI提升声明；v1.3云端Copilot仅作历史可选技术说明。源码、包、记录与文档已落盘；未提交、推送或发布。

## v1.5 PRISM RELAY · 2026-09-18

面向系统／综合策划的独立 Development 游戏包、桌面一键入口、107.97 秒／14.16 MB 的原生演示视频及 PDF／DOCX 设计说明已完成。[交付索引](portfolio-delivery.md) · [最终机器验收](../evidence/portfolio-v1.5/acceptance.json)。游戏模式使用 `-AegisPortfolio`，无需编辑器或云端模型。

最终 Editor 构建、6 项 UE Core 和 Development 构建／cook／归档通过；最终包 25 项受控输入检查通过，使用冻结 AI 与显式夹具伤害，不用于证明战斗表现。正式录制使用正常输入驱动自动玩家，真实战斗完成 3/3 阶段；玩家承伤 70、队友承伤 28 且存活、团队输出 1260、游戏计时 80.9 秒。3 次 Q 与 1 次 E 消耗 145 能量，E 实际仅自疗 14，队友治疗 0；能量溢出 34，不宣称长期平衡。

视频保留连续 2879 张 1920×1080 原生帧，首尾说明与旁注为后期编辑；195 个真实音效事件重混原创 WAV，不是硬件录音。全片 3239 帧完整解码通过、5 张编码后 QA 图复核。固定步长采集不是实时 FPS 基准；无真人试玩、Shipping、当前版本策略胜率或动作稳定性对照结论。历史 v1.4 的 36 局评估独立保留。

最终源码 `30d7bb8f4da778140e5bceb243201d85c5930eeb204350dc2da631d3c6778843`，内容 `fc9cf6d1e9db8f22542f8e4deba1b5ba0d1ec3aed28e7889b987ab1e0f1a7c78`；源码、包、录制与证据绑定见验收。失败记录和被撤回的修复猜测保留；没有 RL 训练，也未提交、推送或发布。

## v1.4 Decision Lab · 2026-09-17

已交付独立 Development 包、桌面直接启动入口、原生决策录像、单机制前后案例和 C++ 调用链。[交付索引](decision-lab-delivery.md) · [机器验收](../evidence/decision-lab-v1.4/acceptance.json)。旧 Copilot 与历史失败保留，未提交或发布。

最终源码 28 项原生感知 / 记忆检查、最终包 35 项合成输入和权限检查、160 项 Python 测试通过。18 局开发校准后选择两敌；最终包 6 局开发对照后冻结配置，36 局 holdout 全部验收完成。胜利 baseline 2/12、improved 6/12、priority 5/12；improved 队友死亡 2→3、切换增加，L0 胜率同为 1/6。[完整报告](decision-lab-results.md)。

原生渲染诊断单独录制，使用脚本玩家、固定游戏步长，不计入 holdout；没有真人体验、Shipping 或性能基准结论。渲染保留一条精确匹配的已知 UE 元数据警告及原文，未声称零警告。早期 Core / 历史 portable 结果没有重新标成最终原生验证。

冻结源码 `8193689c4841b7fe17f41956d338d9b6d1ea2df8474d3e8fab5959f989307c3e`；内容 `4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。本轮唯一策略改动为 Follow 的授权同队位置链接，未训练 RL。

## v1.3 Squad Copilot · 2026-09-11

新增 Tab 自然语言队友面板、真实 DeepSeek 云端计划、四种有界技能、严格目标校验、手动接管和失效回退。沿用已保存的用户 DPAPI 配置，包内提供 Play/Configure 启动助手。详见 [升级说明](upgrade-v4.md)、[3/8 分钟讲解稿](squad-copilot-explained.md)与 [最终验收](../evidence/upgrade-v4/acceptance.json)。

381 portable / 117 Python / 6 UE Core（含 20 条模型协议断言）通过；Tactical 32 / Weapons 31 / Operation 31 / Memory 28 / Trial 54 通过。真实云端十二条合成观测案例的结构、合法性和完整意图均 12/12；Editor 云端流程 13 项、受控 HTTP 故障 25 项通过。最后只修改 UI 错误提示，前后源码绑定在验收记录中明确区分。

最终 Development 包实际通过 30 项输入/正常退出和 13 项真实云端计划检查；八张原生 PNG 已复核。Development / Shipping 均完成构建、烹饪、归档；Shipping 静态 17 标记检查通过。独立包云端场景验证实际移动后手动取消，没有证明完整占点后保护自动完成；无真人手感、中文 IME、多局平衡或最终 Shipping 人工试玩结论。

源码摘要 `2ee8534f16485a895b69628f0bf8b107bde0aa5d05b6a074fad90e0c82cb894f`，资产摘要 `4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。所有更改与原件已落盘，未提交、未发布；历史记录保留。

## v1.2 Uplink · 2026-09-11

新增中继 / 双点传输 / 玩家撤离目标、三选升级、Q 脉冲、Z/X/C 队友命令、三种敌人角色、固定瞄准预警与换位。Esc 打开清楚的可点击菜单，菜单内 X 退出。实际观察发现并修复了出生时遗漏已有玩家 Sight 查询的缺陷。功能与范围见 [upgrade-v3.md](upgrade-v3.md)，最终逐字节记录见 [v1.2 acceptance](../evidence/upgrade-v3/acceptance.json)。

381 portable、62 Python、5 Core、32 Tactical、31 Weapons、31 Operation、28 Combat/memory、54 legacy Trial 检查通过；独立 Development 包 30 项输入 / 正常退出检查通过。两种配置完成构建 / 烹饪 / 归档；Shipping 静态 13 标记检查通过，未真人试玩。

独立 Development 包的默认伤害自动观察实际运行 27.56 秒，结局为 `lost`；玩家 / 队友 / 敌人分别开火 27 / 4 / 16 次，敌人预警 23 次。这是通过正常游戏内输入的有界观察；驾驶器会移动到目标并瞄准射击，没有真人的闪避策略。通过表示交火、移动、真实伤害和记录完整，不能解释为通关、胜率或平衡性结论。

本轮为本地未提交升级，未发布。以下 v1.1 与 v1.0 记录均为历史快照。

## v1.1 Tactical Trial upgrade · 2026-09-10/11

Current upgrade details and final verification are maintained in [upgrade-v2.md](upgrade-v2.md) and the [v1.1 acceptance record](../evidence/upgrade-v2/acceptance.json). New runtime behavior includes the three-wave playable trial, Shipping player HUD / visual feedback, cursor aim / hold fire / dash, and explicit target-memory expiry. Historical raw results below are preserved for comparison and do not certify modified runtime code. The new trial Functional fixture uses disclosed test damage to verify lifecycle, not natural-play success.

Local v1.1 checks passed: 347 portable assertions, 33 Python cases, 5 UE Core tests, 28 Combat/memory world assertions and 54 Trial world assertions. The standalone Development package passed 26 synthetic input checks and produced four reviewed native screenshots. Both configurations built/cooked/staged successfully; all 11 development markers are absent in Shipping and present in Development. Final Shipping manual play and physical desktop focus/input behavior were not inspected. This local upgrade has not been published.

**The remaining sections are the frozen v1.0 acceptance ledger.**

Recorded 2026-09-10. Compiled source, NullRHI game-world execution, rendered measurement and packaged-window operation are distinct evidence levels.

## Complete and actually run

- Portable C++17 strict build: **325 assertions**, **16 Python cases**, **60 model evaluation episodes**, **20 model CPU samples**. The earlier [hosted Ubuntu GCC / Python CI](https://github.com/QiQiyzhu/aegis-arena/actions/runs/34440960072) passed; it runs no Unreal.
- **UE 5.8.2 CL 56702186 / MSVC 14.50.35738 / Windows SDK 26100**: Editor compiled and linked; Game Development and Shipping also have actual successful build logs. The Game packaging builds refresh those separate targets.
- **Ten native assets** generated and saved by the real Editor: two maps, BB, BT, three EQS graphs and three materials. Saved graph structure, keys, positive query scoring, references, camera tag/rotation, movable lights and physical navigation bounds passed [reload inspection](../evidence/unreal/asset-inspection.json).
- **Five Core Automation tests passed**, zero failures/notRun/inProcess. The separate **one native World Functional Test passed**, executing twelve named assertions for physical ranged hit, cooldown, team filtering, death/healing, real possession, a moved Pawn’s EQS Querier and missing-query fallback. The runner requires PIE/begun-play and rejects JSON-only success. [Raw Core](../evidence/unreal/automation/index.json) / [Functional](../evidence/unreal/functional/index.json).
- **60 native policy episodes** with four enemies, paired seeds 1001–1030, NullRHI and 1/60 fixed game step. Actual BT/Perception/EQS/navigation, damage and JSON/CSV capture ran. Both policies won 0/30; utility companion deaths at episode termination 6 vs priority 25, with less allied damage output and greater cumulative player damage taken. Utility episodes also ended earlier; unequal observation windows prevent a causal protection claim. [Protocol and limits](native-evaluation.md).
- **12 rendered performance episodes**: 1/10/25/50 enemies plus two allies, three seeds each, 15 seconds sustained damage-disabled load, RTX 4060 Laptop / D3D12 / 1280×720 offscreen. [Actual results and limits](performance.md).
- **Actual native PNG/GIF**, viewed to verify the scene, camera and lights. The 13-frame GIF is sampled at 2 fps, not a game-framerate measurement. The old portable GIF remains explicitly labelled non-Unreal.
- Nine developer command/overlay/query-debug markers were present in the final Development binary and absent from the final Shipping binary. It is separate from runtime acceptance.
- Complete [A–T interview dossier](interview-dossier.md), including actual failures and unfavorable outcomes; no invented RL, RAG, commercial deployment or optimization percentage.

## Packaged runtime acceptance

The final Development and Shipping revisions were built, cooked, staged and archived in fresh directories. UAT BuildCookRun completed in 153.34s / 128.22s respectively; these are build wall times under concurrent desktop load, not performance benchmarks. The standalone Development executable completed two validated game-world episodes with no Editor dependency. The actual Shipping window rendered the map and running AI; its diagnostics HUD was absent and the grave key did not open the developer console. Nine compiled developer markers were present in Development and absent from Shipping. [Package record and download](release.md).

A late Development GUI run encountered a Windows firewall permission dialog; no security control was automated or changed. The offline batch execution passed, and the final Shipping window ran without that dialog. Basic input bindings were exercised during earlier Development acceptance, but exhaustive mouse/controller/player usability testing is outside this technical AI lab's evidence. The Shipping inspection proves launch/presentation and the diagnostic boundary, not every input combination.

## P1 — explicit remaining scope limits

- The actual runtime overlay and raw EQS candidate scores are captured, with the recent BT action and query IDs. A recording of the Editor’s own live BT/EQS graph debugger remains optional follow-up; the runtime image is not labelled as that debugger.
- Add targeted native tests for perception-loss memory expiry, repeated-batch Actor/Controller count invariants and distinct cancellation/no-candidate categories. Current scenario completion is useful but does not prove every lifecycle edge.
- More arenas and independent holdout seeds. The four-enemy stress case has a win-rate floor; define follow-up experiments before tuning.
- Longer profiling warmup, longer independent sessions and Unreal Insights CPU/GPU/memory traces. Short invulnerable runs have a limited tactical-query count and do not prove worst-case or commercial performance.
- Primitive visuals and basic controls are appropriate to a technical AI lab. This is not a commercial action game or an animation-production portfolio.

## P2 — optional, not implemented

StateTree is not duplicated alongside BT without a specific need. Learning Agents, imitation datasets, learned policies and training curves are absent. Experimental plugin availability is not a training result.
