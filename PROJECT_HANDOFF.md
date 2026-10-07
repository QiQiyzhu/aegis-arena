# Aegis Arena — PROJECT_HANDOFF

## 当前检查点 · 2026-09-22

已完成并核验下述 v2.3 界面／场景表现交付。桌面标准视频 `C:/Users/yzhu/Desktop/Aegis-Arena-v2.3-Demo.mp4` 现统一为 49,067,215 字节版本，123.01 秒，SHA `593a7395390e966fcdd83b018da763acd52e032821ee4dff136d77d69a282057`；28.7MB CRF18 原件保留在 D 盘报告目录。修正了 50MB 报告的源报告哈希、时长和编码来源，移除此前中间编码的耗时字段；文件本身未重新编码。

新增可玩扫描、资源／事件、地图分支、语言持久化及完整翻译仍待完成，不能把当前表现交付说成已完成这些玩法扩展。当前没有运行中的构建或录制进程；用户在此节点询问进度并希望继续完善，下一轮范围优先建议扫描／资源事件与语言保存。

## 最新交付：v2.3 双语界面、路线可读性与可玩流程 Demo · 2026-09-21

本轮面向通用的“可玩流程演示”，没有把作品限定为某一种策划岗位。真实工程为 `C:/Users/yzhu/Documents/ChatGPT/Games/Aegis-Arena`；没有触碰 ARC-SHIFT。HUD 默认简体中文，`L` 可切换 English，暂停、升级、结果、技能提示、队友状态和地图图例同步切换；中文长句按字符换行。场景增加无碰撞的地面内嵌线、调查节点冷色光环、路线暖色锚点、档案框台座和边界晶体碎片，保持伤害、AI、导航、经济与胜负规则不变。[v2.3 交付索引](docs/portfolio-v2.3-delivery.md) · [视觉与玩法升级](docs/v23-visual-gameplay.md) · [界面语言](docs/v23-ui-language.md)

最终 Development 包：`D:/AegisWork/Packages/aegis-v23-final/package/Windows/AegisArena.exe`；桌面试玩快捷方式：`C:/Users/yzhu/Desktop/Aegis Arena v2.3.lnk`。原生录制：`D:/AegisWork/Reports/v2.3-20260921/capture-final`，3656/3656 帧、121.9 秒、自然 Won、正常 X 退出，visualDeliveryVersion=2.3，languageDemo=true。录制 payload SHA：`bc78eebb1471194e955ca448b8a8b653e01c1cd387cbe92ce005ebfd626edb3f`；launcher SHA：`6adf03ef8d250c2303754fcff9434f49fe78721ba7896e91e54ad34eb3d8e8c4`。

最终 CRF18 成片：`D:/AegisWork/Reports/v2.3-native-cut-final-20260921/Aegis-Arena-v2.3-Demo.mp4`，123.00 秒、28,678,786 字节、1080p/30fps，SHA `6165f7387e5e8538e1e9588b2c070ba91e05da0aeef3c5f919f6e62cbefe2977`，完整解码通过。约 50MB 版本：`C:/Users/yzhu/Desktop/Aegis-Arena-v2.3-Demo-50mb.mp4`，49,067,215 字节，SHA `593a7395390e966fcdd83b018da763acd52e032821EE4DFF136D77D69A282057`，完整解码通过；验证记录在 `delivery-50mb-provenance.json`。素材与脚本保留两次升级、路线、RMB 蓄力、Q 脉冲、E 修复、F 超频、队友协同、撤离结算和中英切换。

视频来源为新二进制的新 capture，不是旧视频重命名。运行时 presentation schema 仍为 2.1；v2.3 表示本轮 HUD、场景、语言演示与剪辑交付版本。SURVEY 当前是地图／场景信标预留，没有扫描判定、动态事件或路线风险规则；后续玩法方向已在视觉说明中单独列出。编译、Cook、Stage、Archive 和视频完整解码均通过；未提交、未 push、未发布。

## 最新交付：v2.2 表现优化与投递视频 · 2026-09-20

本轮只在 `Source/AegisArena/Private/AegisPortfolioPresentation.cpp` 增加晶体遗迹层次、地面断续导光纹和开场角色阵营／武器轮廓装饰，保持 HUD、战斗规则、伤害、AI、录制器和音频不变；设计说明见 [v2.2 表现层改动](docs/v22-art-changes.md)。增量编译与 Development 包已经成功，包入口为 `D:/AegisWork/Packages/aegis-v22-art/package/Windows/AegisArena.exe`。角色适配定时器只覆盖 `BuildV2Arena` 启动后 8 秒内出现的开场角色，不声称后续波次自动获得装饰。

`capture-art-01` 已通过：`passed=true`、自然 `won`、3656 帧、121.900006 视频秒、1265.281 秒墙钟、正常结果页 X 退出，源码与包前后哈希一致。最终采用本次新包录制，经 `scripts/build_v22_video.py` 输出到 `D:/AegisWork/Reports/v2.2-native-cut-02`，已复制到桌面 `C:/Users/yzhu/Desktop/Aegis-Arena-v2.2-Demo.mp4`。成片 96.00 秒、1920×1080、30fps、H.264/AAC、18,496,057 字节；SHA256 `24a6bd81ad9fb4b4995f37d16f75b0adca0812150af500587a3bc7dab3f64649`。`passed=true`、`fullDecodePassed=true`，已视觉检查片头、战斗与片尾，旁注无溢出。

