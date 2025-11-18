// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StableActorRegistry.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"

// ★★★ NOTE: .generated.h must always be the last include ★★★
#include "TurnSystemTypes.generated.h"

//------------------------------------------------------------------------------
// Enemy Observation Data
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyObservation
{
    GENERATED_BODY()

    // Distance from the enemy to the player in tiles (Manhattan or project-specific metric)
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    int32 DistanceInTiles = 0;

    // Enemy's current grid position
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FIntPoint GridPosition = FIntPoint::ZeroValue;

    // Player's grid position at the time of observation
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FIntPoint PlayerGridPosition = FIntPoint::ZeroValue;

    // Enemy HP ratio in [0,1]
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    float HPRatio = 1.0f;

    // Environment / terrain tags around the enemy (cover, hazard, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FGameplayTagContainer EnvironmentTags;

    // Custom numeric stats (e.g., threat values, score weights, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    TMap<FName, float> CustomStats;
};

//------------------------------------------------------------------------------
// Enemy Intent
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyIntent
{
    GENERATED_BODY()

    // High-level ability tag (Move / Attack / Wait / etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTag AbilityTag;

    // Base priority of this intent (higher = preferred before tie-break)
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Intent")
    int32 BasePriority = 100;

    // Enemy's current cell at the time the intent was generated
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FIntPoint CurrentCell = FIntPoint::ZeroValue;

    // Target cell for this intent (move destination, attack origin, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FIntPoint NextCell = FIntPoint::ZeroValue;

    // Optional target actor (weak reference, safe across destruction)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TWeakObjectPtr<AActor> Target = nullptr;

    // Additional speed / initiative modifier for priority ordering
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    float SpeedPriority = 0.0f;

    // Strong reference to the target actor (for runtime use only)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TObjectPtr<AActor> TargetActor = nullptr;

    // Selected gameplay ability class to execute for this intent
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TSubclassOf<UGameplayAbility> SelectedAbility = nullptr;

    // Enemy actor that owns this intent
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TWeakObjectPtr<AActor> Actor = nullptr;

    // Logical time slot within the turn (0 = main move, >0 = follow-ups)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 TimeSlot = 0;

    // Cached distance to player at the time of decision (-1 = unknown)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 DistToPlayer = -1;

    // Strong owner reference (used where weak pointer is not sufficient)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TObjectPtr<AActor> Owner = nullptr;

    FEnemyIntent() = default;

    FEnemyIntent(const FGameplayTag& InTag, const FIntPoint& InCell, AActor* InTarget = nullptr)
        : AbilityTag(InTag)
        , NextCell(InCell)
        , Target(InTarget)
    {
    }
};

//------------------------------------------------------------------------------
// Enemy Intent Candidate (scored and filtered before final selection)
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyIntentCandidate
{
    GENERATED_BODY()

    // Concrete intent (Move, Attack, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FEnemyIntent Intent;

    // Final score after evaluation (higher = better)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    float Score = 0.0f;

    // Priority tier used together with Score (e.g., hard vs soft priority)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 Priority = 0;

    // Tags required on the owner / environment for this candidate to be valid
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTagContainer RequiredTags;

    // Tags that block this candidate (if present, candidate is discarded)
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTagContainer BlockedByTags;
};

//------------------------------------------------------------------------------
// Pending Move (pre-resolution move intent)
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FPendingMove
{
    GENERATED_BODY()

    // Actor trying to move
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    TObjectPtr<AActor> Mover = nullptr;

    // Starting cell for the move
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FIntPoint From = FIntPoint::ZeroValue;

    // Destination cell for the move
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FIntPoint To = FIntPoint::ZeroValue;

    // Priority for resolving simultaneous moves
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    float Priority = 0.0f;

    // If true, this move is cancelled and must not be applied
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    bool bCancelled = false;

    // Fallback intent if the move cannot be executed
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FEnemyIntent FallbackIntent;
};

//------------------------------------------------------------------------------
// Simultaneous Move Batch
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FSimulBatch
{
    GENERATED_BODY()

    // Collection of moves that should be resolved atomically / simultaneously
    UPROPERTY(BlueprintReadWrite, Category = "Batch")
    TArray<FPendingMove> Moves;
};

//------------------------------------------------------------------------------
// Board Snapshot
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FBoardSnapshot
{
    GENERATED_BODY()

    // Logical occupancy map (one actor per cell, or empty)
    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TMap<FIntPoint, TObjectPtr<AActor>> OccupancyMap;

    // Per-tile gameplay tags (terrain, hazards, cover, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TMap<FIntPoint, FGameplayTagContainer> TileTags;

    // Cells currently visible to the player / AI
    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TSet<FIntPoint> VisibleCells;
};

//------------------------------------------------------------------------------
// Command Apply Result (2025-11-10)
//------------------------------------------------------------------------------

/**
 * Result of applying a player command:
 * - Applied:        Move (or action) was applied and the turn was consumed.
 * - RotatedNoTurn:  Only rotation was applied, the turn window stays open.
 * - RejectedCloseWindow: Invalid request, closes the input window.
 */
UENUM(BlueprintType)
enum class ECommandApplyResult : uint8
{
    Applied              UMETA(DisplayName = "Applied (Turn Consumed)"),
    RotatedNoTurn        UMETA(DisplayName = "Rotated Only (No Turn)"),
    RejectedCloseWindow  UMETA(DisplayName = "Rejected (Close Window)")
};

