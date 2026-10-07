# Aegis Arena v2.2 表现层改动

本轮只更新 `Source/AegisArena/Private/AegisPortfolioPresentation.cpp`，没有改动 HUD、战斗规则、伤害、AI、录制器或音频。

## 画面目标

- 在已有晶体遗迹场景上增加空间层次：覆盖物和边界遗迹使用少量浅色刻痕与晶体底座，让晶体不再像孤立道具。
- 在地面平面内加入断续的青色导光带。它们位于地面上方极薄高度，不遮挡目标环，也不改变可行走区域。
- 用已有 `M_V2Energy`、`M_V2Ceramic`、`M_V2Basalt`、`SM_V2Armor` 和 `SM_V2Shard`，没有新增美术资产。

## 角色识别

开场角色生成后由表现层进行一次短时、有上限的装饰适配（不覆盖后续波次新生成角色）：

- 玩家使用青蓝色武器轮廓，队友使用更亮的薄荷色分件。
- Flanker 使用橙色后翼与偏转枪身，Suppressor 使用更宽的枪身和高位晶体，Striker 使用紫色后翼。
- 每个角色最多新增两个无碰撞静态网格组件；组件关闭 overlap 和导航影响，并用组件 Tag 防止重复附着。
- 该适配只读取已有 Team、bCompanion、bElite 和 EnemyRole，不写入角色状态，也不执行瞄准、伤害或导航查询。

## 预算与验证边界

场景新增两个 Instanced Static Mesh 层（地面导光带、遗迹刻痕），每层只增加少量实例；开场角色装饰在 `BuildV2Arena` 启动后的 8 秒内每 0.25 秒检查一次，完成适配后自动停止，因此本轮只承诺开场角色的阵营／武器轮廓增强。所有装饰均为 `NoCollision`、`CanEverAffectNavigation=false`、不投射阴影。

已完成的构建与当前录制状态：

1. 已完成 Unreal Development Editor 增量编译。命令为：
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_unreal.ps1 -EngineRoot 'D:\Program Files\UE_5.8' -CacheRoot 'D:\AegisWork\V22Build' -OutputRoot 'D:\AegisWork\Reports\v2.2-art-build'`
   编译、链接和元数据写入成功，耗时 75.34 秒；日志在 `D:\AegisWork\Reports\v2.2-art-build\20260920-074837-2809b057fb794d679db80e67b8da179e\build.log`。
2. 已完成 Development Windows 包构建。命令为：
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package_unreal.ps1 -EngineRoot 'D:\Program Files\UE_5.8' -CacheRoot 'D:\AegisWork\V22PackageBuild' -OutputRoot 'D:\AegisWork\Packages\aegis-v22-art' -Configuration Development`
   BuildCookRun 成功，耗时 393.05 秒；入口为 `D:\AegisWork\Packages\aegis-v22-art\package\Windows\AegisArena.exe`。
3. 新包录制 `D:\AegisWork\Reports\v2.2-20260920\capture-art-01` 已通过原生验收：`passed=true`、`outcome=won`、3656 帧、121.900006 视频秒、1265.281 秒墙钟、正常结果页 X 退出；源码与包前后哈希一致。该录制证明本次包的实际连续画面与流程，不把自动录制当作真人试玩。命令为 `python scripts/run_v2_capture.py --exe 'D:\AegisWork\Packages\aegis-v22-art\package\Windows\AegisArena.exe' --output 'D:\AegisWork\Reports\v2.2-20260920\capture-art-01' --width 1920 --height 1080 --seed 1101`；完整引擎参数保存在该目录的 `provenance.json`。
4. `D:\AegisWork\Reports\v2.2-editorial-cut-05` 沿用旧 v2.1 录制，仅为保留的中间剪辑；`v2.2-native-cut-01` 是新包录制的第一版成片，也作为中间记录保留。
5. 最终 `D:\AegisWork\Reports\v2.2-native-cut-02\Aegis-Arena-v2.2-Demo.mp4` 已通过源证据校验、完整解码和关键帧视觉检查，并复制覆盖桌面同名交付文件。96.00 秒、1080p/30fps、18,496,057 字节，SHA256 `24a6bd81ad9fb4b4995f37d16f75b0adca0812150af500587a3bc7dab3f64649`。详见 `portfolio-v2.2-delivery.md`。
