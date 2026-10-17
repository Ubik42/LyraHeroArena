# Lyra 英雄射击：GAS 深度实践与性能工程

本项目基于 **UE 5.7.4 与 Lyra 5.7** 开发，包含五英雄射击、200 角色性能实验以及中文性能工作台三个自研模块。项目复用 Lyra 的基础移动、武器系统、GAS 框架、队伍机制和美术资产，扩展实现了英雄技能、英雄选择、职责 Bot、战术 UI 及实验数据采集等功能，目前仍在持续开发。

> 公开仓库只包含自研 C++、技术说明和实机截图，不分发 Lyra、Fab 或 Unreal Engine 资产。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/hero-select.jpg" alt="五英雄选择界面"></td>
    <td width="50%"><img src="assets/readme/gameplay.jpg" alt="5v5 对局画面"></td>
  </tr>
</table>

## 项目内容

### 五英雄射击与技能架构

在 Lyra 的武器、队伍、GAS、PawnData、CommonUI 和联网框架上扩展五名英雄与 5v5 比赛流程。

- Striker、Bulwark、Pulse、Phase、Relay 分别覆盖输出、前排、支援、侧翼和战术控制；英雄由 Ability Set、装备和数据定义组合。
- 服务器负责选人校验、能力装配、终极能量、死亡、重生、换人和结算，客户端提交操作意图并接收复制结果。
- 已完成轮换控制点、战场脉冲和团队淘汰三种规则；一名玩家与九名职责机器人可组成完整 5v5 对局。
- 机器人沿用 Lyra 的控制点行为树，并根据英雄职责选择治疗、位移、屏障、部署物和终极技能的使用时机。
- 选人界面、队友职责与生命标记、战术 HUD、计分板和赛后流程已接入实际对局。

自有玩法集中在 `HeroArena` Game Feature。C++ 处理权威规则、生命周期和网络边界，Blueprint 与 Data Asset 负责装配与表现。

### GAS 深度实践：英雄能力与重生生命周期

#### 游骑：恢复场与轮换控制点

