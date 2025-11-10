#include "Turn/ConflictResolverSubsystem.h"
#include "Utility/RogueGameplayTags.h"

// Define a local log category to avoid circular dependencies
DEFINE_LOG_CATEGORY_STATIC(LogConflictResolver, Log, All);

void UConflictResolverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UConflictResolverSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UConflictResolverSubsystem::ClearReservations()
{
    ReservationTable.Empty();
}

void UConflictResolverSubsystem::AddReservation(const FReservationEntry& Entry)
{
    // For now, we only care about TimeSlot 0
    if (Entry.TimeSlot != 0) return;

    // Key is a pair of TimeSlot and Target Cell
    TPair<int32, FIntPoint> Key(Entry.TimeSlot, Entry.Cell);
    ReservationTable.FindOrAdd(Key).Add(Entry);
}

TArray<FResolvedAction> UConflictResolverSubsystem::ResolveAllConflicts()
{
    TArray<FResolvedAction> ResolvedActions;

    // ★★★ CRITICAL FIX (2025-11-10): スワップ検出（敵同士のA↔B入れ替わり防止） ★★★
    // 全エントリを収集して Actor→(CurrentCell, NextCell) マップを構築
    TMap<TWeakObjectPtr<AActor>, TPair<FIntPoint, FIntPoint>> ActorMoves;
    for (const auto& [Key, Contenders] : ReservationTable)
    {
        for (const FReservationEntry& Entry : Contenders)
        {
            if (Entry.Actor.IsValid())
            {
                ActorMoves.Add(Entry.Actor, TPair<FIntPoint, FIntPoint>(Entry.CurrentCell, Entry.Cell));
            }
        }
    }

    // スワップ検出：A→B かつ B→A のペアを見つける
    TSet<TWeakObjectPtr<AActor>> SwapActors;  // スワップに関与する Actor
    for (const auto& [ActorA, MoveA] : ActorMoves)
    {
        const FIntPoint& CurrentA = MoveA.Key;
        const FIntPoint& NextA = MoveA.Value;

        // ActorA が移動しない場合はスキップ
        if (CurrentA == NextA)
        {
            continue;
        }

        // NextA にいる Actor を探す
        for (const auto& [ActorB, MoveB] : ActorMoves)
        {
            if (ActorA == ActorB)
            {
                continue;
            }

            const FIntPoint& CurrentB = MoveB.Key;
            const FIntPoint& NextB = MoveB.Value;

            // スワップ検出：A→B かつ B→A
            if (CurrentA == NextB && NextA == CurrentB)
            {
                SwapActors.Add(ActorA);
                SwapActors.Add(ActorB);
                UE_LOG(LogConflictResolver, Warning,
                    TEXT("[SWAP DETECTED] %s (%d,%d)→(%d,%d) <=> %s (%d,%d)→(%d,%d)"),
                    *GetNameSafe(ActorA.Get()), CurrentA.X, CurrentA.Y, NextA.X, NextA.Y,
                    *GetNameSafe(ActorB.Get()), CurrentB.X, CurrentB.Y, NextB.X, NextB.Y);
            }
        }
    }

    // Iterate through all cell reservations
    for (auto const& [Key, Contenders] : ReservationTable)
    {
        const FIntPoint& Cell = Key.Value;

        if (Contenders.Num() <= 1)
        {
            // No conflict, just convert the single entry to a resolved action
            if (Contenders.Num() == 1)
            {
                const FReservationEntry& Winner = Contenders[0];
                FResolvedAction Action;
                Action.Actor = Winner.Actor;
                Action.CurrentCell = Winner.CurrentCell;

                // ★★★ スワップに関与している場合は Wait に変換 ★★★
                if (SwapActors.Contains(Winner.Actor))
                {
                    Action.bIsWait = true;
                    Action.FinalAbilityTag = RogueGameplayTags::AI_Intent_Wait;
                    Action.NextCell = Winner.CurrentCell;  // 移動しない
                    Action.ResolutionReason = TEXT("Blocked by swap detection");
                    UE_LOG(LogConflictResolver, Warning,
                        TEXT("[SWAP BLOCK] %s at (%d,%d) prevented from swapping"),
                        *GetNameSafe(Winner.Actor.Get()), Winner.CurrentCell.X, Winner.CurrentCell.Y);
                }
                else
                {
                    Action.FinalAbilityTag = Winner.AbilityTag;
                    Action.NextCell = Winner.Cell;
                    Action.bIsWait = false;
                }

                ResolvedActions.Add(Action);
            }
        }
        else
        {
            // CONFLICT! More than one actor wants this cell.
            UE_LOG(LogConflictResolver, Warning, TEXT("Conflict at cell (%d, %d) with %d contenders. Picking winner."), Cell.X, Cell.Y, Contenders.Num());

            // Simple random winner for now
            const int32 WinnerIndex = FMath::RandRange(0, Contenders.Num() - 1);
            const FReservationEntry& Winner = Contenders[WinnerIndex];

            // Add the winner's action
            FResolvedAction WinnerAction;
            WinnerAction.Actor = Winner.Actor;
            WinnerAction.CurrentCell = Winner.CurrentCell;

            // ★★★ 勝者でもスワップに関与している場合は Wait に変換 ★★★
            if (SwapActors.Contains(Winner.Actor))
            {
                WinnerAction.bIsWait = true;
                WinnerAction.FinalAbilityTag = RogueGameplayTags::AI_Intent_Wait;
                WinnerAction.NextCell = Winner.CurrentCell;
                WinnerAction.ResolutionReason = FString::Printf(TEXT("Swap-blocked at cell (%d, %d)"), Cell.X, Cell.Y);
                UE_LOG(LogConflictResolver, Warning,
                    TEXT("[SWAP BLOCK] Contest winner %s at (%d,%d) blocked by swap"),
                    *GetNameSafe(Winner.Actor.Get()), Winner.CurrentCell.X, Winner.CurrentCell.Y);
            }
            else
            {
                WinnerAction.FinalAbilityTag = Winner.AbilityTag;
                WinnerAction.NextCell = Winner.Cell;
                WinnerAction.bIsWait = false;
                WinnerAction.ResolutionReason = FString::Printf(TEXT("Won contest for cell (%d, %d)"), Cell.X, Cell.Y);
                UE_LOG(LogConflictResolver, Log, TEXT("Winner for (%d, %d) is %s"), Cell.X, Cell.Y, *GetNameSafe(Winner.Actor));
            }

            ResolvedActions.Add(WinnerAction);

            // Add all losers as "Wait" actions
            for (int32 i = 0; i < Contenders.Num(); ++i)
            {
                if (i == WinnerIndex) continue;

                const FReservationEntry& Loser = Contenders[i];
                FResolvedAction LoserAction;
                LoserAction.Actor = Loser.Actor;
                LoserAction.bIsWait = true; // This is the key part
                LoserAction.FinalAbilityTag = RogueGameplayTags::AI_Intent_Wait;
                LoserAction.CurrentCell = Loser.CurrentCell;
                LoserAction.NextCell = Loser.CurrentCell; // They don't move
                LoserAction.ResolutionReason = FString::Printf(TEXT("Lost contest for cell (%d, %d)"), Cell.X, Cell.Y);
                ResolvedActions.Add(LoserAction);
                UE_LOG(LogConflictResolver, Log, TEXT("Loser for (%d, %d) is %s, will wait."), Cell.X, Cell.Y, *GetNameSafe(Loser.Actor));
            }
        }
    }

    return ResolvedActions;
}

// Stub implementation for this function, as it's in the header
int32 UConflictResolverSubsystem::GetActionTier(const FGameplayTag& AbilityTag) const
{
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Attack)) return 3;
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Move)) return 1; // Assuming Dash is 2
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Wait)) return 0;
    return 0;
}