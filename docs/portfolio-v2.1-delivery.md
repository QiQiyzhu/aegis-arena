# Aegis Arena v2.1 新投递附件

本轮完成射击反馈与阶段配乐升级，延续科幻遗迹与生长晶体方向。参考 Returnal 的状态反馈、Hades II 的音乐主题关联、VALORANT 的表现可读性原则；具体转化与官方来源见 [视听设计](portfolio-v2.1-audiovisual.md)。音源由本项目原创合成，没有引用这些游戏的素材。

## 直接投递

目录：`D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v2.1`。

| 文件 | 内容 | 大小 |
|---|---|---|
| Aegis-Arena-v2.1-Application.zip | 视频、PDF、DOCX、C++阅读链、案例与验收 | 32.68 MB |
| Aegis-Arena-v2.1-Demo.mp4 | 133.87秒，1080p／30fps，中文说明 | 18.72 MB |
| Aegis-Arena-v2.1-Design.pdf | 11页系统策划设计说明 | 1.49 MB |
| Aegis-Arena-v2.1-Design.docx | 可编辑版本 | 12.79 MB |
| Aegis-Arena-v2.1-Windows.zip | 独立游戏包及启动脚本 | 160.19 MB |

有分别上传入口时，优先提交 MP4 与 PDF；支持压缩附件时可交 Application.zip。Windows.zip 是额外试玩包，不包含在 Application.zip 内。文件大小使用十进制 MB。

## 启动与操作

本机桌面双击 **Aegis Arena v2.1**。在其他电脑上解压完整 Windows.zip，双击 `Start-Aegis-Arena-v2.1.bat`，不要只移动 EXE。首次运行可能需要同包运行库。无需编辑器或 API Key。

Enter 开始；WASD 移动，鼠标瞄准、左键射击，右键蓄能0.7秒后释放，Space冲刺。Q脉冲，E按实际治疗收费，F中继超频；V在简报／首次升级时改目标顺序；1/2/3选升级，Z/X/C指挥队友。M只静音或恢复音乐，保留枪声。**Esc或P打开菜单后X退出；结算页直接X退出。** R同种子重开。F1打开诊断，独立 `Start-Decision-Lab.bat` 查看实际Utility／EQS链。

## 新版内容与证据

普通和蓄能攻击新增枪口亮核、短弹道、撞击分类、死亡碎晶与就绪提示，所有触发遵从真实攻击／命中，不另算装饰伤害。每个Portfolio角色27个表现组件，玩家实机峰值18。7段原创BGM按公开阶段切换，双轨交叉淡化；17个事件SFX区分玩家、队友、敌人和能力。暂停、静音、快速重开和退出均管理组件生命周期。

最终包控制36项、音乐15项、独立Lab35项原生断言通过。UE Master Submix的简报播放／静音／恢复RMS分别0.0548／0／0.0567，证明这些音频渲染片段有输出及控制差异；并非全部7曲实录或物理扬声器测量。

完整视频来自3656张连续原生帧、正常接口的脚本操作；没有注入血量、伤害或胜负。视频声音按引擎真实事件离线重混，并非原生回环录音。全片解码、5张编码关键帧和11页渲染文档检查通过。

本局3阶段通关、106.8秒，团队输出1260（玩家1020／队友240）、玩家承伤238／队友42，队友存活。状态变化122、1秒内短回返18含正常瞄准开火周期。保留无截图回归的自然失败、原始录制崩溃和原生音频失败。历史AI案例明确标为v2.0，不借视听升级宣称策略提升，没有RL训练。

## 版本和旧附件清理

源码 `4512e5e07bad285542e35911f7915d0d84beaae134aa7195861d67659343d3ea`；最终包 r2。验收原件 `D:/AegisWork/Reports/v2.1-20260918/acceptance-final-01.json`，精简索引见 [evidence](../evidence/portfolio-v2.1/README.md)。投递目录的 `delivery-manifest.json` 保存每份附件SHA，两个ZIP通过完整CRC检查。

按用户要求，在新附件核验后删除了 `D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v1.5` 与 `D:/AegisWork/Deliveries/Aegis-Arena-Portfolio-v2.0`。删除前清单和删除后不存在检查存于报告根目录 `retired-attachments-inventory.json`、`retired-attachments.json`。历史评测、失败原件、源代码、构建归档和独立Decision Lab附件保留。

项目未提交、未推送、未发布。候选人的游戏经历、时长和个人职责需按本人真实情况填写；附件不代造独立开发经历。
