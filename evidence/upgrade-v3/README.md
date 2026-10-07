# v1.2 本地原始证据归档（upgrade-v3）

## 最终交付记录

最终结果见 [acceptance.json](acceptance.json)：最终快照的 32 / 31 / 31 / 28 / 54 原生回归、381 C++ / 62 Python / 5 Core、独立 Development 包 30 项输入与正常退出检查通过。两种配置包文件清单与 13 项 Shipping 标记对照保存在 `packages/`。

独立 Development 包的默认伤害自动观察实际运行 27.56 秒，结局为 `lost`；玩家 / 队友 / 敌人分别开火 27 / 4 / 16 次，敌人预警 23 次。这是通过正常游戏内输入的有界观察；驾驶器会移动到目标并瞄准射击，没有真人的闪避策略。通过表示交火、移动、真实伤害和记录完整，不能解释为通关、胜率或平衡性结论。

`failures/play01` 保存一分钟敌人零开火的原始失败。`sources.json` 仍记录下方最初归档阶段；新增副本、来源和哈希在 `acceptance.json.finalCopiedFiles`。下文的未完成描述仅适用于最初阶段，不是当前状态。

## 最初归档阶段（保留）

本目录逐字节保存已完成的本地运行记录；不是总体验收声明，也没有生成 `acceptance.json`。`sources.json` 将每个复制文件映射到原绝对路径、相对归档路径、字节数及 SHA-256，另列明各组证据范围与源码绑定。复制时已核对来源、目标哈希和大小。

| 目录 | 原始结果 | 证据范围 |
| --- | --- | --- |
| portable | 381 条 C++ 断言、52 项 Python 测试通过；2 个短 smoke episode | portable C++ 模型与 gate 单元测试，未调用 Unreal |
| tactical | 29/29 通过 | PIE 原生夹具：显式队伍链接、Guard/Rally/Focus、合法视觉、预警取消与三敌人角色的真实移动/开火/换位；伤害关闭，夹具控制感知开关并生成遮挡物 |
| weapons | 31/31 通过 | PIE 固定几何夹具；冻结移动、注入一次 windup，使用真实 TryPulse 入口和实际伤害 |
| operation | 31/31 通过 | PIE 目标集成夹具；介入时停止 AI、摆放占圈角色、施加伤害，验证目标、升级暂停、终局与重开 |
| editor-build | 第二次构建成功 | 原编译日志；runner-console 同时含随后 Core automation 输出 |
| core-automation | 5 项通过 | Aegis.Core 自动化，独立于上面三张功能测试图 |
| assets | 三个创建标记出现 | Tactical、Weapons、Operation 功能测试图生成日志 |
| failures/build01 | 首次构建失败 | 原失败日志，详见 failures/README.md |

三组功能测试均为 `unreal-runtime / PIE / NullRHI`。29/31/31 条是有界原生夹具断言，不是三组真人游玩、完整输入验收或帧率/性能证据。Weapons 的 `-AegisInputProbe` 提供离屏本地控制器上下文，不代表真人键鼠操作。

三组原生报告保存相同的原始源码摘要 `278a30cd6308d6b40ae58d042a7436272973b5cfde30837dd3a15ef7732b88a4`、资产摘要 `4333792af4623cd2d405171a76b80741abb7b393063f4d3a218e45bb25e440a5`。源码范围为 Source、core/include、Config、uproject；资产范围为 Content/Aegis，均按排序后的相对路径与原字节摘要。Tactical 原 provenance 未写 scope 字符串，manifest 明确将其范围标为根据 runner 实现解释。Operation 原报告还记录运行后相同摘要。

Portable smoke 的 `source_sha256` 为 `57f517fc8c348e8ec96bcf8b9cf1728e93aa51768772ca06b5e2cdc048188d78`，只覆盖 core/**，与原生摘要口径不同。构建和地图生成日志没有独立源码/资产摘要，不强行绑定到随后测试。未来源码、UI 或打包变化不会改写这些旧记录，也不能将这些记录自动视为后续包的验证。

未收集尚未完成的 input/memory/trial 报告、打包产物、HTML/网页资源、exe/PDB 或 cache。所有复制文件均保持原字节；仅本 README、failures/README.md 与 sources.json 是归档说明。
