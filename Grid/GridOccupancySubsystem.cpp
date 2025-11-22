// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// GridOccupancySubsystem.cpp

#include "GridOccupancySubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/GridUtils.h"

DEFINE_LOG_CATEGORY(LogGridOccupancy);

void UGridOccupancySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] Initialized"));
}

void UGridOccupancySubsystem::Deinitialize()
{
    ActorToCell.Empty();
    OccupiedCells.Empty();
    ReservedCells.Empty();
    ActorToReservation.Empty();
    UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] Deinitialized"));
    Super::Deinitialize();
}

FIntPoint UGridOccupancySubsystem::GetCellOfActor(AActor* Actor) const
{
    if (!Actor)
    {
        return FIntPoint::ZeroValue;
    }

    if (const FIntPoint* CellPtr = ActorToCell.Find(Actor))
    {
        return *CellPtr;
    }

    return FIntPoint::ZeroValue;
}

bool UGridOccupancySubsystem::WillLeaveThisTick(AActor* Actor) const
{
    if (!Actor)
    {
        return false;
    }

    // ★★★ CRITICAL FIX (2025-11-11): Use reservation + TurnId, do not depend on animation state ★★★
    // We only treat an actor as "leaving" if it has a valid reservation for this turn.

    const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor);
    if (!InfoPtr)
    {
        return false; // No reservation = not leaving
    }

    // Ignore stale reservations from previous turns
    if (InfoPtr->TurnId != CurrentTurnId)
    {
        return false;
    }

    // ★★★ BUGFIX [INC-2025-00002]: Removed hard dependency on animation-driven commit ★★★
    // We now detect "leaving" by comparing current cell vs reserved cell.
    // Old behavior relied on bCommitted (tied to animation completion), which caused race conditions
    // when followers completed animations before leaders and hit "REJECT UPDATE".

    const FIntPoint* CurrentCell = ActorToCell.Find(Actor);
    if (!CurrentCell)
    {
        return false; // Actor not in grid
    }

    return (*CurrentCell != InfoPtr->Cell); // Leaving if reserved cell differs from current
}

bool UGridOccupancySubsystem::IsPerfectSwap(AActor* A, AActor* B) const
{
    if (!A || !B)
    {
        return false;
    }

    const FIntPoint* ACurrent = ActorToCell.Find(A);
    const FIntPoint* BCurrent = ActorToCell.Find(B);
    if (!ACurrent || !BCurrent)
    {
        return false; // Both actors must be in grid
    }

    const FIntPoint AReserved = GetReservedCellForActor(A);
    const FIntPoint BReserved = GetReservedCellForActor(B);

    // Perfect swap: A moves to B's current cell AND B moves to A's current cell
    const bool bAtoB = (AReserved == *BCurrent);
    const bool bBtoA = (BReserved == *ACurrent);
    const bool bBothHaveValidReservations = (AReserved != FIntPoint(-1, -1) && BReserved != FIntPoint(-1, -1));

    return (bAtoB && bBtoA && bBothHaveValidReservations);
}

