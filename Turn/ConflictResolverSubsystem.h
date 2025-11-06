 
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "StableActorRegistry.h"
#include "GameplayTagContainer.h"
#include "ConflictResolverSubsystem.generated.h"

/**
 * UConflictResolverSubsystem: 予約テーブルと衝突解決（v2.2 第7条・第17条）
 */
UCLASS()
class LYRAGAME_API UConflictResolverSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * 予約テーブルをクリア（ターン開始時）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void ClearReservations();

    /**
     * 予約応募を追加
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void AddReservation(const FReservationEntry& Entry);

    /**
     * 全ての衝突を解決（v2.2 三段バケット + サイクル許可）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    TArray<FResolvedAction> ResolveAllConflicts();

    /**
     * 行動タグからTierを取得（Attack=3, Dash=2, Move=1, Wait=0）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Resolve")
    int32 GetActionTier(const FGameplayTag& AbilityTag) const;

private:
    // 予約テーブル: Key=(TimeSlot, Cell), Value=応募者リスト
    TMap<TPair<int32, FIntPoint>, TArray<FReservationEntry>> ReservationTable;

    // 三段バケット解決（v2.2 第17条）
    TArray<FResolvedAction> ResolveWithTripleBucket(const TArray<FReservationEntry>& Applicants);

    // サイクル検出（k≧3の循環許可）
    bool DetectAndAllowCycle(const TArray<FReservationEntry>& Applicants, TArray<FStableActorID>& OutCycle);

    // フォールバック候補（近傍1回のみ）
    FResolvedAction TryFallbackMove(const FReservationEntry& LoserEntry);

    // Wait降格
    FResolvedAction CreateWaitAction(const FReservationEntry& Entry);
};
