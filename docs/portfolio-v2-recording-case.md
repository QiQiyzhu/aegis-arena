# v2 录制工具故障：清敌后没有输出撤离移动

本案例只讨论 `AAegisPortfolioCapture` 录制驾驶器的可靠性。它通过正常玩家输入演示游戏，却在没有可用导航路径时持续停止输出移动，导致已经清敌的行动超时。这里没有证明队友或敌人的策略退化；后续驾驶器修复也不能算作 AI 策略提升、胜率提升或真人体验改善。

原始运行完整保留在 `D:/AegisWork/Reports/v2-20260918/capture-packaged-01/`。这是 seed 1101 的 Development 包，使用脚本输入、真实战斗规则、1920×1080 连续原生图像和固定 30 FPS 游戏时钟。原生报告明确 `scriptedPlayer=true`、`fixtureDamage=false`、`humanPlaytest=false`。运行结果是 **Lost，180.000009 游戏秒超时**；没有为展示目的改写结果。

## 能够直接复核的停滞

以下时间来自 `capture.json` 的实际采样。`videoSeconds` 是录制时钟，`worldSeconds` 是世界时钟，均不能直接当作任务面板中的已用时间。阶段间暂停会使两个时钟产生差值。

| 录制时间 | 世界时间 | 实际观察 |
|---|---|---|
| 94.633338 s | 88.566671 s | 玩家约 `(-488.739,-19.070)`；路径已为 0 点，WASD 全松开，速度仍为 166.70 cm/s。 |
| 94.733338 s | 88.666671 s | 玩家在 `(-489.739526,-19.246729)` 完全停住，速度 0；当时还剩 1 名敌人。 |
| 113.433339 s | 107.366672 s | 敌人数归零；驾驶器目标正确变为撤离点 `(-1250,0,10)`。玩家未动，水平距离为 760.504060 cm。 |
| 191.033343 s | 184.966676 s | 超时前最后一条 Active 采样，仍是同一位置、同一目标和零移动输入。 |

清敌后的 Active 区间 `113.433339–191.033343` 录制秒，持续约 **77.6 秒，共 777 条采样**。这些采样全部满足：

- `pathPoints=0`，`pathIndex=0`，`driverDirection=[0,0]`。
- `heldWasd` 与 `actualWasd` 都是四个 `false`，即驾驶器记录与 PlayerController 实际状态一致。
- `speed=0`，位置始终为 `(-489.739526,-19.246729)`；角色中心 Z 约 90.15 cm。
- `combatEnabled=true`，`moveInputIgnored=false`，阶段仍为 Active。

另有 41 条终局采样中 `combatEnabled=false`，那是结算后的正常禁用，不能倒推为先前停滞的原因。停滞也不是清敌后才开始：角色已经在最后一名敌人死亡前约 18.7 个录制秒停住。

## 已定位到的机制与未记录的底层原因

冻结版本的 [Drive](../Source/AegisArena/Private/AegisPortfolioCapture.cpp) 每 0.4 秒清空旧 `Path`，投影角色脚底与目标到导航面，再请求同步路径。只有 `IsValid() && !IsPartial()` 时才保存路径点。没有有效路径点时，方向保持零，随后 `SetKey()` 主动松开 WASD。

本次样本支持的直接机制是：**驾驶器持续没有可接受路径，没有恢复分支，因此停止发送移动输入。** 路径点为零而不是索引走到路径末端，排除了单纯的 `PathIndex` 耗尽。目标正确、输入未被禁止、记住与实际按键一致，也不支持“目标切换错了”“菜单锁住移动”或“丢失按下事件”的解释。停住前仍有正常减速样本，不能把该位置直接解释成被墙卡死。

当前记录没有分别保存 `ProjectPointToNavigation` 起点 / 终点结果、返回路径有效性和 partial 状态。因此不能进一步断言是哪一次投影、哪个 Recast 内部错误或哪块导航多边形导致失败。画面中的开放地面也不等价于已经证明该处导航查询有效。

## 有边界的修复建议与验收状态