bool UGridOccupancySubsystem::UpdateActorCell(AActor* Actor, FIntPoint NewCell)
{
    if (!Actor)
    {
        return false;
    }

    // ★★★ CRITICAL FIX (2025-11-11): Respect other actors' reservations ★★★
    // If another actor has reserved this cell, we reject by default (except for a perfect swap).
    const bool bIHaveReservation = IsReservationOwnedByActor(Actor, NewCell);
    if (!bIHaveReservation)
    {
        if (AActor* ForeignReserver = GetReservationOwner(NewCell))
        {
            if (ForeignReserver != Actor)
            {
                // CodeRevision: INC-2025-1156-R1 (Disable Perfect Swap - Strict Reservation Enforcement) (2025-11-20 11:30)
                // We strictly reject moves into cells reserved by others. Swapping is not allowed.
                UE_LOG(LogGridOccupancy, Warning,
                    TEXT("[GridOccupancy] REJECT by foreign reservation: %s -> (%d,%d) reserved by %s"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ForeignReserver));
                return false;
            }
        }
    }

    // ★★★ CRITICAL FIX (2025-11-11): Two-phase commit - wait until the leaver has committed ★★★
    // If the target cell is currently occupied, we only allow the move when:
    // 1. This actor has reserved the cell AND
    // 2. The existing occupant is scheduled to leave this turn AND
    // 3. The existing occupant has already committed its move (two-phase commit).
    if (const TWeakObjectPtr<AActor>* ExistingActorPtr = OccupiedCells.Find(NewCell))
    {
        AActor* ExistingActor = ExistingActorPtr->Get();
        if (ExistingActor && ExistingActor != Actor)
        {
            const bool bMoverHasReservation = IsReservationOwnedByActor(Actor, NewCell);
            const bool bOccupantWillLeave = WillLeaveThisTick(ExistingActor);
            const bool bOccupantCommitted = HasCommittedThisTick(ExistingActor);

            // ★★★ Two-phase commit: follower must wait until owner commits ★★★
            if (bOccupantWillLeave && !bOccupantCommitted)
            {
                UE_LOG(LogGridOccupancy, Verbose,
                    TEXT("[GridOccupancy] REJECT (follower before owner commit): %s -> (%d,%d), occupant=%s will leave but not committed yet"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ExistingActor));
                return false; // Follower arrived before leader's commit; reject for now
            }

            // Even if mover has a reservation, we do not allow overwriting if the occupant does not leave.
            // This prevents "crushing" a waiting unit.
            if (!(bMoverHasReservation && bOccupantWillLeave))
            {
                UE_LOG(LogGridOccupancy, Error,
                    TEXT("[GridOccupancy] REJECT UPDATE: %s cannot move to (%d,%d) - occupied by %s (moverReserved=%d, occupantLeaves=%d)"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ExistingActor),
                    bMoverHasReservation ? 1 : 0, bOccupantWillLeave ? 1 : 0);
                return false;
            }
            else
            {
                UE_LOG(LogGridOccupancy, Log,
                    TEXT("[GridOccupancy] ACCEPT (reserved + occupant committed): %s -> (%d,%d) - was occupied by %s (left to %s)"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ExistingActor),
                    *GetReservedCellForActor(ExistingActor).ToString());
            }
        }
    }

    // ---- Commit Phase (Success) ----
    ReleaseReservationForActor(Actor);

    if (const FIntPoint* OldCellPtr = ActorToCell.Find(Actor))
    {
        OccupiedCells.Remove(*OldCellPtr);
    }

    ActorToCell.Add(Actor, NewCell);
    OccupiedCells.Add(NewCell, Actor);

    // ★★★ Two-phase commit: mark the move as committed so followers can proceed ★★★
    CommittedThisTick.Add(Actor);

    UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] COMMIT: %s -> (%d,%d)"),
        *GetNameSafe(Actor), NewCell.X, NewCell.Y);

    return true;
}

bool UGridOccupancySubsystem::IsCellOccupied(const FIntPoint& Cell) const
{
    if (const TWeakObjectPtr<AActor>* OccupierPtr = OccupiedCells.Find(Cell))
    {
        if (OccupierPtr->IsValid())
        {
            return true;
        }
    }

    // ★★★ CRITICAL FIX (2025-11-11): Check FReservationInfo as well ★★★
    if (const FReservationInfo* InfoPtr = ReservedCells.Find(Cell))
    {
        if (InfoPtr->Owner.IsValid())
        {
            return true;
        }
    }

    return false;
}

AActor* UGridOccupancySubsystem::GetActorAtCell(const FIntPoint& Cell) const
{
    if (const TWeakObjectPtr<AActor>* OccupierPtr = OccupiedCells.Find(Cell))
    {
        return OccupierPtr->Get();
    }

    return nullptr;
}

void UGridOccupancySubsystem::OccupyCell(const FIntPoint& Cell, AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    OccupiedCells.Add(Cell, Actor);
    ActorToCell.Add(Actor, Cell);

    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) occupied by %s"),
        Cell.X, Cell.Y, *GetNameSafe(Actor));
}

void UGridOccupancySubsystem::ReleaseCell(const FIntPoint& Cell)
{
    OccupiedCells.Remove(Cell);
    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) released"),
        Cell.X, Cell.Y);
}

void UGridOccupancySubsystem::UnregisterActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    if (const FIntPoint* CellPtr = ActorToCell.Find(Actor))
    {
        OccupiedCells.Remove(*CellPtr);
    }
    ActorToCell.Remove(Actor);
    ReleaseReservationForActor(Actor);

    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] Actor %s unregistered"),
        *GetNameSafe(Actor));
}

