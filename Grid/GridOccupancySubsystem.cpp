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

void UGridOccupancySubsystem::UpdateActorCell(AActor* Actor, FIntPoint NewCell)
{
    if (!Actor)
    {
        return;
    }

    ReleaseReservationForActor(Actor);

    if (const FIntPoint* OldCellPtr = ActorToCell.Find(Actor))
    {
        OccupiedCells.Remove(*OldCellPtr);
    }

    ActorToCell.Add(Actor, NewCell);
    OccupiedCells.Add(NewCell, Actor);

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] %s moved to (%d, %d)"),
        *GetNameSafe(Actor), NewCell.X, NewCell.Y);
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

    if (const TWeakObjectPtr<AActor>* ReservedPtr = ReservedCells.Find(Cell))
    {
        if (ReservedPtr->IsValid())
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

    ReleaseReservationForActor(Actor);
    ReservedCells.Add(Cell, Actor);
    ActorToReservation.Add(Actor, Cell);

    UE_LOG(LogTemp, Verbose, TEXT("[GridOccupancy] Cell (%d, %d) reserved by %s"),
        Cell.X, Cell.Y, *GetNameSafe(Actor));
}

void UGridOccupancySubsystem::ReleaseReservationForActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    if (const FIntPoint* CellPtr = ActorToReservation.Find(Actor))
    {
        ReservedCells.Remove(*CellPtr);
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
    if (const TWeakObjectPtr<AActor>* ReservedPtr = ReservedCells.Find(Cell))
    {
        return ReservedPtr->IsValid();
    }

    return false;
}

AActor* UGridOccupancySubsystem::GetReservationOwner(const FIntPoint& Cell) const
{
    if (const TWeakObjectPtr<AActor>* ReservedPtr = ReservedCells.Find(Cell))
    {
        return ReservedPtr->Get();
    }

    return nullptr;
}

bool UGridOccupancySubsystem::IsReservationOwnedByActor(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor)
    {
        return false;
    }

    if (const TWeakObjectPtr<AActor>* ReservedPtr = ReservedCells.Find(Cell))
    {
        if (ReservedPtr->IsValid())
        {
            return ReservedPtr->Get() == Actor;
        }
    }

    return false;
}