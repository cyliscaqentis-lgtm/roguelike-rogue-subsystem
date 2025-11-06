// Copyright Epic Games, Inc. All Rights Reserved.

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
    UE_LOG(LogTemp, Log, TEXT("[GridOccupancy] Deinitialized"));
    Super::Deinitialize();
}

// ★★★ 神速対応の核心：アクターの現在セル位置を取得 ★★★
FIntPoint UGridOccupancySubsystem::GetCellOfActor(AActor* Actor) const
{
    if (!Actor)
    {
        return FIntPoint::ZeroValue;
    }

    const FIntPoint* CellPtr = ActorToCell.Find(Actor);
    return CellPtr ? *CellPtr : FIntPoint::ZeroValue;
}

// ★★★ セル位置を更新（移動実行後に呼び出す） ★★★
void UGridOccupancySubsystem::UpdateActorCell(AActor* Actor, FIntPoint NewCell)
{
    if (!Actor)
    {
        return;
    }

    // 旧セルの占有を解除
    const FIntPoint* OldCellPtr = ActorToCell.Find(Actor);
    if (OldCellPtr)
    {
        OccupiedCells.Remove(*OldCellPtr);
    }

    // 新セルに更新
    ActorToCell.Add(Actor, NewCell);
    OccupiedCells.Add(NewCell, Actor);

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] %s moved to (%d, %d)"),
        *GetNameSafe(Actor), NewCell.X, NewCell.Y);
}

// ★★★ IsWalkableはPathFinderに統一するため削除 ★★★
/*
bool UGridOccupancySubsystem::IsWalkable(const FIntPoint& Cell) const
{
    // ★★★ マップ範囲チェック ★★★
    const int32 MapSize = 64; // ★ プロジェクトのマップサイズに合わせる

    if (Cell.X < 0 || Cell.X >= MapSize || Cell.Y < 0 || Cell.Y >= MapSize)
    {
        return false;
    }

    // ★★★ 占有チェック ★★★
    const TWeakObjectPtr<AActor>* OccupierPtr = OccupiedCells.Find(Cell);
    if (OccupierPtr && OccupierPtr->IsValid())
    {
        return false;  // 占有されている
    }

    return true;
}
*/

bool UGridOccupancySubsystem::IsCellOccupied(const FIntPoint& Cell) const
{
    // ★★★ 占有チェック ★★★
    const TWeakObjectPtr<AActor>* OccupierPtr = OccupiedCells.Find(Cell);
    if (OccupierPtr && OccupierPtr->IsValid())
    {
        return true;  // 占有されている
    }
    
    return false;  // 占有されていない
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
    // セル→アクターのマップから削除
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

    // アクター→セルのマップから削除
    const FIntPoint* CellPtr = ActorToCell.Find(Actor);
    if (CellPtr)
    {
        OccupiedCells.Remove(*CellPtr);
    }
    ActorToCell.Remove(Actor);

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Actor %s unregistered"),
        *GetNameSafe(Actor));
}