后续代码已加入录制器的有限恢复分支。首次编辑器校准没有触发恢复；之后 r2 暴露了 profile 查询错误，r3 才取得下文列出的有效局部恢复证据。该分支只在 `bV2`、敌人数为零、没有可见攻击目标、`Path.Num()==0` 且距公开目标超过 120 cm 时运行。它用角色完整胶囊检查通向目标的水平通道；只有全程无阻挡才输出正常 WASD，有阻挡则不直冲。

扫掠终点保持当前胶囊中心高度，没有直接使用导航目标的 Z=10。第一次实现通过 profile 名字查询，但后续包体校准暴露了 `Custom` 没有注册的问题，不能把该实现视为已正确验证角色碰撞。当前修订改用 `SweepTestByChannel`，传入胶囊实际 `GetCollisionObjectType()` 及 `FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())`，保留缩放后的完整 capsule 并只忽略自身。正常碰撞、角色移动、输入、资源与任务规则仍负责实际执行；不瞬移，不调整导航资产，不清敌，不修改伤害，也不修改队友或敌人的策略。

新增 `pathQueryStatus` 区分导航不可用、起点投影失败、目标投影失败、没有完整路径及完整路径。`clearRouteChecks` / `clearRouteAccepted` 是检查与扫掠资格通过的帧数，不是到达次数。`clearRouteRecovery` 也不能代替真实位移证明：最后一次躲避的短计时可能暂时覆盖最终方向。后续仍需检查实际位置、输入与正常撤离结果。

这是一个录制工具修复；既有 Guard 受控比较和经典 Utility / BT / EQS 的独立案例继续按各自证据范围解释，不能与本例合并成同一个 AI 改进结论。

## 首次编辑器校准没有触发恢复

`D:/AegisWork/Reports/v2-20260918/capture-editor-calibration-02/` 的严格包装验收为 PASS，任务自然 Won，实际任务时间 **92.600005 秒**，完成 3 阶段与 12 次击杀。该次使用相同 seed 1101、improved Guard 与同一资产摘要，但启动方式为 `editor-game`，关闭 PNG 保存，`frames=0`。它不是最终包体或最终视频证据。

原始全部样本中，`clearRouteChecks` 和 `clearRouteAccepted` 的最大值均为 **0**，`clearRouteRecovery=true` 的样本数也为 **0**。因此，本次顺利结束说明这一局没有持续停在原故障中，不能说明胶囊恢复分支曾执行或帮助通关。即使出现过 `no_complete_path` 状态，也不能在触发计数为零时把它算作一次恢复成功。原故障与这次运行的战斗和运动轨迹已经不同，不能做同一状态下的因果配对。

这次校准绑定的新源码为 `e9efa67021632fcf667365e11301ff6bb042c1562d9aa04680fc189f596afa2d`；源码和资产运行前后保持一致。原始 provenance SHA-256 为 `95b862a071fce459e3a3559063731c42c5c017e73be173d09193745bc3f34083`，`capture.json` 为 `4f0d2b9ed01a2133ace656a3e8b1ee1c43a922464a378867dce90b6fca006e79`，系统结算 JSON 为 `4912d1b4042e3e88ecbbb1333611f3cde93442ad0f528b651256206dada10243`。对应的 r2 包体失败另列于下一节，不与这次未触发恢复的编辑器校准混合。

## 包体校准暴露了错误的碰撞 profile 查询

`D:/AegisWork/Reports/v2-20260918/capture-packaged-calibration-02/` 使用同一 `e9efa...` 源码与资产。原生游戏报告为 Won，任务时间 **106.800006 秒**，`clearRouteChecks=24`、`clearRouteAccepted=24`；但包装验收为 **FAIL**，日志出现 **24 条** `COLLISION PROFILE [Custom] is not found`。不能把这组数值当作正确胶囊检查帮助通关的证据，也不能仅因游戏 Won 而豁免引擎警告。

角色构造函数改变了 capsule 的碰撞响应，因此组件的 profile 名称可能是未注册为预设的 `Custom`。UE 5.8 `World.cpp` 的 `GetCollisionProfileChannelAndResponseParams()` 在 profile 查找失败时会发出警告，并回退为 `ECC_WorldStatic` 和默认全 Block 响应。旧代码仍执行了查询，但过滤配置不再代表该角色的实际碰撞规则。因此“24 次接受”只说明这条错误配置的调用返回无阻，不能说明它正确检查了角色实际通道。

