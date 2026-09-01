#include "Abilities/HeroArenaGameplayAbility_BotSpiralGrenade.h"

#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_BotSpiralGrenadeCooldown.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_BotSpiralGrenade)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBotSpiralGrenade, Log, All);

UHeroArenaGameplayAbility_BotSpiralGrenade::UHeroArenaGameplayAbility_BotSpiralGrenade(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_BotSpiralGrenadeCooldown::StaticClass();

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.BotSpiralGrenade")));
	SetAssetTags(Tags);

	static ConstructorHelpers::FClassFinder<AActor> Projectile(
		TEXT("/HeroArena/Heroes/Striker/Projectiles/B_StrikerSpiralGrenade"));
	ProjectileClass = Projectile.Class;
}

void UHeroArenaGameplayAbility_BotSpiralGrenade::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	const APlayerState* PlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (!Character || !Character->HasAuthority() || !PlayerState || !PlayerState->IsABot() ||
		!ProjectileClass || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const AController* Controller = Character->GetController();
	const FRotator Aim = Controller ? Controller->GetControlRotation() : Character->GetActorRotation();
	const FVector Forward = Aim.Vector();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Character;
	SpawnParameters.Instigator = Character;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Projectile = GetWorld()->SpawnActor<AActor>(ProjectileClass,
		Character->GetActorLocation() + Forward * 100.0f + FVector::UpVector * 60.0f,
		Aim, SpawnParameters);

	UE_LOG(LogHeroArenaBotSpiralGrenade, Log,
		TEXT("Bot spiral grenade thrown: player=%s pawn=%s projectile=%s"),
		*GetNameSafe(PlayerState), *GetNameSafe(Character), *GetNameSafe(Projectile));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !IsValid(Projectile));
}
