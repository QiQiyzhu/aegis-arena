# 面试讲解与逐行阅读指南

## 先准确描述当前阶段

“Aegis Arena 是一个小规模游戏 AI 系统与评测实验室。我把伤害、阵营、Utility 和 Director 规则做成不依赖引擎的 C++ 核心，再提供 UE 5.8 的角色、感知、BT/EQS、评测与调试适配。portable C++ 模型已有 325 次断言、9 项 Python 测试、60 局策略评测与 20 局 CPU 采样。Unreal 5.8.2 的 Development 和 Shipping 游戏目标已真实编译链接，调试入口的二进制静态检查通过；Editor、10 个原生资产、5 Core Automation、1 Functional Test（12条PIE世界断言）、60 局原生策略评估与 12 次实渲染采样已有独立证据。Dev独立包两轮场景、Shipping真实窗口和调试入口关闭均已验收，具体范围见 status；不把 portable 数字说成 Unreal 性能。”

这个边界必须保留，直到 [Unreal 验收清单](status.md) 真正完成。代码由 AI 辅助生成与审阅；用户应亲自阅读、修改、重跑和讲解后，再把对应能力写入个人简历。仓库的存在本身不证明作者已经掌握全部技术。

## 10 个必须逐行理解的 C++ 文件

| 文件 | 必须能回答的问题 |
|---|---|
| [rules.hpp](../core/include/aegis/rules.hpp) | 伤害如何避免负数/NaN、友军误伤、死后回血？Utility 如何隔离真值？Director 的硬上限为何绕过冷却？ |
| [geometry.hpp](../core/include/aegis/geometry.hpp) | 线段圆形遮挡如何计算？为什么这个采样器不是 EQS？ |
| [simulation.hpp](../core/include/aegis/simulation.hpp) | 感知→决策→移动→攻击顺序怎样影响结果？指标何时递增？简化导航有哪些偏差？ |
| [main.cpp](../core/src/main.cpp) | CLI 如何拒绝错类型、未知策略、负 seed、整数溢出？为什么非零退出很重要？ |
| [tests.cpp](../core/tests/tests.cpp) | 哪些是边界不变量，哪些是完整 episode 一致性检查？325 次断言不等于 325 个独立测试场景。 |
| [AegisCharacter.cpp](../Source/AegisArena/Private/AegisCharacter.cpp) | Actor 和组件怎样分工？射线、冷却、阵营、事件、死亡顺序如何组织？ |
| [AegisAIController.cpp](../Source/AegisArena/Private/AegisAIController.cpp) | 为什么失去视野要清 TargetActor？异步 EQS 如何取消？共享 BT 节点为何不存 Pawn 状态？ |
| [AegisLab.cpp](../Source/AegisArena/Private/AegisLab.cpp) | ScenarioRunner 如何拥有自己的 Actor、记录真实伤害、清理计时器并写报告？ |
| [AegisDebugCommands.cpp](../Source/AegisArena/Private/AegisDebugCommands.cpp) | Pause 和 StepDecision 各停止什么？Shipping 编译如何移除调试命令？ |
| [AegisAutomationTests.cpp](../Source/AegisArena/Private/Tests/AegisAutomationTests.cpp) | UE Automation 与 portable 测试有什么互补关系？为何不能把一个通过当成另一个通过？ |

## 常见问题

**Behavior Tree 的优缺点？** 它把优先级和可中断行为显式化，便于设计师组织任务、观察执行历史。代价是条件过多时图会膨胀，异步任务和中断清理也容易出错。UE 的事件驱动 Blackboard observer 可以减少无意义遍历；不代表 BT 内所有代码自动没有 Tick 成本。

**Blackboard 是什么？** 它是供行为节点读写的带类型工作记忆。TargetActor、LastKnownLocation、HasLOS 各自有更新/失效规则。Blackboard 不负责证明信息合法；写入前必须遵守感知权限。

**AI Perception 和直接 GetPlayer 有何区别？** Perception 提供感知成功/失败、刺激位置和时效，体现角色能知道什么。每次直接获取玩家位置会让遮挡和丢失目标失去意义。本项目把当前可见目标与历史位置分开，并把 value observation 传给 Utility。

**EQS 解决什么问题？** 它生成候选位置，再通过路径、视线、距离测试过滤和评分。BT/Utility 决定“撤退”，EQS 帮助选择“撤退到哪里”。它不能代替 NavMesh，也不能保证一个高分位置在队友占用、动态遮挡变化后仍然最优。

**Utility 为何适合同伴？** 同伴需要在攻击、支援、跟随和自保之间权衡，连续评分比不断堆叠固定优先级容易调参。它不保证更强：本项目 Utility 的同伴死亡少，但玩家受伤更多，胜率也没有提高。应把目标和权衡公开。

