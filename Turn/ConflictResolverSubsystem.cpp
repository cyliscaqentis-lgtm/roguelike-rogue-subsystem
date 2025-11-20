#include "Turn/ConflictResolverSubsystem.h"
#include "Utility/RogueGameplayTags.h"
#include "Grid/GridOccupancySubsystem.h"
#include "EngineUtils.h"
#include "Turn/GameTurnManagerBase.h"
#include "Templates/Tuple.h"
// CodeRevision: INC-2025-1148-R1 (Allow rerouting moves off blocked attack tiles instead of forced WAIT when a closer tile exists) (2025-11-20 15:30)
#include "Turn/DistanceFieldSubsystem.h"

// Local log category (avoid circular dependencies)
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
    if (Entry.TimeSlot != 0)
    {
        return;
    }

    // Key is a pair of (TimeSlot, TargetCell)
    TPair<int32, FIntPoint> Key(Entry.TimeSlot, Entry.Cell);
    ReservationTable.FindOrAdd(Key).Add(Entry);
}

TArray<FResolvedAction> UConflictResolverSubsystem::ResolveAllConflicts()
{
    // ========================================================================
    // CONTRACT (Priority 2): Count input reservations for invariant check
    // ========================================================================
    int32 NumReservations = 0;
    for (const auto& Pair : ReservationTable)
    {
        const TPair<int32, FIntPoint>& Key = Pair.Key;
        const TArray<FReservationEntry>& Contenders = Pair.Value;
        (void)Key; // unused in this loop, but kept for clarity
        NumReservations += Contenders.Num();
    }

    UE_LOG(LogConflictResolver, Log,
        TEXT("[ResolveAllConflicts] START: NumReservations=%d"), NumReservations);

    TArray<FResolvedAction> OptimisticActions;

    // ------------------------------------------------------------------------
    // Resolve GridOccupancy and current TurnId
    // ------------------------------------------------------------------------
    UGridOccupancySubsystem* GridOccupancy = ResolveGridOccupancy();
    int32 CurrentTurnId = 0;
    if (GridOccupancy)
    {
        CurrentTurnId = GridOccupancy->GetCurrentTurnId();
    }

    // ------------------------------------------------------------------------
    // Resolve current TurnMode (Sequential / Simultaneous) via TurnManager
    // NOTE: TurnManager is the SSOT for TurnMode; resolver must not recompute.
    // ------------------------------------------------------------------------
    const bool bSequentialAttackMode = [this]() -> bool
    {
        if (AGameTurnManagerBase* TurnManager = ResolveTurnManager())
        {
            return TurnManager->IsSequentialModeActive();
        }
        return false;
    }();

    UE_LOG(LogConflictResolver, Log,
        TEXT("[ResolveAllConflicts] Mode: %s"),
        bSequentialAttackMode ? TEXT("Sequential") : TEXT("Simultaneous"));

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
    const FGameplayTag MoveTag   = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag   = RogueGameplayTags::AI_Intent_Wait;

    // ========================================================================
    // SWAP DETECTION (Perfect A<->B swaps)
    //
    // Build a per-actor map of (CurrentCell, NextCell) and detect pairs:
    //   A: X->Y and B: Y->X
    //
    // Assumption: one reservation per actor per turn. If this changes,
    // swap detection and invariants must be revisited.
    // ========================================================================
    TMap<AActor*, TPair<FIntPoint, FIntPoint>> ActorMoves;

    for (const auto& Pair : ReservationTable)
    {
        const TPair<int32, FIntPoint>& Key = Pair.Key;
        const TArray<FReservationEntry>& Contenders = Pair.Value;
        (void)Key;

        for (const FReservationEntry& Entry : Contenders)
        {
            if (Entry.Actor != nullptr && IsValid(Entry.Actor))
            {
                // We assume exactly one reservation per actor per turn.
                ensureAlwaysMsgf(
                    !ActorMoves.Contains(Entry.Actor),
                    TEXT("[ConflictResolver] Multiple reservations for a single actor detected (%s). ")
                    TEXT("Swap detection assumes a 1:1 mapping Actor -> Reservation."),
                    *GetNameSafe(Entry.Actor)
                );

                ActorMoves.Add(
                    Entry.Actor,
                    TPair<FIntPoint, FIntPoint>(Entry.CurrentCell, Entry.Cell)
                );
            }
            else
            {
                UE_LOG(LogConflictResolver, Error,
                    TEXT("[ResolveAllConflicts] SKIP invalid Entry.Actor (nullptr or destroyed) at Cell=(%d,%d)"),
                    Entry.Cell.X, Entry.Cell.Y);
            }
        }
    }

    // Detect A<->B swaps
    TSet<AActor*> SwapActors;  // actors participating in perfect swap pairs
    for (const auto& PairA : ActorMoves)
    {
        AActor* ActorA = PairA.Key;
        const FIntPoint& CurrentA = PairA.Value.Key;
        const FIntPoint& NextA    = PairA.Value.Value;

        // Ignore actors that do not move
        if (CurrentA == NextA)
        {
            continue;
        }

        for (const auto& PairB : ActorMoves)
        {
            AActor* ActorB = PairB.Key;
            if (ActorA == ActorB)
            {
                continue;
            }

            const FIntPoint& CurrentB = PairB.Value.Key;
            const FIntPoint& NextB    = PairB.Value.Value;

            // Perfect swap: A->B.current AND B->A.current
            if (CurrentA == NextB && NextA == CurrentB)
            {
                SwapActors.Add(ActorA);
                SwapActors.Add(ActorB);

                UE_LOG(LogConflictResolver, Warning,
                    TEXT("[SWAP DETECTED] %s (%d,%d)->(%d,%d) <=> %s (%d,%d)->(%d,%d)"),
                    *GetNameSafe(ActorA), CurrentA.X, CurrentA.Y, NextA.X, NextA.Y,
                    *GetNameSafe(ActorB), CurrentB.X, CurrentB.Y, NextB.X, NextB.Y);
            }
        }
    }

    // ========================================================================
    // PRIORITY 2.2: Static blockers from GridOccupancy
    //
    // Phase A:
    //   - Collect the set of actors that have made at least one reservation
    //     this turn (ReservationTable).
    //   - Scan the current Occupancy map.
    //   - Any occupant that has NO reservation is treated as a "stationary
    //     blocker" for its cell in this turn.
    //
    // These blockers do not need FResolvedAction entries because they have
    // no intents; they simply prevent others from entering their cells.
    // ========================================================================

    // Phase A-1: Build the set of actors that made reservations this turn.
    TSet<AActor*> ActorsWithReservationsThisTurn;
    for (const auto& Pair : ReservationTable)
    {
        const TArray<FReservationEntry>& Contenders = Pair.Value;
        for (const FReservationEntry& Entry : Contenders)
        {
            if (Entry.Actor && IsValid(Entry.Actor))
            {
                ActorsWithReservationsThisTurn.Add(Entry.Actor);
            }
        }
    }

    UE_LOG(LogConflictResolver, Log,
        TEXT("[StaticBlockers] Found %d actors with reservations this turn"),
        ActorsWithReservationsThisTurn.Num());

    // Phase A-2: Scan GridOccupancy for cells occupied by actors that have
    // NO reservation this turn → these become StationaryBlockers.
    TMap<FIntPoint, AActor*> StationaryBlockers;  // Key = Cell, Value = Stationary actor

    if (GridOccupancy)
    {
        TMap<FIntPoint, AActor*> AllOccupiedCells = GridOccupancy->GetAllOccupiedCells();

        for (const auto& OccPair : AllOccupiedCells)
        {
            const FIntPoint& Cell = OccPair.Key;
            AActor* Occupant = OccPair.Value;

            if (Occupant && IsValid(Occupant))
            {
                if (!ActorsWithReservationsThisTurn.Contains(Occupant))
                {
                    // This actor is not moving this turn and blocks the cell.
                    StationaryBlockers.Add(Cell, Occupant);

                    UE_LOG(LogConflictResolver, Verbose,
                        TEXT("[StaticBlockers] Stationary blocker %s at cell (%d,%d)"),
                        *GetNameSafe(Occupant), Cell.X, Cell.Y);
                }
            }
        }
    }

    UE_LOG(LogConflictResolver, Log,
        TEXT("[StaticBlockers] Total stationary blockers detected: %d"),
        StationaryBlockers.Num());

    // ========================================================================
    // Phase B: Per-cell conflict resolution (OptimisticActions)
    //
    // For each reserved cell:
    //   1) If a GridOccupancy-based stationary blocker exists:
    //        - All reservations to that cell become WAIT.
    //        - The blocker itself has no reservation and therefore no action.
    //
    //   2) Otherwise:
    //        - If only one contender → direct success (no conflict).
    //        - If multiple contenders:
    //            - In Sequential mode: ATTACK beats MOVE for the same cell.
    //            - Otherwise: pick a winner (currently random tie-break).
    //            - Losers are converted to WAIT.
    //
    // The result of Phase B is OptimisticActions: a 1:1 mapping from
    // reservations to resolved actions, but not yet revalidated against
    // "stationary cells" emitted by other actions.
    // ========================================================================
    for (const auto& Pair : ReservationTable)
    {
        const TPair<int32, FIntPoint>& Key = Pair.Key;
        const TArray<FReservationEntry>& Contenders = Pair.Value;
        const FIntPoint& Cell = Key.Value;

        // Case 1: Cell is blocked by a non-moving occupant (no intent this turn).
        if (AActor** StationaryBlockerPtr = StationaryBlockers.Find(Cell))
        {
            AActor* StationaryBlocker = *StationaryBlockerPtr;

            // All reservations to this cell are turned into WAIT actions.
            for (const FReservationEntry& Blocked : Contenders)
            {
                if (Blocked.Actor == nullptr || !IsValid(Blocked.Actor))
                {
                    continue;
                }

                // If we ever change the definition of StationaryBlockers to include
                // actors that also produced reservations, we treat their own intent
                // as a successful stationary WAIT.
                if (Blocked.Actor == StationaryBlocker)
                {
                    FResolvedAction SelfWait = CreateWaitAction(Blocked);
                    SelfWait.AbilityTag       = WaitTag;
                    SelfWait.FinalAbilityTag  = WaitTag;
                    SelfWait.ResolutionReason = FString::Printf(
                        TEXT("Success: Stationary unit holds its own cell (%d,%d)"),
                        Cell.X, Cell.Y);

                    OptimisticActions.Add(SelfWait);
                    continue;
                }

                FResolvedAction WaitAction = CreateWaitAction(Blocked);
                WaitAction.AbilityTag       = WaitTag;
                WaitAction.FinalAbilityTag  = WaitTag;
                WaitAction.ResolutionReason = FString::Printf(
                    TEXT("Blocked by non-moving unit at cell (%d,%d)"),
                    Cell.X, Cell.Y);

                OptimisticActions.Add(WaitAction);

                UE_LOG(LogConflictResolver, Warning,
                    TEXT("[StaticBlockers] %s blocked by stationary %s at (%d,%d)"),
                    *GetNameSafe(Blocked.Actor), *GetNameSafe(StationaryBlocker), Cell.X, Cell.Y);
            }

            // This cell is fully handled in static-blocker flow.
            continue;
        }

        // Case 2: No static blocker in this cell → normal conflict resolution.
        if (Contenders.Num() <= 1)
        {
            // No conflict, just convert the single entry to a resolved action.
            if (Contenders.Num() == 1)
            {
                const FReservationEntry& Winner = Contenders[0];

                if (Winner.Actor == nullptr || !IsValid(Winner.Actor))
                {
                    UE_LOG(LogConflictResolver, Error,
                        TEXT("[ResolveAllConflicts] SKIP invalid Winner.Actor at Cell=(%d,%d)"),
                        Cell.X, Cell.Y);
                    continue;
                }

                FResolvedAction Action;
                Action.Actor        = Winner.Actor;
                Action.SourceActor  = Winner.Actor;
                Action.CurrentCell  = Winner.CurrentCell;
                Action.NextCell     = Winner.Cell;
                Action.bIsWait      = false;
                Action.AbilityTag   = Winner.AbilityTag;
                Action.FinalAbilityTag = Winner.AbilityTag;

                AActor* WinnerActorPtr = Winner.Actor;

                // ResolutionReason for non-conflict actions
                if (SwapActors.Contains(WinnerActorPtr))
                {
                    // CodeRevision: INC-2025-1148-R2 (Forbid swaps to prevent GridOccupancy deadlocks) (2025-11-20 19:05)
                    // Swaps are design-illegal and cause deadlocks due to Origin Hold (Backstab Protection).
                    // We must force both actors to WAIT.
                    
                    Action.bIsWait         = true;
                    Action.NextCell        = Action.CurrentCell;
                    Action.AbilityTag      = WaitTag;
                    Action.FinalAbilityTag = WaitTag;
                    Action.ResolutionReason = TEXT("Conflict: Swap move forbidden (GridOccupancy deadlock prevention)");

                    UE_LOG(LogConflictResolver, Warning,
                        TEXT("[SWAP REJECTED] %s at (%d,%d)->(%d,%d) forced to WAIT"),
                        *GetNameSafe(WinnerActorPtr),
                        Winner.CurrentCell.X, Winner.CurrentCell.Y,
                        Winner.Cell.X, Winner.Cell.Y);
                }
                else if (Winner.AbilityTag.MatchesTag(AttackTag))
                {
                    Action.ResolutionReason = TEXT("Success: Attack (no conflict)");
                }
                else if (Winner.AbilityTag.MatchesTag(MoveTag))
                {
                    Action.ResolutionReason = TEXT("Success: Move (no conflict)");
                }
                else
                {
                    Action.ResolutionReason = TEXT("Success: Action (no conflict)");
                }

                // Mark the winner's reservation as committed for this turn
                if (GridOccupancy)
                {
                    GridOccupancy->MarkReservationCommitted(WinnerActorPtr, CurrentTurnId);
                }

                OptimisticActions.Add(Action);
            }
        }
        else
        {
            // ----------------------------------------------------------------
            // CONFLICT: More than one actor wants this cell.
            // ----------------------------------------------------------------
            UE_LOG(LogConflictResolver, Warning,
                TEXT("Conflict at cell (%d,%d) with %d contenders. Resolving..."),
                Cell.X, Cell.Y, Contenders.Num());

            const auto ApplySwapRejection = [&](const FReservationEntry& Entry, FResolvedAction& Action, const FString& Reason) -> bool
            {
                if (Entry.Actor == nullptr || !IsValid(Entry.Actor))
                {
                    return false;
                }

                if (!SwapActors.Contains(Entry.Actor))
                {
                    return false;
                }

                // CodeRevision: INC-2025-1148-R2 (Forbid swaps to prevent GridOccupancy deadlocks) (2025-11-20 19:05)
                Action.bIsWait         = true;
                Action.NextCell        = Action.CurrentCell;
                Action.AbilityTag      = WaitTag;
                Action.FinalAbilityTag = WaitTag;
                Action.ResolutionReason = Reason;

                UE_LOG(LogConflictResolver, Warning,
                    TEXT("[SWAP REJECTED] %s at (%d,%d)->(%d,%d) forced to WAIT"),
                    *GetNameSafe(Entry.Actor),
                    Entry.CurrentCell.X, Entry.CurrentCell.Y,
                    Entry.Cell.X, Entry.Cell.Y);

                return true;
            };

            // Default winner index (random tie-breaker for equal tiers).
            int32 WinnerIndex = FMath::RandRange(0, Contenders.Num() - 1);
            bool bAttackWonConflict = false;

            // Sequential mode: ATTACK entries take absolute priority.
            if (bSequentialAttackMode)
            {
                for (int32 AttackIndex = 0; AttackIndex < Contenders.Num(); ++AttackIndex)
                {
                    const FReservationEntry& Candidate = Contenders[AttackIndex];
                    if (Candidate.Actor != nullptr &&
                        IsValid(Candidate.Actor) &&
                        Candidate.AbilityTag.MatchesTag(AttackTag))
                    {
                        WinnerIndex = AttackIndex;
                        bAttackWonConflict = true;

                        UE_LOG(LogConflictResolver, Log,
                            TEXT("[Sequential] Attack entry locks cell (%d,%d), blocking %d other contender(s)"),
                            Cell.X, Cell.Y, Contenders.Num() - 1);
                        break;
                    }
                }
            }

            const FReservationEntry& Winner = Contenders[WinnerIndex];

            if (Winner.Actor == nullptr || !IsValid(Winner.Actor))
            {
                UE_LOG(LogConflictResolver, Error,
                    TEXT("[ResolveAllConflicts] SKIP invalid Winner.Actor (conflict) at Cell=(%d,%d)"),
                    Cell.X, Cell.Y);
                continue;
            }

            // Winner action
            FResolvedAction WinnerAction;
            WinnerAction.Actor        = Winner.Actor;
            WinnerAction.SourceActor  = Winner.Actor;
            WinnerAction.CurrentCell  = Winner.CurrentCell;
            WinnerAction.NextCell     = Winner.Cell;
            WinnerAction.bIsWait      = false;
            WinnerAction.AbilityTag   = Winner.AbilityTag;
            WinnerAction.FinalAbilityTag = Winner.AbilityTag;

            AActor* WinnerActorPtr = Winner.Actor;

            const bool bWinnerSwapRejected = ApplySwapRejection(
                Winner,
                WinnerAction,
                FString::Printf(
                    TEXT("Conflict: Swap move forbidden at cell (%d,%d)"),
                    Cell.X, Cell.Y));

            if (!bWinnerSwapRejected)
            {
                WinnerAction.ResolutionReason = FString::Printf(
                    TEXT("Won contest for cell (%d,%d)"),
                    Cell.X, Cell.Y);

                UE_LOG(LogConflictResolver, Log,
                    TEXT("Winner for (%d,%d) is %s"),
                    Cell.X, Cell.Y, *GetNameSafe(WinnerActorPtr));
            }

            if (GridOccupancy)
            {
                GridOccupancy->MarkReservationCommitted(WinnerActorPtr, CurrentTurnId);
            }

            OptimisticActions.Add(WinnerAction);

            // Losers → WAIT actions
            for (int32 i = 0; i < Contenders.Num(); ++i)
            {
                if (i == WinnerIndex)
                {
                    continue;
                }

                const FReservationEntry& Loser = Contenders[i];

                if (Loser.Actor == nullptr || !IsValid(Loser.Actor))
                {
                    UE_LOG(LogConflictResolver, Error,
                        TEXT("[ResolveAllConflicts] SKIP invalid Loser.Actor at Cell=(%d,%d)"),
                        Cell.X, Cell.Y);
                    continue;
                }

                FResolvedAction LoserAction;
                LoserAction.Actor        = Loser.Actor;
                LoserAction.SourceActor  = Loser.Actor;
                LoserAction.CurrentCell  = Loser.CurrentCell;
                LoserAction.NextCell     = Loser.CurrentCell; // stays in place
                LoserAction.bIsWait      = true;
                // For losers, the final behavior is WAIT; the original intent
                // is no longer relevant for movement resolution.
                LoserAction.AbilityTag      = WaitTag;
                LoserAction.FinalAbilityTag = WaitTag;

                const bool bLoserSwapRejected = ApplySwapRejection(
                    Loser,
                    LoserAction,
                    FString::Printf(
                        TEXT("Conflict: Swap move forbidden (non-winning contender) at cell (%d,%d)"),
                        Cell.X, Cell.Y));

                if (!bLoserSwapRejected && bAttackWonConflict && !Loser.AbilityTag.MatchesTag(AttackTag))
                {
                    // In Sequential mode, MOVE lost to ATTACK.
                    LoserAction.ResolutionReason = FString::Printf(
                        TEXT("LostConflict: Cell (%d,%d) locked by attacker (Sequential)"),
                        Cell.X, Cell.Y);
                }
                else if (!bLoserSwapRejected)
                {
                    // Generic conflict loss (Simultaneous or Attack vs Attack).
                    LoserAction.ResolutionReason = FString::Printf(
                        TEXT("LostConflict: Cell (%d,%d) occupied by higher priority"),
                        Cell.X, Cell.Y);
                }

                OptimisticActions.Add(LoserAction);

                UE_LOG(LogConflictResolver, Log,
                    TEXT("Loser for (%d,%d) is %s, will wait. Reason: %s"),
                    Cell.X, Cell.Y,
                    *GetNameSafe(Loser.Actor),
                    *LoserAction.ResolutionReason);
            }
        }
    }

    // ========================================================================
    // Phase C: Revalidation against stationary cells (Priority 2.1)
    //
    // At this point, OptimisticActions is a 1:1 mapping from reservations to
    // actions. However, some MOVE winners may be trying to enter a cell that
    // is effectively stationary (e.g., a WAIT result).
    //
    // We:
    //   - Collect all cells that remain occupied by non-moving actions
    //     (WAIT, and any (future) stationary attacks).
    //   - For each MOVE action whose target cell is in StationaryCells,
    //     downgrade it to WAIT.
    // ========================================================================

    // ========================================================================
    // Phase C: Revalidation against stationary cells (Priority 2.1)
    //
    // We must iteratively resolve blockages because a unit converting to WAIT
    // might block another unit that was previously clear.
    // Example: A->B->C->(Blocked).
    // Iter 1: C blocked -> C Waits.
    // Iter 2: B blocked by C -> B Waits.
    // Iter 3: A blocked by B -> A Waits.
    // ========================================================================

    bool bChanged = false;
    int32 IterationCount = 0;
    const int32 MaxIterations = OptimisticActions.Num() + 2; // Safety cap

    do
    {
        bChanged = false;
        IterationCount++;

        // 1. Collect all cells that are currently effectively stationary
        TSet<FIntPoint> StationaryCells;
        for (const FResolvedAction& Action : OptimisticActions)
        {
            // A stationary attack is defined as "attack that does not move"
            const bool bIsStationaryAttack =
                Action.AbilityTag.MatchesTag(AttackTag) &&
                Action.NextCell == Action.CurrentCell;

            if (Action.bIsWait || bIsStationaryAttack)
            {
                StationaryCells.Add(Action.CurrentCell);
            }
        }

        UE_LOG(LogConflictResolver, Verbose,
            TEXT("[ResolveAllConflicts] Phase C Iteration %d: %d stationary cells"),
            IterationCount, StationaryCells.Num());

        // 2. Check for moves into stationary cells
        for (FResolvedAction& Action : OptimisticActions)
        {
            if (Action.bIsWait)
            {
                continue;
            }

            const bool bIsMoveAction =
                Action.FinalAbilityTag.MatchesTag(MoveTag) ||
                Action.AbilityTag.MatchesTag(MoveTag);

            if (bIsMoveAction)
            {
                const FIntPoint TargetCell = Action.NextCell;

                if (StationaryCells.Contains(TargetCell))
                {
                    AActor* ActorPtr = Action.Actor.Get();

                    UE_LOG(LogConflictResolver, Warning,
                        TEXT("[ResolveAllConflicts] Revalidation (Iter %d): %s move to (%d,%d) blocked by stationary unit"),
                        IterationCount, *GetNameSafe(ActorPtr), TargetCell.X, TargetCell.Y);

                    bool bRerouted = false;

                    // CodeRevision: INC-2025-1148-R1
                    // Try to reroute to an adjacent cell if blocked
                    if (UWorld* World = GetWorld())
                    {
                        UGridOccupancySubsystem* GridOcc = ResolveGridOccupancy();
                        UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>();

                        if (GridOcc && DF && ActorPtr)
                        {
                            const FIntPoint CurrentCell = Action.CurrentCell;
                            const int32 CurrentDist = DF->GetDistance(CurrentCell);

                            if (CurrentDist >= 0)
                            {
                                const FIntPoint NeighborOffsets[4] = {
                                    FIntPoint(1, 0),
                                    FIntPoint(-1, 0),
                                    FIntPoint(0, 1),
                                    FIntPoint(0, -1)
                                };

                                FIntPoint BestCell = CurrentCell;
                                int32 BestDist = CurrentDist;

                                for (const FIntPoint& Offset : NeighborOffsets)
                                {
                                    const FIntPoint Candidate = CurrentCell + Offset;

                                    // Do not enter stationary cells
                                    if (StationaryCells.Contains(Candidate))
                                    {
                                        continue;
                                    }

                                    // Avoid cells occupied by others (unless it's us, though we are moving)
                                    if (GridOcc->IsCellOccupied(Candidate))
                                    {
                                        AActor* Occupant = GridOcc->GetActorAtCell(Candidate);
                                        if (Occupant && Occupant != ActorPtr)
                                        {
                                            continue;
                                        }
                                    }

                                    const int32 Dist = DF->GetDistance(Candidate);
                                    if (Dist >= 0 && Dist < BestDist)
                                    {
                                        BestDist = Dist;
                                        BestCell = Candidate;
                                    }
                                }

                                if (BestCell != CurrentCell)
                                {
                                    Action.NextCell        = BestCell;
                                    Action.bIsWait         = false;
                                    Action.AbilityTag      = MoveTag;
                                    Action.FinalAbilityTag = MoveTag;
                                    Action.ResolutionReason = FString::Printf(
                                        TEXT("Revalidated(Iter%d): rerouted move off blocked cell (%d,%d) to (%d,%d)"),
                                        IterationCount, TargetCell.X, TargetCell.Y, BestCell.X, BestCell.Y);

                                    bRerouted = true;
                                    bChanged = true; // State changed, need another pass

                                    UE_LOG(LogConflictResolver, Log,
                                        TEXT("[ResolveAllConflicts] Rerouted %s from blocked (%d,%d) to (%d,%d)"),
                                        *GetNameSafe(ActorPtr),
                                        TargetCell.X, TargetCell.Y,
                                        BestCell.X, BestCell.Y);
                                }
                            }
                        }
                    }

                    if (!bRerouted)
                    {
                        Action.bIsWait         = true;
                        Action.NextCell        = Action.CurrentCell;
                        Action.AbilityTag      = WaitTag;
                        Action.FinalAbilityTag = WaitTag;
                        Action.ResolutionReason = FString::Printf(
                            TEXT("LostConflict: Target cell (%d,%d) blocked by stationary unit (Revalidation Iter %d)"),
                            TargetCell.X, TargetCell.Y, IterationCount);
                        
                        bChanged = true; // State changed, need another pass
                    }
                }
            }
        }

    } while (bChanged && IterationCount < MaxIterations);

    if (IterationCount >= MaxIterations)
    {
        UE_LOG(LogConflictResolver, Error,
            TEXT("[ResolveAllConflicts] Hit max iterations (%d) in Phase C! Potential cycle or complex chain."),
            MaxIterations);
    }

    UE_LOG(LogConflictResolver, Log,
        TEXT("[ResolveAllConflicts] Generated %d final actions after %d iterations"),
        OptimisticActions.Num(), IterationCount);

    for (int32 i = 0; i < OptimisticActions.Num(); ++i)
    {
        const FResolvedAction& Action = OptimisticActions[i];
        UE_LOG(LogConflictResolver, Verbose,
            TEXT("[ResolvedAction %d] SourceActor=%s, Actor=%s, From=(%d,%d), To=(%d,%d), bIsWait=%d, Reason=\"%s\""),
            i,
            *GetNameSafe(Action.SourceActor),
            *GetNameSafe(Action.Actor.Get()),
            Action.CurrentCell.X, Action.CurrentCell.Y,
            Action.NextCell.X, Action.NextCell.Y,
            Action.bIsWait ? 1 : 0,
            *Action.ResolutionReason);
    }

    // Use OptimisticActions as the final result
    TArray<FResolvedAction>& FinalActions = OptimisticActions;

    // ========================================================================
    // CONTRACT (Priority 2): INVARIANT 1 - Intent Non-Disappearance
    //
    // The number of input reservations and output actions must match. This
    // guarantees that no intent silently disappears inside the resolver.
    // ========================================================================
    const int32 NumResolved = FinalActions.Num();

    checkf(
        NumReservations == NumResolved,
        TEXT("[ConflictResolver] CONTRACT VIOLATION: Input reservations (%d) != Output actions (%d). ")
        TEXT("This indicates intents were silently lost during conflict resolution!"),
        NumReservations, NumResolved
    );

    UE_LOG(LogConflictResolver, Log,
        TEXT("[ResolveAllConflicts] END: Contract validated (%d reservations = %d actions)"),
        NumReservations, NumResolved);

    return FinalActions;
}

