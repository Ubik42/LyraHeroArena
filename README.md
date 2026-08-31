# Lyra Hero Arena

基于 **Unreal Engine 5.7.4 / Lyra Starter Game 5.7** 开发的多人英雄射击游戏与性能工程作品集。

这个仓库围绕同一套真实游戏工程建设三项可以独立讲述的作品：**五英雄多人对战游戏**、**多角色高负载场景性能优化**、**UE Performance Workbench 性能实验工作台**。三项共享角色、战斗、地图和采集数据，但职责保持分离：游戏不依赖性能工具，运行时实验模块不依赖 Editor，工作台只读取实验合同与结果。

> 公开仓库只保留自研 C++、技术说明和实际运行截图，不重新分发 Lyra、Fab 或 Unreal Engine 资产。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/hero-select.jpg" alt="五英雄选择界面"></td>
    <td width="50%"><img src="assets/readme/gameplay.jpg" alt="游戏内实际交战场景"></td>
  </tr>
</table>

## 作品一：Lyra Hero Arena｜五英雄多人对战游戏

在 Lyra 已有的武器、队伍、GAS、PawnData、CommonUI 和网络框架上继续开发一套完整可玩的英雄射击游戏。重点不是堆积孤立技能，而是让角色差异、比赛规则、网络权威与反馈系统能够稳定组合和扩展。

计划完成的游戏内容：

- **五名英雄**：Striker、Bulwark、Pulse、Phase、Relay；每名英雄拥有主武器、明确职责和完整能力组，覆盖机动、前排、支援、侧翼与战术控制。
- **服务器权威的英雄流程**：选择、校验、装配、换人、死亡和重生沿用同一条状态路径；客户端只提交意图，不持有比赛权威。
- **数据化能力组合**：英雄差异由 Ability Set、装备和数据定义组成，避免为每名英雄复制整套 Pawn 与规则代码。
- **完整比赛闭环**：选人、出生、交战、技能、目标推进、死亡、重生、结算均可在一局内完成。
- **两种正式模式**：轮换控制点与团队死斗共享同一英雄体系；娱乐模式只有在不阻塞主流程时加入。
- **可参与比赛的机器人**：机器人能够按英雄职责移动、争夺目标、使用武器，并触发至少一项代表性技能。
- **必要的战斗反馈**：命中、受击、治疗、护盾、控制、冷却、目标状态与结算结果保持一致且可读。

自有玩法集中在 `HeroArena` Game Feature 中。C++ 负责权威规则、生命周期和网络边界，Blueprint 与 Data Asset 负责装配和表现；伤害、治疗、队伍、库存与武器 TargetData 尽量复用 Lyra 的成熟路径。

## 作品二：多角色高负载场景性能优化

这项作品关注的不是“场景里能摆多少人”，而是在大量角色同时拥有 AI、寻路、动画、渲染、物理与网络行为时，如何定位真实瓶颈、提出单变量改进，并证明优化没有破坏行为或画质。

工程使用固定地图、随机种子、镜头、画质、RHI 与角色负载构造可复现压力；200 个真实 Lyra Pawn 是其中一组标准负载，而不是作品名称。每个方向都保留独立的 Baseline / Optimized：

- **AI 决策**：比较逐角色查询与共享战场事实，减少重复目标扫描和状态机工作量，同时守住目标获取率和行为分布。
- **Navigation**：分析路径请求、重规划、拥堵和长期未服务；优化方案必须兼顾全局预算与请求公平性，不能用让角色停止移动换取收益。
- **Animation / IK LOD**：基于 Animation Budget Allocator、距离与战斗重要性分配更新频率；本地与近景角色保持完整动画和 IK 响应。
- **Rendering**：分别评估角色阴影、附件、材质和可见距离；使用正常 RHI 与固定镜头，区分 Game、Render、RHI 和 GPU 收益。
- **Networking**：在真实服务器与多客户端拓扑下检查相关性、复制频率、出站带宽和服务器复制开销。
- **Physics / VFX**：对角色物理、碰撞和 Niagara 开销先做 Profile，再选择有明确热点的单项建立前后对比，避免为了凑案例制造无效优化。

优化有效性由性能指标和质量条件共同决定。角色存活、移动、到达率、请求失败、最长等待、近景动画质量、阴影保留和网络连接数等条件不成立时，即使帧时下降也不会被写成成功案例。

## 作品三：UE Performance Workbench｜性能实验工作台

工作台是运行在 Unreal Editor 内的中文性能实验工具，用来把一次性的 Profile 操作整理为可复跑、可比较的工程流程。它既服务上述高负载实验，也用于验证 Lyra Hero Arena 开发过程中对渲染、网络和 Gameplay 架构的改进。

计划形成的完整链路：

- 用版本化 Preset 保存地图、Experience、人数、随机种子、画质、RHI、采样时长和进程拓扑。
- 从 Editor 启动单机、服务器和多个客户端，并统一管理运行编号与输出目录。
- 汇集 CSV Profiler、AI、Navigation、Animation、Rendering、Network 等指标及原始文件。
- 为每类实验配置 Required Guard，自动拒绝负载不足、条件不一致、NullRHI 冒充渲染结论或质量退化的样本。
- 聚合同一合同下的 Baseline 3 次与 Optimized 3 次，展示中位数、P95、变化率和质量代价。
- 对缺失样本、不可比较结果、失败 Guard 和潜在回归给出明确说明，不自动宣称“优化成功”。
- 提供 AI、Navigation、Animation、Rendering、Networking 等统一入口，让实验室和真实游戏中的优化使用同一套比较方法。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/workbench-render.png" alt="性能工作台渲染实验对比"></td>
    <td width="50%"><img src="assets/readme/workbench-network.png" alt="性能工作台网络实验对比"></td>
  </tr>
</table>

## 实验方法

正式 A/B 固定引擎版本、源码提交、地图、Experience、负载、随机种子、分辨率、RHI、画质、镜头、预热时间、采样时间和网络拓扑。Baseline 必须是合理可运行的实现，Optimized 一次只改变一个可说明的变量。

每个正式结论至少使用三次 Baseline 与三次 Optimized，并保留原始 CSV 和运行元数据。局部热点的下降只描述为局部指标收益，不等同于整帧收益，也不外推为所有项目都能获得同样结果。

## 工程结构

```text
Plugins/
├── GameFeatures/HeroArena/   # 英雄、能力、模式与比赛流程
├── PerformanceLab/           # 运行时压力、指标与质量条件
└── PerformanceWorkbench/     # Editor 内实验编排与结果比较
docs/
└── 技术报告.md
```

## 环境与源码边界

- 开发基线：Windows 11、Unreal Engine 5.7.4（CL 51494982）、Lyra Starter Game 5.7。
- 工程保留 Epic 原始的 `LyraStarterGame.uproject`、`LyraGame` 和 `LyraEditor` 标识，避免无收益的模块与资产路径迁移。
- 本仓库是作品集源码快照，不是可独立运行的 Lyra 分发包；地图、角色、动画、武器、音频和 UI 需通过 Epic/Fab 按许可获取。
- `HeroArena`、`PerformanceLab` 与 `PerformanceWorkbench` 是项目自研模块；对 Lyra 源文件的必要接入会在技术报告中单独说明，不把 Epic 原有能力归为自研。

更完整的架构与 Benchmark 合同见 [技术报告](docs/技术报告.md)。

## 许可说明

Unreal Engine、Lyra Starter Game 及相关 Epic 内容遵循 Epic Games 的许可条款。本仓库不授予这些第三方内容的再分发权。自研源码当前用于作品展示与技术评审；如需复用，请先联系作者。