//------------------------------------------------------------------------------
// Player Command
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FPlayerCommand
{
    GENERATED_BODY()

    // High-level command tag (Move, Attack, Wait, etc.)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FGameplayTag CommandTag;

    // Target cell (for movement, targeted abilities, etc.)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FIntPoint TargetCell = FIntPoint::ZeroValue;

    // Optional target actor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    TObjectPtr<AActor> TargetActor = nullptr;

    // Direction vector (for orientation / directional abilities)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FVector Direction = FVector::ZeroVector;

    // Logical turn ID assigned by the turn manager (INDEX_NONE if unset)
    UPROPERTY(BlueprintReadWrite, Category = "Command")
    int32 TurnId = INDEX_NONE;

    // WindowId: identifies the input window / command batch this command belongs to
    UPROPERTY(BlueprintReadWrite, Category = "Command")
    int32 WindowId = 0;

    FPlayerCommand()
        : CommandTag(FGameplayTag())
        , TargetCell(FIntPoint::ZeroValue)
        , TargetActor(nullptr)
        , Direction(FVector::ZeroVector)
        , TurnId(INDEX_NONE)
        , WindowId(0)
    {
    }
};

//------------------------------------------------------------------------------
// Turn Context
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FTurnContext
{
    GENERATED_BODY()

    // Current turn index (0-based or 1-based depending on project convention)
    UPROPERTY(BlueprintReadWrite, Category = "Context")
    int32 TurnIndex = 0;

    // Player actor participating in this turn
    UPROPERTY(BlueprintReadWrite, Category = "Context")
    TObjectPtr<AActor> PlayerActor = nullptr;

    // Enemies visible to the player (or relevant for this context)
    UPROPERTY(BlueprintReadWrite, Category = "Context")
    TArray<TObjectPtr<AActor>> VisibleEnemies;

    // Snapshot of the board state at the beginning of the turn
    UPROPERTY(BlueprintReadWrite, Category = "Context")
    FBoardSnapshot BoardState;
};

//------------------------------------------------------------------------------
// Phase 2: Reservation / Resolution Structures
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct LYRAGAME_API FReservationEntry
{
    GENERATED_BODY()

    // Actor requesting the reservation
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    TObjectPtr<AActor> Actor = nullptr;

    // Stable ID for the actor (survives save/load, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FStableActorID ActorID;

    // Actor's current cell at the time of reservation
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FIntPoint CurrentCell = FIntPoint(-1, -1);

    // Time slot for this reservation (0 = main move)
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 TimeSlot = 0;

    // Target cell being reserved
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FIntPoint Cell = FIntPoint(-1, -1);

    // Requested ability tag (Move, Attack, Wait, etc.)
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FGameplayTag AbilityTag;

    // Tier of the action for conflict resolution (higher tier wins first)
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 ActionTier = 0;

    // Base priority within the same action tier
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 BasePriority = 0;

    // Priority bonus based on distance reduction or path efficiency
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 DistanceReduction = 0;

    // Global reservation order (used as a final tie-breaker)
    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 GenerationOrder = 2147483647;
};

USTRUCT(BlueprintType)
struct LYRAGAME_API FResolvedAction
{
    GENERATED_BODY()

    // Resolved actor (weak reference to survive destruction)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    TWeakObjectPtr<AActor> Actor;

    // Original requested ability tag (intent)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FGameplayTag AbilityTag;

    // Optional target data for abilities
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FGameplayAbilityTargetDataHandle TargetData;

    // Strong pointer to the source actor (convenience mirror of Actor)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    TObjectPtr<AActor> SourceActor = nullptr;

    // Stable ID for the actor
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FStableActorID ActorID;

    // Starting cell for this resolved action
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FIntPoint CurrentCell = FIntPoint(-1, -1);

    // Final cell after resolution (may differ from requested Cell)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FIntPoint NextCell = FIntPoint(-1, -1);

    // Allowed dash steps (used in multi-tile dash moves)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    int32 AllowedDashSteps = 0;

    // Actual ability tag to execute after resolution (may differ from AbilityTag)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FGameplayTag FinalAbilityTag;

    // True if the actor ends up waiting (no positional change)
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    bool bIsWait = false;

    // Original generation order from the reservation
    UPROPERTY(BlueprintReadOnly, Category = "Resolved")
    int32 GenerationOrder = INT32_MAX;

    // Time slot this action belongs to
    UPROPERTY(BlueprintReadOnly, Category = "Resolved")
    int32 TimeSlot = 0;

    // Human-readable reason for how this action was resolved
    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FString ResolutionReason;

    // BUGFIX [INC-2025-TIMING]: Pre-registered Barrier ActionId for sync with animation / effects
    FGuid BarrierActionId;
};

//------------------------------------------------------------------------------
// Phase 3: Dash Stop Configuration
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct LYRAGAME_API FDashStopConfig
{
    GENERATED_BODY()

    // Stop dash when becoming adjacent to an enemy
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnEnemyAdjacent = true;

    // Stop dash when entering a danger tile
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnDangerTile = true;

    // Stop dash when hitting an obstacle / non-walkable tile
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnObstacle = true;

    // Maximum number of tiles that can be traversed in a single dash
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    int32 MaxDashDistance = 10;
};
