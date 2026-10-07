# Aegis Arena v2.1 C++ 阅读指南

v2.1 沿用原有玩法和 AI，新增射击反馈与阶段音乐。最值得讲清楚的边界是：真实攻击先经过世界规则，再触发表现；播放成功不等于命中；音乐不能读取隐藏敌人信息。本文按 2026 年 9 月 18 日源码快照 `4512e5e07bad` 核对函数与行号，最终构建哈希及运行结果见同包验收文件。若源码继续修改，优先按函数名定位。

## 从一次射击开始

普通攻击链是玩家输入 → `CanAcceptCombatInput` → `Combat::FireAt` → 实际射线 → `Health::ApplyDamage` → `ShowShotImpact`／`ShowRangedShot` → 画面和声音。原生射线决定命中，视觉线段读取这次射线的真实终点，不做第二套伤害判定。

| 入口 | 阅读要点 |
|---|---|
| [AegisCharacter.cpp 第 74 行](../Source/AegisArena/Private/AegisCharacter.cpp#L74) `FireAt` | 存活、硬直和冷却守卫；墙壁与友军阻断；伤害与视觉终点来自同一射线 |
| [第 118 行](../Source/AegisArena/Private/AegisCharacter.cpp#L118) `FireChargedAt` | 合法方向后提交 12 能量，再做最多两人／升级三人的穿透；打空仍付费 |
| [第 775 行](../Source/AegisArena/Private/AegisCharacter.cpp#L775) `CanAcceptCombatInput` | 暂停、输入禁用、失焦或死亡时拒绝战斗；离屏探针的例外有显式开关 |
| [第 809 行](../Source/AegisArena/Private/AegisCharacter.cpp#L809) `Tick` | 检查实际 RMB 仍按下与可充能状态；跨越 0.7 秒只发一次 `S_ChargeReady` |
| [第 857 行](../Source/AegisArena/Private/AegisCharacter.cpp#L857) `Strike`／`ReleaseCharge` | 新充能重置就绪声标记；释放先取消状态，满足就绪条件才尝试发射 |

就绪音不是自动开火，也不是提前扣费。`CancelCharge` 清除计时和一次提示标记；未满释放或上下文取消不会制造蓄能射击。费用是否正确、是否命中多人，仍需看实际交易和伤害事件。

## 新表现如何保持有界

[BuildShotVFXPool](../Source/AegisArena/Private/AegisCharacter.cpp#L356) 为每名角色建立 24 个新粒子槽，加上 [Portfolio 表现](../Source/AegisArena/Private/AegisPortfolioPresentation.cpp#L533) 的 3 个已有射线槽，共 27 个。组件无碰撞、不影响导航；满池复用最早活动槽。[UpdateShotVFX](../Source/AegisArena/Private/AegisCharacter.cpp#L433) 只按寿命更新位置、缩放、颜色和可见性，全部结束后清计时器。

[ShowRangedShot](../Source/AegisArena/Private/AegisCharacter.cpp#L460) 显示枪口亮核、短射线与尾迹，复用原 Tracers 驱动枪体反冲。[ShowShotImpact](../Source/AegisArena/Private/AegisCharacter.cpp#L504) 读取既有命中的分类与法线，区分世界碰撞、角色无伤害阻挡、实际伤害和致死命中。[ShowDeathVFX](../Source/AegisArena/Private/AegisCharacter.cpp#L536) 由真实死亡触发一次，碎晶池与角色装饰隐藏分开。

普通射击和冲刺共用射线池，两条入口都必须把 NextTracer 保持在合法区间。首次完整录制暴露了连射后冲刺的越界；现两条入口统一在每次使用后保存取模结果。回归同时覆盖连续射击与后续高速冲刺，不能只检查单独开火或单独冲刺。

数量上限和计数让验收可以检查资源不会无限增加，不等于已证明某个 GPU 的帧率。`ShotVFXStats` 的 `renderedShots` 应与实际射击数一致，命中分类之和应等于真实命中计数；帧截图还要检查预警、目标环和队友状态没有被特效盖住。

## 声音从真实事件到原生组件

[Presentation::Sound](../Source/AegisArena/Private/AegisPortfolioPresentation.cpp#L252) 将既有事件映射到 `/Game/Aegis/V21/Audio`。玩家、队友、敌人各 3 个枪声变体按各自序号轮换，固定 Pitch=1，不调用玩法随机流。新就绪声由 Character 的真实充能阈值调用，不在每帧循环播放。

音效同时受 16 个请求配额与真实活动组件约束。按世界时间和原音源时长建立的配额也作用于 `-nosound` 录制，防止离线音轨仅因没有硬件声部而收到更多请求。事件记录保存真实资产路径、增益、音高与时刻，但只表示发出了播放命令。

7 首音乐和 17 个短音效由 `scripts/generate_v21_audio.py` 的显式音符、振荡器和噪声生成。`scripts/unreal/import_v21_audio.py` 核对源 SHA、真实循环标记与 Play When Silent 设置。后者很重要：音乐从零增益开始淡入时，渲染器不应先将其丢弃。没有外部采样、云端生成调用或训练模型。

## 音乐状态和 M 控制

[BuildArena](../Source/AegisArena/Private/AegisPortfolioPresentation.cpp#L314) 调用 [Music::Ensure](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L44)，一个世界仅保留一个音乐管理 Actor，加载 7 首曲目、创建 2 个组件。首次 Tick 才启动音乐，使录制器能看到首条命令。

[SelectState](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L81) 只读公开的 phase、wave、升级和菜单状态；没有敌人坐标、血量、距离或隐藏数量。简报稀疏，中继与撤离增加节奏，升级收束，胜负播放一次性尾声。它是确定性状态映射，不是生成式实时作曲。

[Transition](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L119) 让旧槽从当前增益淡出，新槽从零淡入，常规 0.65 秒。快速切换先 Stop 被复用槽，因此不会叠出第三轨。组件作为 UI Sound 可以在暂停中播放，淡化 Tick 使用 FApp 时间，不推进游戏规则时钟。

[BindController](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L66) 将 M 设为暂停也可执行，[ToggleMute](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L181) 在 0.15 秒内淡出音乐；恢复时按当前公开状态从头播放。枪声不受 M 影响。Tick 检测新 Pawn 后停止旧轨；[EndPlay](../Source/AegisArena/Private/AegisPortfolioMusic.cpp#L201) 停止两组件并解除输入。`GetPlayingComponentCount` 使用真实 `IsPlaying`，不能与“加载了七首音乐”混为同一证据。

## 系统规则和 AI 分开读

[AegisPortfolio.cpp](../Source/AegisArena/Private/AegisPortfolio.cpp#L280) 的 `TryRepair`、`TrySpendChargedShot`、`TryOverclock` 负责世界合法性与交易；E 按实际可治疗量重新报价，零治疗不扣费。账本保持 `初始60 + 实际入账 - 实际消费 = 当前能量`，四种用途与溢出分别统计。

[AegisLab.cpp](../Source/AegisArena/Private/AegisLab.cpp#L660) 的 `UpdateObjective` 与 `ChooseUpgrade` 负责占圈、争夺、真实进度、升级及胜负。F 用 35 能量换目标加速，共生则按有效游戏时间恢复；更快占完会缩短恢复窗口。声音和视觉读取这些结果，不能自行推进目标。

[AIController::ExecuteAction](../Source/AegisArena/Private/AegisAIController.cpp#L979) 在战术模式转入 `ExecuteTacticalTrial`。BT 仍调度观察与任务，Utility 分数仍计算，但不控制当前战术分支，Guard 筛选也不调用历史 EQS 选点。独立 Decision Lab 才展示其实际 Utility／BT／EQS 路径，F1 需说明当前执行的机制。

v2.0 历史 Guard 改进位于 [GetGuardCandidateMuzzle](../Source/AegisArena/Private/AegisAIController.cpp#L683) 与 [RepositionForTarget](../Source/AegisArena/Private/AegisAIController.cpp#L748)：有自身 Sight 才检查候选阵位的实际枪口射线，首位挡枪再试另侧。历史局部夹具从 0 枪变为真实换位后 1 枪；独立种子却有耗时增加、玩家承伤上升的负项。本轮没有重调策略，不将旧 Guard 对照包装为 v2.1 AI 成果。

## 录制与验证分别证明什么

[Capture::Tick](../Source/AegisArena/Private/AegisPortfolioCapture.cpp#L79) 通过正常按键接口操纵自动玩家并截图；这不是人类试玩。`RecordSound` [第 431 行](../Source/AegisArena/Private/AegisPortfolioCapture.cpp#L431) 与 `RecordMusic` [第 448 行](../Source/AegisArena/Private/AegisPortfolioCapture.cpp#L448) 保存真实请求；音乐额外保存状态、槽号、循环、淡化时间和原因。`scripts/v21_audio.py` 按两槽复用与增益重建离线音轨，不声称与硬件时钟逐样本一致。

[AVProbe::Tick](../Source/AegisArena/Private/AegisAVProbe.cpp#L51) 通过 Enter、M、P、R、X 验证音乐、静音、暂停与重开，用 [StartAudio／StopAudio](../Source/AegisArena/Private/AegisAVProbe.cpp#L153) 导出 UE Master Submix。它证明引擎混音输出，不是声卡回环或扬声器录音。原生受控交易夹具可显式设置位置和伤害；完整录像不做这种注入，两者不能混用结论。

建议面试按一条链讲完：按键被接受 → 世界规则执行 → 实际收益记账 → 视觉和音频读取结果 → 原生事件与画面交叉验证。用一次取消、一条无伤害阻挡和一次暂停静音说明边界，比背诵类名更能表达策划如何与实现协作。个人职责须按真实贡献说明；AI 协助实现和制作，不代填独立开发经历。未训练 RL，不声称自主学习。
