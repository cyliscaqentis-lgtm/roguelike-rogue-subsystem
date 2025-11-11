#include "GridOccupancySubsystem.h"

void UGridOccupancySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] Initialized"));
}

void UGridOccupancySubsystem::Deinitialize()
{
    ActorToCell.Empty();
    OccupiedCells.Empty();
    ReservedCells.Empty();
    ActorToReservation.Empty();
    UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] Deinitialized"));
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

    // ★★★ CRITICAL FIX (2025-11-11): bCommitted ベースに変更 ★★★
    // 予約があるだけでは不十分。ConflictResolver で勝者と決まり、bCommitted=true になった場合のみ退去扱い
    const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor);
    if (!InfoPtr)
    {
        return false; // No reservation = not leaving
    }

    // TurnId チェック：古い予約は無視
    if (InfoPtr->TurnId != CurrentTurnId)
    {
        return false;
    }

    // bCommitted チェック：確定した予約のみ退去扱い
    if (!InfoPtr->bCommitted)
    {
        return false;
    }

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

    // Perfect swap: A->B's current AND B->A's current
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

    // ★★★ CRITICAL FIX (2025-11-11): 他者予約の尊重 ★★★
    // セルが他のActorに予約されている場合、原則拒否（完全スワップは例外）
    const bool bIHaveReservation = IsReservationOwnedByActor(Actor, NewCell);
    if (!bIHaveReservation)
    {
        if (AActor* ForeignReserver = GetReservationOwner(NewCell))
        {
            if (ForeignReserver != Actor)
            {
                // 完全スワップ（A<->B）の場合のみ許可
                const bool bIsPerfectSwap = IsPerfectSwap(Actor, ForeignReserver);
                if (!bIsPerfectSwap)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[GridOccupancy] REJECT by foreign reservation: %s -> (%d,%d) reserved by %s (not a perfect swap)"),
                        *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ForeignReserver));
                    return false;
                }
                else
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[GridOccupancy] ALLOW perfect swap: %s <-> %s at (%d,%d)"),
                        *GetNameSafe(Actor), *GetNameSafe(ForeignReserver), NewCell.X, NewCell.Y);
                }
            }
        }
    }

    // ★★★ CRITICAL FIX (2025-11-11): 二相コミット - 退去元が確定するまで待つ ★★★
    // 新しいセルが他の Actor で占有されている場合、以下の条件で許可：
    // 1. このActorがセルを予約している AND
    // 2. 既占有者が今ターンでそのセルを離れる予定がある AND
    // 3. 既占有者が実際に移動をコミット済み（二相コミット）
    if (const TWeakObjectPtr<AActor>* ExistingActorPtr = OccupiedCells.Find(NewCell))
    {
        AActor* ExistingActor = ExistingActorPtr->Get();
        if (ExistingActor && ExistingActor != Actor)
        {
            const bool bMoverHasReservation = IsReservationOwnedByActor(Actor, NewCell);
            const bool bOccupantWillLeave = WillLeaveThisTick(ExistingActor);
            const bool bOccupantCommitted = HasCommittedThisTick(ExistingActor);

            // ★★★ 二相コミット: 退去予定があっても、まだコミットしていないなら拒否 ★★★
            if (bOccupantWillLeave && !bOccupantCommitted)
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("[GridOccupancy] REJECT (follower before owner commit): %s -> (%d,%d), occupant=%s will leave but not committed yet"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ExistingActor));
                return false; // 先に来たフォロワーは一旦ロールバック
            }

            // 予約があっても、既占有者が退去しないなら上書き禁止（待機者の踏み潰し防止）
            if (!(bMoverHasReservation && bOccupantWillLeave))
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[GridOccupancy] REJECT UPDATE: %s cannot move to (%d,%d) - occupied by %s (moverReserved=%d, occupantLeaves=%d)"),
                    *GetNameSafe(Actor), NewCell.X, NewCell.Y, *GetNameSafe(ExistingActor),
                    bMoverHasReservation ? 1 : 0, bOccupantWillLeave ? 1 : 0);
                return false;  // 条件不満：書き込み拒否
            }
            else
            {
                UE_LOG(LogTemp, Log,
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

    // ★★★ 二相コミット: 移動を確定としてマーク → 後続のフォロワーを許可 ★★★
    CommittedThisTick.Add(Actor);

    UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] COMMIT: %s -> (%d,%d)"),
        *GetNameSafe(Actor), NewCell.X, NewCell.Y);

    return true;  // 成功
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

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo を使用 ★★★
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

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) occupied by %s"),
        Cell.X, Cell.Y, *GetNameSafe(Actor));
}

void UGridOccupancySubsystem::ReleaseCell(const FIntPoint& Cell)
{
    OccupiedCells.Remove(Cell);
    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) released"),
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

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Actor %s unregistered"),
        *GetNameSafe(Actor));
}