[连续实机片段与机制说明](https://ubik42.github.io/?work=lyra-hero-arena)：约 65 秒，单机 1 玩家 + 9 Bot，通过 Enhanced Input 自动输入。保留完整过程与等待，按游戏时间播放，无声；未开启无敌、传送、伤害注入或强制占点。

**RecoveryField** 采用 LocalPredicted GameplayAbility，激活时先 CommitAbility，只有 Authority 生成恢复场 Actor。Actor 保存队伍与到期时间，服务器定时检索范围内友方，经标准 GameplayEffect 路径治疗；实际治疗成功后递增 HealingPulseSequence 并 ForceNetUpdate，客户端 OnRep 驱动 Niagara 脉冲。该段录像验证单机流程，多人治疗效果仍需单独验证。

控制点组件保存各点的原始 Transform。非活动点被隐藏并移到地下，旧逻辑却读取其当前位置作为下一目标，导致预告期间目标高度错误、导航失败。本次修复改读 OriginalTransforms，在初始预告及 A→B 切换中确认目标高度与路径恢复正常。

代码入口：[恢复场能力](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Abilities/HeroArenaGameplayAbility_RecoveryField.cpp)、[恢复场 Actor](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Actors/HeroArenaRecoveryField.cpp)、[控制点轮换](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Components/HeroArenaControlPointRotationComponent.cpp)。录制条件见 [接入与验证](docs/接入与验证.md#连续玩法录制)。

#### Phase 闪回：充能、落点与历史状态

Blink 使用 **LocalPredicted GameplayAbility**，沿朝向执行 Capsule Sweep，遇障碍时缩短位移；有效距离不足 80 单位时拒绝释放且保留充能。三格充能由 PlayerState 组件维护，服务器依次恢复并仅向拥有客户端复制。充能耗尽通过 GAS 失败 Tag 进入 Lyra 消息通道，战术 HUD 匹配所属玩家并显示中文原因。

Recall 使用 **ServerOnly GameplayAbility**，服务器采样 Pawn 的位置、朝向、生命与同一手枪实例的弹匣。触发时检查约 3 秒前的记录、对象身份与落点碰撞；落点受阻则尝试较新的合法样本。成功后恢复历史数值，清空旧记录并进入冷却；重生、换人和武器变化会清除不匹配记录。

[闪回技能实机演示与机制说明](https://ubik42.github.io/?work=lyra-hero-arena)：两段案例共约 19 秒，单机 1 玩家 + 9 Bot，自动输入射击与技能，录制前设置起点、选人并开启无敌保护。分别展示充能耗尽，以及碰撞拒绝后回溯位置和弹匣。另有 14 秒双视角弱网演示，展示一次服务器拒绝、一次成功闪现及充能恢复。

#### 闪现充能预测回滚与网络同步修复

原实现直接修改客户端收到的服务器充能值。开发指令仅在服务端阻止 Blink 激活时，CMC 纠正了客户端位移，但充能仍停留在 2 格，服务器为 3 格；2.4 秒后仍未恢复。

修复采用**预测键结算模型**：通过 `PendingPredictionKeys` 独立记录客户端待确认扣减，`FPredictionKey` 拒绝回调撤销对应请求；服务端处理成功或在技能内因碰撞拒绝后，将已处理键与充能状态一起复制，在 `OnRep` 中统一结算。`ResetEpoch` 隔离换人、重生前后的请求，弱引用委托避免组件销毁后继续调用。

普通网络与模拟弱网均完成同进程双 PIE 验证。两端 NetDriver 各配置 150 ms 出站延迟与 2% 出站丢包后，记录到拒绝恢复 3 格、成功释放 2 格、连续释放 0 格、恢复 1 格以及换人重置 3 格；稳定采样时双端位置一致，客户端待确认请求为零。测试采用开发专用故障注入，延迟配置不等同于实测 RTT。跨进程、专用服务器与长时间对战仍待验证。详见 [测试记录](docs/data/blink-network.json) 与 [运行方法](docs/接入与验证.md#blink-预测拒绝与弱网检查)。

| 实现入口 | 对应内容 |
| --- | --- |
| [Blink](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Abilities/HeroArenaGameplayAbility_Blink.cpp) | 激活条件、碰撞缩距、失败 Tag 与表现 |
| [BlinkChargeComponent](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Components/HeroArenaBlinkChargeComponent.cpp) | 拥有者充能复制、本地暂扣和顺序恢复 |
| [RecallHistoryComponent](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Components/HeroArenaRecallHistoryComponent.cpp) | 历史采样、对象身份与合法落点检查 |
| [CombatHUDWidget](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/UI/HeroArenaCombatHUDWidget.cpp) | 技能状态与所属玩家失败提示 |

#### 换人与重生

玩家选择的英雄数据保存在 PlayerState 的 HeroSelectionComponent。客户端调用 `RequestHeroSelection` 后经 RPC `ServerRequestHeroSelection` 发往服务器，服务端逐项校验英雄名单、HeroId、锁定状态并过滤重复请求。能力变更依托 Lyra 原生 ASC 与 Pawn 初始化流程：通过 **AbilitySet 授予句柄撤销上一位英雄的能力后再授予新能力**，并将 `Spec.SourceObject` 指定为对应 HeroDefinition 以追踪能力来源；武器与装备通过 QuickBarComponent、InventoryManagerComponent 与 EquipmentManagerComponent 替换，新 Pawn 完成能力系统初始化后应用保留的配置。

终极技能能量由造成的伤害、治疗、击杀及助攻等战斗贡献累加；角色死亡保留当前能量，更换英雄时能量清零。重生开始时，系统调用 `PushContentToLayer_ForPlayer` 将选人窗口重新压入 CommonUI 栈，重生完成后关闭该窗口。

### 英雄切换与重生同步验证

| 阶段 | 监听服务器 | 拥有客户端 |
| --- | --- | --- |
| Striker 初始状态 | 7 项英雄来源 AbilitySpec | 7 项英雄来源 AbilitySpec |
| 切换为 Phase | 4 项，无旧来源残留 | 4 项，无旧来源残留 |
| 死亡重生后 | 4 项，ASC Avatar 指向新 Pawn | 4 项，ASC Avatar 指向新 Pawn |

Striker 的 7 项包含选人控制与 Bot 辅助 AbilitySpec。测试于 2026-10-17 在 UE 5.7.4 D3D12 同进程双 PIE 环境下执行，通过开发命令触发死亡与换人；本轮未做弱网、跨进程独立服务器与完整对局复测。详细条件与来源见 [GAS 双端记录](docs/data/gas-network.json) 和 [接入与验证](docs/接入与验证.md)。

[单机实机录像：选人、死亡与重生（1 玩家 + 9 Bot，13 秒）](https://ubik42.github.io/?work=lyra-hero-arena)。该录像与上表双端测试分别采集。

| 实现入口 | 对应内容 |
| --- | --- |
| [HeroSelectionComponent](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/Hero/HeroArenaHeroSelectionComponent.cpp) | 服务器选人校验、能力撤销与授予、装备替换 |
| [HeroSelectUIComponent](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/UI/HeroArenaHeroSelectUIComponent.cpp) | 重生消息、CommonUI 选人窗口生命周期 |
| [GAS 只读诊断](Plugins/GameFeatures/HeroArena/Source/HeroArenaRuntime/Private/HeroArenaRuntimeModule.cpp) | 按英雄来源输出 AbilitySpec、Pawn 与 ASC Avatar |

### 多角色高负载场景性能优化

[200 AI 动画质量并排视频与策略说明](https://ubik42.github.io/?work=lyra-performance-lab)：全景、近景及局部放大，共 15 秒。2026-10-17 独立运行记录补充了 URO、后处理动画与近景保护状态；本轮网格仅有 LOD0，实际未减少网格细节。录像与下方历史性能采样分开，具体条件见 [动画质量对照](docs/动画质量对照.md)。

性能实验以 200 个 Lyra Pawn、固定 Chaos/Niagara 负载以及真实服务器与多客户端会话构造压力场景。AI、寻路、动画/IK、渲染、物理、特效和网络分别保留 Baseline / Optimized，并同时记录角色行为、画质和连接状态。

当前具有稳定对比的代表结果：

| 方向 | 调整 | 当前 3+3 结果 |
| --- | --- | --- |
| AI 目标发现 | 逐角色扫描改为共享战场事实 | 决策平均中位数 `0.1392 → 0.0240 ms/帧`；Frame P95 约下降 `9.1%` |
| Animation / IK | 远景更新降频，并在 2400 uu 外省略 FootPlant Post Process | Animation 平均中位数 `15.18 → 10.99 ms`；Frame P95 约下降 `10.3%` |
| Navigation | 合并相同目标并限制重复重规划 | 路径请求约下降 `82%`；Frame 均值与 P95 约下降 `1.7%` |
| 角色光追 | 1500 uu 外角色退出 Ray Tracing Scene | 动态光追几何平均中位数 `4.02 → 2.50 ms`；GPU 总平均基本持平 |
| 网络相关性 | Relay 部署物按连接相关性复制 | 服务器出站带宽约 `20.7 → 17.7 KB/s`；复制阶段约 `1.94 → 1.60 ms` |

物理距离 LOD、角色阴影和部分动态 Niagara 方案保留了测试记录，但没有作为默认游戏改动：它们在现有场景中没有形成稳定的整帧收益，或会改变画面与负载条件。

### UE 性能实验工作台

[战术 HUD 每帧与分频更新实机对照](https://ubik42.github.io/?work=ue-performance-workbench)：12.5 秒并排展示，附原生技能栏放大。同长度窗口的状态刷新为 370 次与 103 次，双方均显示冷却、充能恢复及碰撞失败原因。计数与历史耗时分开，机制、取舍和录制条件见 [HUD 更新质量对照](docs/HUD更新质量对照.md)。

`PerformanceWorkbench` 是运行在 Unreal Editor 内的中文实验工具；`PerformanceLab` 是不依赖 Editor 的运行时采集模块。两者通过版本化 Preset 和 JSON/CSV 结果连接。

- 14 条实验轨道覆盖真实 5v5、受控子系统负载和 200 角色压力场。
- 可启动单机、Dedicated Server 与多个 Client，统一运行编号、预热、采样和输出目录。
- 连续执行 Baseline / Optimized 各三次，展示中位数、P95、变化率和样本范围。
- 时间轨道列出 CPU、工作线程、Render Thread 与 GPU Pass 的主要开销；Niagara 可继续下钻到具体 System 与 Owner。
- 每条轨道同时显示人数、行为、画质、RHI 或网络拓扑，避免比较不同负载下的两组数据。
- 原始 CSV、运行参数和未采用的结果均保留，便于复查归因。

<table>
  <tr>
    <td width="50%"><img src="assets/readme/workbench-render.png" alt="性能工作台渲染实验"></td>
    <td width="50%"><img src="assets/readme/workbench-network.png" alt="性能工作台网络实验"></td>
  </tr>
</table>

## 工程结构

```text
Plugins/
├── GameFeatures/HeroArena/   # 英雄、能力、模式与比赛流程
├── PerformanceLab/           # 运行时压力场、采集与场景状态
└── PerformanceWorkbench/     # Editor 内实验编排与结果比较
docs/
├── 技术报告.md
├── 接入与验证.md
├── data/                     # 源码指纹、逐次采样摘要与 GAS 双端记录
└── integration/              # Gameplay Tag 接入配置
tools/
└── check_snapshot.py         # 源码、链接与记录检查
```

## 当前状态

- 五英雄 5v5 对局、三种比赛规则、机器人职责行为和完整比赛生命周期已经连通。
- 性能实验覆盖七个方向，工作台可读取并比较 14 条轨道；本仓库中的数字来自固定条件下的阶段采样，不作为其他项目的通用性能承诺。
- 下一阶段以实际对局的可读性、机器人技能决策和发布演示收口为主，不继续横向增加英雄、模式或无明确热点的性能案例。

开发环境为 Windows 11、Unreal Engine 5.7.4（CL 51494982）和 Lyra Starter Game 5.7。本公开快照包含三个自研插件的 **199 个源码与描述文件**及工程说明，与完整开发工程提交 `f424df9e` 对齐。工程运行还需合并 Lyra 宿主接入改动、装配 48 个 HeroArena 项目资产、引入具备许可的 Epic/Fab 内容并配置相关 Gameplay Tag；完整开发工程已编译和实机运行，公开快照尚未提供独立启动所需的全部内容。

架构与实验结果见 [技术报告](docs/技术报告.md)；宿主依赖、资产清单、运行入口和本轮检查范围见 [接入与验证](docs/接入与验证.md)。

## 来源与许可

Lyra 原有的射击、队伍、GAS、装备、基础机器人、地图和资产不计入自研内容。`HeroArena`、`PerformanceLab` 与 `PerformanceWorkbench` 为本项目新增模块；对 Lyra 源码的少量接入在技术报告中说明。

Unreal Engine、Lyra Starter Game 及 Epic 内容遵循 Epic Games 的许可条款。本仓库不授予第三方内容的再分发权。
