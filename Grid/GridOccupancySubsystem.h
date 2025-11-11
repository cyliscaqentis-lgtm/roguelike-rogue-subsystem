// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridOccupancySubsystem.generated.h"

/**
 * ★★★ CRITICAL FIX (2025-11-11): 予約情報構造体（TurnId + bCommitted + bIsOriginHold） ★★★
 * - TurnId: 予約が作成されたターン番号（古い予約の検出用）
 * - bCommitted: ConflictResolver で勝者が決まり、確定した予約かどうか
 * - bIsOriginHold: 移動元セルの保護用予約（backstab防止）
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
 * UGridOccupancySubsystem: グリチE��の占有�E通行可否管琁E
 * - 神速対応�E要E��Actor→セル位置の最新マップを管琁E
 * - Slot0の移動結果がSlot1に確実に反映されめE
 */
UCLASS()
class LYRAGAME_API UGridOccupancySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ☁E�E☁E神速対応�E核忁E��アクターの現在セル位置を取征E☁E�E☁E
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    FIntPoint GetCellOfActor(AActor* Actor) const;

    // ☁E�E☁Eセル位置を更新�E�移動実行後に呼び出す！E☁E�E☁E
    // ★★★ CRITICAL FIX (2025-11-10): 二重書き込みガード（成功=true, 失敗=false） ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    bool UpdateActorCell(AActor* Actor, FIntPoint NewCell);

    /**
     * 持E��セルが占有されてぁE��か判宁E
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

    // ☁E�E☁EIsWalkableはPathFinderに統一するため削除 ☁E�E☁E
    // UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    // bool IsWalkable(const FIntPoint& Cell) const;

    /**
     * セルを占有する（アクター配置時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void OccupyCell(const FIntPoint& Cell, AActor* Actor);

    /**
     * セルの占有を解除�E�アクター削除時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseCell(const FIntPoint& Cell);

    /**
     * アクターの登録を削除�E�死亡時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UnregisterActor(AActor* Actor);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): bool型に変更（成功/失敗を返す） ★★★
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
     * Begin move phase - clears committed actors set
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void BeginMovePhase();

    /**
     * End move phase - cleanup
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void EndMovePhase();

    /**
     * Check if an actor has committed its move this tick
     */
    bool HasCommittedThisTick(AActor* Actor) const;

    /**
     * ★★★ CRITICAL FIX (2025-11-11): 勝者の予約を committed にマーク ★★★
     * ConflictResolver で勝者が決まった後に呼び出す
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void MarkReservationCommitted(AActor* Actor, int32 TurnId);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): 古い予約を削除 ★★★
     * ターン開始時 or CleanupPhase で呼び出す
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
     * ★★★ CRITICAL FIX (2025-11-11): 既存の重なりを強制修復 ★★★
     * ターン開始時に呼び出し、同一セルに複数のActorが占有している場合に修正する
     * - 1セルに複数Actorがいる場合、1体だけを残して他は最寄りの空セルへ退避
     * - Cell→Actor の一意性を強制
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void EnforceUniqueOccupancy();

    /**
     * ★★★ CRITICAL FIX (2025-11-11): 物理座標ベースの占有マップ再構築 ★★★
     * 全ユニットの物理座標から占有マップを再構築し、論理/物理の不整合を修正
     * - ActorToCell と OccupiedCells を完全にクリアして再構築
     * - 物理的に重なっている場合、1体を残して他は最寄りの空セルへ退避
     * - ターン開始時に呼び出すことで、あらゆる不整合をリセット
     * @param AllUnits すべてのユニット（Player + Enemies）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void RebuildFromWorldPositions(const TArray<AActor*>& AllUnits);

private:
    /**
     * ★★★ CRITICAL FIX (2025-11-11): 整合性チェック用ヘルパー ★★★
     * 指定セルから最寄りの空セルを BFS で探す
     * @return 空セル、見つからない場合は (-1, -1)
     */
    FIntPoint FindNearestFreeCell(const FIntPoint& Origin, int32 MaxSearchRadius = 10) const;

    /**
     * ★★★ CRITICAL FIX (2025-11-11): 整合性チェック用ヘルパー ★★★
     * Actorを強制的に指定セルへ移動（予約なしの直接書き込み）
     * - ActorToCell と OccupiedCells の両方を更新
     * - Actor側の座標も同期（SetActorLocation）
     */
    void ForceRelocate(AActor* Actor, const FIntPoint& NewCell);

    /**
     * ★★★ CRITICAL FIX (2025-11-11): 整合性チェック用ヘルパー ★★★
     * 重なりが発生した場合、どのActorを残すか決定
     * 優先順位：OriginHold所有者 > Team優先度 > ランダム
     */
    AActor* ChooseKeeperActor(const FIntPoint& Cell, const TArray<TWeakObjectPtr<AActor>>& Actors) const;

    /**
     * Check if an actor will leave its current cell this tick
     * Returns true if the actor has a reservation for a different cell
     */
    bool WillLeaveThisTick(AActor* Actor) const;

    /**
     * Check if two actors form a perfect swap (A->B and B->A)
     * Returns true if both actors have reservations to each other's current cells
     */
    bool IsPerfectSwap(AActor* A, AActor* B) const;

    // ☁EActor ↁEセル位置のマップ（神速対応�E要E��E
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FIntPoint> ActorToCell;

    // セル ↁE占有アクターのマップ（通行可否判定用�E�E
    UPROPERTY()
    TMap<FIntPoint, TWeakObjectPtr<AActor>> OccupiedCells;

    // ★★★ CRITICAL FIX (2025-11-11): FReservationInfo 形式に変更（TurnId + bCommitted） ★★★
    UPROPERTY()
    TMap<FIntPoint, FReservationInfo> ReservedCells;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FReservationInfo> ActorToReservation;

    // ★★★ 二相コミット用: このターンで移動を確定したActor集合 (2025-11-11) ★★★
    UPROPERTY()
    TSet<TWeakObjectPtr<AActor>> CommittedThisTick;

    // ★★★ CRITICAL FIX (2025-11-11): 現在のターンID（古い予約の検出用） ★★★
    UPROPERTY()
    int32 CurrentTurnId = 0;
};