视频强化目标、资源取舍、阶段升级、同伴协作与撤离的展示，旁注为标题和两句短思路。录制是引擎内正常接口的脚本输入，音频按真实事件重混；游戏 HUD/录制 schema 仍保留 2.1 标识，2.2 指本次表现与视频交付版本。原 `v2.2-editorial-cut-05`（旧包素材）和 `v2.2-native-cut-01`（中间版）报告保留，均不再作为桌面最终成片。详见 [v2.2 交付索引](docs/portfolio-v2.2-delivery.md)。没有提交、推送或发布。

## 最新进度：v2.1 PRISM FALL 视听升级已交付 · 2026-09-18

新增分层枪口、弹道、真实撞击与击破表现，7 段原创 BGM、17 个原创 SFX，M 音乐静音／恢复。没有改动 AI、伤害、经济或难度。桌面 **Aegis Arena v2.1** 指向 `D:/AegisWork/Playable/Aegis-Arena-v2.1/AegisArena.exe`，参数 `-AegisV2 -AegisPortfolioSeed=1101 -d3d11 -windowed -ResX=1600 -ResY=900`；旁边独立 Decision Lab 快捷方式保留实际 Utility／EQS 入口。无需 API Key。

最终原生包 `D:/AegisWork/Packages/prism-fall-v2.1-release-r2/package/Windows`，源码 SHA `4512e5e07bad285542e35911f7915d0d84beaae134aa7195861d67659343d3ea`，内容 SHA `22f45d7d2d271bafeb26b88791ab66d93f46dc31bbce747d8bb3ffd826f61cf6`，payload SHA `cb296319c3067f01a82de6ffd0810ab8e756a9f3bdcb06a06c4c461d9aea30fa`。报告根目录 `D:/AegisWork/Reports/v2.1-20260918`；`acceptance-final-01.json` 已通过，绑定控制36、音频15、Lab35、连续实机录像、5张编码视频帧检查与11页文档检查。音频原生成功为 `audio-package-03`，允许并披露引擎48k与设备44.1k的精确自动转换警告对，其他错误仍拒绝。

`capture-01` 的连续射击后冲刺索引越界原件保留，修复共享 NextTracer 写回取模不变量。`regression-editor-01` 获胜，`regression-package-01` 技术通过但自然失败（80秒、2阶段、输出1016、玩家承伤184、队友42且存活），二者不是策略配对。最终 `capture-02` 自然获胜：106.8游戏秒、3阶段、输出1260（玩家1020／队友240）、承伤238／42、队友存活，状态变化122、短回返18。玩家58次射击全部有渲染、1次蓄能、70次撞击=59伤害+11场景；27组件峰值18，非帧率基准。

新附件在 `D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v2.1`：视频133.87秒、18,718,486字节；PDF／DOCX11页；Application.zip 32,683,886字节，Windows.zip 160,185,889字节，CRC与SHA通过。视频原生连续画面加中文侧栏，正常接口脚本输入；声音按真实事件重混，另有Master Submix原生输出证据，不冒充真人试玩或回环声轨。旧 `Aegis-Arena-Portfolio-v1.5`、`Aegis-Arena-Portfolio-v2.0` 投递目录已按用户要求删除，删除清单和确认位于 `retired-attachments*.json`；旧Reports/Packages/源码未删。详见[新交付索引](docs/portfolio-v2.1-delivery.md)。

保持未提交、未 push、未发布。所有本轮工作已落盘，可重启电脑；没有后台构建或采集依赖。历史 AI Guard 案例仍明确属于 v2.0，没有 RL 训练。

## 历史进度：v2.0 PRISM FALL，系统／综合策划作品集 · 2026-09-18

科幻遗迹＋生长晶体方向完成原生场景、角色、灯光、HUD、音效升级；共享能量新增 RMB 蓄能、动态 E 计费、F 中继超频，V 选择目标顺序，中继共生依赖实际目标进度。桌面 **Aegis Arena v2.0** 指向 `D:/AegisWork/Playable/Aegis-Arena-v2.0/AegisArena.exe`，参数 `-AegisV2 -AegisPortfolioSeed=1101 -d3d11 -windowed -ResX=1600 -ResY=900`。Enter 开始，Esc 再 X 退出，R 同种子重开。独立 **Aegis Arena v2.0 Decision Lab** 展示实际 Utility／EQS，不能与主模式战术执行分支混写。

最终构建 r3：`D:/AegisWork/Packages/prism-fall-v2-release-r3/package/Windows`；源码 SHA `6dad19526dbc6b093a841ccf46f040e48148e37700d3e5a2184d6f66d42b3cc2`，内容 SHA `9667582db87935a68d8823eca0801b1c47ade897f12a1c848f926a4e02e88fd5`，payload SHA `b1fc5c4fb2dca50f3e5bc937a20e83669827d3550769b679e489061eb5bcb87c`。最终 release gate `D:/AegisWork/Reports/v2-20260918/acceptance-final-01.json` 通过，SHA `b68164b067c54b17e0144de63817ee26afb99ecf746ae68c8ed3bc730b901965`；原件与所有失败均保留。

最终视频133.87秒、16,656,237字节、1080p/30fps，来自3656张连续原生帧；脚本正常输入、无夹具伤害，非真人试玩。音效由引擎真实事件混音，非硬件回环录制。实际完成3阶段，用时106.8游戏秒，团队输出1260，玩家／队友承伤238／42，队友存活。PDF／DOCX10页，逐页视觉检查通过。投递目录 `D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v2.0`，详见[交付索引](docs/portfolio-v2-delivery.md)、[验收](evidence/portfolio-v2/acceptance.json)。

