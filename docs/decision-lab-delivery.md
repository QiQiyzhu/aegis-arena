# Decision Lab v1.4 交付与验收

本轮复用了既有 Unreal 场景、角色、行为树、Utility 和 EQS 资产。原 v1.3 互动战术流程会绕过这条 Utility/BT 执行链，因此新增独立 Decision Lab 入口，把手动试玩、真正执行的任务、可解释遥测和实验记录放在同一条原生路径上。旧 Copilot 入口保留。

## 启动与交付物

桌面双击 **Aegis Arena 决策演示**。快捷方式直接启动游戏并带上 `-AegisDecisionLab`；裸运行通用 `AegisArena.exe` 仍进入既有模式。包内双击 `PLAY-Aegis-Decision-Lab.cmd` 可获得相同效果，无需 Unreal 编辑器、Python 或 API Key。第一次在另一台 Windows 电脑运行若缺 VC++ 运行库，可使用包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`。

- [完整 Windows 演示包](D:/AegisWork/Deliveries/Aegis-Arena-Decision-Lab-v1.4.zip)：解压后保留整个 Windows 目录。
- [操作指南](decision-lab-play.md)：Enter 开始，WASD / 鼠标左键操作，F1 决策调试，Esc / P 菜单、菜单 X 退出。
- [一分钟真实决策录像与讲解](decision-lab-demo.md)。
- [一个失败、一个机制、前后证据](decision-lab-case.md)。
- [36 局冻结评估与完整负结果](decision-lab-results.md)。
- [需要读懂的 C++ 调用链](decision-lab-cpp-guide.md)。
- [机器可读验收记录](../evidence/decision-lab-v1.4/acceptance.json)。

独立包是 Development 配置，本轮没有另做 Shipping 包。`Presentation` 内含讲解、源码参考、协议、原始日志与录像；无需这些参考文件也能玩游戏。原件中的绝对路径忠实保留采集环境，打包的 Markdown 副本将本轮报告链接转换为包内相对链接。

## 已完成验证

| 范围 | 证据与边界 |
|---|---|
| 最终源码与包 | Editor 构建、Development 编译 / cook / archive 成功；源码、资产、游戏 payload 与 cooked containers 绑定。 |
| 感知与记忆 | 最终源码 28 项原生断言通过。 |
| 手动入口和 UI | 最终独立包 35 项合成检查通过：PlayerController 输入、4 项链接权限 API 检查、4 张原生截图；包含 F1 保持 Lit、暂停、重置、持续射击。未冒充真人手感测试。 |
| 验收脚本 | 160 项 Python 测试通过。 |
| 校准 / 案例 / 独立评估 | 18 局开发难度校准、同一最终包 6 局开发前后对照、冻结后 36 局独立进程 holdout，分别统计。 |
| 原生录像 | 一局渲染诊断，200 张原件和编码 provenance；全片解码及关键帧目视复核。 |
| 桌面入口 | 快捷方式回读了真实 exe / 参数 / 工作目录；实际启动后确认对应 payload 进程、地图加载和导航初始化。 |

早期快照通过的 6 项 UE Core 测试、历史 381 条 portable 断言及旧 v1.3 验收按原版本保留，不冒充最终源码新测，也不计作本轮原生策略证据。先前编译失败、输入探针失败、严格 warning gate 拒绝和游戏失败局均保留。

渲染仍有一条已定位的 UE 5.8 `r.MotionVectorSimulation` 注册元数据警告。仅允许其精确完整文本出现一次并保留原文；其他 warning/error、ensure 及重复警告仍拒绝。NullRHI 评估不使用此例外。详情见 [展示验收](decision-lab-presentation-audit.md)，不能称为零警告。

桌面普通启动还启用了本机音频设备：引擎 48000 Hz 与设备 44100 Hz 不同，另记录两条 WASAPI 采样率转换警告，随后 `InitializeHardware succeeded`。本机 UE 源码 `AudioMixerWasapiRenderStream.cpp:156` 对应这段转换分支。它们完整保存在 `manual-launch.log`；桌面检查只证明进程、地图和导航正常启动，不证明音频质量。自动评估使用 `-nosound`，其冻结验收脚本没有为此放宽；交付组装最初因误将普通音频启动交给自动评估 gate 而拒绝的记录也保留。

## 团队分工与结果

| 工作方 | 完成内容 |
|---|---|
| AI 可靠性 agent | 受权限约束的同队位置链接、观察 / BT / EQS 遥测、C++ 调用链，以及失败局的实际执行复核。 |
| 交互展示 agent | 正常 / 调试界面分离、按键探针、F1 / 暂停反馈与独立包截图验收。 |
| 研究评估 agent | 旧证据审计、冻结协议核查、配对分析、前后案例与完整负结果报告。 |
| 主代理 | Lab 场景和记录、集成、编译、原生实跑、视频、桌面入口及完整包。 |

唯一策略改动解决一类 Follow 没有可执行对象的问题。独立样本 baseline 2/12、improved 6/12、priority 5/12；improved 玩家平均承伤下降、队友平均输出提高，但队友死亡与动作切换增加，普通布局胜率未提高。waiting 减少不等于静止时间同比减少。没有修改多个参数追逐 holdout 胜率，没有 RL 训练或真人胜率结论。