bool UGridOccupancySubsystem::ReserveCellForActor(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        return false;  // Null Actor is always failure
    }

    // ★★★ CRITICAL FIX (2025-11-11): Protect existing reservations (OriginHold support) ★★★
    // If the cell is already reserved by another actor, do not overwrite it by default.
    if (const FReservationInfo* ExistingInfo = ReservedCells.Find(Cell))
    {
        if (ExistingInfo->Owner.IsValid() && ExistingInfo->Owner.Get() != Actor)
        {
            // For OriginHold, the owner is in the process of leaving this cell.
            // We treat this as a hard block to prevent "backstabbing" (someone stepping into the origin too early).
            if (ExistingInfo->bIsOriginHold)
            {
                // ★★★ FIX (2025-11-11): Allow follow-up compression pattern (SoftHold) ★★★
                // Pattern: A moves from Origin to Destination, and B moves into A's Origin (follow behavior).
                AActor* OriginOwner = ExistingInfo->Owner.Get();
                const FReservationInfo* OriginOwnerDestReservation = ActorToReservation.Find(OriginOwner);

                // Follow-up pattern conditions:
                // 1. OriginOwner has a destination reservation.
                // 2. Destination differs from the current cell (they are actually moving).
                // 3. Destination reservation is a normal reservation (not OriginHold).
                if (OriginOwnerDestReservation &&
                    OriginOwnerDestReservation->Cell != Cell &&
                    !OriginOwnerDestReservation->bIsOriginHold)
                {
                    // CodeRevision: INC-2025-1157-R3 (フォローアップ制限: プレイヤー経路追尾可、距離1のみ、チーム不一致のみ拒否) (2025-11-20 21:00)
                    // プレイヤーの元セル追随を許可しつつ、直近(チェビシェフ距離1)のユニットのみ追従を許可する。
                    // これで遠距離の追従による重なりを防ぎ、隊列を1マス刻みで維持する。
                    const IGenericTeamAgentInterface* OriginTeamAgent   = Cast<IGenericTeamAgentInterface>(OriginOwner);
                    const IGenericTeamAgentInterface* FollowerTeamAgent = Cast<IGenericTeamAgentInterface>(Actor);

                    // CodeRevision: INC-2025-1157-R4 (Allow cross-team follow-up for chasing) (2025-11-21 01:20)
                    // Removed team mismatch check to allow enemies to chase the player into their old cell.
                    /*
                    if (OriginTeamAgent && FollowerTeamAgent)
                    {
                        if (OriginTeamAgent->GetGenericTeamId() != FollowerTeamAgent->GetGenericTeamId())
                        {
                            UE_LOG(LogGridOccupancy, Warning,
                                TEXT("[GridOccupancy] REJECT FOLLOW-UP: %s -> (%d,%d) team mismatch with origin owner %s"),
                                *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(OriginOwner));
                            return false;
                        }
                    }
                    */

                    // 直近(距離1)のみフォローを許可
                    const FIntPoint* FollowerCellPtr = ActorToCell.Find(Actor);
                    if (FollowerCellPtr)
                    {
                        const int32 ChebDist = FGridUtils::ChebyshevDistance(*FollowerCellPtr, Cell);
                        if (ChebDist > 1)
                        {
                            UE_LOG(LogGridOccupancy, Warning,
                                TEXT("[GridOccupancy] REJECT FOLLOW-UP: %s -> (%d,%d) distance=%d (must be <=1)"),
                                *GetNameSafe(Actor), Cell.X, Cell.Y, ChebDist);
                            return false;
                        }
                    }

                    // Follow-up compression: allow this as a "SoftHold" override.
                    UE_LOG(LogGridOccupancy, Log,
                        TEXT("[GridOccupancy] ALLOW FOLLOW-UP: %s -> (%d,%d) following %s who moves to (%d,%d) [SOFTHOLD -> ALLOW]"),
                        *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(OriginOwner),
                        OriginOwnerDestReservation->Cell.X, OriginOwnerDestReservation->Cell.Y);
                    // Continue and allow reservation (overwriting the OriginHold)
                }
                else
                {
                    // Hard block remains: no valid follow-up pattern, keep OriginHold.
                    UE_LOG(LogGridOccupancy, Error,
                        TEXT("[GridOccupancy] REJECT RESERVATION: %s cannot reserve (%d,%d) - OriginHold by %s (TurnId=%d) [HARDHOLD -> BLOCKED]"),
                        *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(OriginOwner), ExistingInfo->TurnId);
                    return false;  // Block reservation to prevent backstab
                }
            }
            else
            {
                UE_LOG(LogGridOccupancy, Error,
                    TEXT("[GridOccupancy] REJECT RESERVATION: %s cannot reserve (%d,%d) - already reserved by %s (TurnId=%d)"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(ExistingInfo->Owner.Get()), ExistingInfo->TurnId);
                return false;  // Reservation denied - another actor already reserved this cell
            }
        }
    }

    // ★★★ OriginHold implementation: capture current origin cell ★★★
    const FIntPoint* CurrentCellPtr = ActorToCell.Find(Actor);
    const FIntPoint CurrentCell = CurrentCellPtr ? *CurrentCellPtr : FIntPoint(-1, -1);

    // ★★★ CRITICAL FIX (2025-11-11): Use FReservationInfo + OriginHold ★★★
    // Clear all existing reservations (destination + OriginHold) for this actor.
    ReleaseReservationForActor(Actor);

    // Create normal destination reservation
    FReservationInfo DestInfo(Actor, Cell, CurrentTurnId, false);
    ReservedCells.Add(Cell, DestInfo);
    ActorToReservation.Add(Actor, DestInfo);

    UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] RESERVE DEST: %s -> (%d, %d) (TurnId=%d)"),
        *GetNameSafe(Actor), Cell.X, Cell.Y, CurrentTurnId);

    // ★★★ OriginHold implementation: place an OriginHold reservation on the origin cell ★★★
    // ★★★ OriginHold implementation: place an OriginHold reservation on the origin cell ★★★
    if (CurrentCell != FIntPoint(-1, -1) && CurrentCell != Cell)
    {
        // CodeRevision: INC-2025-1157-R5 (Fix OriginHold Overwrite) (2025-11-21 01:30)
        // Only apply OriginHold if the cell is NOT already reserved by another actor.
        // If another actor (e.g., a follower) has already reserved this cell, we respect their reservation
        // and do NOT overwrite it with our OriginHold. The Two-Phase Commit in UpdateActorCell will handle physical safety.
        bool bCanApplyOriginHold = true;
        if (const FReservationInfo* ExistingInfo = ReservedCells.Find(CurrentCell))
        {
            if (ExistingInfo->Owner.IsValid() && ExistingInfo->Owner.Get() != Actor)
            {
                UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] SKIP ORIGIN-HOLD: %s skipping origin (%d, %d) - already reserved by %s"),
                    *GetNameSafe(Actor), CurrentCell.X, CurrentCell.Y, *GetNameSafe(ExistingInfo->Owner.Get()));
                bCanApplyOriginHold = false;
            }
        }

        if (bCanApplyOriginHold)
        {
            // Reserve origin cell with OriginHold to prevent other actors from entering prematurely.
            FReservationInfo OriginHoldInfo(Actor, CurrentCell, CurrentTurnId, true);
            ReservedCells.Add(CurrentCell, OriginHoldInfo);

            UE_LOG(LogGridOccupancy, Log, TEXT("[GridOccupancy] RESERVE ORIGIN-HOLD: %s protects origin (%d, %d) (TurnId=%d) [BACKSTAB PROTECTION]"),
                *GetNameSafe(Actor), CurrentCell.X, CurrentCell.Y, CurrentTurnId);
        }
    }

    return true;  // Reservation succeeded
}