原生控制36项、Lab输入35项、Guard对照baseline05/improved04各12项。Guard只修改自身Sight授权下的既有阵位枪线筛选；旧0枪，新实际换位并开火，不扩大隐藏信息。冻结新种子5501/6607/7703的6局均Won；5501新组多输出38却慢10秒、玩家多伤14；7703多输出24却玩家／队友多伤104／42；6607表内结果一致。旧1280×720六局及完整路径卡角全部保留，未证明分辨率导致或解决卡角；新批次统一开发分辨率1920×1080，源码、AI和驾驶参数未变。不能合并两批称总体胜率提升。[AI案例](docs/portfolio-v2-ai-case.md)含完整指标与动作计数。

空路径撤离恢复属于录制工具改进，实际胶囊无障碍扫掠才发送正常WASD；不是AI策略成果，完整路径卡角尚未修复。[C++阅读链](docs/portfolio-v2-cpp-guide.md)与[工具案例](docs/portfolio-v2-recording-case.md)区分了两者。无RL训练，v1.3云端Copilot只是历史可选说明；未提交、推送或发布。用户个人经历与职责仍须按本人事实填写。旧版入口保留，所有交付文件可跨重启使用。

## 历史进度：v1.5 PRISM RELAY，系统／综合策划作品集 · 2026-09-18

用户明确投系统／综合策划岗位，视频限制 300 MB。完成共享能量攻防选择、场景／角色／HUD／音效升级，以及三阶段任务与升级的可解释展示。最终原生视频 107.97 秒、14.16 MB、1080p，附设计说明 PDF／DOCX、可直接上传附件 ZIP 与独立 Development 游戏 ZIP。[交付索引](docs/portfolio-delivery.md) · [验收](evidence/portfolio-v1.5/acceptance.json)。

本机桌面 **Aegis Arena 作品集** 指向 `D:/AegisWork/Packages/portfolio-v1.5-release-r2/package/Windows/AegisArena.exe`，参数 `-AegisPortfolio -windowed -ResX=1280 -ResY=720 -d3d11`。该目录 `PLAY-Aegis-Portfolio.cmd` 可直接启动。不要使用 development / final / release 中间包代替 release-r2。投递文件位于 `D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v1.5`。

最终源码 SHA `30d7bb8f4da778140e5bceb243201d85c5930eeb204350dc2da631d3c6778843`，payload SHA `b4fbb3a6015ded6c47800f709d6d46af86d28de04eaa6254f5c2e1ff0808026c`。6 项 UE Core、最终包 25 项受控输入检查、真实自然战斗录制通过；原始成功与失败位于 `D:/AegisWork/Reports/portfolio-v1.5-20260918`。25 项探针使用冻结 AI 和夹具伤害，正式录制没有这两项。录制为正常输入驱动自动玩家，非真人试玩、胜率或性能基准。

可玩模式由战术逻辑控制，Utility／EQS 未控制该模式；F1 如实说明。v1.4 的原包与冻结评估独立保留，不移植结论。录制器路径修复是工具案例，不是 AI 策略改进。文档披露 AI 协助，个人游戏经历／时长与个人职责待用户按真实情况填写；无 RL 训练。未提交、未推送、未发布；最终文件可跨重启使用。

## 历史进度：v1.4 Decision Lab，本地演示与冻结评估交付 · 2026-09-17

[交付与验收](docs/decision-lab-delivery.md)汇总桌面启动、包、真实视频、C++ 阅读链、团队工作和验证边界。桌面 **Aegis Arena 决策演示** 直接启动独立包并指定 `-AegisDecisionLab`；裸 exe 仍是旧模式。旧 AI 版快捷方式与 Copilot 包保留。

最终包开发案例 6 局、冻结 holdout 36 局及单独渲染诊断完成。胜利 baseline 2/12、improved 6/12、priority 5/12；队友死亡、稳定性与布局差异详见 [报告](docs/decision-lab-results.md)。不得在看过 holdout 后继续调参并沿用未见测试集称谓。最终源码 SHA `8193689c4841b7fe17f41956d338d9b6d1ea2df8474d3e8fab5959f989307c3e`，协议与 payload 绑定在 [验收记录](evidence/decision-lab-v1.4/acceptance.json)。

项目保持未提交、未 push、未发布；无 RL 训练。重启后直接用新桌面快捷方式。所有工作和原始成功 / 失败已落盘，D 盘包和报告不依赖临时服务。

## 历史进度：v1.3 Squad Copilot，本地优化交付 · 2026-09-11

已接入用户保存的 DeepSeek / deepseek-flash 配置。Tab 暂停面板可输入中英文目标，模型返回 1–3 步，游戏以四种有限技能执行；Z/X/C 接管、R 重开、无效/迟到响应和超时回退均已接入。密钥仅在用户目录以 DPAPI 保存，启动助手通过子进程环境传入，不包含在项目或包中。

