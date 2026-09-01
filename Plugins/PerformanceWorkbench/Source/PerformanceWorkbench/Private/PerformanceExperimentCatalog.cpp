#include "PerformanceExperimentCatalog.h"

namespace
{
	const TArray<FPerformanceExperimentDefinition>& GetDefinitions()
	{
		static const TArray<FPerformanceExperimentDefinition> Definitions = {
			{ 1, EPerformanceExperimentSection::HeroArena, TEXT("HeroArenaHUD"), TEXT("hero-arena-hud-refresh"), TEXT("UI"),
				TEXT("战术 HUD 更新"), TEXT("真实 5v5 对局；逐帧查询技能与目标状态 / 按显示精度 10 Hz 更新") },
			{ 2, EPerformanceExperimentSection::HeroArena, TEXT("HeroArenaAnimationLOD"), TEXT("hero-arena-character-rendering"), TEXT("AnimationLOD"),
				TEXT("动画更新 LOD"), TEXT("真实 5v5 对局中比较动画更新频率、远景骨骼 LOD 与近场保护") },
			{ 3, EPerformanceExperimentSection::HeroArena, TEXT("Rendering"), TEXT("hero-arena-character-rendering"), TEXT("Rendering"),
				TEXT("角色附加通道 LOD"), TEXT("远距离角色省略 Custom Depth，近场角色与伤害数字保留；同步分解 GPU 阶段") },
			{ 4, EPerformanceExperimentSection::HeroArena, TEXT("HeroArenaGameplayTick"), TEXT("hero-arena-weapon-spawner-tick"), TEXT("GameplayTick"),
				TEXT("拾取点更新频率"), TEXT("13 个武器拾取点；全帧率更新 / 客户端 30 Hz、专用服务器停用") },
			{ 5, EPerformanceExperimentSection::HeroArena, TEXT("HeroArenaLiveWeaponVFX"), TEXT("hero-arena-live-weapon-vfx-budget"), TEXT("LiveWeaponVFX"),
				TEXT("真实对局武器表现观测"), TEXT("真实 5v5 对局；记录开火率、远距降级与 Niagara 系统，不把不同交火节奏直接当作性能收益") },
			{ 6, EPerformanceExperimentSection::HeroArena, TEXT("HeroArenaGunPadVFX"), TEXT("hero-arena-live-weapon-vfx-budget"), TEXT("GunPadVFX"),
				TEXT("拾取点常驻特效距离管理"), TEXT("真实 5v5 对局；常驻 GunPad 全部运行 / 3000 uu 外停用粒子，武器模型与交互保持") },
			{ 7, EPerformanceExperimentSection::HeroArena, TEXT("Network"), TEXT("hero-arena-network"), TEXT("Network"),
				TEXT("多人网络 · Relay 空间相关性"), TEXT("每轮 1 Dedicated Server + 2 Client、64 Relay；批次逐轮等待三端采集完成") },
			{ 8, EPerformanceExperimentSection::ControlledLoad, TEXT("HeroArenaWeaponVFX"), TEXT("hero-arena-weapon-vfx-budget"), TEXT("WeaponVFX"),
				TEXT("武器表现距离预算"), TEXT("固定 48 组件事件/秒；全量枪口、弹壳、曳光、命中 / 远场保留曳光与命中") },
			{ 9, EPerformanceExperimentSection::ControlledLoad, TEXT("Niagara"), TEXT("hero-arena-niagara-status-lifecycle"), TEXT("Niagara"),
				TEXT("Niagara 持续特效生命周期"), TEXT("24 个持续状态特效；周期重启 / 保持实例运行；爆发负载相同") },
			{ 10, EPerformanceExperimentSection::ControlledLoad, TEXT("Physics"), TEXT("hero-arena-physics"), TEXT("Physics"),
				TEXT("物理距离 LOD"), TEXT("300 个受控刚体；远距离冻结，近场保留 Chaos 模拟") },
			{ 11, EPerformanceExperimentSection::Crowd, TEXT("AIDecision"), TEXT("crowd-200"), TEXT("AIDecision"),
				TEXT("AI 状态机与目标发现"), TEXT("200 个 Lyra Pawn；Acquire / Chase / Engage 状态机，逐角色目标扫描 / 共享空间分桶") },
			{ 12, EPerformanceExperimentSection::Crowd, TEXT("Navigation"), TEXT("crowd-200-navigation-replan"), TEXT("Navigation"),
				TEXT("寻路重规划节流"), TEXT("200 个角色沿固定路线巡航；逐次重规划 / 0.8 秒冷却与目标合并") },
			{ 13, EPerformanceExperimentSection::Crowd, TEXT("AnimationLOD"), TEXT("crowd-200-animation"), TEXT("AnimationLOD"),
				TEXT("动画与 IK LOD"), TEXT("固定镜头下比较 URO、骨骼 LOD、远场后处理动画省略与近景保护") },
			{ 14, EPerformanceExperimentSection::Crowd, TEXT("RenderingLOD"), TEXT("crowd-200-rendering"), TEXT("RenderingLOD"),
				TEXT("角色光追场景 LOD"), TEXT("固定镜头；1500 uu 外角色主体与外观退出 Ray Tracing Scene，主通道、动画和光栅阴影保持不变") }
		};
		return Definitions;
	}
}

TConstArrayView<FPerformanceExperimentDefinition> PerformanceExperimentCatalog::All()
{
	return GetDefinitions();
}

const FPerformanceExperimentDefinition* PerformanceExperimentCatalog::FindByLaunchDimension(const FString& LaunchDimension)
{
	return GetDefinitions().FindByPredicate([&LaunchDimension](const FPerformanceExperimentDefinition& Definition)
	{
		return Definition.LaunchDimension == LaunchDimension;
	});
}

const FPerformanceExperimentDefinition* PerformanceExperimentCatalog::FindByResult(const FString& PresetId, const FString& ResultDimension)
{
	return GetDefinitions().FindByPredicate([&PresetId, &ResultDimension](const FPerformanceExperimentDefinition& Definition)
	{
		return Definition.PresetId == PresetId && Definition.ResultDimension == ResultDimension;
	});
}

bool PerformanceExperimentCatalog::IsRegisteredResult(const FString& PresetId, const FString& ResultDimension)
{
	return FindByResult(PresetId, ResultDimension) != nullptr;
}

TArray<FPerformanceComparisonQuery> PerformanceExperimentCatalog::BuildComparisonQueries()
{
	TArray<FPerformanceComparisonQuery> Queries;
	Queries.Reserve(GetDefinitions().Num());
	for (const FPerformanceExperimentDefinition& Definition : GetDefinitions())
	{
		Queries.Add({ Definition.PresetId, Definition.ResultDimension });
	}
	return Queries;
}