修订改为直接传入对象类型和实际响应容器。UE 的 `FCollisionResponseParams` 容器构造函数直接复制该表；`SweepTestByChannel` 将该对象通道、响应参数与完整胶囊交给几何 sweep，不再依赖 profile 名称查找。触发范围与正常 WASD 执行保持不变。r3 原生校准结果单独列于下一节。

| 失败原件 | SHA-256 |
|---|---|
| `capture-packaged-calibration-02/provenance.json` | `d9ace2d1049ec3b2068617681ee482413b5bb1a9c0d416b2e02c948da74e5a20` |
| `capture-packaged-calibration-02/capture.json` | `ec9125d796d3a374bc05c187a8a4492c71b9315bea438714f3da46e8a5b8d7ba` |
| `system/portfolio-75B3DCFA47AE16966B5746BDE76A1A52.json` | `3ba8e90e7abca8c2fa7510a56ec523cdca16357e852fb68e464bacb930e3515d` |

这次 `passed=false` 会被最终验收脚本自动列入保留失败记录；它与第一局“证据 PASS、游戏 Lost”的工具故障是两种不同结果，均不覆盖。

## r3 在原停滞区域实际触发恢复

`D:/AegisWork/Reports/v2-20260918/capture-packaged-calibration-03/` 使用 r3 Development 包、seed 1101、新源码 `6dad19526dbc6b093a841ccf46f040e48148e37700d3e5a2184d6f66d42b3cc2` 及同一资产摘要。严格验收 **PASS**，任务自然 Won，用时 **106.800006 游戏秒**。它仍是 `frames=0` 的包体校准，不是完整录像证据。

独立只读重放 `validate_capture()` 与 `validate_system_capture()` 通过，原件清单和源码 / 资产 / 包体 / 包装脚本前后绑定一致；日志中没有 Custom profile 警告。`clearRouteChecks` 和 `clearRouteAccepted` 均为 **24**，这是 24 个检查帧，在 0.1 秒采样中对应 8 条 `clearRouteRecovery=true` 记录，不是 24 次到达。

| 录制时间 | 世界时间 | 实际位置 cm | 查询与输入 |
|---|---|---|---|
| 113.333339 s | 107.266672 s | `(-489.739526,-19.246729)` | 与原始局同一停点；剩 1 敌，零路径，未触发恢复。 |
| 113.433339 s | 107.366672 s | `(-491.980511,-19.641875)` | 敌人为零；`no_complete_path`、恢复为 true，实际与记住的按键均为 S+A，速度约 68.27 cm/s。 |
| 114.133339 s | 108.066672 s | `(-757.397159,-66.441993)` | 仍为 `no_complete_path`、恢复 true、S+A，速度约 420 cm/s。 |
| 114.233339 s | 108.166672 s | `(-798.759086,-73.735217)` | 已返回 3 点 `complete_path`，恢复 false，累计检查 / 接受停在 24。 |
| 114.733339 s | 108.666672 s | `(-995.357740,-25.824653)` | 玩家实际进入撤离点的 260 cm 范围。 |

前两条恢复采样之间的 0.7 秒净位移为 **269.511128 cm**，距撤离点由 **758.273928 cm 降至 497.063475 cm**。这段所有采样均 `heldWasd == actualWasd`，没有以“资格通过”代替真实移动。随后沿完整导航路径推进，首次 Won 采样在 `video117.900006 / world111.833339`；任务总用时与这两个时钟含义不同。

与历史全帧 `capture-packaged-01` 作有限比对：恢复开始前 1140 个匹配采样时刻的玩家位置、速度、敌人数、波次、射击次数、生命、能量及 Q / E / 蓄能 / 超载次数共 11 个字段逐项一致。双方在 `video113.333339` 仍处于同一停点，之后原局持续静止，r3 从该区域实际移出。这个局部对应比“另一局恰好赢了”提供更明确的故障复现依据；它仍然只有一个 seed、一个开放通道，不能证明其他障碍、坡面或导航异常都能恢复，也不证明战斗策略或真人体验改善。

