# 性能实验室

性能实验室是 LyraHeroArena57 的运行时性能实验底座。它不替代 Unreal Insights、CSV Profiler 或 Lyra 的性能统计，而是统一实验条件并判断一次运行是否有效。

当前能力：

- 使用 Lyra `L_ShooterPerf` 与真实 Shooter Pawn、AIController、GAS、动画和角色部件。
- 通过固定网格生成 200 个 AI 角色，不与玩家出生点争抢。
- 以 `ExperimentPreset` 统一目标地图、负载、采集域、时长和实验条件；200 角色负载只是一个内置预设。
- 以 `Session` 记录运行编号、预设、维度、变体和源码版本；命令行字段可覆盖预设，兼容现有自动化。
- CSV 异步写完后才完成结果写入；负载、目标系统、RHI、拓扑和采样完整性满足后，结果标记为可比较。
- 向每次运行目录写入带产物、条件检查与采集域信息的 `manifest.json`、`summary.json` 和 `capture.csv`。
- 正常 RHI 渲染预设记录 Game/Render/RHI/GPU 帧时、Draw Call、Primitive、Lyra 角色数和阴影角色数，并锁定采样期间的初始玩家视点。

运行参数示例：

```text
-PerfLab -PerfLabAutoExit -PerfLabAgents=200 -PerfLabSeed=1337
-PerfLabWarmup=10 -PerfLabDuration=30 -PerfLabReadyTimeout=60
-PerfLabRunId=<运行编号> -PerfLabDimension=AIDecision -PerfLabVariant=Baseline
-PerfLabPreset=<可选的 UPerformanceExperimentPreset 对象路径>
```

结果位于 `Saved/PerformanceLab/Runs/<运行编号>`。无渲染模式只用于验证数量与采集链路，不能作为正式性能成绩。
