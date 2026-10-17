# 战术 HUD 更新质量对照

[实机视频与工作台详情](https://ubik42.github.io/?work=ue-performance-workbench) · [本轮状态摘要](data/hud-quality.json)

## 实现关系

`UHeroArenaCombatHUDWidget` 在 Lyra 的 ASC 和 GameplayMessageSubsystem 上扩展技能栏、状态、目标与阵容显示。UI 只读查询带标签 GameplayEffect 的剩余时间，技能结算由 GAS 负责。

- 冷却、状态和目标：NativeTick 通过 FPlatformTime 检查下一刷新时点，命中后设置为当前时间加 0.1 秒。配置上限 10 Hz，帧调度会使实际间隔更长。
- 阵容：独立 0.25 秒间隔。
- 英雄变化：委托直接刷新。占点、重生和失败原因通过消息回调更新，无须等待常规轮询。
- 文本：SetTextIfChanged 通过 FText::EqualTo 跳过相同内容的 SetText；查询和字符串构建不一定被跳过。
- 生命周期：NativeDestruct 移除英雄委托并注销消息监听。失败提示只匹配所属 PlayerController，显示 1.8 秒后由 Tick 隐藏。

源码入口：[HeroArenaCombatHUDWidget.cpp](../Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/UI/HeroArenaCombatHUDWidget.cpp)。

## 2026-10-17 的独立录制

UE 5.7.4，两个编辑器进程串行运行单机 PIE。地图 `/ShooterMaps/Maps/L_Convolution_Blockout`，Experience `B_HeroArena_ControlPoints`，1 玩家、9 Bot。两组使用相同 Phase 英雄、起点、朝向和 Enhanced Input 脚本，顺序为射击、回溯、重复回溯、四次闪现和等待恢复。开启 God 保护防止死亡打断。机器人战场状态不逐帧一致。

基准增加 `-HeroArenaHUDFullRate`，优化组省略该参数。两组均启用 `-HeroArenaHUDTrace`、D3D12、`t.MaxFPS 30`；原生视口 1591×843，真实游戏时间，未固定步长。完整原始记录约 12.66 秒，成片取共同的前 12.5 秒，并排展示与技能栏原帧放大，无声。

| 同长度窗口 | 基准 | 降频 |
| --- | ---: | ---: |
| RefreshRuntimeState 调用 | 370 | 103 |
| 使用的原生画面帧数 | 370 | 371 |
| 首末刷新记录的墙钟跨度 | 12.496 s | 12.431 s |
| 闪现空间不足提示次数 | 3 | 3 |

两组均记录到回溯进入倒计时并恢复就绪、闪现消耗一格及充能恢复。降频组约每秒 8.3 次；这是本次录制条件下的观察，不能保证始终为 10 Hz。视频计数只对应该函数调用，不是 Draw Call、总 UI 刷新或 FPS。

`-HeroArenaHUDTrace` 仅在非 Shipping 构建有效，读取实际 Slate 文本及 game/wall/frame/fullRate 字段。它会引入额外开销，正式计时必须关闭；本轮没有测量输入到像素的精确延迟。摘要中的失败消息时间是最近一次刷新记录的位置，不能当成事件发生或显示的准确时刻。

## 与历史性能测试的区别

2026-08-31 的 `712a1cd` 版本独立采样为 1 玩家 + 9 Bot，8 秒预热、10 秒采样，3+3 ABBAAB，分别取三次中位数。局部 HUD 平均耗时 0.0620 → 0.00574 ms，UI 总平均耗时 1.666 → 1.554 ms；整帧 P95 为 11.268 → 11.242 ms，基本持平。历史刷新为 103.9 → 9.6 次/秒，条件与本轮录制不同。详见 [历史摘要](data/performance-results.json)。

常规状态可能等待下一次刷新，低帧率下间隔可超过 0.1 秒，倒计时显示可能跳档。关键消息采用独立回调；本次确认显示了失败原因，不能据此宣称零延迟或所有战斗场景均无影响。

公开仓为自有插件覆盖层；完整运行仍需合法 Lyra 5.7 宿主、HeroArena 资产和既有宿主接入。自动输入及原生录帧使用开发工程中的 UELLMToolkit 桥接，未随本快照分发。