- 推荐入口：[Development Play Aegis Copilot.cmd](<D:/AegisWork/Packages/copilot-v1.3-development/package/Windows/Play Aegis Copilot.cmd>)；同级 Configure 可修改提供方。Shipping 同名入口位于 `D:/AegisWork/Packages/copilot-v1.3-shipping/package/Windows/`。
- [最终验收](evidence/upgrade-v4/acceptance.json)：381 C++ / 117 Python / 6 Core；五组战斗回归 32/31/31/28/54；真实云端语义 12/12（合成观测）、Editor 云端 13、故障夹具 25；最终独立 Development 输入/退出 30、真实云端 13。两包构建归档完成，Shipping 17 标记检查通过。
- [升级说明](docs/upgrade-v4.md)、[3/8 分钟讲解稿](docs/squad-copilot-explained.md)、[近两年参考](docs/upgrade-v4-references.md)。原始成功/失败、真实响应和八张包内截图已保存；本地 Qwen 小模型 0/6→4/6 的负结果保留，未声称训练或 RAG。
- 最终源码摘要 `2ee8534f16485a895b69628f0bf8b107bde0aa5d05b6a074fad90e0c82cb894f`，资产摘要 `4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。早期回归在 UI 错误提示最终修订前执行，其他源码一致；各报告未混写为同一快照。

保持未提交、未 push、未发布；公开版本仍为 v1.0。最终包的云端场景验证计划/真实移动/取消，没有证明完整占点后保护自动完成或实战收益；未进行真人平衡、中文 IME、最终 Shipping 人工输入及长时性能测试。模型服务已停止，测试进程结束，工作可安全跨重启继续。

## 历史进度：v1.2 Uplink，本地优化交付 · 2026-09-11

用户反馈玩法单一、双方 AI 不聪明、退出不明显。本轮已加入中继 / 双点传输 / 撤离、三选升级、Q 脉冲、Z/X/C 队友命令与三种敌人预警 / 换位；Esc 菜单含继续、重开、退出按钮，菜单 X 可退出。默认伤害观察抓到真实出生顺序下的感知查询漏建，已在附身与队伍就绪后刷新监听器，并补三条不切换该敌人感知的回归。

- 源码仍在 `C:/Users/yzhu/Documents/ChatGPT/Games/Aegis-Arena`，分支 `codex/aegis-v1`；未提交、未 push、未发布。
- [最终验收](evidence/upgrade-v3/acceptance.json)：381 C++ / 62 Python / 5 Core；32 Tactical / 31 Weapons / 31 Operation / 28 Combat-memory / 54 legacy Trial；独立包 30 项输入与正常退出检查。
- Development 入口：`D:\AegisWork\Packages\uplink-v1.2-development\package\Windows\AegisArena.exe`。
- Shipping 入口：`D:\AegisWork\Packages\uplink-v1.2-shipping\package\Windows\AegisArena.exe`。保留完整 Windows 目录；两包均完成构建、烹饪、归档，Shipping 13 标记静态检查通过。
- 最终源码摘要：`78e833c4a2cfbf7df58bbe91b807c141d2957464676f1db2aaaf16f02af953d5`；资产摘要：`4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。
- [功能与按键](docs/upgrade-v3.md)、[一手获奖设计参考](docs/upgrade-v3-references.md)、原始成功 / 失败报告 `D:/AegisWork/Reports/upgrade-v3-*`；精选原件在 `evidence/upgrade-v3`，包括失败实战与首败编译。

独立 Development 包的默认伤害自动观察实际运行 27.56 秒，结局为 `lost`；玩家 / 队友 / 敌人分别开火 27 / 4 / 16 次，敌人预警 23 次。这是通过正常游戏内输入的有界观察；驾驶器会移动到目标并瞄准射击，没有真人的闪避策略。通过表示交火、移动、真实伤害和记录完整，不能解释为通关、胜率或平衡性结论。

未进行真人平衡评测、长时性能、配对策略实验或最终 Shipping 实体输入验收；本轮没有音效、手柄、重绑定、中文 UI 或 RL 训练。历史 v1.0 / v1.1 包与原始证据保留。

## 历史进度：v1.1 Tactical Trial，本地优化交付 · 2026-09-11

已补齐三波试玩（两档难度、180 秒时限、恢复、胜负与重开）、鼠标瞄准 / 连发 / 方向闪避、玩家 HUD、角色轮廓与射击反馈，并修复 AI 失明记忆到期、EQS 取消和关闭后回调边界。实际源代码根仍为 `C:\Users\yzhu\Documents\ChatGPT\Games\Aegis-Arena`。

- 本地分支 `codex/aegis-v1`，基线 HEAD `daabbe2bee5ab2fe8336d04aa058c0da13f9722a`；本轮改动未提交、未 push、未发布，公开下载仍是 v1.0。没有调用 DeepSeek。
- 已保存源码、原生材质与新增测试地图；重型构建与包继续放 D 盘，未移动共享工具链。
- 复核结果及精确快照以 [evidence/upgrade-v2/acceptance.json](evidence/upgrade-v2/acceptance.json) 为准：347 portable 断言、33 Python 测试、5 Core Automation、28 原生 Combat/memory 断言、54 原生 Trial 断言；独立 Development 包 26 项合成输入检查和四张原始截图。
- 两个最终包已构建 / 烹饪 / 归档；Shipping 11 个开发标记缺失、Development 对照存在。最终 Shipping 未做真人窗口操作验证；不能把 Development 截图称作 Shipping 实测。
- 已验证的试玩入口：`D:\AegisWork\Packages\tactical-trial-v1.1-development-final\package\Windows\AegisArena.exe`。Shipping：`D:\AegisWork\Packages\tactical-trial-v1.1-shipping-final\package\Windows\AegisArena.exe`。应保留完整 `Windows` 文件夹。
- 详细功能、按键、复现命令和证据边界见 [docs/upgrade-v2.md](docs/upgrade-v2.md)。原始成功 / 失败日志在 `D:\AegisWork\Reports\upgrade-v2-*`，精选原件已逐字节归档进 `evidence/upgrade-v2/`。
- 没有新的人类难度测评、长时多设备性能实验、配对策略实验、音效、手柄、重绑定、中文 UI 或 RL 训练。Trial fixture 的明示测试伤害只验证生命周期；离屏合成输入不验证实体鼠标与 Alt-Tab。

