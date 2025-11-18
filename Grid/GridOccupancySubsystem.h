// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridOccupancySubsystem.generated.h"

/**
 * ★★★ CRITICAL FIX (2025-11-11): Reservation info struct (TurnId + bCommitted + bIsOriginHold) ★★★
 * - TurnId: Turn number when the reservation was created (for detecting stale reservations)
 * - bCommitted: True if ConflictResolver selected this reservation as a winner and it is now committed
 * - bIsOriginHold: Reservation used to protect the origin cell (backstab protection)
 */
USTRUCT()
struct FReservationInfo
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<AActor> Owner;

    UPROPERTY()
    FIntPoint Cell;

    UPROPERTY()
    int32 TurnId;

    UPROPERTY()
    bool bCommitted;

    UPROPERTY()
    bool bIsOriginHold;

    FReservationInfo()
        : Owner(nullptr)
        , Cell(-1, -1)
        , TurnId(-1)
        , bCommitted(false)
        , bIsOriginHold(false)
    {}

    FReservationInfo(AActor* InOwner, FIntPoint InCell, int32 InTurnId, bool bInIsOriginHold = false)
        : Owner(InOwner)
        , Cell(InCell)
        , TurnId(InTurnId)
        , bCommitted(false)
        , bIsOriginHold(bInIsOriginHold)
    {}
};

/**
 * UGridOccupancySubsystem: Grid occupancy and passability management
 * - Maintains an up-to-date Actor → Cell map for fast queries
 * - Ensures that movement results of Slot 0 are reliably reflected into Slot 1
 */
