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

bool UGridOccupancySubsystem::ReserveCellForActor(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        return false;  // Actor が null の場合は失敗
    }

    // ★★★ CRITICAL FIX (2025-11-11): 既存の予約を保護（OriginHold対応） ★★★
    // セルが既に他の Actor に予約されている場合、上書きしない
    if (const FReservationInfo* ExistingInfo = ReservedCells.Find(Cell))
    {
        if (ExistingInfo->Owner.IsValid() && ExistingInfo->Owner.Get() != Actor)
        {
            // OriginHold の場合、owner が移動中なので destination として予約できない（backstab防止）
            if (ExistingInfo->bIsOriginHold)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[GridOccupancy] REJECT RESERVATION: %s cannot reserve (%d,%d) - OriginHold by %s (TurnId=%d) [BACKSTAB BLOCKED]"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(ExistingInfo->Owner.Get()), ExistingInfo->TurnId);
                return false;  // OriginHold により予約拒否（backstab防止）
            }
            else
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[GridOccupancy] REJECT RESERVATION: %s cannot reserve (%d,%d) - already reserved by %s (TurnId=%d)"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y, *GetNameSafe(ExistingInfo->Owner.Get()), ExistingInfo->TurnId);
                return false;  // 予約拒否 - 他のActorが既に予約済み
            }
        }
    }

    // ★★★ OriginHold実装: 移動元セルを取得 ★★★
    const FIntPoint* CurrentCellPtr = ActorToCell.Find(Actor);
    const FIntPoint CurrentCell = CurrentCellPtr ? *CurrentCellPtr : FIntPoint(-1, -1);

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo 形式に変更 + OriginHold追加 ★★★
    ReleaseReservationForActor(Actor);  // 既存の予約（destination + originHold）を全解放

    // Destination cell に通常の予約を作成
    FReservationInfo DestInfo(Actor, Cell, CurrentTurnId, false);
    ReservedCells.Add(Cell, DestInfo);
    ActorToReservation.Add(Actor, DestInfo);

    UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] RESERVE DEST: %s -> (%d, %d) (TurnId=%d)"),
        *GetNameSafe(Actor), Cell.X, Cell.Y, CurrentTurnId);

    // ★★★ OriginHold実装: 移動元セルにOriginHold予約を配置（backstab防止） ★★★
    if (CurrentCell != FIntPoint(-1, -1) && CurrentCell != Cell)
    {
        // Origin cell に OriginHold を作成（他のActorが入ってこないようにする）
        FReservationInfo OriginHoldInfo(Actor, CurrentCell, CurrentTurnId, true);
        ReservedCells.Add(CurrentCell, OriginHoldInfo);

        UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] RESERVE ORIGIN-HOLD: %s protects origin (%d, %d) (TurnId=%d) [BACKSTAB PROTECTION]"),
            *GetNameSafe(Actor), CurrentCell.X, CurrentCell.Y, CurrentTurnId);
    }

    return true;  // 予約成功
}

void UGridOccupancySubsystem::ReleaseReservationForActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // ★★★ CRITICAL FIX (2025-11-11): OriginHold対応 - Actor が owner の全ての予約を解放 ★★★
    // Destination reservation を解放
    if (const FReservationInfo* InfoPtr = ActorToReservation.Find(Actor))
    {
        ReservedCells.Remove(InfoPtr->Cell);
        UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Reservation cleared for %s (destination: %d,%d)"),
            *GetNameSafe(Actor), InfoPtr->Cell.X, InfoPtr->Cell.Y);
    }
    ActorToReservation.Remove(Actor);

    // OriginHold reservation を解放（ReservedCells から Actor が owner の OriginHold を検索して削除）
    for (auto It = ReservedCells.CreateIterator(); It; ++It)
    {
        const FReservationInfo& Info = It.Value();
        if (Info.bIsOriginHold && Info.Owner.IsValid() && Info.Owner.Get() == Actor)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] OriginHold cleared for %s (origin: %d,%d)"),
                *GetNameSafe(Actor), It.Key().X, It.Key().Y);
            It.RemoveCurrent();
        }
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

// ★★★ CRITICAL FIX (2025-11-11): 整合性チェック - 重なり検出＆修復 ★★★

