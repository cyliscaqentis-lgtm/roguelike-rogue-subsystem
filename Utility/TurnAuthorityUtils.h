#pragma once

#include "CoreMinimal.h"

FORCEINLINE bool IsAuthorityLike(const UWorld* World, const AActor* Context)
{
	if (!World || !Context) return false;
	const ENetMode NetMode = World->GetNetMode();
	return Context->HasAuthority() || NetMode == NM_Standalone;
}