void UGridOccupancySubsystem::ReleaseReservationForActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // ★★★ CRITICAL FIX (2025-11-11): OriginHold support - clear all reservations owned by this actor ★★★
    // Clear destination reservation.
    if (const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        ReservedCells.Remove(InfoPtr->Cell);
        UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] Reservation cleared for %s (destination: %d,%d)"),
            *GetNameSafe(Actor), InfoPtr->Cell.X, InfoPtr->Cell.Y);
    }
    ActorToReservation.Remove(Actor);

    // Clear OriginHold reservations owned by this actor.
    for (auto It = ReservedCells.CreateIterator(); It; ++It)
    {
        const FReservationInfo& Info = It.Value();
        if (Info.bIsOriginHold && Info.Owner.IsValid() && Info.Owner.Get() == Actor)
        {
            UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] OriginHold cleared for %s (origin: %d,%d)"),
                *GetNameSafe(Actor), It.Key().X, It.Key().Y);
            It.RemoveCurrent();
        }
    }
}

void UGridOccupancySubsystem::ClearAllReservations()
{
    ReservedCells.Reset();
    ActorToReservation.Reset();
    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] All reservations cleared"));
}

bool UGridOccupancySubsystem::IsCellReserved(const FIntPoint& Cell) const
{
    // ★★★ CRITICAL FIX (2025-11-11): Use FReservationInfo ★★★
    if (const FReservationInfo* InfoPtr = ReservedCells.Find(Cell))
    {
        return InfoPtr->Owner.IsValid();
    }

    return false;
}

AActor* UGridOccupancySubsystem::GetReservationOwner(const FIntPoint& Cell) const
{
    // ★★★ CRITICAL FIX (2025-11-11): Use FReservationInfo ★★★
    if (const FReservationInfo* InfoPtr = ReservedCells.Find(Cell))
    {
        return InfoPtr->Owner.Get();
    }

    return nullptr;
}

