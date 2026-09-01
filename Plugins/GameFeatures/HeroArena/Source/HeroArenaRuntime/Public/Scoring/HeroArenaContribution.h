#pragma once

#include "CoreMinimal.h"

namespace HeroArenaContribution
{
	/** Adds an already-resolved gameplay value to the authoritative player's replicated contribution stat. */
	HEROARENARUNTIME_API void Add(UObject* SourceObject, float EffectiveAmount);
}