**StateTree 什么时候更合适？** 当行为围绕稳定阶段、进入/退出动作与明确转换组织，例如准备→作战→恢复→撤离，层级状态机比较自然。当前没有实装 StateTree，不会说做过三种策略对比。

**如何避免每帧做昂贵决策？** 让感知事件更新记忆，把周期观察放到 5 Hz BT service，给 EQS 每控制器 1 Hz 提交上限与单请求限制，死亡/离开时取消工作。是否需要全局调度应看 Insights 的队列和成本，不先虚构“优化了 60%”。

**RL observation/action/reward 怎么设计？** 先选一个小问题，例如闪避。输入仅包含可见投射物的相对状态、角色速度/生命/冷却；动作受移动和技能预算限制；奖励按真实存活、伤害与碰撞结果定义。还需遮罩缺失观察、固定归一化和超时终止。这里只提供实验方案，没有训练成果。

**Reward hacking 是什么？** 策略找到奖励定义的漏洞，例如为了存活奖励一直绕圈却不完成任务。奖励并不等于设计目标，需要用成功率、受伤、卡住时间和失败案例独立检查。

**训练 reward 高为什么不说明好？** 它可能记住训练地图、利用奖励漏洞，或在评测条件变化后失败。应冻结策略，用没见过的 seed/布局/攻击组合，与同等感知权限和动作预算的 baseline 比较。

**为什么需要独立 evaluation seed？** 它减少把参数反复调到特定样本上的偏差。开发 seed 1–10 与评测 1001–1030 分离；一旦用评测失败案例调整参数，就应把那批样本视为开发集，并另建新的评测集。

**C++ 与 Blueprint 怎么分工？** C++ 管规则、不变量、生命周期、异步调用和记录工具；Blueprint/资产管图的编排、参数与视觉连线。不能把所有东西写成 C++ 当作目标，也不能用巨型蓝图掩盖难以测试的核心规则。

**UObject 生命周期？** UObject 由 Unreal 的对象系统/GC 管理。反射强引用让对象可追踪，弱引用不延长目标生命。普通 C++ 资源用 RAII；不要 `delete UObject`，也不要以为一个裸指针就拥有对象。Actor Destroy 与 C++ 内存立即释放不是同一件事，异步回调必须重新检查有效性。

**Actor / Component / Controller / Pawn 区别？** Actor 是世界中的实体，Component 给实体附加能力，Pawn 是可被控制的 Actor，Controller 表达控制意图并 possession Pawn。角色的生命/攻击组件不应依赖某一种具体 AI 策略。

**如何评测 Bot？** 固定规则、场景和 seed，先定义成功/超时/死亡，再记录真实应用的伤害、存活、支援、卡住与资源使用。比较多目标而不是只看胜率；给样本量和不确定区间，保留逐局数据。模型、引擎与真人体验三者都要区分。

**如何设计 AI 调试工具？** 让观察、记忆、分数、目标、路径和请求状态同屏；能暂停/单步决策/改策略；把失败请求和信息时效显示出来。调试显示可以有真值权限，但不能反过来喂给游戏策略，Shipping 也必须移除调试入口。

## 5 条当前有证据支持的简历表述

只有亲自理解并复跑后使用，且保持“portable”边界；不要把下面的模型数字换成 Unreal/FPS/RL 数字。

1. 在 AI 辅助下实现可脱离引擎编译的 C++17 游戏 AI 规则核心，涵盖阵营伤害、冷却、Utility 与 Director；严格警告构建通过，325 次不变量与回合一致性断言通过。
2. 构建 Python 驱动的固定 seed 批量评测工具，输出逐局 JSON/CSV、源码和二进制 SHA256、编译器与参数；完成 60 局 portable 模型策略对比，并通过 7 项工具/集成测试。
3. 用授权观察数据与 0.08 分差迟滞实现同伴 Utility；30 对固定 seed 中同伴死亡由 16 次降为 6 次，同时如实报告胜局 20→19 的攻防权衡，不宣称策略整体更优。
4. 实现 EMA、5 秒调整冷却、双阈值迟滞与人数硬约束的 Encounter Director；补充“冷却期间人口上限仍必须立即生效”的回归断言。
5. 建立初始 1/10/25/50 敌人的 20 局 portable CPU 采样与真实轨迹回放工具，记录决策/几何采样/步耗时，并明确未测量 Unreal Game Thread、EQS 或渲染帧率。

## 三分钟展示顺序

先用 20 秒说明这是 UE 5.8.2 技术实验场，播放真实 native GIF 或启动当前已验收程序。指出视野、记忆与动作；打开 `rules.hpp` 解释观察接口与迟滞；运行一次测试和短 scenario；打开原生结果表说明两组 0/30 的胜率下界、Utility 自保与输出的代价。再展示 portable 层的独立验证价值与剩余测试边界，不混用两个环境。