bool UGridOccupancySubsystem::IsReservationOwnedByActor(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor)
    {
        return false;
    }

    // ★★★ CRITICAL FIX (2025-11-11): Use FReservationInfo ★★★
    if (const FReservationInfo* InfoPtr = ReservedCells.Find(Cell))
    {
        if (InfoPtr->Owner.IsValid())
        {
            return InfoPtr->Owner.Get() == Actor;
        }
    }

    return false;
}

FIntPoint UGridOccupancySubsystem::GetReservedCellForActor(AActor* Actor) const
{
    if (!Actor)
    {
        return FIntPoint(-1, -1);
    }

    // ★★★ CRITICAL FIX (2025-11-11): Get Cell from FReservationInfo ★★★
    if (const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        return InfoPtr->Cell;
    }

    return FIntPoint(-1, -1);
}

bool UGridOccupancySubsystem::TryReserveCell(AActor* Actor, const FIntPoint& Cell, int32 TurnId)
{
    if (!Actor)
    {
        return false;
    }

    // Check if cell is already reserved by another actor
    if (AActor* Owner = GetReservationOwner(Cell))
    {
        if (Owner != Actor)
        {
            UE_LOG(LogGridOccupancy, Warning,
                TEXT("[Reserve] FAIL: (%d,%d) already reserved by %s (attempt by %s)"),
                Cell.X, Cell.Y, *GetNameSafe(Owner), *GetNameSafe(Actor));
            return false; // Exclusive reservation - first come first served
        }
        // Already reserved by this actor - OK
        UE_LOG(LogGridOccupancy, Verbose,
            TEXT("[Reserve] ALREADY OWNED: %s -> (%d,%d)"),
            *GetNameSafe(Actor), Cell.X, Cell.Y);
        return true;
    }

    // Reserve the cell
    ReserveCellForActor(Actor, Cell);
    UE_LOG(LogGridOccupancy, Log,
        TEXT("[Reserve] OK: %s -> (%d,%d) TurnId=%d"),
        *GetNameSafe(Actor), Cell.X, Cell.Y, TurnId);
    return true;
}

void UGridOccupancySubsystem::BeginMovePhase()
{
    CommittedThisTick.Reset();
    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] BeginMovePhase - committed set cleared"));
}

void UGridOccupancySubsystem::EndMovePhase()
{
    CommittedThisTick.Reset();
    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] EndMovePhase - committed set cleared"));
}

bool UGridOccupancySubsystem::HasCommittedThisTick(AActor* Actor) const
{
    return CommittedThisTick.Contains(Actor);
}

// ★★★ CRITICAL FIX (2025-11-11): New method implementation ★★★

void UGridOccupancySubsystem::MarkReservationCommitted(AActor* Actor, int32 TurnId)
{
    if (!Actor)
    {
        return;
    }

    // Look up reservation and mark bCommitted = true
    if (FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        if (InfoPtr->TurnId == TurnId)
        {
            InfoPtr->bCommitted = true;

            // Mirror the committed flag in ReservedCells as well
            if (FReservationInfo* CellInfoPtr = ReservedCells.Find(InfoPtr->Cell))
            {
                CellInfoPtr->bCommitted = true;
            }

            UE_LOG(LogGridOccupancy, Log,
                TEXT("[GridOccupancy] MarkCommitted: %s -> (%d,%d) TurnId=%d"),
                *GetNameSafe(Actor), InfoPtr->Cell.X, InfoPtr->Cell.Y, TurnId);
        }
    }
}

void UGridOccupancySubsystem::PurgeOutdatedReservations(int32 InCurrentTurnId)
{
    int32 PurgedCount = 0;

    // Remove stale reservations from ReservedCells
    for (auto It = ReservedCells.CreateIterator(); It; ++It)
    {
        if (It.Value().TurnId != InCurrentTurnId)
        {
            UE_LOG(LogGridOccupancy, Verbose,
                TEXT("[GridOccupancy] Purging outdated reservation: Cell=(%d,%d) TurnId=%d (Current=%d)"),
                It.Key().X, It.Key().Y, It.Value().TurnId, InCurrentTurnId);
            It.RemoveCurrent();
            PurgedCount++;
        }
    }

    // Remove stale entries from ActorToReservation
    for (auto It = ActorToReservation.CreateIterator(); It; ++It)
    {
        if (It.Value().TurnId != InCurrentTurnId)
        {
            It.RemoveCurrent();
        }
    }

    if (PurgedCount > 0)
    {
        UE_LOG(LogGridOccupancy, Log,
            TEXT("[GridOccupancy] PurgeOutdatedReservations: Removed %d old reservations (CurrentTurnId=%d)"),
            PurgedCount, InCurrentTurnId);
    }
}