void UGridOccupancySubsystem::EnforceUniqueOccupancy()
{
    // Step 1: Cell→Actors[] マップを構築（ActorToCell から逆引き）
    TMap<FIntPoint, TArray<TWeakObjectPtr<AActor>>> CellToActors;
    for (const auto& [Actor, Cell] : ActorToCell)
    {
        if (Actor && Actor->IsValidLowLevel())
        {
            CellToActors.FindOrAdd(Cell).Add(Actor);
        }
    }

    // Step 2: 重なり（複数Actorが同一セル）を検出
    int32 FixedCount = 0;
    for (const auto& [Cell, Actors] : CellToActors)
    {
        if (Actors.Num() <= 1)
        {
            continue;  // 正常：1セルに1Actor以下
        }

        // ★★★ 重なり発見！ ★★★
        UE_LOG(LogTemp, Error,
            TEXT("[GridOccupancy] OVERLAP DETECTED at (%d,%d): %d actors stacked!"),
            Cell.X, Cell.Y, Actors.Num());

        // Step 3: Keeper（残すActor）を決定
        AActor* Keeper = ChooseKeeperActor(Cell, Actors);
        if (!Keeper)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[GridOccupancy] Failed to choose keeper for (%d,%d) - skipping"),
                Cell.X, Cell.Y);
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[GridOccupancy] Keeper for (%d,%d): %s"),
            Cell.X, Cell.Y, *GetNameSafe(Keeper));

        // Step 4: Keeper 以外を退避
        for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
        {
            AActor* Actor = ActorPtr.Get();
            if (!Actor || Actor == Keeper)
            {
                continue;
            }

            // 最寄りの空セルを探す
            const FIntPoint SafeCell = FindNearestFreeCell(Cell, 10);
            if (SafeCell == FIntPoint(-1, -1))
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[GridOccupancy] No free cell found for %s - keeping stacked (WARN)"),
                    *GetNameSafe(Actor));
                continue;  // 退避先がない場合はスキップ（重なりのまま）
            }

            // 強制移動
            ForceRelocate(Actor, SafeCell);
            FixedCount++;

            UE_LOG(LogTemp, Warning,
                TEXT("[GridOccupancy] [OCC FIX] Split stack: keep=%s at (%d,%d) | move %s -> (%d,%d)"),
                *GetNameSafe(Keeper), Cell.X, Cell.Y, *GetNameSafe(Actor), SafeCell.X, SafeCell.Y);
        }
    }

    if (FixedCount > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GridOccupancy] EnforceUniqueOccupancy: Fixed %d overlapping actors"),
            FixedCount);
    }
}

FIntPoint UGridOccupancySubsystem::FindNearestFreeCell(const FIntPoint& Origin, int32 MaxSearchRadius) const
{
    // BFS で最寄りの空セルを探す
    TQueue<FIntPoint> Queue;
    TSet<FIntPoint> Visited;

    Queue.Enqueue(Origin);
    Visited.Add(Origin);

    // 8方向（上下左右＋斜め）
    static const TArray<FIntPoint> Directions = {
        FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
        FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)
    };

    while (!Queue.IsEmpty())
    {
        FIntPoint Current;
        Queue.Dequeue(Current);

        // 距離チェック
        const int32 Distance = FMath::Abs(Current.X - Origin.X) + FMath::Abs(Current.Y - Origin.Y);
        if (Distance > MaxSearchRadius)
        {
            continue;
        }

        // Origin 自身はスキップ
        if (Current != Origin)
        {
            // セルが占有されていない＆予約されていないか確認
            const bool bOccupied = OccupiedCells.Contains(Current);
            const bool bReserved = ReservedCells.Contains(Current);

            if (!bOccupied && !bReserved)
            {
                // 空セル発見！
                return Current;
            }
        }

        // 隣接セルを探索
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

    // 見つからなかった
    return FIntPoint(-1, -1);
}

void UGridOccupancySubsystem::ForceRelocate(AActor* Actor, const FIntPoint& NewCell)
{
    if (!Actor)
    {
        return;
    }

    // 旧セルから削除
    if (const FIntPoint* OldCellPtr = ActorToCell.Find(Actor))
    {
        OccupiedCells.Remove(*OldCellPtr);
        UE_LOG(LogTemp, Verbose,
            TEXT("[GridOccupancy] ForceRelocate: %s released old cell (%d,%d)"),
            *GetNameSafe(Actor), OldCellPtr->X, OldCellPtr->Y);
    }

    // 新セルへ配置
    ActorToCell.Add(Actor, NewCell);
    OccupiedCells.Add(NewCell, Actor);

    // ★★★ Actor 側の座標も同期（World座標へ変換が必要） ★★★
    // 注：GridPathFinder を使用して Grid→World 変換が必要
    // ここでは簡易的に、セルサイズを 100 と仮定（プロジェクトに合わせて調整）
    const float CellSize = 100.0f;
    const FVector NewWorldLocation(
        NewCell.X * CellSize,
        NewCell.Y * CellSize,
        Actor->GetActorLocation().Z  // Z座標は保持
    );
    Actor->SetActorLocation(NewWorldLocation, false, nullptr, ETeleportType::TeleportPhysics);

    UE_LOG(LogTemp, Log,
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

    // 優先順位1: OriginHold を持っているActorを優先
    // （移動元セルを保護しているActorは、そのセルに留まる権利がある）
    for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (!Actor)
        {
            continue;
        }

        // OriginHold チェック（ReservedCells に bIsOriginHold=true でこのセルを保持しているか）
        if (const FReservationInfo* Info = ReservedCells.Find(Cell))
        {
            if (Info->bIsOriginHold && Info->Owner.Get() == Actor)
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("[GridOccupancy] ChooseKeeper: %s has OriginHold at (%d,%d)"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y);
                return Actor;
            }
        }
    }

    // 優先順位2: 最初の有効なActorを選ぶ（TODO: Team優先度などを実装可能）
    for (const TWeakObjectPtr<AActor>& ActorPtr : Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (Actor)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("[GridOccupancy] ChooseKeeper: %s (first valid)"),
                *GetNameSafe(Actor));
            return Actor;
        }
    }

    return nullptr;
}