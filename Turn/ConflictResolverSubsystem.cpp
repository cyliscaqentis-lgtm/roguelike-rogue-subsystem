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
                Action.FinalAbilityTag = Winner.AbilityTag;
                Action.CurrentCell = Winner.CurrentCell;
                Action.NextCell = Winner.Cell;
                Action.bIsWait = false;
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
            WinnerAction.FinalAbilityTag = Winner.AbilityTag;
            WinnerAction.CurrentCell = Winner.CurrentCell;
            WinnerAction.NextCell = Winner.Cell;
            WinnerAction.bIsWait = false;
            WinnerAction.ResolutionReason = FString::Printf(TEXT("Won contest for cell (%d, %d)"), Cell.X, Cell.Y);
            ResolvedActions.Add(WinnerAction);
            UE_LOG(LogConflictResolver, Log, TEXT("Winner for (%d, %d) is %s"), Cell.X, Cell.Y, *GetNameSafe(Winner.Actor));

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