void UGridOccupancySubsystem::SetCurrentTurnId(int32 TurnId)
{
    CurrentTurnId = TurnId;
    UE_LOG(LogGridOccupancy, Verbose, TEXT("[GridOccupancy] SetCurrentTurnId: %d"), CurrentTurnId);
}

// ★★★ CRITICAL FIX (2025-11-11): Consistency check - detect and fix overlapping occupancy ★★★

void UGridOccupancySubsystem::EnforceUniqueOccupancy()
{
    // Step 1: Build Cell -> Actors[] map from ActorToCell
    TMap<FIntPoint, TArray<TWeakObjectPtr<AActor>>> CellToActors;
    for (const auto& Pair : ActorToCell)
    {
        AActor* Actor = Pair.Key;
        const FIntPoint Cell = Pair.Value;

        if (Actor && Actor->IsValidLowLevel())
        {
            CellToActors.FindOrAdd(Cell).Add(Actor);
        }
    }

    // Step 2: Detect overlaps (multiple actors on the same cell)
    int32 FixedCount = 0;
    for (const auto& Pair : CellToActors)
    {
        const FIntPoint Cell = Pair.Key;
        const TArray<TWeakObjectPtr<AActor>>& Actors = Pair.Value;

        if (Actors.Num() <= 1)
        {
            continue;  // Normal: 0 or 1 actor per cell
        }

        // ★★★ Overlap detected ★★★
        UE_LOG(LogGridOccupancy, Error,
            TEXT("[GridOccupancy] OVERLAP DETECTED at (%d,%d): %d actors stacked!"),
            Cell.X, Cell.Y, Actors.Num());

        // Step 3: Choose keeper (the actor that stays in this cell)
        AActor* Keeper = ChooseKeeperActor(Cell, Actors);
        if (!Keeper)
        {
            UE_LOG(LogGridOccupancy, Error,
                TEXT("[GridOccupancy] Failed to choose keeper for (%d,%d) - skipping"),
                Cell.X, Cell.Y);
            continue;
        }

        UE_LOG(LogGridOccupancy, Warning,
            TEXT("[GridOccupancy] Keeper for (%d,%d): %s"),
            Cell.X, Cell.Y, *GetNameSafe(Keeper));

        // Step 4: Relocate all other actors to nearby free cells
        for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
        {
            AActor* Actor = ActorPtr.Get();
            if (!Actor || Actor == Keeper)
            {
                continue;
            }

            // Find the nearest free cell
            const FIntPoint SafeCell = FindNearestFreeCell(Cell, 10);
            if (SafeCell == FIntPoint(-1, -1))
            {
                UE_LOG(LogGridOccupancy, Error,
                    TEXT("[GridOccupancy] No free cell found for %s - keeping stacked (WARN)"),
                    *GetNameSafe(Actor));
                continue;  // No safe cell; leave overlapping (logical best effort)
            }

            // Force relocation
            ForceRelocate(Actor, SafeCell);
            FixedCount++;

            UE_LOG(LogGridOccupancy, Warning,
                TEXT("[GridOccupancy] [OCC FIX] Split stack: keep=%s at (%d,%d) | move %s -> (%d,%d)"),
                *GetNameSafe(Keeper), Cell.X, Cell.Y, *GetNameSafe(Actor), SafeCell.X, SafeCell.Y);
        }
    }

    if (FixedCount > 0)
    {
        UE_LOG(LogGridOccupancy, Warning,
            TEXT("[GridOccupancy] EnforceUniqueOccupancy: Fixed %d overlapping actors"),
            FixedCount);
    }
}

FIntPoint UGridOccupancySubsystem::FindNearestFreeCell(const FIntPoint& Origin, int32 MaxSearchRadius) const
{
    // BFS search for nearest free cell
    TQueue<FIntPoint> Queue;
    TSet<FIntPoint> Visited;

    Queue.Enqueue(Origin);
    Visited.Add(Origin);

    // 8 directions (orthogonal + diagonal)
    static const TArray<FIntPoint> Directions = {
        FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
        FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)
    };

    while (!Queue.IsEmpty())
    {
        FIntPoint Current;
        Queue.Dequeue(Current);

        // Distance check (Manhattan)
        const int32 Distance = FMath::Abs(Current.X - Origin.X) + FMath::Abs(Current.Y - Origin.Y);
        if (Distance > MaxSearchRadius)
        {
            continue;
        }

        // Skip origin itself
        if (Current != Origin)
        {
            // Check that cell is neither occupied nor reserved
            const bool bOccupied = OccupiedCells.Contains(Current);
            const bool bReserved = ReservedCells.Contains(Current);

            if (!bOccupied && !bReserved)
            {
                // Free cell found
                return Current;
            }
        }

        // Enqueue neighbors
        for (const FIntPoint& Dir : Directions)
        {
            const FIntPoint Next = Current + Dir;
            if (!Visited.Contains(Next))
            {
                Visited.Add(Next);
                Queue.Enqueue(Next);
            }
        }
    }

    // Nothing found within radius
    return FIntPoint(-1, -1);
}

