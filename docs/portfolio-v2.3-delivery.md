# Aegis Arena v2.3 Demo 交付记录

2026-09-22 已完成本轮扫描玩法、双语界面与新视频交付。唯一工程：`C:/Users/yzhu/Documents/ChatGPT/Games/Aegis-Arena`。沿用现有 Unreal 项目增量改进，采用通用可玩项目展示定位。

## 视频与试玩

| 交付物 | 路径 |
|---|---|
| 桌面最终视频 | `C:/Users/yzhu/Desktop/Aegis-Arena-v2.3-Demo.mp4` |
| 桌面试玩入口 | `C:/Users/yzhu/Desktop/Aegis Arena v2.3.lnk` |
| 新 Development 包 | `D:/AegisWork/Packages/aegis-v23-survey-20260922/package/Windows` |
| 包内入口 | `PLAY-Aegis-v2.3.cmd` |
| 原始录制与日志 | `D:/AegisWork/Reports/v23-native-final-20260922` |
| 最终成片、PCM、EDL、帧清单与 provenance | `D:/AegisWork/Reports/v23-survey-film-sync-20260922` |
| 独立解码、抽帧、音量与同步检查 | `D:/AegisWork/Reports/v23-final-qa-20260922` |
| 源码与包审计 | `D:/AegisWork/Reports/v23-source-audit-20260922/audit.json` |
| 桌面复制与交付清单 | `D:/AegisWork/Reports/v23-final-delivery-20260922/delivery.json` |

最终视频 **49,230,305 字节（49.23 MB）、115.87 秒、1920×1080、30fps、H.264/AAC、48kHz 双声道**。SHA-256：

`61778af2d600a7661b843f1ab3225d8e84f99159624d537a345d0f806ccdbb1f`

桌面标准文件和原有 `Aegis-Arena-v2.3-Demo-50mb.mp4` 副本均更新为此哈希。旧桌面视频、快捷方式和旧交付说明已备份于 `D:/AegisWork/Reports/v23-final-delivery-20260922`；此前视觉版及本轮首版编码仍保留，不作为最新成片。

## 本轮实现

首次运行默认中文，L 或语言按钮切换 English；普通会话把偏好写入 `AegisUI.ini` 并在下次启动恢复。录制和探针隔离此设置。任务、能力、队友、扫描、升级、暂停和结果界面使用当前语言，调整换行、边距与按钮可读性。

两处可选档案缓存 S1/S2 已成为真实玩法：1.8 米内按住 G 连续扫描 2.5 秒；松键、离开范围、受伤或不可交互状态中断进度。扫描前 H 选择补给或中继密钥，每处每局只能领取一次。补给按缺失生命真实治疗，密钥绑定下一座有效数据中继并提供 1.25 倍进度；不强化撤离。地图、场景标记、扫描卡片和结算统计读取实际状态。

原创简报插画、冷暖遗迹色彩、地面线、晶体与全息缓存地标已接入原有场景。当前仍是一张竞技场；没有宣称新增第二张地图、动态护送事件或撤离分支。[玩法与范围](v23-visual-gameplay.md) · [语言实现](v23-ui-language.md) · [试玩操作](portfolio-v2.3-play.md)。

## 新录制覆盖

全部 **3116 张**原生 1080p 帧连续保留，源时钟终点 103.90 秒。加 5 秒片头与 7 秒片尾形成 3476 帧；没有删掉失败的扫描尝试，没有插入诊断状态页。侧栏只保留少量章节说明。

- 中文简报 → 英文与路线选择 → 实际部署；英文升级页后恢复中文。
- 第一处缓存领取密钥并真实强化中继；保留 6 次受击等原因造成的扫描取消与重试。
- 第二处选择补给，实际恢复玩家 **12.400024 HP**，同伴本次补给治疗为 0。
- 蓄能射击 1 次、Q 3 次、E 1 次、F 2 次；E 另恢复玩家 14 HP，不与缓存补给混算。
- 北侧优先路线、两次升级、固守／转移／撤离三阶段完成；队友存活，结算页通过正常 X 退出。
- 共享能量 60 → 80，获得 219、消耗 199、溢出 0；团队实际输出 1260，玩家／队友承伤 90／42。

## 验证

组合阶段 Editor 构建和 8 项 Aegis.Core 通过；扫描原生探针 25 条断言通过。之后录制驱动的路径拐角修复重新编译成功，新 Development 包的 Build/Cook/Stage/Archive 日志确认 BUILD SUCCESSFUL、ExitCode=0。上述阶段测试没有冒充本轮再次执行；最终包原生捕获另外通过输入、事件、账本、完整帧与结果退出验证。

恢复阶段修复了音频首帧时间重复偏移约 33ms，以及 PNG concat 默认 25Hz 导致的帧时间量化。修正版从原 PNG 与 PCM 直接双遍编码至约 50MB，没有填充文件或对旧 MP4 再压缩。27 项相关回归通过，包括真实 FFmpeg 30 张不同帧的完整顺序验证和非零首帧音频脉冲定位；日志为 `editorial-tests.log`。

最终文件独立完整音视频解码成功，**3476/3476 帧**。已查看 16 处编码后抽帧，并检查中英文简报、英文升级、扫描与结算的全尺寸画面。音频综合响度 **-21.5 LUFS**、真峰值 **-1.2 dBFS**，无削波；六段 AAC 解码与交付 PCM 比较，测得延迟均为 **0ms**，相关性最低 0.9969。PCM 时长与 EDL 一致，AAC 解码末尾约 16ms 编码补齐不影响事件定位。

当前源码摘要 `0f6ee445616d2e3b48d03d8df76dc0103bee042478791e890e7966aea4aebd63`，Content/Aegis 摘要 `2ef7570f5e847a3fdbd96e6eb687790d3fcc3443d13d1e6fca2719aa85682489`，游戏 payload SHA-256 `b6e664319115193e8500ba40af3db680d02dc37d24e0d4be9c896b6fb34c587e`。均与录制前后记录匹配；3116 张原帧和打包容器也已核对。

## 录制范围

视频使用 Unreal 原生渲染、固定 1/30 秒步长和正常输入接口的脚本玩家；不是真人试玩或实时帧率基准。最终流程没有冻结 AI、位置传送或夹具伤害。声音依据本次运行的 **189 个真实音效事件、8 个音乐事件**与原创游戏 PCM 重建并同步，非声卡回环录音。普通试玩入口保留实际游戏音频。

原生扫描探针使用受控位置、伤害和冻结 AI，范围与完整演示分开记录。引擎保留一条已知 r.MotionVectorSimulation 警告；未观察到对应画面错误。未重新开展多局平衡、长期性能或策略胜率实验；没有提交、推送或公开发布。

## 复现

所有命令在真实工程执行，输出使用尚不存在的新目录。先用 `scripts/package_unreal.ps1` 构建包，再运行：

```powershell
python scripts/run_v2_capture.py --exe <新包AegisArena.exe> --output <新录制目录> --width 1920 --height 1080 --seed 1101 --v23
python scripts/make_v23_edl.py --capture <新录制目录> --output <新的edl.json>
python scripts/build_v23_video.py --capture <新录制目录> --output <新成片目录> --source-version 2.1 --editorial-version 2.3 --edl <新的edl.json> --target-mb 50
python D:/AegisWork/v23_final_qa.py --film <新成片目录> --output <新QA目录>
```

`presentationVersion=2.1` 是沿用的捕获 schema；实际 `gameplayVersion` 与 `visualDeliveryVersion` 均为 2.3。完成技术 QA 后仍须审阅抽帧，不能仅凭进程退出码交付。