**以下为 2026-09-10 目录整理阶段的历史审计原文；其中“本次没有启动测试 / 构建”仅适用于那个阶段，不覆盖上述 v1.1 优化。**

核对日期：2026-09-10，Asia/Shanghai。本记录来自本地 Git、源码、配置、原始 JSON/CSV、构建日志、现存文件及只读获取的既有 GitHub Actions 执行日志；本次只做目录与交接审计，没有启动构建、测试、游戏、模型评测、新 CI 或 DeepSeek 调用。下列验证命令供后续使用，不表示本次执行过。

## 1. 用途、实际目录、启动与环境

- 用途：Unreal C++ 游戏 AI 架构与评估实验场。包含 portable C++17 规则模型、实际 UE BT/BB/EQS/Perception/Navigation 接入、原生关卡与可复核实验数据；不是完整商业动作游戏。
- 整理后实际源码与 Git 根：`C:\Users\yzhu\Documents\ChatGPT\Games\Aegis-Arena`；Git 元数据为 `C:\Users\yzhu\Documents\ChatGPT\Games\Aegis-Arena\.git`。
- 整理后实际源码目录：`C:\Users\yzhu\Documents\ChatGPT\Games\Aegis-Arena`。主交接流程已确认完成迁移；旧路径 `C:\Users\yzhu\Documents\ChatGPT\AegisArena` 保留为指向新根的兼容 junction，供旧 UE/构建缓存路径继续解析。整理后的精确 Git 状态已记录在末节及审计目录 `final-git-status.json`。
- 已读：`README.md`、`docs/status.md`、`docs/unreal-setup.md`、`docs/release.md`、`docs/ai-development-log.md`、`docs/rl-experiment.md`。没有在源码目录、适用 C 盘祖先目录、D 盘 AegisWork 祖先/项目目录找到 AGENTS.md。
- `D:\AegisWork\AegisArena` 不是 Git 仓库，也不是第二套源码工作区。它是本机生成物/工具目录，内含 `.tools`、`Binaries`、`build`、`DerivedDataCache`、`Intermediate`、`Saved`、历史 `outputs`。
- 源码根有 **6 个 junction**：`.tools`、`Binaries`、`build`、`DerivedDataCache`、`Intermediate`、`Saved`，分别指向 `D:\AegisWork\AegisArena\` 下同名目录，审计时六个目标全部存在。源码根 `outputs` 是普通目录；主流程补充检查发现 `outputs/fresh-clone-delivery/.git` 隐藏残留仍是可读的独立 Git 仓库，不能称为空目录。此历史交付 fixture 与主库均各有唯一 worktree，全部保留；D 盘还存在历史生成物。主流程维护其独立 Git 状态/路径登记，不把它当成附加开发项目。
- 引擎：`D:\Program Files\UE_5.8`，日志记录 UE 5.8.2 CL 56702186。MSVC 14.50.35738、Windows SDK 10.0.26100.0；Editor 构建另需 .NET Framework SDK（已使用 4.8 SDK），不同于 UE 自带 .NET SDK。
- Portable：Python 3.10+、C++17 编译器。实测工具为 `D:\AegisWork\AegisArena\.tools\zig-x86_64-windows-0.15.2\zig.exe`（Clang 20.1.2）；基础测试和 benchmark 无 Python 第三方包要求，只有 GIF/PNG 再生成需要 Pillow。
- D 盘共享/重型环境保持原位：`D:\AegisWork\DerivedDataCache`、`Temp`、`UBA`、`Reports`、`Packages`、`Release-v1.0.0`、`PackagedUser` 及上述工具和引擎。没有发现源码/脚本对其余四个项目的直接构建依赖；它们不应一起移动。

独立运行现存成品（未在本次启动）：

```powershell
& 'D:\AegisWork\Packages\shipping-release\package\Windows\AegisArena.exe'
```

从实际源码目录打开 `AegisArena.uproject`。源代码模式的构建/验证命令见第 4 节；这些命令可能启动 UE 或编译，目录整理阶段不运行。

## 2. Git 状态与中断前后的实际进度

- 分支：`codex/aegis-v1`；HEAD：`daabbe2bee5ab2fe8336d04aa058c0da13f9722a`。
- 上游：`origin/codex/aegis-v1`。审计时本地已记录的上游比较为 ahead 0 / behind 0；本次未联网 fetch，不代表实时远端状态。
- Git 目录：源码根的普通 `.git` 目录；主库 `git worktree list --porcelain` 只列出此主工作区，无附加 worktree。`outputs/fresh-clone-delivery/.git` 是独立的历史 fixture 仓库，并非主库 worktree；主流程已确认它也只有唯一 worktree。D 盘 `D:\AegisWork\AegisArena` 自身的 Git 命令返回“not a git repository”。
- 审计开始时 `git status --porcelain=v2 --branch` 没有已跟踪改动、暂存改动或未跟踪文件。忽略的生成物/工具仍存在，干净 Git 状态不等于没有本地成果。
- `e024c3976b21e3f600b3d36010a3dc832ce72d6f`（13:24 +08:00）：portable 核心、实际模型验证、UE 接入源码阶段。
- `68f7431271f5464c48c215f433a613aee63ca67b`（16:40 +08:00）：UE 5.8 原生资产/运行时验证、可复现证据与包。D 盘 `Release-v1.0.0/github-validation.json` 记录发布目标为此提交，两个 ZIP 的本地大小与发布清单一致。
- `daabbe2...`（17:59 +08:00）：加入既有 60 个原生 episode 的配对终点再分析、`decision-case.json`、7 个再分析测试定义及文档/CI 更新。此提交没有改变 UE Runtime 源码或产生新的原生实验；当前运行时/内容摘要仍与原生证据一致。
- 本次交接只能确认上述落盘进度，无法由旧对话计划推断额外完成项。本库本轮仅新增未跟踪交接文档，已在末节及最终Git状态中单独记录；共享脚本路径变化发生在父目录。

## 3. 已完成、失败、运行中、状态不明与待做

### 有落盘原始证据支持的完成项

- Portable 严格 C++17 构建和 325 条 assertions：`evidence/portable/evaluation/report.json` 内 build/tests 记录 failed=0。60 个 portable evaluation episode、20 个 CPU 采样 episode 的原始 JSON/CSV 存在。
- 当前 HEAD 的 **16 项 Python 测试已找到既有 CI 原始执行证据**：GitHub Actions run `34463719748`，headSha=`daabbe2bee5ab2fe8336d04aa058c0da13f9722a`，portable job 完成且 success；UTC 10:00:22 日志列出全部 16 个测试 `... ok`，并明确 `Ran 16 tests in 0.096s` / `OK`。本次只下载既有日志，没有触发重跑。环境为 Ubuntu GCC 13.3.0 / Python 3.11.16；同一 CI 还保存 325 assertions、failed=0 及 frozen endpoint export 检查成功。原本本地 `evidence/portable/python-tests.txt` 与 `D:\AegisWork\python-tests-final.txt` 的 9 项日志、`validation.json` 与 `delivery-check.json` 的 7 项旧阶段记录继续原样保留。
- UE Editor、Game Development、Game Shipping 构建成功；`evidence/unreal/environment/` 下有真实日志，两个 package log 明确含 `BUILD SUCCESSFUL` / `ExitCode=0`。
- 10 个本地关卡/AI/材质资产已保存；`evidence/unreal/asset-inspection.json` 记录 reload 检查结果。
- Core Automation 的 index.json：5 succeeded、0 failed/notRun/inProcess。
- 世界 Functional Test 的 index.json：1 succeeded、0 failed/notRun/inProcess；engine.log 含 `AEGIS_FUNCTIONAL_PASS CombatAndTactical | WorldType=3 | BegunPlay=1`。provenance 记录 12 个 native assertions。
- 最终原生策略评估：priority 和 utility 各 30 个 JSON，共 60 个，配对种子 1001–1030、4 敌人、1/60 fixed game timestep、NullRHI。双方胜利均为 0/30；结束时同伴死亡为 25/6。Utility 平均 episode 更短，不能把该终点比较当成同伴寿命或保护能力因果改善。
- 最终渲染负载采样：1/10/25/50 敌人，每档 3 个，共 12 个原始 episode；D3D12/RTX 4060 Laptop、1280×720、15 秒 damage-disabled 负载。不是长时全场景性能保证。
- 独立 Development package：2 个原始 episode 和 passed provenance 存在；Shipping 静态门禁记录 9 个开发标记均缺失。Shipping UI 验收有原始截图及原有文档陈述，本次未再次观察运行窗口。
- `decision-case.json` 保存 30 对/60 episode 的终点再分析、反例与 canonical JSON 输入摘要；它不包含新模型调用或新 UE episode。

### 保留的失败与已纠正问题

- EQS Controller 未跟随 Pawn、无可用 query 时 BT 错误 Success、arrival 边界已在后续源码纠正。修复前 60 个策略/12 个采样记录保留于 `evidence/unreal/before-eqs-fix/`，不可混入最终结果。
- Functional Test 曾出现 JSON Success 但地图没有测试 Actor 的假阳性，显式地图又暴露 Editor World 问题。两次失败保留在 `before-eqs-fix/functional-false-positive/`、`functional-editor-world-failure/`；当前 wrapper 需要真实 PIE marker、12 条具名断言及无 handled ensure。
- 历史导航冷启动、相机选择、资产生成、渲染超时等失败在 `D:\AegisWork\Reports`、`AssetGenerationFailures` 和根级诊断日志继续保留。不得用最后成功报告覆盖历史失败。
- 原记录有 VC++ x86 旧 MSI 缓存问题及人工修复步骤；本次未运行修复、安装器或权限更改。

### 运行中 / 状态不明 / 待做

- 本次 Win32_Process 快照没有发现命令行指向 Aegis 路径的 Unreal、构建、测试、Python 模型进程；唯一路径命中是本次只读审计 PowerShell。主流程已核对其他 Codex 会话，旧长会话为idle，并在移动前停止本轮子任务访问；迁移已完成。该进程结论仍是时点观察。
- 初查本地测试日志停留在 9 项的证据缺口，已由本次只读取得的 GitHub Actions run `34463719748` 原始日志补齐。16 项成功有与当前 HEAD 绑定的真实 Ubuntu CI 证据；它不代表当前 Windows 环境在目录迁移后重新运行过，也不代表执行了 Unreal。
- `docs/rl-experiment.md` 中“引擎尚不可用”的背景是旧阶段说明；当前引擎构建已经有新证据，但 Learning Agents/RL/模仿训练仍确实未实现、未训练、未评估。不得把实验提案当成已完成。
- 后续剩余范围：感知丢失后的记忆过期、重复 batch 的 Actor/Controller 数量不变量、EQS 取消与无候选分类的定向原生测试；更多地图/独立 holdout seeds；更长 profiling/Unreal Insights。都没有因旧计划而自动完成。

## 4. 验证命令、结果目录与源码/配置绑定

目录整理仅需轻量检查（本次审计已执行 Git/路径读取及源码摘要核对）：

```powershell
git status --porcelain=v2 --branch
git rev-parse --show-toplevel --absolute-git-dir HEAD
git worktree list --porcelain
Get-ChildItem -Force | Where-Object LinkType | Select-Object Name,LinkType,Target
```

后续低成本 portable 核对（本次未执行；先保留现有输出并选新目录）：

```powershell
python scripts/verify_portable.py --compiler 'D:/AegisWork/AegisArena/.tools/zig-x86_64-windows-0.15.2/zig.exe'
python -m unittest discover -s scripts -p 'test_*.py' -v
```

完整 `--full` 评估、UE 构建和运行不属于本次整理范围。今后确需重现时，已保存的入口为：

```powershell
./scripts/build_unreal.ps1 -EngineRoot 'D:/Program Files/UE_5.8' -CacheRoot 'D:/AegisWork' -Automation
python scripts/run_unreal_functional.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/NEW-UNIQUE-functional'
python scripts/run_unreal_scenario.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/NEW-UNIQUE-smoke' --episodes 2 --duration 15
```

`NEW-UNIQUE-*` 是待选占位名，必须换成尚不存在的输出目录；不要使用资产再生成去覆盖已提交关卡。`scenarios/smoke.json`、`evaluation.json`、`performance.json` 属于 portable 协议；UE 场景参数在各 provenance 的 command 和 raw scenario 中，二者不可混为同一环境。

原始证据定位：

| 证据 | 仓库内副本 | D 盘原始/附加结果 |
| --- | --- | --- |
| portable 验证 | `evidence/portable/` | `D:\AegisWork\AegisArena\outputs`、`build` |
| 当前 HEAD 的 16 项 Python CI | [既有 Actions run 34463719748](https://github.com/QiQiyzhu/aegis-arena/actions/runs/34463719748) | `D:\CodexData\ProjectOrganization-20260910\aegis-ci-34463719748.json`、同名 `.log` |
| native 策略 | `evidence/unreal/evaluation/{priority,utility}/` | `D:\AegisWork\Reports\native-priority-release-verified`、`native-utility-release-verified` |
| native 功能测试 | `evidence/unreal/functional/` | `D:\AegisWork\Reports\native-functional-release-verified` |
| native 渲染采样 | `evidence/unreal/performance/enemies-{1,10,25,50}/` | `D:\AegisWork\Reports\native-perf-{1,10,25,50}-release` |
| 独立包执行 | `evidence/unreal/packaged-development/` | `D:\AegisWork\Reports\packaged-development-release` |
| 发布包/校验 | `evidence/unreal/release-manifest.json` | `D:\AegisWork\Packages\{development,shipping}-release`、`D:\AegisWork\Release-v1.0.0` |
| 历史失败 | `evidence/unreal/before-eqs-fix/` | `D:\AegisWork\Reports`、`AssetGenerationFailures`、根级诊断日志 |

按现有脚本的“排序相对路径 + 原始字节”算法，本次只读重新计算并确认：

- 当前 runtime 源码摘要：`58eeb1037ce8338801598d8f9eaa47fec6cd41003cd20bef665d72206fb894e9`。范围 `Source`、`core/include`、`Config`、`AegisArena.uproject`；匹配最终 UE provenance 和发布 manifest。
- 当前 `Content/Aegis` 摘要：`91547daf2b2a7e81c88d18d0e71cabf738018f7b74f942b8ff0443443ddc74c2`；匹配最终 UE provenance 和 manifest。
- 当前 portable `core` 摘要：`948e423c87e652d7e325fa30e6a034f342815edf4be3d3690fe69961af66778e`；匹配 portable report。报告另保存 compiler、flags、binary SHA256、完整 config。
- Functional provenance 本身没有 runtime 源码摘要；其原始时间、执行命令、PIE marker 和断言可核对，但不能假设它具有和策略报告完全相同的独立哈希绑定。
- 当前 HEAD 在 68f7431 之后只增加再分析/测试/文档/CI；源码及内容摘要不变支持复用现有 UE 结果。路径调整不应改写历史 provenance 的 command 或 hash；它们记录的是当时真实路径。
- 当前 HEAD 的 CI 元数据 SHA256：`19516795f53dce45eb788b077a995665662466352161bdbeaaa78c0ad9db99d7`；下载的原始完整日志 SHA256：`cd13f72073a51d85d03fbfc6e18bf9ae1975a8366f13b1cdd0e2358ebefe3906`。元数据记录 completed/success、完整 headSha、jobs/steps 和时间；日志的 checkout SHA、Python 测试列表与最终 OK 可交叉核对。

迁移路径风险：版本控制的 `scripts`、`Config`、`Source`、`.github` 未发现固定旧仓库绝对路径；主要脚本由 `$PSScriptRoot` 或 `__file__` 推导根目录。六个 junction 指向不移动的 D 盘目标。主流程采用旧源码路径兼容 junction，以下本机设置保留原值仍可解析，不必因此改写缓存：

- `D:\AegisWork\AegisArena\Saved\Config\WindowsEditor\EditorPerProjectUserSettings.ini` 的 `CommandLineMapCache`。
- `D:\AegisWork\AegisArena\Saved\SourceControl\UncontrolledChangelists.json` 的 3 个材质资源路径；这是 UE 缓存，不等于 Git 未提交文件。
- Unreal `Intermediate` / build cache 可能含编译期绝对路径；本次不清理、不重建。后续首次构建前复核其可用性，必要时按 UE 机制生成新产物并保留旧记录。
- 历史 evidence、Reports、Saved 日志与发布记录中的旧路径保留，不做全局字符串替换。D 盘临时诊断脚本/环境不因分类重写或搬迁。

## 5. DeepSeek 调用、费用与复用条件

没有在此源码目录、`evidence`、项目 `outputs`、`D:\AegisWork\Reports` 或 D 盘项目历史 outputs 中找到 DeepSeek 请求、响应、usage 或费用原始记录。`docs/interview-dossier.md` 明确将 LLMOps 标为运行时 N/A；这是游戏 AI 实验，没有项目运行时模型 API 或 token 成本管线。

因此无法列出已完成 DeepSeek 调用或金额；这表示本项目范围未找到记录，不能据此断言历史全部会话从未调用。若长会话把外部辅助请求保存到其他共享目录，需要主流程另行关联，不能凭模型生成文档反推已执行。无可用请求/响应、配置、源码版本与账单对应记录时，不声称可缓存复用。本次没有发出任何 DeepSeek 请求，也没有写入密钥。

现有 portable/UE 原始结果可在源码/内容摘要、配置、种子、执行模式及引擎版本一致的前提下复用做再分析；这不是重新运行模型，也不能作为修改后版本的新验证结果。

## 6. 下一轮最值得做的一项任务与验收

**补一项“失去感知后目标记忆按配置过期”的定向原生世界测试。** 这是 `docs/status.md` 明列而未完成的生命周期边界；先读取当前感知与记忆源码定义，在独立 fixture 中验证目标出现、感知丢失、有效记忆期内、期限后和重新感知的行为。本次只记录计划，不修改或执行测试。

验收：下一轮新增一项有界 Functional/Automation fixture，实际运行于 begun-play 的 PIE/Game world；有具名断言验证“目标被实际感知”“失去感知后的记忆状态符合现有配置”“到期后不再使用过期目标信息”“重新感知后恢复”，对不可见目标的检查不得引入全知信息。记录当前 HEAD/未提交源码摘要、配置的记忆期限、固定场景/seed、引擎版本、命令、原始 stdout/engine.log/report 和明确 world marker；需要真实断言执行，不能仅凭 JSON Success 或退出码接受。输出使用新唯一目录，失败保留，不覆盖现有 60 个策略/12 个性能 episode。此任务仅运行定向测试，不要求重跑整套评估，也不需要 DeepSeek。

## 本轮目录整理结果

- 整理完成记录时间：2026-09-10T22:30:09.441410+08:00。适用祖先和主项目范围未发现 AGENTS.md；已读项目说明。仅第三方缓存内局部 AGENTS 不适用于本次主库文档。
- 原工作目录从 `C:\Users\yzhu\Documents\ChatGPT\AegisArena` 在同一 C: 卷重命名到 `C:\Users\yzhu\Documents\ChatGPT\Games\Aegis-Arena`；保留原 `.git`、ignored 文件和未提交成果，没有重新克隆。旧路径创建了指向该目录的兼容 junction。桌面应用新会话应选择新实际目录；旧入口和新目录是同一份文件，不能作为两个独立项目同时开发。
- 迁移前后5个主库共2,816个源码/配置/原始结果文件逐字节哈希一致；包含历史夹具的41个Git元数据目录内容一致；无附加registered worktree，分支/提交/原有status均未变。package/cache目录未逐文件哈希，完整原目录重命名保留，未重装或删除。详细基线、排除范围、junction和比对见 `D:\CodexData\ProjectOrganization-20260910\migration-verification.json`。
- 本轮最终新增未跟踪文件为 PROJECT_HANDOFF.md；没有暂存、提交、push、reset、clean或功能源码更改。ARC原五项未提交改动另行完整保留。最终状态见 `D:\CodexData\ProjectOrganization-20260910\final-git-status.json`。
- 旧长会话“开发 ARC//SHIFT 动作肉鸽游戏”已核对为idle、最近恢复turn失败；移动前的进程/端口核验未发现项目构建、测试、数据库、模型评测任务。参与本轮审计的子任务在移动前已停止读取主库。进程可见性有时点限制，恢复开发前仍应检查占用；本轮没有终止用户进程。
- 父目录三个共享手册脚本已备份并只更新项目/Playwright路径；语法检查通过。未重新生成桌面手册、旧公开JSON或网站，历史状态/哈希/命令路径原样保留。D盘历史脚本、venv editable、UE/CMake缓存通过旧路径兼容入口继续可定位，不要批量改写历史结果或删除这些入口。
- 6个构建junction目标与迁移前相同且可访问。两处Saved设置和编译缓存继续使用旧入口，本轮不修改或重建UE环境。
- 解释器核验只做模块位置查找，没有导入应用、启动服务或调用模型；结果见 `D:\CodexData\ProjectOrganization-20260910\path-check-results.json`。本轮未执行完整构建/测试/昂贵评测，未发起新的DeepSeek请求。
