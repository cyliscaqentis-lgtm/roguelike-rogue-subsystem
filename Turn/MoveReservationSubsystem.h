// Copyright Epic Games, Inc. All Rights Reserved.
// CodeRevision: INC-2025-1122-R2 (Extract move reservation from GameTurnManagerBase) (2025-11-22)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "MoveReservationSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMoveReservation, Log, All);

class AUnitBase;
class UAbilitySystemComponent;
class UGridPathfindingSubsystem;
class UTurnActionBarrierSubsystem;
class UGridOccupancySubsystem;
class AGameTurnManagerBase;

/**
 * Barrier info for manual enemy moves
 */
USTRUCT()
struct FManualMoveBarrierInfo
{
    GENERATED_BODY()

    UPROPERTY()
    int32 TurnId = INDEX_NONE;

    UPROPERTY()
    FGuid ActionId;
};

/**
 * UMoveReservationSubsystem
 *
 * Manages move reservations, dispatch of resolved moves, and manual move delegate tracking.
 * Extracted from GameTurnManagerBase to reduce file size and improve modularity.
 */
UCLASS()
class LYRAGAME_API UMoveReservationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //==========================================================================
    // Lifecycle
    //==========================================================================

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //==========================================================================
    // Move Reservation API
    //==========================================================================

    /**
     * Register a resolved move for an actor.
     * @return true if reservation succeeded, false if failed
     */
    bool RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell);

    /** Check if a move to cell is authorized for an actor */
    bool IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const;

    /** Check if actor has a reservation for a specific cell */
    bool HasReservationFor(AActor* Actor, const FIntPoint& Cell) const;

    /** Release move reservation for an actor */
    void ReleaseMoveReservation(AActor* Actor);

    /** Clear all resolved move reservations */
    void ClearResolvedMoves();

    //==========================================================================
    // Move Dispatch API
    //==========================================================================

    /**
     * Dispatch a resolved move action.
     * @return true if dispatch succeeded
     */
    bool DispatchResolvedMove(const FResolvedAction& Action, AGameTurnManagerBase* TurnManager);

    /**
     * Trigger player move ability via GAS event.
     * @return true if ability was triggered successfully
     */
    bool TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit, AGameTurnManagerBase* TurnManager);

    //==========================================================================
    // Manual Move Delegate Management
    //==========================================================================

    /** Register a manual move delegate for a unit */
    void RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback);

    /** Handle manual move finished callback */
    UFUNCTION()
    void HandleManualMoveFinished(AUnitBase* Unit);

    /** Check if unit is a pending player fallback move */
    bool IsPendingPlayerFallback(AUnitBase* Unit) const;

    /** Remove from pending player fallback set and return true if found */
    bool RemoveFromPendingPlayerFallback(AUnitBase* Unit);

    //==========================================================================
    // Accessors
    //==========================================================================

    /** Get pending move reservations map */
    const TMap<TWeakObjectPtr<AActor>, FIntPoint>& GetPendingMoveReservations() const { return PendingMoveReservations; }

private:
    //==========================================================================
    // Internal Move Dispatch Helpers
    //==========================================================================

    /** Handle wait/no-op resolved action */
    bool HandleWaitResolvedAction(AUnitBase* Unit, const FResolvedAction& Action);

    /** Handle player resolved move */
    bool HandlePlayerResolvedMove(AUnitBase* Unit, const FResolvedAction& Action, AGameTurnManagerBase* TurnManager);

    /** Handle enemy resolved move */
    bool HandleEnemyResolvedMove(AUnitBase* Unit, const FResolvedAction& Action, UGridPathfindingSubsystem* PathFinder);

    //==========================================================================
    // Data Members
    //==========================================================================

    /** Resolved move reservations for the current turn (Actor -> Cell) */
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, FIntPoint> PendingMoveReservations;

    /** Active move delegate bindings */
    TSet<TWeakObjectPtr<AUnitBase>> ActiveMoveDelegates;

    /** Pending player fallback moves */
    TSet<TWeakObjectPtr<AUnitBase>> PendingPlayerFallbackMoves;

    /** Pending barrier registrations for manual enemy moves */
    UPROPERTY()
    TMap<TWeakObjectPtr<AUnitBase>, FManualMoveBarrierInfo> PendingMoveActionRegistrations;

    /** Weak reference to TurnManager for callbacks */
    TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
};
