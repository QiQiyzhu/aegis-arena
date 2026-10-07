# v2.1 原生音频实现说明

音频仅改变表现，不改变攻击判定、伤害、经济、AI 感知或策略。成品参考借鉴的是状态反馈与音乐贴合场景的原则，没有复制其音源、旋律或声称复现其内部系统。

## 原创音源与导入

`scripts/generate_v21_audio.py` 使用确定性的振荡器、滤波噪声和显式音符作曲，输出 `assets/v21_source_audio/` 中 24 个 48 kHz / 16-bit WAV。7 首配乐共享 D 小调与 100 BPM，含和声、低频、原创五音动机、鼓点与分句变奏；17 个短音效包括玩家／队友／敌人的各 3 个枪声变体，以及命中、碎裂、脉冲、治疗、升级、蓄能释放、超频、蓄能就绪提示。未使用第三方采样、录音、音乐或训练模型；源代码、素材与逐文件 SHA 均保留。

`scripts/unreal/import_v21_audio.py` 只写入 `/Game/Aegis/V21/Audio`，核对源文件 SHA，将 5 首持续状态音乐设置为真实 SoundWave 循环，胜／负为非循环；所有音源设为 Play When Silent，避免从零增益淡入时被音频渲染器丢弃。旧音源不覆盖。

## 实际调用链

`AAegisScenarioRunner::BuildArenaPresentation → AegisPortfolioPresentation::BuildArena → AAegisPortfolioMusic::Ensure → Tick → SelectState → Transition → AudioComponent::Play/SetVolumeMultiplier/Stop`。

`SelectState` 只使用玩家已知的 Trial phase/wave、升级菜单、暂停菜单与新玩家 Pawn，不读取任何敌人坐标、距离、血量、隐藏数量或 AI 黑板。音乐不能成为策略读取隐藏敌人信息的途径。

| 公开状态 | 音源 | 播放方式 | 目标增益 |
| --- | --- | --- | --- |
| 简报 | M_Briefing | 19.2 秒循环 | 0.38 |
| 第一中继阶段 | M_Relay1 | 38.4 秒循环 | 0.40 |
| 第二中继阶段 | M_Relay2 | 38.4 秒循环 | 0.42 |
| 撤离阶段 | M_Extraction | 38.4 秒循环 | 0.44 |
| 升级间歇 | M_Upgrade | 19.2 秒循环 | 0.32 |
| 暂停／战术菜单 | M_Briefing | 低音量循环 | 0.16 |
| 胜利／失败 | M_Victory / M_Defeat | 6.8 秒一次性播放 | 0.48 / 0.38 |
| M 音乐静音 | 无新音源 | 两轨在 0.15 秒内淡出并停止 | 0 |

最多两个音乐组件，常规切换 0.65 秒线性交叉淡化，M 恢复时当前状态从头播放。暂停音乐组件为 UI Sound，可在真实游戏暂停中持续，音乐 Tick 不推进玩法时间。新 Pawn 重开时先停止两个旧组件，再启动当前状态；世界销毁时立即停止并解除按键。快速切换复用指定槽时先 Stop 该槽，避免叠出第三轨。音效最多 16 个同时请求，另检查实际活动组件；原生与 `-nosound` 录制均受按音源时长计算的请求配额限制。

`AegisPortfolioPresentation::Sound` 保持玩法原调用点，将真实枪声按角色映射到确定性的 3 个变体，不调用玩法 RNG；固定 Pitch=1。RMB 蓄能就绪声由 Character 在真实就绪阈值跨越时触发一次，释放／取消后重置；是否完成此接入须由原生输入检查确认。

## 录制事件协议与证据边界

`RecordSound` 保存真实资产名、路径、增益、音高和请求时刻。`RecordMusic` 保存 state、asset、volume、fadeSeconds、loop、reason 与 channel。非空音源使用指定 0／1 槽从头播放，该槽旧声立即停止，另一槽从当前增益淡出；空音源、channel=-1 表示两槽淡出。淡化使用与录制相同的 FApp 时间增量。

这些是实际发给音频组件的命令，不等于已证明扬声器发声。`GetPlayingComponentCount` 使用真实 `AudioComponent::IsPlaying`；`GetLoadedTrackCount` 返回已加载的配乐数。另需使用有音频设备的 Unreal 原生运行／Master Submix 录音验证音频输出、M 静音／恢复和暂停生命周期。PCM 格式与频谱检查仅验证文件结构与信号，不替代实际听音。慢速逐帧实机录像使用事件混音时须明确注明：游戏帧来自 Unreal，音轨按实机事件离线重混，不是硬件回录；音乐非视觉生成配乐。

## 参考来源

- [Returnal 官方 UX 设计说明](https://blog.playstation.com/2021/05/11/unpacking-returnals-ux-design-gameplay-first-ui-retro-futuristic-tech-and-accessibility/)：蓄能状态使用声音等通道反馈；本作据此增加真实阈值触发的一次就绪提示。
- [Supergiant 官方 Hades II 开发文章](https://www.supergiantgames.com/blog/3/)：音乐服务人物与环境；本作据此围绕晶体遗迹建立共享动机并按公开阶段安排配器密度，不声称采用相同内部音频算法。