void UGridOccupancySubsystem::ForceRelocate(AActor* Actor, const FIntPoint& NewCell)
{
    if (!Actor)
    {
        return;
    }

    // Remove from old cell
    if (const FIntPoint* OldCellPtr = ActorToCell.Find(Actor))
    {
        OccupiedCells.Remove(*OldCellPtr);
        UE_LOG(LogGridOccupancy, Verbose,
            TEXT("[GridOccupancy] ForceRelocate: %s released old cell (%d,%d)"),
            *GetNameSafe(Actor), OldCellPtr->X, OldCellPtr->Y);
    }

    // Register at new cell
    ActorToCell.Add(Actor, NewCell);
    OccupiedCells.Add(NewCell, Actor);

    // ★★★ Sync actor world position as well (simple grid->world mapping) ★★★
    // NOTE: Ideally this should be done via GridPathfindingSubsystem::GridToWorld().
    // Here we use a simple fixed cell size as a fallback; adjust to your project.
    const float CellSize = 100.0f;
    const FVector NewWorldLocation(
        NewCell.X * CellSize,
        NewCell.Y * CellSize,
        Actor->GetActorLocation().Z  // Preserve Z
    );
    Actor->SetActorLocation(NewWorldLocation, false, nullptr, ETeleportType::TeleportPhysics);

    UE_LOG(LogGridOccupancy, Log,
        TEXT("[GridOccupancy] ForceRelocate: %s -> (%d,%d) at World=(%f,%f,%f)"),
        *GetNameSafe(Actor), NewCell.X, NewCell.Y,
        NewWorldLocation.X, NewWorldLocation.Y, NewWorldLocation.Z);
}

AActor* UGridOccupancySubsystem::ChooseKeeperActor(const FIntPoint& Cell, const TArray<TWeakObjectPtr<AActor>>& Actors) const
{
    if (Actors.Num() == 0)
    {
        return nullptr;
    }

    // Priority 1: Prefer the actor that owns an OriginHold on this cell
    // (the actor protecting this origin cell has the strongest claim to stay).
    for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (!Actor)
        {
            continue;
        }

        // Check for OriginHold reservation on this cell
        if (const FReservationInfo* Info = ReservedCells.Find(Cell))
        {
            if (Info->bIsOriginHold && Info->Owner.Get() == Actor)
            {
                UE_LOG(LogGridOccupancy, Verbose,
                    TEXT("[GridOccupancy] ChooseKeeper: %s has OriginHold at (%d,%d)"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y);
                return Actor;
            }
        }
    }

    // Priority 2: Fallback to the first valid actor (TODO: extend with team priority, etc.)
    for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (Actor)
        {
            UE_LOG(LogGridOccupancy, Verbose,
                TEXT("[GridOccupancy] ChooseKeeper: %s (first valid)"),
                *GetNameSafe(Actor));
            return Actor;
        }
    }

    return nullptr;
}

// ★★★ CRITICAL FIX (2025-11-11): Rebuild occupancy map from physical positions (based on diagnostics) ★★★

