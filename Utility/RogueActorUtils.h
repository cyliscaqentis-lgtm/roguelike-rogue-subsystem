#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "Utility/ProjectDiagnostics.h"

// CodeRevision: INC-2025-1205-R1 (Centralize ASC/team helper utilities used by GameTurnManagerBase) (2025-11-21 23:08)
/** Try to find the ASC from an actor or its controller. */
static FORCEINLINE UAbilitySystemComponent* TryGetASC(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
	{
		return ASI->GetAbilitySystemComponent();
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const IAbilitySystemInterface* ControllerASI = Cast<IAbilitySystemInterface>(Pawn->GetController()))
		{
			return ControllerASI->GetAbilitySystemComponent();
		}
	}

	return nullptr;
}

/** Retrieve a unit/actor's team ID through its controller or the actor itself. */
static FORCEINLINE int32 GetTeamIdOf(const AActor* Actor)
{
	if (!Actor)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GetTeamIdOf] Actor=NULL"));
		return INDEX_NONE;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const IGenericTeamAgentInterface* ControllerTeam = Cast<IGenericTeamAgentInterface>(Pawn->GetController()))
		{
			const int32 TeamID = ControllerTeam->GetGenericTeamId().GetId();
			UE_LOG(LogTurnManager, Log, TEXT("[GetTeamIdOf] %s -> Controller TeamID=%d"), *Actor->GetName(), TeamID);
			return TeamID;
		}
	}

	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Actor))
	{
		const int32 TeamID = TeamAgent->GetGenericTeamId().GetId();
		UE_LOG(LogTurnManager, Log, TEXT("[GetTeamIdOf] %s -> Actor TeamID=%d"), *Actor->GetName(), TeamID);
		return TeamID;
	}

	UE_LOG(LogTurnManager, Warning, TEXT("[GetTeamIdOf] %s -> No TeamID found"), *Actor->GetName());
	return INDEX_NONE;
}
