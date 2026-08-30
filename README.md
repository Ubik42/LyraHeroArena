# Lyra Hero Arena

基于 **Unreal Engine 5.7.4 / Lyra 5.7** 的客户端与性能工程作品集。项目最终由三部分组成：一套可完整游玩的英雄射击玩法、一组固定 200 NPC 的可归因性能实验，以及一个负责运行和比较实验的 UE 性能工作台。

> 当前仓库公开自研 C++ 与技术说明，不重新分发 Lyra、Fab 资产和 Unreal Engine 内容。截图来自实际工程，界面与表现仍会继续迭代。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/hero-select.jpg" alt="英雄选择界面"></td>
    <td width="50%"><img src="assets/readme/gameplay.jpg" alt="实际射击场景"></td>
  </tr>
</table>

## 三个项目

### 1. Lyra 英雄射击

在 Lyra 的武器、队伍、GAS、PawnData、CommonUI 和多人框架上扩展五名定位明确的英雄，并完成选人、出生、交战、目标、死亡、重生、换人和结算的完整比赛循环。

实现重点不是堆叠技能数量，而是建立稳定的客户端边界：

- 服务器保存并校验英雄选择，客户端只提交意图；
- 英雄差异由可撤销的 AbilitySet、装备和数据定义组合，不复制整套 Pawn；
- 伤害、治疗、队伍、库存和武器 TargetData 尽量沿用 Lyra 原路径；
- 自有玩法放在 `HeroArena` Game Feature 内，避免主工程反向依赖具体模式；
- 同一英雄体系服务占点、团队死斗和后续娱乐模式。

### 2. Crowd 200 性能实验室

用固定地图、固定随机种子和 200 个真实 Lyra Pawn 构造稳定压力，分别研究：

- AI 决策与目标查询；
- 路径请求、重规划与拥堵；
- 动画更新、IK、骨骼与远景 LOD；
- 角色材质、阴影、附件和可见距离。

每个方向只改变一个变量。优化不能靠少生成角色、停止行为或不可接受的画质退化换取数字。实验同时记录性能指标与存活、移动、到达、失败、近景质量等 Guard；质量不成立的候选不会进入正式结论。

### 3. UE 性能工作台

一个运行在 Unreal Editor 内的中文工具，用于启动压力场景、保存实验合同并比较 Baseline / Optimized：

- 编排单机、服务器和多客户端进程；
- 采集 CSV Profiler、渲染、网络、AI、Navigation 与 Animation 指标；
- 记录引擎版本、Git 提交、地图、Experience、人数、种子、RHI 和拓扑；
- 自动检查 Required Guard，拒绝条件不一致或负载失真的样本；
- 聚合同一实验的 3+3 结果，展示中位数、P95、变化率和质量代价。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/workbench-render.png" alt="性能工作台渲染 A/B"></td>
    <td width="50%"><img src="assets/readme/workbench-network.png" alt="性能工作台网络 A/B"></td>
  </tr>
</table>

## 代表性实验

以下数字来自同机、同地图、同负载的独立 A/B，不把局部热点收益写成整帧收益：

| 案例 | 代表指标 | 结果 |
| --- | --- | ---: |
| 200 NPC 共享战场事实 | 目标发现决策 CPU 均值 | -82.75% |
| Relay 空间相关性 | 服务器出站带宽均值 | -14.46% |
| 角色阴影 LOD | Render Thread 均值 | -3.86% |

## 结构

```text
Plugins/
├── GameFeatures/HeroArena/   # 英雄、能力、模式与客户端 Gameplay
├── PerformanceLab/           # 运行时压力、指标与质量合同
└── PerformanceWorkbench/     # Editor 内实验编排与结果比较
docs/
└── 技术报告.md
```

三部分共享工程和数据，但保持单向依赖：Gameplay 不依赖工作台，运行时实验室不依赖 Editor 模块，工作台只消费实验合同与结果。

## 环境与源码边界

- 已验证环境：Windows 11、Unreal Engine 5.7.4（CL 51494982）、Lyra Starter Game 5.7。
- 本仓库是作品集源码快照，不是可独立运行的 Lyra 分发包。
- 地图、角色、动画、武器、音频、UI 与其他 Lyra/Fab 二进制内容由 Epic Games 提供，需通过 Epic/Fab 按其许可获取。
- 仓库中的 `HeroArena`、`PerformanceLab` 和 `PerformanceWorkbench` 为项目自研模块；它们依赖 Lyra 的公开类型和项目内资产装配。

更完整的设计取舍与 Benchmark 合同见 [技术报告](docs/技术报告.md)。

## 许可说明

Unreal Engine、Lyra Starter Game 及相关 Epic 内容遵循 Epic Games 的许可条款。本仓库不授予这些第三方内容的再分发权。自研源码当前仅用于作品展示与技术评审；如需复用，请先联系作者。
