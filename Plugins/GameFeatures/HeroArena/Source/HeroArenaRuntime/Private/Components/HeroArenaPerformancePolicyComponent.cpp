#include "Components/HeroArenaPerformancePolicyComponent.h"

#include "Character/LyraCharacter.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SignificanceManager.h"
#include "System/LyraSignificanceManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaPerformancePolicyComponent)

namespace HeroArenaPerformancePolicy
{
	int32 AnimationPolicyUsers = 0;
	int32 PreviousAnimationLODValue = 0;
	int32 WeaponVFXPolicyUsers = 0;
	int32 PreviousWeaponVFXBudgetValue = 0;
	int32 WeaponPadVFXPolicyUsers = 0;
	int32 PreviousWeaponPadVFXBudgetValue = 0;
}

void UHeroArenaPerformancePolicyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("HeroArenaDisableAnimationLOD")))
	{
		if (IConsoleVariable* AnimationLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.CharacterAnimationLOD")))
		{
			if (HeroArenaPerformancePolicy::AnimationPolicyUsers++ == 0)
			{
				HeroArenaPerformancePolicy::PreviousAnimationLODValue = AnimationLOD->GetInt();
				AnimationLOD->Set(1, ECVF_SetByCode);
			}
			bAnimationPolicyApplied = true;

			// The component normally starts before pawns spawn. Registering any already
			// present characters also makes late Game Feature activation deterministic.
			if (ULyraSignificanceManager* Manager = USignificanceManager::Get<ULyraSignificanceManager>(GetWorld()))
			{
				for (TActorIterator<ALyraCharacter> It(GetWorld()); It; ++It)
				{
					Manager->RegisterCharacter(*It);
				}
			}
		}
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("HeroArenaDisableWeaponVFXBudget")))
	{
		if (IConsoleVariable* WeaponVFXBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.WeaponVFX.DistanceBudget")))
		{
			if (HeroArenaPerformancePolicy::WeaponVFXPolicyUsers++ == 0)
			{
				HeroArenaPerformancePolicy::PreviousWeaponVFXBudgetValue = WeaponVFXBudget->GetInt();
				WeaponVFXBudget->Set(1, ECVF_SetByCode);
			}
			bWeaponVFXPolicyApplied = true;
		}
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("HeroArenaDisableWeaponPadVFXBudget")))
	{
		if (IConsoleVariable* WeaponPadVFXBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.WeaponSpawnerVFX.DistanceBudget")))
		{
			if (HeroArenaPerformancePolicy::WeaponPadVFXPolicyUsers++ == 0)
			{
				HeroArenaPerformancePolicy::PreviousWeaponPadVFXBudgetValue = WeaponPadVFXBudget->GetInt();
				WeaponPadVFXBudget->Set(1, ECVF_SetByCode);
			}
			bWeaponPadVFXPolicyApplied = true;
		}
	}
}

void UHeroArenaPerformancePolicyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bAnimationPolicyApplied)
	{
		bAnimationPolicyApplied = false;
		HeroArenaPerformancePolicy::AnimationPolicyUsers = FMath::Max(0, HeroArenaPerformancePolicy::AnimationPolicyUsers - 1);
		if (HeroArenaPerformancePolicy::AnimationPolicyUsers == 0)
		{
			if (IConsoleVariable* AnimationLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.CharacterAnimationLOD")))
			{
				AnimationLOD->Set(HeroArenaPerformancePolicy::PreviousAnimationLODValue, ECVF_SetByCode);
			}
		}
	}

	if (bWeaponVFXPolicyApplied)
	{
		bWeaponVFXPolicyApplied = false;
		HeroArenaPerformancePolicy::WeaponVFXPolicyUsers = FMath::Max(0, HeroArenaPerformancePolicy::WeaponVFXPolicyUsers - 1);
		if (HeroArenaPerformancePolicy::WeaponVFXPolicyUsers == 0)
		{
			if (IConsoleVariable* WeaponVFXBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.WeaponVFX.DistanceBudget")))
			{
				WeaponVFXBudget->Set(HeroArenaPerformancePolicy::PreviousWeaponVFXBudgetValue, ECVF_SetByCode);
			}
		}
	}

	if (bWeaponPadVFXPolicyApplied)
	{
		bWeaponPadVFXPolicyApplied = false;
		HeroArenaPerformancePolicy::WeaponPadVFXPolicyUsers = FMath::Max(0, HeroArenaPerformancePolicy::WeaponPadVFXPolicyUsers - 1);
		if (HeroArenaPerformancePolicy::WeaponPadVFXPolicyUsers == 0)
		{
			if (IConsoleVariable* WeaponPadVFXBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("lyra.WeaponSpawnerVFX.DistanceBudget")))
			{
				WeaponPadVFXBudget->Set(HeroArenaPerformancePolicy::PreviousWeaponPadVFXBudgetValue, ECVF_SetByCode);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}
