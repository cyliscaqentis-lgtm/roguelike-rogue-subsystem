#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

FORCEINLINE bool IsAuthorityLike(const UWorld* World, const AActor* Context)
{
	if (!World || !Context) return false;
	const ENetMode NetMode = World->GetNetMode();
	return Context->HasAuthority() || NetMode == NM_Standalone;
}

FORCEINLINE bool IsAuthorityLike(const UWorld* World)
{
	if (!World) return false;
	const ENetMode NetMode = World->GetNetMode();
	return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer || NetMode == NM_Standalone;
}




