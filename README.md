# Aegis Arena · 棱镜坠落

一款单人科幻俯视角射击游戏。与 AI 队友协作，夺回中继、传输数据并撤离；途中选择升级，扫描可选档案获得补给或密钥。

## ▶ 下载后直接玩

**[下载 Windows 试玩版](https://github.com/QiQiyzhu/aegis-arena/releases/tag/v2.5.0-rc.1)** · **[详细游玩说明](docs/PLAY.md)** · [版本与验证](docs/release-v2.5.md)

**[查看 UI 与战斗特效的美术策划档案](https://arc-shift.black-kid-3047.chatgpt.site/art-direction/index.html)**：真实界面对照、战术信息层级、技能节奏与验证依据。

1. 打开上方下载页，在 **Assets** 中下载 **`Aegis-Arena-v2.5.0-rc.1-Windows.zip`**。不要选择 `Source code`。
2. 右键 ZIP → **全部解压缩**，保留整个文件夹。
3. 双击 **`PLAY-Aegis.cmd`**（也可以打开 `AegisArena.exe`），按 **Enter** 开始。

无需 Unreal 编辑器、Python、账号或 API Key。面向 Windows 10/11 64 位电脑，使用键盘和鼠标；最低硬件要求尚未完成系统测定。

| 最常用操作 | 按键 |
|---|---|
| 移动 / 瞄准 | WASD / 鼠标 |
| 射击 / 蓄能射击 | 左键 / 按住右键后释放 |
| 闪避 | Space |
| 脉冲 / 修复 / 中继超频 | Q / E / F |
| 暂停 | Esc 或 P |
| 退出游戏 | 暂停菜单或结算页中按 X |

**第一次玩：跟随当前目标，留意共享能量，最后亲自进入撤离区。** L 切换中英文；M 开关音乐；K 切换精简特效。更多队友指令、扫描与路线操作见 [游玩说明](docs/PLAY.md)。

这是经过本机验证的独立游戏试玩候选版，包含一张竞技场和三阶段任务。没有多人联机，也没有中途存档恢复；当前局退出后需重新开始。已知限制与本次测试范围见 [v2.5 记录](docs/release-v2.5.md)。

<details>
<summary>开发者：源码、构建与历史证据</summary>

Unreal 项目使用 UE 5.8.x 与配套 Windows C++ 工具链。可移植 C++ 层可独立验证：

```sh
python3 -m pip install -r requirements-media.txt
python3 scripts/verify_portable.py --compiler g++
```

原生构建请读 [Unreal 安装与验收](docs/unreal-setup.md)。GitHub 的 Portable CI 验证可移植核心和 Python 工具，不代表云端构建或运行了 Unreal。

[架构](docs/architecture.md) · [AI 设计](docs/ai-design.md) · [历史首页](docs/readme-history-before-v24.md) · [v2.3 交付](docs/portfolio-v2.3-delivery.md) · [源码许可](LICENSE) · [资产来源](ASSETS.md)

当前主模式使用战术规则队友；Decision Lab 和云端 Copilot 是单独的历史实验入口，不需要它们即可游玩。没有 RL 训练或新策略效果提升声明。项目由用户提出方向，Codex 协助开发、测试和文档；自动化操作不等于真人研究。Unreal 运行时保留 Epic 自身许可。

</details>
