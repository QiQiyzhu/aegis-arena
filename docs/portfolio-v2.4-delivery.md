# Aegis Arena v2.4.0-rc.1

本版将已有的「棱镜坠落」三阶段流程整理为 Windows 发布候选包，重点是玩家第一次运行、暂停、设置与退出时的可靠性。内容范围仍是一个竞技场、三个阶段、两处可选档案，不宣称已完成商业商店上架或跨硬件验收。

## 玩家可见变化

- Shipping 版双击 `AegisArena.exe` 直接进入最新版中文简报，包含蓄能射击、路线、阶段升级、档案扫描与现有音画资源。
- `PLAY-Aegis.cmd` 只使用自身所在目录，缺少完整解压内容时显示明确说明，没有作者机器上的盘符回退。
- 音乐开关与语言一起保存；简报、暂停、升级与结算页均可点击语言、音乐和显示按钮。
- F11 切换窗口与无边框全屏并保存。新安装默认 1600×900 窗口、VSync、60 FPS 上限。
- 正常游玩切出窗口会自动暂停，取消蓄力并清空按键状态。返回窗口后保持暂停，由玩家按 Esc 继续。
- 已开始的一局按 R 先显示重开确认。Esc 取消后保留本局且保持暂停；再次按 R 才重置。结算页可直接开始新一局。
- 包内包含简明游玩说明、VC++ x64 运行库、项目许可证及字体/FreeType 说明。

## 已完成的本机验证（2026-10-07）

| 验证 | 结果与范围 |
|---|---|
| UE 5.8.2 Editor C++ 编译 | 成功 |
| `Aegis.Core` Automation | 8/8；包括语言和音乐持久化、自动化不读写玩家偏好 |
| 新发布流程原生 fixture | 10/10；最新版启动、菜单设置、焦点回调暂停、返回不自动恢复、重开取消/确认与干净新局 |
| v2.3 调查缓存原生回归 | 25/25；连续扫描、中断、唯一领取、实际治疗、中继密钥与正常菜单退出 |
| Windows Shipping BuildCookRun | BUILD SUCCESSFUL，AutomationTool ExitCode=0 |
| 发布二进制隔离 | 实际包中 19 项开发注册/探针输出字符串均不存在；对应新 Editor 模块均存在 |
| 原生截图检查 | 已检查 1600×900 中文简报、失焦暂停页、重开确认页，无遮挡或文字溢出 |

发布流程 fixture 使用真实 D3D11 游戏世界和正常菜单方法，但按键/焦点事件由代码驱动，不能等同于真人操作或 Windows Alt-Tab 验证。调查回归明确使用冻结 AI、位置和伤害夹具，也不是自然战斗效果评价。Shipping 实际窗口检查与最终压缩包散列由发布整合步骤另行记录。

第一次发布流程 fixture 在并行构建期间导出截图超时，失败报告保留；已修正 fixture 初始化和有界截止时间，并在独立的新输出目录完成 10/10 复验。未以进程退出码替代断言成功。

音乐与语言存放在 Unreal 生成配置目录中的 `AegisUI.ini`；显示设置由 `GameUserSettings.ini` 管理。录制与探针不覆盖这些玩家设置。发行版本为 2.4；原有玩法账本的 `gameplayVersion=2.3` 保持其规则验证协议，避免把启动/设置升级冒充新的战斗规则。

## 开发者复验入口

```powershell
./scripts/build_unreal.ps1 -EngineRoot '<UE_5.8>' -CacheRoot '<cache>' -OutputRoot '<new-report-root>' -Automation
python scripts/run_release_probe.py --editor '<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --cache-root '<cache>' --output '<new-release-flow-output>'
python scripts/run_v23_probe.py --editor --exe '<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --cache-root '<cache>' --output '<new-survey-output>'
./scripts/package_unreal.ps1 -EngineRoot '<UE_5.8>' -CacheRoot '<cache>' -OutputRoot '<new-package-root>' -Configuration Shipping
```

Development/Editor 的历史测试模式仍保持显式选择；开发者可用 `-AegisRelease` 进入当前发布配置。所有新探针执行逻辑均排除于 Shipping 编译。
