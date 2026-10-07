# v1.5 需要读懂的 C++ 调用链

面试讲解建议从“输入怎样变成规则、实际结果怎样回到界面”开始，先讲规则与取舍，再按需展开实现。本文只描述作品集模式；历史可解释 Utility / BT / EQS 链见 decision-lab-cpp-guide.md。

## 启动、阶段与胜负

`AAegisScenarioRunner::Startup` 识别 `-AegisPortfolio`，建立既有 Uplink 互动场景，再创建 `AAegisPortfolio`。`StartInteractive` 创建玩家、队友并重置到简报；按 Enter 后经 `DeployTrial → SpawnTrialWave` 生成敌人。`AAegisPortfolio::SynchronizePlayer` 在简报时应用 140 HP、18 射击伤害、0.28 秒间隔及队友 120 HP 的预设，建立新账本。

`AAegisScenarioRunner::UpdateInteractive` 从真实生命、位置和敌人数推进 Trial；`UpdateObjective` 检查占圈、争夺及撤离资格。必须目标完成且敌人数归零才进入下一阶段。`Trial.observe` 之后立即调用 `Portfolio::ObserveProgress` 发放一次阶段奖励，再暂停升级选择，避免把奖励推迟到暂停期间无法 Tick。

## Q：一次原子消费，然后执行实际技能

`PlayerController / InputComponent → AAegisPlayerCharacter::TryPulse → AAegisPortfolio::TrySpendPulse → aegis::PortfolioEconomy → 实际伤害 / 推开 / 治疗升级 → 事件反馈`。

先检查存活、场景状态和已有技能冷却，再检查能量。扣费成功才启动 Q 冷却与作用；余额不足不触发技能，也不进入冷却。Q 不保证命中，这本身就是释放时机的成本。没有为了录制而注入技能命中。

## E：先收集合法受益者，再付款

`AAegisPortfolio` 自己的 `InputComponent` 绑定 E → `TryRepair` 检查正在游戏且没有模态菜单 → 读取玩家实际缺血及队友存活、缺血、距离、掩体射线 → 没有治疗量直接拒绝 → 账本支付 40 → 分别恢复至各自上限 → 记录实际治疗量。

队友不满足条件时仍可给受伤玩家自疗；“支付成功”不意味着两个人都恢复了固定数值。统计必须记实际恢复，不把超出生命上限的名义治疗计入收益。

## 奖励、溢出与重开

`Health.OnDamaged → AAegisPortfolio::OnDamage` 记录实际承伤与输出，并为真实敌人死亡发放击杀奖励；`ObserveProgress` 跟踪完成阶段与升级。每局角色使用稳定序号去重，避免对象销毁后底层 ID 复用导致漏发。敌人 +12、阶段 +25；超过 100 的部分单独记录为 overflow，不计入实际 earned。

`R → StartInteractive → 新玩家实例 → SynchronizePlayer` 保存旧局终止记录，再清空角色绑定、已奖励身份和升级状态，恢复 60 能量。检查恒等式：`60 + 实际入账 − 实际支出 = 当前库存`。

## 画面与证据

`AAegisDebugHUD::DrawHUD → DrawPortfolio` 读取同一套真实角色、Trial、Operation、账本和战术状态；正式界面与 F1 诊断分离。遮挡时 YOU 标记只使用玩家自身坐标，可见敌人标签需要视线。

`AegisPortfolioPresentation` 负责原始基础几何、材质、灯光、动画与音效。装饰不影响导航/碰撞，不参与策略观察。`AAegisPortfolioCapture` 仅在显式开发参数下通过正常输入驱动玩家并保存原生帧、真实音效触发时间和局面采样；这是自动演示工具，不是真人，也不是策略评估基准。

`AAegisPortfolioProbe` 是另一种受控测试：冻结 AI 并施加明确夹具伤害，检查输入、扣费、冷却、暂停、重开和正常退出。该伤害不能混入自然战斗视频或性能结论。独立 C++ 账本检查、Unreal 原生夹具和完整实机演示各有自己的证据范围。

没有强化学习训练。历史 v1.4 的机制改进及冻结评估保留在原报告，不作为本版数值平衡成果。
