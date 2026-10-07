# v2.2 Demo 交付记录 · 2026-09-20

桌面最终视频：`C:/Users/yzhu/Desktop/Aegis-Arena-v2.2-Demo.mp4`。

- 96.00 秒，1920×1080，30fps，H.264 / AAC 48kHz stereo。
- 18,496,057 字节（约 18.5 MB），可作为独立附件上传。
- SHA256：`24a6bd81ad9fb4b4995f37d16f75b0adca0812150af500587a3bc7dab3f64649`。
- 成片、EDL、源帧清单、音频事件、哈希和完整解码日志：`D:/AegisWork/Reports/v2.2-native-cut-02`。
- 新包原生连续录制：`D:/AegisWork/Reports/v2.2-20260920/capture-art-01`。
- 录制使用的 Development 包：`D:/AegisWork/Packages/aegis-v22-art/package/Windows/AegisArena.exe`。

## 投递呈现

7 段按源时间顺序精剪：目标与第一段占领、第一次构筑、转移与蓄能、第二次构筑、撤离开局、走位协作、撤离结算。剔除诊断页面及较长跑图，每段只保留标题和两行设计观察。新增原生场景导光纹、遗迹刻痕和开场角色轮廓增强，范围见 `v22-art-changes.md`。

画面来自新包真实引擎渲染，使用正常接口脚本输入；音效与音乐按引擎真实事件重混。源画面 HUD 和 capture schema 仍为 2.1，交付 2.2 指表现与剪辑更新，玩法规则未变。没有把录制作为真人试玩结论。

## 验收

Editor 编译和 Development 打包成功。新录制 `passed=true`，3656 帧完整，结果 `won`，三个阶段完成，结果页正常 X 退出；运行前后源码、资源、包摘要一致。成片 `passed=true`、`fullDecodePassed=true`，桌面文件 SHA 与报告匹配。视觉抽查片头、战斗、构筑、撤离、片尾，旁注不溢出；检查图为 `qa-contact-sheet.png` 与 `qa-gameplay.png`。

旧版与中间报告完整保留。此次不提交、不推送、不发布。

重建命令（`NEW-UNIQUE` 必须替换成不存在的输出目录）：

```powershell
python scripts/build_v22_video.py --capture 'D:/AegisWork/Reports/v2.2-20260920/capture-art-01' --output 'D:/AegisWork/Reports/NEW-UNIQUE' --source-version 2.1 --editorial-version 2.2
```
