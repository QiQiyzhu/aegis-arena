# Aegis Arena v1.1 · Tactical Trial

本轮把已有 UE AI 实验场补成有完整开局、操作、目标和结算的三波战术演示。新交互模式与历史 scripted benchmark 使用不同的关卡流程；不能把它们的胜率或时间混在一起。

## 怎么玩

进入简报后，按 **1** 选 Guided（每波 2 / 3 / 4 敌人），或 **2** 选 Pressure（3 / 4 / 5）。按 **Enter** 开始，在 180 秒内清理三波。第三波含一名精英。

- **WASD / 方向键**：按屏幕方向移动。
- **鼠标**：瞄准地面位置；**按住左键**连续射击，**右键**近战。
- **Space**：向移动方向闪避；静止时向瞄准方向。冷却 2.4 秒，有墙体碰撞，没有无敌时间。
- **P**：暂停 / 继续。**R**：返回简报，重新生成完整角色状态。
- **F1**：仅 Development 提供原有 AI 诊断；Shipping 保留玩家界面和战斗反馈。

每次清场有 5 秒恢复时间。Guided 恢复玩家 30 / 同伴 40 生命，Pressure 恢复 15 / 20，均不超过上限，也不复活死亡同伴。可利用恢复时间调整位置。玩家死亡或时间耗尽判负；同时死亡和最终清场优先判负。

## 本轮改动

1. 独立的 C++ Trial 状态模型管理简报、战斗、恢复、胜利和失败；固定三波及上限，旧批次评估流程保留。
2. Shipping 玩家 HUD 显示生命、同伴决策意图、敌人数、倒计时、闪避冷却、操作提示和结算数据。简报、暂停和结算有独立同伴状态提示；决策意图不等于执行成功。Canvas 随视口缩放；没有宣称读屏支持。
3. 鼠标地面瞄准、持续开火、方向闪避、输入失焦处理与重开按键清理。
4. 用引擎基础网格制作不同阵营 / 角色轮廓、枪口朝向、落地点、受击闪白，以及正式版可见的短射线。每个角色复用两个射线组件，NullRHI 不创建新增显示组件和计时器。
5. 地面网格、入口标记、掩体轮廓通过三个实例化网格组件绘制，不参与碰撞或导航。
6. 修复失去视野后仍引用目标、过期位置仍进入调查 / EQS、停止后迟到查询恢复状态等边界；详见 [AI 修复与证据](ai-reliability-v2.md)。新增 `cancelledQueries` 是原有失败计数中的取消子集，不能与失败计数再次相加。
7. 导航启动检查改为验证双方实际出生区域之间存在完整路径；修复渲染运行中等待辅助 Actor 地面点而无法开场的问题。材质保存实例化网格支持，HUD 使用烘焙字体，解决实测的材质回退和间歇缺字。

## 验证与交付

2026-09-11 本地交付的验收记录见 [acceptance.json](../evidence/upgrade-v2/acceptance.json)，原件索引见 [证据目录](../evidence/upgrade-v2/README.md)。各层检查分别统计：

| 层次 | 实测结果 |
| --- | --- |
| Portable C++17 / Python 工具 | 347 条 C++ 断言、33 项 Python 测试通过 |
| UE 5.8.2 Editor | 编译成功；5 项 Core Automation 通过 |
| 原生 PIE Combat / memory | 28 条断言通过，含实际感知、记忆截止、重获目标和 EQS 取消 |
| 原生 PIE Trial | 54 次断言通过，覆盖波次、恢复、胜负优先级及重复重开生命周期 |
| 独立 Development 包 | 构建、烹饪和归档成功；26 项合成输入检查及四张原始截图验证通过 |
| 独立 Shipping 包 | 构建、烹饪和归档成功；11 个开发标记均不存在，Development 对照全部存在 |

最终 Development 包四张截图已目视检查文字、布局、瞄准反馈和暂停状态。Shipping 的本轮证据止于打包与静态边界检查，没有用 Development 截图代替 Shipping 试玩证明。

本机可直接双击以下路径中的 `AegisArena.exe`；分发时需保留整个 `Windows` 文件夹，不能只复制 EXE：

```text
D:\AegisWork\Packages\tactical-trial-v1.1-development-final\package\Windows\AegisArena.exe
D:\AegisWork\Packages\tactical-trial-v1.1-shipping-final\package\Windows\AegisArena.exe
```

Development 是已完成输入验证的试玩入口；F1 可打开开发诊断。两个包各有文件清单和 SHA-256，位于验收目录 `packages/`。源码改动保留在本地 `codex/aegis-v1` 工作区，尚未提交或发布；公开下载仍为 v1.0。

原始运行使用 `D:/AegisWork/Reports/upgrade-v2-*` 的独立目录。首次编译失败、编辑器主页联网超时导致的整体验收失败，以及渲染启动导航失败均保留。修复后使用新目录复跑，没有覆盖失败原件。

关键复现入口：

```powershell
python scripts/verify_portable.py --compiler '<your zig.exe or clang++.exe>'
./scripts/build_unreal.ps1 -EngineRoot 'D:/Program Files/UE_5.8' -CacheRoot 'D:/AegisWork' -Automation
python scripts/run_unreal_functional.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output '<new-memory-directory>'
python scripts/run_unreal_trial.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output '<new-trial-directory>'
python scripts/run_unreal_input_probe.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output '<new-input-directory>'
```

输入检查向真实 PlayerController 注入按键并使用离屏虚拟光标，覆盖简报禁用战斗、移动、闪避、按住开火、暂停冻结和按键按住时重开，共 26 项；同时验证四张未改写的 1280×720 UE PNG。它不移动操作系统鼠标。可增加 `--packaged-exe '<Development game exe>'` 验证独立包；此时 `--engine-root` 只读取构建版本，不启动编辑器。

`AegisTrialFunctional.umap` 为本轮新增的独立测试地图；脚本 `scripts/unreal/create_trial_fixture.py` 可在该资产不存在时创建它，拒绝覆盖已有资产。原有两个地图和历史原始实验未被改写。

## 证据边界

Trial Functional Test 停止 AI 后施加明示的测试伤害，检验状态转换、恢复和生命周期；它不证明自然对局可通关或难度合理。自动输入检查同样不等于真人首次试玩。

原有 60 个原生策略 episode、0/30 对 0/30 和观测终点混杂仍保留；新版 AI 需要新的配对实验才能谈策略质量。旧 12 次渲染测量也不能直接代表新增显示组件后的性能。

声音设计、可重绑定输入、手柄、中文界面、读屏、真人难度测试和多设备长时性能是后续工作。当前是范围受控的技术展示，不把赛事调研等同于参赛资格或已提交。具体调研见 [赛事与产品体验参考](competition-research-2026.md)。