| r3 校准原件 | SHA-256 |
|---|---|
| `provenance.json` | `bfd011d97ebe8617c9760b0c6857fd74da7aa75d1c67b7d41029710384e0a31b` |
| `capture.json` | `ec9125d796d3a374bc05c187a8a4492c71b9315bea438714f3da46e8a5b8d7ba` |
| `system/portfolio-4861C887425E1B39C93B58A58CE1A198.json` | `108cbd9c7ae744598d4bb0f56b8ee40d92a515d5564e57fb348dd8976ad4d280` |
| `system/portfolio-4861C887425E1B39C93B58A58CE1A198.jsonl` | `0b6f5150da777a93145b4f31b8610ca63b229d645fe74cc599aa1ef19c0f9b99` |

## 原始停滞局的完整结果与证据边界

原生记录共有 **5852 张原生 PNG**，录制时间为 **195.100010 秒**。任务清除了 12 名敌人，但只完成 2 个阶段，没有完成撤离。实际账本记录：

- 名义收入 `12×12 + 2×25 = 194`，无溢出；余额 `60 + 194 − 237 = 17`。
- Q 1 次、E 4 次、蓄能 1 次、超载 3 次；E 实际恢复玩家 104 HP、同伴约 0.93335 HP。
- 团队实际输出 1260；玩家承伤 238，同伴承伤 42；同伴存活。
- 中继共生另恢复同伴约 1.06667 HP，不算作 E 修复量。

证据门禁通过表示原始运行、图像、账本及退出等记录自洽，不表示游戏结果为 Won，也不表示驾驶器没有缺陷。本局不得被用作已经完成全部阶段的最终展示。固定步长离线采集亦不是硬件性能测试。

冻结源码 SHA-256：`df57a548edcfcfce40e0d1855286d75894985e7cbf89c71173c80a765ad65b69`。

资产摘要：`9667582db87935a68d8823eca0801b1c47ade897f12a1c848f926a4e02e88fd5`。

`capture.json` SHA-256：`199b20f42bcc764ef8b7a9df5bcaa05074380993bd3acf0ceeeed0a6b8d5106d`。

包装脚本已完成完整 PNG 校验：`provenance.json` 为 `passed=true`，`outcome=lost`。5852 张图像均列入原始文件哈希清单；源码 / 资产、启动器 / 游戏载荷 / cooked 容器以及包装脚本的前后快照一致。281 条系统事件完成账本核对，伤害角色归属为 `full`；结算停留约 4 秒后由正常 X 输入退出。没有据此把 Lost 改为 Won。

| 原始文件 | SHA-256 |
|---|---|
| `provenance.json` | `45043515567c5247bf5d51e5fcca02ccf9947ff6ddc7219d93ae7726d00cbf03` |
| `system/portfolio-A23976734FA7FB6C81782FB2ECBFD54F.json` | `223e94ad0c739e4728ddb2553fee568d509d8f0876ead0468fc9d9fc95faccdb` |
| `system/portfolio-A23976734FA7FB6C81782FB2ECBFD54F.jsonl` | `c5cc5380832a5b8ebe63a4b3e59bb11d05e68cf2ce04889479ca5a15fd2252ed` |

## 最终验收中如何保留本例

`review_v2_release.py` 的 `preserve_failures()` 自动发现 `passed=false` 的包装记录，但不会自动把 `passed=true / outcome=lost` 的自然结果当作工具故障。最终调用应显式增加：

```text
--failed-run D:/AegisWork/Reports/v2-20260918/capture-packaged-01
```

这个现有参数会绑定该目录内原始 JSON / JSONL / 日志，保留实际 PASS 或 FAIL 与 Lost 结果，不覆盖原件。原始 PNG 继续保留在原目录，并由原生 provenance 的文件清单绑定。这样可明确保存本次已经定位的录制工具故障，同时避免把所有正常 Lost 配对观察都误分类成工具失败。
