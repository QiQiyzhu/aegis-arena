# AI 感知记忆与查询生命周期修复

验证时间：2026-09-10 23:58（Asia/Shanghai）。本页记录本轮未提交源码的定向验证；旧策略评估与发布包继续保留原有版本和结论。

原来的 `HasMemory` 只在每 0.2 秒的观察服务中更新。即使它变为 false，`LastKnown`、黑板位置、已发出的移动与战术查询仍可能保留；直接执行 Investigate、Chase 或查询上下文也没有统一检查期限。另外，`SetFocus(TargetActor)` 保存 Actor 焦点，失去视线后仍可解析目标的实时位置。停止 AI 后的感知回调还可能重新写入记忆。

[控制器](../Source/AegisArena/Private/AegisAIController.cpp) 现在统一检查 `HasTargetMemory()`，并用世界定时器清理过期状态。实际视线丢失事件立即清除活目标、LOS 黑板值及 Actor 焦点；Attack 在读取目标位置前复核当前视线刺激。到期时清除最后位置、黑板位置、战术点与查询状态，取消查询，并停止依赖旧威胁点的移动。Investigate、Chase、战术查询入口和 EQS 威胁上下文均检查有效记忆。死亡、解除控制及 EndPlay 使用相同清理路径，已停止控制器拒绝后续感知写入。

**2.5 秒的含义。** `TargetMemorySeconds` 默认 2.5，最小有效值为 0.1 秒；计时使用游戏世界时间。成功的敌对感知刺激或观察服务对可见目标的有效观察会更新时间并重设定时器。期限从最后一次有效观察计算，失去视线本身不延长期限。伤害刺激只提供事件报告的位置，不授予当前视觉跟踪。没有新观察时，即使行为树观察服务停止，定时器仍清理记忆。有效性判断使用严格小于期限；定时清理在引擎可执行该回调的帧发生，不承诺墙钟上的逐毫秒精度。

**取消计数兼容性。** UE 5.8 的 `AbortQuery` 会同步执行完成委托，因此取消前先使查询 ID 失效，防止回调重新发布战术点。成功取消仍计入旧有 `CompletedQueries`、`FailedQueries` 和对应的查询 CPU/采样计数，同时新增 `CancelledQueries`。ScenarioRunner 已汇总该字段，经结构体 JSON 序列化导出为 `cancelledQueries`；CSV 原有列保持不变。取消数是旧失败数的子集，不能再与失败数相加。旧 JSON 没有该字段，缺失不等于零；`failedQueries - cancelledQueries` 也不能全部解释成“无候选”，它仍包含其他失败原因。

**已执行的原生夹具。** [CombatFunctionalTest](../Source/AegisArenaEditor/Private/AegisCombatFunctionalTest.cpp) 保留原有 12 条战斗/战术断言，新增 16 条。在 begun-play 的 PIE 世界中，以 seed 1001 生成隔离的静止角色，使用真实 Sight 感知：获取目标、移出视距、再次移动不可见目标、确认记忆位置冻结及攻击被拒绝、接近期限前仍保留记忆、期限后清理、重新感知后更新位置。夹具在最后观察后的约 2.25 秒检查保留，在约 2.65 秒检查过期，轮询间隔 0.05 秒；等待期间不运行观察服务。然后启动真实 EQS 查询，在其仍 pending 时停止控制器，再等待 0.3 秒检查无战术点回写。

本次 28 条具名断言全部执行并通过：1 succeeded、0 failed、0 succeededWithWarnings；原生测试耗时 3.603 秒，进程总耗时 16.672 秒，退出码 0。日志同时包含 `AEGIS_MEMORY_FIXTURE | Seconds=2.500 | Seed=1001 | WorldType=3 | BegunPlay=1` 和最终 PIE 成功标记。[Python 门禁](../scripts/run_unreal_functional.py) 要求两种标记及全部断言，拒绝 handled ensure、失败/缺失测试；对应 4 项 Python 单元测试通过。原始引擎启动日志存在测试开始前的 `LogAutomationTest: Error: Condition failed` 行；本页的零失败/零警告指该功能测试报告，不表示整个引擎日志没有 Error 文本。

运行环境为 UE 5.8.2，CL 56702186，Windows，NullRHI。Editor Development 构建日志明确包含控制器和夹具的编译，并以 `Result: Succeeded` 结束。原始记录保存在：

- 功能测试：[provenance.json](D:/AegisWork/Reports/upgrade-v2-memory-01/provenance.json)、[index.json](D:/AegisWork/Reports/upgrade-v2-memory-01/index.json)、[engine.log](D:/AegisWork/Reports/upgrade-v2-memory-01/engine.log)、[stdout.log](D:/AegisWork/Reports/upgrade-v2-memory-01/stdout.log)。
- 构建：[build.log](D:/AegisWork/Reports/upgrade-v2-builds/20260910-155632-df36c29f28c84f3788e3cde3519815be/build.log)。

该次运行的 Git HEAD 是 `daabbe2bee5ab2fe8336d04aa058c0da13f9722a`，存在源码改动，具体状态已写入 provenance；不能仅用 HEAD 代表已测代码。摘要算法与原生 scenario runner 相同：按路径排序，连续输入相对路径 UTF-8 字节和文件原始字节。门禁确认运行前后源码与资产摘要一致。

| 对象 | SHA-256 |
| --- | --- |
| 源码：Source、core/include、Config、AegisArena.uproject | `3ae2b82c29227846cb1cf750ed0db547ff25f7121098459251e6ac46f3e34b35` |
| 资产：Content/Aegis | `4d6c15dbf47979046ae8790d585017994724334645ec799ef7f91ed3ed0540a5` |
| 本次 provenance.json | `2dc464b34b535a57d99105c4f3622b757eaa77006c484ae05221ce69fc5c4d35` |
| 本次 engine.log | `2520ba37f06a8d7bb9969f74604ca1dc48f8ec7d792ddeb97862ea1c89f6eab2` |
| 本次 index.json | `4bfb3a15c35a46b16dbd098591e994108b4251d62f323adac7daa2d0ae738ef7` |
| 上述 build.log | `617289beb1b2e3782d7b4ae668c9c3d62a8882d35383d165bbfeb74cd18572c0` |

验证边界：这是一个固定 seed、视距丢失方式和默认期限的真实世界夹具，验证范围小于新增运行时代码的适用范围。没有覆盖所有遮挡几何、多目标竞争、伤害记忆、所有非默认期限、所有移动分支或异步取消时序，也未单独完成无候选分类测试。没有据此声称策略胜率、长期性能、渲染效果或新 Shipping 包得到验证；历史 60 个策略/12 个性能 episode 不作为本轮改动的结果。