void UGridOccupancySubsystem::ReserveCellForActor(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        return;
    }

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo 形式に変更 ★★★
    ReleaseReservationForActor(Actor);
    FReservationInfo Info(Actor, Cell, CurrentTurnId);
    ReservedCells.Add(Cell, Info);
    ActorToReservation.Add(Actor, Info);

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) reserved by %s (TurnId=%d)"),
        Cell.X, Cell.Y, *GetNameSafe(Actor), CurrentTurnId);
}

void UGridOccupancySubsystem::ReleaseReservationForActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo から Cell を取得 ★★★
    if (const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        ReservedCells.Remove(InfoPtr->Cell);
        ActorToReservation.Remove(Actor);
        UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Reservation cleared for %s"),
            *GetNameSafe(Actor));
    }
}

void UGridOccupancySubsystem::ClearAllReservations()
{
    ReservedCells.Reset();
    ActorToReservation.Reset();
    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] All reservations cleared"));
}

bool UGridOccupancySubsystem::IsCellReserved(const FIntPoint& Cell) const
{
    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo を使用 ★★★
    if (const FReservationInfo* InfoPtr = ReservedCells.Find(Cell))
    {
        return InfoPtr->Owner.IsValid();
    }

    return false;
}

AActor* UGridOccupancySubsystem::GetReservationOwner(const FIntPoint& Cell) const
{
    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo を使用 ★★★
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

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo を使用 ★★★
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

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo から Cell を取得 ★★★
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
            UE_LOG(LogTemp, Warning,
                TEXT("[Reserve] FAIL: (%d,%d) already reserved by %s (attempt by %s)"),
                Cell.X, Cell.Y, *GetNameSafe(Owner), *GetNameSafe(Actor));
            return false; // Exclusive reservation - first come first served
        }
        // Already reserved by this actor - OK
        UE_LOG(LogTemp, Verbose,
            TEXT("[Reserve] ALREADY OWNED: %s -> (%d,%d)"),
            *GetNameSafe(Actor), Cell.X, Cell.Y);
        return true;
    }

    // Reserve the cell
    ReserveCellForActor(Actor, Cell);
    UE_LOG(LogTemp, Log,
        TEXT("[Reserve] OK: %s -> (%d,%d) TurnId=%d"),
        *GetNameSafe(Actor), Cell.X, Cell.Y, TurnId);
    return true;
}

void UGridOccupancySubsystem::BeginMovePhase()
{
    CommittedThisTick.Reset();
    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] BeginMovePhase - committed set cleared"));
}

void UGridOccupancySubsystem::EndMovePhase()
{
    CommittedThisTick.Reset();
    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] EndMovePhase - committed set cleared"));
}

bool UGridOccupancySubsystem::HasCommittedThisTick(AActor* Actor) const
{
    return CommittedThisTick.Contains(Actor);
}

// ★★★ CRITICAL FIX (2025-11-11): 新しいメソッドの実装 ★★★

void UGridOccupancySubsystem::MarkReservationCommitted(AActor* Actor, int32 TurnId)
{
    if (!Actor)
    {
        return;
    }

    // ActorToReservation から予約情報を取得して bCommitted を true にする
    if (FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        if (InfoPtr->TurnId == TurnId)
        {
            InfoPtr->bCommitted = true;

            // ReservedCells の方も更新
            if (FReservationInfo* CellInfoPtr = ReservedCells.Find(InfoPtr->Cell))
            {
                CellInfoPtr->bCommitted = true;
            }

            UE_LOG(LogTemp, Log,
                TEXT("[GridOccupancy] MarkCommitted: %s -> (%d,%d) TurnId=%d"),
                *GetNameSafe(Actor), InfoPtr->Cell.X, InfoPtr->Cell.Y, TurnId);
        }
    }
}

void UGridOccupancySubsystem::PurgeOutdatedReservations(int32 InCurrentTurnId)
{
    int32 PurgedCount = 0;

    // ReservedCells から古い予約を削除
    for (auto It = ReservedCells.CreateIterator(); It; ++It)
    {
        if (It.Value().TurnId != InCurrentTurnId)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("[GridOccupancy] Purging outdated reservation: Cell=(%d,%d) TurnId=%d (Current=%d)"),
                It.Key().X, It.Key().Y, It.Value().TurnId, InCurrentTurnId);
            It.RemoveCurrent();
            PurgedCount++;
        }
    }

    // ActorToReservation から古い予約を削除
    for (auto It = ActorToReservation.CreateIterator(); It; ++It)
    {
        if (It.Value().TurnId != InCurrentTurnId)
        {
            It.RemoveCurrent();
        }
    }

    if (PurgedCount > 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[GridOccupancy] PurgeOutdatedReservations: Removed %d old reservations (CurrentTurnId=%d)"),
            PurgedCount, InCurrentTurnId);
    }
}

void UGridOccupancySubsystem::SetCurrentTurnId(int32 TurnId)
{
    CurrentTurnId = TurnId;
    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] SetCurrentTurnId: %d"), CurrentTurnId);
}