void UGridOccupancySubsystem::RebuildFromWorldPositions(const TArray<AActor*>& AllUnits)
{
    UE_LOG(LogGridOccupancy, Warning,
        TEXT("[GridOccupancy] RebuildFromWorldPositions: Rebuilding occupancy map from physical positions for %d units"),
        AllUnits.Num());

    // Acquire PathFinder (needed for Grid <-> World conversion)
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogGridOccupancy, Error, TEXT("[GridOccupancy] RebuildFromWorldPositions: No World!"));
        return;
    }

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // Get UGridPathfindingSubsystem
    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

    if (!PathFinder)
    {
        UE_LOG(LogGridOccupancy, Error, TEXT("[GridOccupancy] RebuildFromWorldPositions: No PathFinder found!"));
        return;
    }

    // ★★★ Clear current logical occupancy state completely ★★★
    ActorToCell.Empty();
    OccupiedCells.Empty();

    // ★★★ Build Cell -> Actors[] map from physical positions ★★★
    TMap<FIntPoint, TArray<AActor*>> CellToActors;
    for (AActor* Unit : AllUnits)
    {
        if (!Unit || !Unit->IsValidLowLevel())
        {
            continue;
        }

        const FVector WorldPos = Unit->GetActorLocation();
        const FIntPoint CurrentCell = PathFinder->WorldToGrid(WorldPos);

        CellToActors.FindOrAdd(CurrentCell).Add(Unit);

        UE_LOG(LogGridOccupancy, Verbose,
            TEXT("[GridOccupancy] RebuildFromWorldPositions: %s at World=(%s) -> Cell=(%d,%d)"),
            *GetNameSafe(Unit), *WorldPos.ToCompactString(), CurrentCell.X, CurrentCell.Y);
    }

    // ★★★ Re-register occupancy per cell; resolve overlaps if any ★★★
    int32 OverlapCount = 0;
    int32 RelocatedCount = 0;

    for (const auto& Pair : CellToActors)
    {
        const FIntPoint Cell = Pair.Key;
        const TArray<AActor*>& Actors = Pair.Value;

        if (Actors.Num() == 1)
        {
            // Normal: one actor per cell
            AActor* Actor = Actors[0];
            ActorToCell.Add(Actor, Cell);
            OccupiedCells.Add(Cell, Actor);
            UE_LOG(LogGridOccupancy, Verbose,
                TEXT("[GridOccupancy] RebuildFromWorldPositions: Registered %s at (%d,%d)"),
                *GetNameSafe(Actor), Cell.X, Cell.Y);
        }
        else if (Actors.Num() > 1)
        {
            // ★★★ Physical overlap detected ★★★
            OverlapCount++;
            UE_LOG(LogGridOccupancy, Error,
                TEXT("[GridOccupancy] RebuildFromWorldPositions: PHYSICAL OVERLAP at (%d,%d) with %d actors!"),
                Cell.X, Cell.Y, Actors.Num());

            // Choose keeper (first actor in the list for now)
            AActor* Keeper = Actors[0];
            ActorToCell.Add(Keeper, Cell);
            OccupiedCells.Add(Cell, Keeper);

            UE_LOG(LogGridOccupancy, Warning,
                TEXT("[GridOccupancy] RebuildFromWorldPositions: Keeper=%s at (%d,%d)"),
                *GetNameSafe(Keeper), Cell.X, Cell.Y);

            // Relocate the remaining actors to nearest free cells
            for (int32 i = 1; i < Actors.Num(); ++i)
            {
                AActor* Evictee = Actors[i];
                const FIntPoint FreeCell = FindNearestFreeCell(Cell, 10);

                if (FreeCell != FIntPoint(-1, -1))
                {
                    // Free cell found -> teleport there
                    const FVector NewWorldPos = PathFinder->GridToWorld(FreeCell, Evictee->GetActorLocation().Z);
                    Evictee->SetActorLocation(NewWorldPos, false, nullptr, ETeleportType::TeleportPhysics);

                    // Register in occupancy map
                    ActorToCell.Add(Evictee, FreeCell);
                    OccupiedCells.Add(FreeCell, Evictee);
                    RelocatedCount++;

                    UE_LOG(LogGridOccupancy, Warning,
                        TEXT("[GridOccupancy] RebuildFromWorldPositions: RELOCATED %s from (%d,%d) -> (%d,%d)"),
                        *GetNameSafe(Evictee), Cell.X, Cell.Y, FreeCell.X, FreeCell.Y);
                }
                else
                {
                    // No free cell: keep overlapped physically but do not register logically.
                    UE_LOG(LogGridOccupancy, Error,
                        TEXT("[GridOccupancy] RebuildFromWorldPositions: NO FREE CELL for %s - remains overlapped at (%d,%d)!"),
                        *GetNameSafe(Evictee), Cell.X, Cell.Y);
                    // Not registering in occupancy map = treated as "not present" logically.
                }
            }
        }
    }

    if (OverlapCount > 0)
    {
        UE_LOG(LogGridOccupancy, Error,
            TEXT("[GridOccupancy] RebuildFromWorldPositions: Fixed %d physical overlaps, relocated %d actors"),
            OverlapCount, RelocatedCount);
    }
    else
    {
        UE_LOG(LogGridOccupancy, Log,
            TEXT("[GridOccupancy] RebuildFromWorldPositions: No physical overlaps detected - occupancy map rebuilt successfully"));
    }
}

// Priority 2.2: Return all occupied cells (for static blocker detection)
TMap<FIntPoint, AActor*> UGridOccupancySubsystem::GetAllOccupiedCells() const
{
    TMap<FIntPoint, AActor*> Result;
    for (const auto& Pair : OccupiedCells)
    {
        const FIntPoint Cell = Pair.Key;
        const TWeakObjectPtr<AActor> ActorPtr = Pair.Value;

        if (ActorPtr.IsValid())
        {
            Result.Add(Cell, ActorPtr.Get());
        }
    }
    return Result;
}
