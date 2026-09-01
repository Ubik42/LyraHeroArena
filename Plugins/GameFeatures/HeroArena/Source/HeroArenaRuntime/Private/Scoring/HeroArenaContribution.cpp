#include "Scoring/HeroArenaContribution.h"

#include "Messages/LyraVerbMessageHelpers.h"
#include "Player/LyraPlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaContribution, Log, All);

namespace HeroArenaContribution
{
	void Add(UObject* SourceObject, const float EffectiveAmount)
	{
		ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(
			ULyraVerbMessageHelpers::GetPlayerStateFromObject(SourceObject));
		if (!PlayerState || !PlayerState->HasAuthority() || EffectiveAmount <= 0.0f)
		{
			return;
		}

		const FGameplayTag ContributionTag =
			FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.RoleContribution"));
		const int32 PreviousTotal = PlayerState->GetStatTagStackCount(ContributionTag);
		const int32 Contribution = FMath::Max(1, FMath::RoundToInt(EffectiveAmount));
		PlayerState->AddStatTagStack(ContributionTag, Contribution);
		PlayerState->ForceNetUpdate();
		if (PreviousTotal == 0)
		{
			UE_LOG(LogHeroArenaContribution, Log,
				TEXT("Role contribution started: player=%s first=%d"),
				*GetNameSafe(PlayerState), Contribution);
		}
	}
}
