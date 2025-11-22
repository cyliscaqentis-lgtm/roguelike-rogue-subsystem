
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "StableActorRegistry.h"
#include "GameplayTagContainer.h"
#include "ConflictResolverSubsystem.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogConflictResolver, Log, All);

class AGameTurnManagerBase;

/**
 * UConflictResolverSubsystem: 予約テーブルと衝突解決(v2.2 第7章・第17章)
 */
UCLASS()
class LYRAGAME_API UConflictResolverSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * 予約テーブルをクリア(ターン開始時)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void ClearReservations();

    /**
     * 予約応募追加
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void AddReservation(const FReservationEntry& Entry);

    /**
     * Resolve all conflicts and produce final action list (v2.2: TripleBucket + Cycle)
     *
     * CONTRACT (Priority 2 guarantees):
     *   1. Intent Non-Disappearance: Input reservation count MUST equal output action count.
     *      - Enforced by checkf() at function end to prevent silent intent loss
     *      - Every reservation becomes exactly one FResolvedAction (success, fallback, or wait)
     *   2. ResolutionReason Completeness: All actions MUST have ResolutionReason set.
     *      - Explains why each action was resolved as-is (success, lost conflict, fallback, etc.)
     *      - Used for debugging and UI feedback
     *
     * MODE-SPECIFIC BEHAVIOR:
     *   SEQUENTIAL MODE (has ATTACK entries):
     *     - Attack entries ALWAYS win against Move entries for the same cell
     *     - This effectively "locks" the attacker's current cell from Move intrusion
     *     - Rationale: Attacker must stay in place to execute attack animation
     *     - Move losers receive reason: "Cell locked by attacker (Sequential)"
     *   SIMULTANEOUS MODE (all MOVE entries):
     *     - No attack priority - all reservations are moves
     *     - Winner selected by action tier priority or random tie-breaking
     *     - Move losers receive reason: "Cell occupied by higher priority"
     *
     * @return Array of FResolvedAction, guaranteed to have same count as input reservations
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    TArray<FResolvedAction> ResolveAllConflicts();

    /**
     * 行動タグからTierを取得(Attack=3, Dash=2, Move=1, Wait=0)
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Resolve")
    int32 GetActionTier(const FGameplayTag& AbilityTag) const;

private:
    // 予約テーブル: Key=(TimeSlot, Cell), Value=競争者リスト
    TMap<TPair<int32, FIntPoint>, TArray<FReservationEntry>> ReservationTable;

    // 三バケット解決(v2.2 第17章)
    TArray<FResolvedAction> ResolveWithTripleBucket(const TArray<FReservationEntry>& Applicants);

    // サイクル検出(k≧3の巡回)
    bool DetectAndAllowCycle(const TArray<FReservationEntry>& Applicants, TArray<FStableActorID>& OutCycle);

    // フォールバック移動(隣接T1のみ)
    FResolvedAction TryFallbackMove(const FReservationEntry& LoserEntry);

    // Wait生成
    FResolvedAction CreateWaitAction(const FReservationEntry& Entry);

    // Priority 2.1: GridOccupancySubsystem への参照
    mutable TWeakObjectPtr<class UGridOccupancySubsystem> CachedGridOccupancy;
    class UGridOccupancySubsystem* ResolveGridOccupancy() const;

    mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
    AGameTurnManagerBase* ResolveTurnManager() const;

};