// Stub implementation for this function, as it's in the header
int32 UConflictResolverSubsystem::GetActionTier(const FGameplayTag& AbilityTag) const
{
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Attack)) return 3;
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Move))   return 1; // Dash etc. would sit between if needed.
    if (AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Wait))   return 0;
    return 0;
}

// CodeRevision: INC-2025-1125-R1 (Cache TurnManager to scope sequential checks)
AGameTurnManagerBase* UConflictResolverSubsystem::ResolveTurnManager() const
{
    if (CachedTurnManager.IsValid())
    {
        return CachedTurnManager.Get();
    }

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AGameTurnManagerBase> It(World); It; ++It)
        {
            CachedTurnManager = *It;
            return CachedTurnManager.Get();
        }
    }

    return nullptr;
}

// Priority 2.1: Helper to resolve GridOccupancy subsystem (cached)
UGridOccupancySubsystem* UConflictResolverSubsystem::ResolveGridOccupancy() const
{
    if (CachedGridOccupancy.IsValid())
    {
        return CachedGridOccupancy.Get();
    }

    if (UWorld* World = GetWorld())
    {
        CachedGridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>();
        return CachedGridOccupancy.Get();
    }

    return nullptr;
}

// Priority 2.1: Helper to create a WAIT action from a reservation entry.
// ResolutionReason must be filled by the caller.
FResolvedAction UConflictResolverSubsystem::CreateWaitAction(const FReservationEntry& Entry)
{
    FResolvedAction WaitAction;
    WaitAction.Actor        = Entry.Actor;
    WaitAction.SourceActor  = Entry.Actor;
    WaitAction.bIsWait      = true;
    WaitAction.CurrentCell  = Entry.CurrentCell;
    WaitAction.NextCell     = Entry.CurrentCell;  // No movement
    WaitAction.AbilityTag   = RogueGameplayTags::AI_Intent_Wait;
    WaitAction.FinalAbilityTag = RogueGameplayTags::AI_Intent_Wait;

    // ResolutionReason is intentionally left empty here and must be set by caller.
    return WaitAction;
}
