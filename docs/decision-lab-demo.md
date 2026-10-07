# 一分钟原生决策演示

[播放 MP4](../evidence/decision-lab-v1.4/media/decision-demo.mp4) · [视频与逐帧来源](../evidence/decision-lab-v1.4/media/video-provenance.json) · [原始事件](D:/AegisWork/Reports/decision-lab-20260917/native-decision-capture/decision-000.jsonl)

这是最终独立 Unreal Development 包的实际渲染：Improved、layout 0、seed 2003、两名敌人。主玩家由脚本控制，固定 1/60 游戏步长；200 张未经重绘的原生 PNG 按采集请求的游戏时间组成视频。编码为 30 fps 会重复采样帧，不表示原游戏录制帧率，视频也不是性能测试或真人操作记录。视频约从游戏第 0.3 秒开始，定位时应以事件的游戏秒为准。

| 游戏时间 | 可讲解的内容 |
|---|---|
| 开局 | 观察队友位置链接与 Follow。视野中的队友信息和显式同队位置链接是两种来源。 |
| 约 12 秒 | Support 意图、实际 BT 任务和真实血量分别展示；接受任务不等于已完成治疗。 |
| 约 13.4 秒 | 实际 Attack 开始出现；结合射击与伤害记录，不把高分直接当成命中。 |
| 约 19.5 秒 | Support 原始分数最高，Retreat 仍被选中；界面解释切换裕量，并显示真实 EQS 查询 Q14。 |
| 60 秒 | 本局 timeout，两敌未全部清除，不能称作通关演示。该诊断局不并入 36 局 holdout。 |

## 用第 19.5 秒讲清整条链

观测 #95 中，队友自身生命为 44%，Sight 中玩家生命约 45%、距离约 1.1 米；敌人当前可见、距离约 12.9 米，支援冷却就绪。Follow / Attack / Support / Retreat 分数分别约为 0.202 / 0.582 / 0.764 / 0.700。

原始最高分是 Support，但比当前 Retreat 只高约 0.064，小于既有 0.08 切换裕量，因此策略保留 Retreat。面板的 `selected`、`raw best` 和 `Reason` 与这次规则判断同步。

随后查看真实执行：BT 最近接受的是 Retreat。EQS #14 使用的是较早观测 #91，异步结果已接受，金色菱形标出其移动目标；此时最新观测是 #95。**结果接受只表示移动请求被接受，不表示到达、安全或已经恢复生命。** 候选集合没有保留，面板据实显示 `candidate data not retained`，不伪造热力图。

这段解释来自可观测输入、规则分数、任务返回和查询结果，没有模型隐藏思维链，也没有 RL 训练。代码阅读入口见 [C++ 调用链](decision-lab-cpp-guide.md)。

## 验证与复用

视频已完整解码检查，并抽取编码后的第 19.2 秒画面核对可读性。原始帧、原生日志、逐局结果、采集 manifest 和视频 SHA-256 均保留。打包目录的 `Presentation/evidence/decision-lab-v1.4/raw/native-decision-capture/frames` 收录全部 200 张原件。

想亲自演示时，从桌面 **Aegis Arena 决策演示** 启动，Enter 开始，F1 展开同一个面板，Esc 暂停阅读；具体操作见 [试玩指南](decision-lab-play.md)。真人操作会改变战斗轨迹，不承诺在相同秒数重现视频。
