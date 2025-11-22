// Copyright Epic Games, Inc. All Rights Reserved.
// CodeRevision: INC-2025-1122-R1 (Extract tag cleanup utilities from GameTurnManagerBase) (2025-11-22)

#include "Utility/TurnTagCleanupUtils.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Utility/RogueGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogTurnTagCleanup, Log, All);

namespace TurnTagCleanupUtils
{

int32 RemoveGameplayTagLoose(UAbilitySystemComponent* ASC, const FGameplayTag& TagToRemove)
{
    if (!ASC || !TagToRemove.IsValid())
    {
        return 0;
    }

    const int32 Count = ASC->GetTagCount(TagToRemove);
    for (int32 i = 0; i < Count; ++i)
    {
        ASC->RemoveLooseGameplayTag(TagToRemove);
    }
    return Count;
}

int32 CleanseBlockingTags(UAbilitySystemComponent* ASC, AActor* ActorForLog)
{
    if (!ASC)
    {
        return 0;
    }

    int32 ClearedCount = 0;

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ++ClearedCount;
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[CleanseBlockingTags] Cleared State.Ability.Executing from %s"),
            *GetNameSafe(ActorForLog));
    }

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        ++ClearedCount;
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[CleanseBlockingTags] Cleared State.Action.InProgress from %s"),
            *GetNameSafe(ActorForLog));
    }

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Moving))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Moving);
        ++ClearedCount;
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[CleanseBlockingTags] Cleared State.Moving from %s"),
            *GetNameSafe(ActorForLog));
    }

    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));
    if (FallingTag.IsValid() && ASC->HasMatchingGameplayTag(FallingTag))
    {
        ASC->RemoveLooseGameplayTag(FallingTag);
        ++ClearedCount;
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[CleanseBlockingTags] Cleared Movement.Mode.Falling from %s"),
            *GetNameSafe(ActorForLog));
    }

    return ClearedCount;
}

void ClearResidualInProgressTags(UWorld* World, APawn* PlayerPawn, const TArray<AActor*>& EnemyActors)
{
    static const FGameplayTag InProgressTag = FGameplayTag::RequestGameplayTag(FName("State.Action.InProgress"));
    static const FGameplayTag ExecutingTag = FGameplayTag::RequestGameplayTag(FName("State.Ability.Executing"));
    static const FGameplayTag MovingTag = FGameplayTag::RequestGameplayTag(FName("State.Moving"));
    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));

    if (!InProgressTag.IsValid() || !ExecutingTag.IsValid() || !MovingTag.IsValid())
    {
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[ClearResidualInProgressTags] One or more state tags not found"));
        return;
    }

    TArray<AActor*> AllUnits;

    if (PlayerPawn)
    {
        AllUnits.Add(PlayerPawn);
    }

    AllUnits.Append(EnemyActors);

    int32 TotalInProgress = 0;
    int32 TotalExecuting = 0;
    int32 TotalMoving = 0;
    int32 TotalFalling = 0;

    for (AActor* Actor : AllUnits)
    {
        if (!Actor)
        {
            continue;
        }

        UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
        if (!ASC)
        {
            continue;
        }

        const int32 InProgressCount = RemoveGameplayTagLoose(ASC, InProgressTag);
        TotalInProgress += InProgressCount;

        const int32 ExecutingCount = RemoveGameplayTagLoose(ASC, ExecutingTag);
        TotalExecuting += ExecutingCount;

        const int32 MovingCount = RemoveGameplayTagLoose(ASC, MovingTag);
        TotalMoving += MovingCount;

        int32 FallingCount = 0;
        if (FallingTag.IsValid())
        {
            FallingCount = RemoveGameplayTagLoose(ASC, FallingTag);
            TotalFalling += FallingCount;
        }

        if (InProgressCount > 0 || ExecutingCount > 0 || MovingCount > 0 || FallingCount > 0)
        {
            UE_LOG(LogTurnTagCleanup, Warning,
                TEXT("[ClearResidualInProgressTags] %s cleared: InProgress=%d, Executing=%d, Moving=%d, Falling=%d"),
                *GetNameSafe(Actor), InProgressCount, ExecutingCount, MovingCount, FallingCount);
        }
    }

    if (TotalInProgress > 0 || TotalExecuting > 0 || TotalMoving > 0 || TotalFalling > 0)
    {
        UE_LOG(LogTurnTagCleanup, Warning,
            TEXT("[ClearResidualInProgressTags] Total residual tags cleared: InProgress=%d, Executing=%d, Moving=%d, Falling=%d"),
            TotalInProgress, TotalExecuting, TotalMoving, TotalFalling);
    }
}

} // namespace TurnTagCleanupUtils
