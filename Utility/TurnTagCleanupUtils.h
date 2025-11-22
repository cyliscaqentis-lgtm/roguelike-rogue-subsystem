// Copyright Epic Games, Inc. All Rights Reserved.
// CodeRevision: INC-2025-1122-R1 (Extract tag cleanup utilities from GameTurnManagerBase) (2025-11-22)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class AActor;

/**
 * Utility functions for cleaning up GameplayTags during turn transitions.
 * Extracted from GameTurnManagerBase to reduce file size and improve reusability.
 */
namespace TurnTagCleanupUtils
{
    /**
     * Remove all instances of a GameplayTag from an ASC using RemoveLooseGameplayTag.
     * @param ASC The AbilitySystemComponent to modify
     * @param TagToRemove The tag to remove
     * @return The number of tag instances removed
     */
    LYRAGAME_API int32 RemoveGameplayTagLoose(UAbilitySystemComponent* ASC, const FGameplayTag& TagToRemove);

    /**
     * Clear common blocking tags from an ASC that may prevent abilities from activating.
     * Clears: State.Ability.Executing, State.Action.InProgress, State.Moving, Movement.Mode.Falling
     * @param ASC The AbilitySystemComponent to modify
     * @param ActorForLog Optional actor reference for logging purposes
     * @return The number of tags cleared
     */
    LYRAGAME_API int32 CleanseBlockingTags(UAbilitySystemComponent* ASC, AActor* ActorForLog = nullptr);

    /**
     * Clear residual in-progress tags from all units (player + enemies).
     * Used as a safety fallback after barriers to prevent stuck turns.
     * @param World The world context
     * @param PlayerPawn The player pawn (can be null)
     * @param EnemyActors Array of enemy actors to process
     */
    LYRAGAME_API void ClearResidualInProgressTags(UWorld* World, APawn* PlayerPawn, const TArray<AActor*>& EnemyActors);
}