UCLASS()
class LYRAGAME_API UGridOccupancySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Fast-path core API: get the current cell of an actor
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    FIntPoint GetCellOfActor(AActor* Actor) const;

    // Update cell position (must be called after a move is actually executed)
    // ★★★ CRITICAL FIX (2025-11-10): Guard against double-write (returns true on success, false on failure) ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    bool UpdateActorCell(AActor* Actor, FIntPoint NewCell);

    /**
     * Check if the given cell is currently occupied
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsCellOccupied(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    AActor* GetActorAtCell(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsCellReserved(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    AActor* GetReservationOwner(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsReservationOwnedByActor(AActor* Actor, const FIntPoint& Cell) const;

    /**
     * Get the reserved cell for an actor
     * Returns the reserved destination cell, or (-1,-1) if no reservation exists
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    FIntPoint GetReservedCellForActor(AActor* Actor) const;

    /**
     * Priority 2.2: Get all occupied cells for static blocker detection
     * @return Map of Cell -> Actor for all currently occupied cells
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    TMap<FIntPoint, AActor*> GetAllOccupiedCells() const;

    // NOTE: IsWalkable is removed; walkability is unified in PathFinder
    // UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    // bool IsWalkable(const FIntPoint& Cell) const;

    /**
     * Mark a cell as occupied (call when placing/spawning an actor)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void OccupyCell(const FIntPoint& Cell, AActor* Actor);

    /**
     * Release occupancy of a cell (call when the actor is removed/despawned)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseCell(const FIntPoint& Cell);

    /**
     * Unregister an actor from the occupancy system (call on death / removal)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UnregisterActor(AActor* Actor);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Changed to bool return (success/failure) ★★★
     * Reserve a cell for an actor. Returns false if the cell is already reserved by another actor.
     * @param Actor - The actor requesting the reservation
     * @param Cell - The grid cell to reserve
     * @return true if reservation succeeded, false if already reserved by another actor
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    bool ReserveCellForActor(AActor* Actor, const FIntPoint& Cell);

    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseReservationForActor(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ClearAllReservations();

    /**
     * Try to reserve a cell exclusively (first-come-first-served)
     * Returns false if the cell is already reserved by another actor
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    bool TryReserveCell(AActor* Actor, const FIntPoint& Cell, int32 TurnId);

    /**
     * Begin move phase - clears the committed actors set
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void BeginMovePhase();

    /**
     * End move phase - cleanup committed actors set
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void EndMovePhase();

    /**
     * Check if an actor has committed its move this tick
     */
    bool HasCommittedThisTick(AActor* Actor) const;

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Mark winning reservations as committed ★★★
     * Must be called by ConflictResolver after a winner has been determined
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void MarkReservationCommitted(AActor* Actor, int32 TurnId);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Remove stale reservations ★★★
     * Call at turn start or during a cleanup phase
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void PurgeOutdatedReservations(int32 CurrentTurnId);

    /**
     * Set the current turn ID for reservation tracking
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void SetCurrentTurnId(int32 TurnId);

    /**
     * Get the current turn ID
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Force repair of existing overlaps ★★★
     * Call at turn start to fix cases where multiple actors occupy the same cell:
     * - If multiple actors share a cell, keep one actor and relocate others to the nearest free cells
     * - Enforces uniqueness of Cell -> Actor mapping
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void EnforceUniqueOccupancy();

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Rebuild occupancy map from physical positions ★★★
     * Rebuilds the logical occupancy map from the physical world positions of all units,
     * fixing any logical/physical inconsistencies:
     * - Completely clears ActorToCell and OccupiedCells and reconstructs them
     * - If multiple actors physically overlap, keeps one and relocates others to nearest free cells
     * - Call at turn start to reset any accumulated inconsistencies
     * @param AllUnits All units (players + enemies)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void RebuildFromWorldPositions(const TArray<AActor*>& AllUnits);

private:
    /**
     * ★★★ CRITICAL FIX (2025-11-11): Consistency helper ★★★
     * Find the nearest free cell from the given origin using BFS.
     * @return A free cell, or (-1, -1) if none is found within the given radius
     */
    FIntPoint FindNearestFreeCell(const FIntPoint& Origin, int32 MaxSearchRadius = 10) const;

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Consistency helper ★★★
     * Forcefully relocate an actor to the specified cell (no reservation, direct write):
     * - Updates both ActorToCell and OccupiedCells
     * - Also synchronizes the actor's world position via SetActorLocation
     */
    void ForceRelocate(AActor* Actor, const FIntPoint& NewCell);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): Consistency helper ★★★
     * Decide which actor should remain on an overlapped cell.
     * Priority: OriginHold owner > team/priority rules (future) > first/other
     */
    AActor* ChooseKeeperActor(const FIntPoint& Cell, const TArray<TWeakObjectPtr<AActor>>& Actors) const;

    /**
     * Check if an actor will leave its current cell this tick
     * Returns true if the actor holds a reservation to a different cell
     */
    bool WillLeaveThisTick(AActor* Actor) const;

    /**
     * Check if two actors form a perfect swap (A -> B and B -> A)
     * Returns true if both actors have reservations to each other's current cells
     */
    bool IsPerfectSwap(AActor* A, AActor* B) const;

    // Map from Actor -> current cell (fast-path access to latest positions)
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FIntPoint> ActorToCell;

    // Map from Cell -> occupying actor (used for passability checks)
    UPROPERTY()
    TMap<FIntPoint, TWeakObjectPtr<AActor>> OccupiedCells;

    // ★★★ CRITICAL FIX (2025-11-11): Use FReservationInfo (TurnId + bCommitted + bIsOriginHold) ★★★
    UPROPERTY()
    TMap<FIntPoint, FReservationInfo> ReservedCells;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FReservationInfo> ActorToReservation;

    // ★★★ Two-phase commit: set of actors that have committed their move this tick (2025-11-11) ★★★
    UPROPERTY()
    TSet<TWeakObjectPtr<AActor>> CommittedThisTick;

    // ★★★ CRITICAL FIX (2025-11-11): Current turn ID (for detecting stale reservations) ★★★
    UPROPERTY()
    int32 CurrentTurnId = 0;
};
