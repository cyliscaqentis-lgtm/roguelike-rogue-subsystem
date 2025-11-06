// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "TurnCorePhaseManager.generated.h"

// ログカテゴリの宣言
DECLARE_LOG_CATEGORY_EXTERN(LogTurnCore, Log, All);

// ★★★ 前方宣言 ★★★
class UDistanceFieldSubsystem;
class UConflictResolverSubsystem;
class UStableActorRegistry;
class UAbilitySystemComponent;

/**
 * UTurnCorePhaseManager
 * ターンフローの核（v2.5 Complete Edition）
 *
 * 新機能:
 * - ExecuteMovePhaseWithSlots: TimeSlot別の移動実行（神速対応）
 * - ExecuteAttackPhaseWithSlots: TimeSlot別の攻撃実行（v2.4追加）
 * - CoreThinkPhaseWithTimeSlots: 神速対応Intent一括生成（v2.4追加）
 * - EnsurePawnASC: Pawn所有ASC確保（v2.5追加・非static）
 * - バケット化による高速フィルタリング（1パス処理）
 */
UCLASS()
class LYRAGAME_API UTurnCorePhaseManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ========================================================================
    // Core Phase Pipeline
    // ========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreObservationPhase(const FIntPoint& PlayerCell);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FEnemyIntent> CoreThinkPhase(const TArray<AActor*>& Enemies);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FResolvedAction> CoreResolvePhase(const TArray<FEnemyIntent>& Intents);

    /**
     * Phase 4: Execute Phase
     * ★★★ 演出起動はBlueprintへ完全委譲。C++では何もしない ★★★
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreCleanupPhase();

    // ========================================================================
    // TimeSlot対応：高速実行ラッパ（v2.4完全版）
    // ========================================================================

    /**
     * 移動フェーズをTimeSlot別に実行
     *
     * 処理フロー:
     * 1. バケット化（1パスで全Intentを振り分け）
     * 2. スロット0→MaxSlotの順に実行
     * 3. 各スロットでCoreResolvePhase→CoreExecutePhase
     * 4. 占有更新はCoreExecutePhase内で実施（スロット間の一貫性保証）
     *
     * @param AllIntents 全Intent（TimeSlot混在OK）
     * @param OutActions 実行されたResolvedActionの統合配列
     * @return MaxTimeSlot値（0=通常速度、1=神速、2=3倍速...）
     *
     * 使用例（Blueprint）:
     *   ExecuteMovePhaseWithSlots(Intents) → ResolvedActions, MaxTimeSlot
     *   BP_ExecuteResolvedActions(ResolvedActions)  // 演出のみ
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteMovePhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * 攻撃フェーズをTimeSlot別に実行（v2.4追加）
     *
     * 処理フロー:
     * 1. バケット化（1パスで全Intentを振り分け）
     * 2. 各スロットで「AI.Intent.Attack」タグのみフィルタ
     * 3. ASCへ攻撃イベント送信（SendGameplayEventToActor）
     * 4. 再帰・Delay不要（ASC内で完結）
     *
     * @param AllIntents 全Intent
     * @param OutActions 実行されたResolvedActionの統合配列
     * @return MaxTimeSlot値
     *
     * 使用例（Blueprint）:
     *   ExecuteAttackPhaseWithSlots(Intents) → ResolvedActions, MaxTimeSlot
     *   BP_ExecuteResolvedActions(ResolvedActions)  // 演出（任意）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteAttackPhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * 神速対応Intent一括生成（v2.4追加）
     *
     * 処理:
     * 1. 全敵のTimeSlot=0のIntentを生成（CoreThinkPhase呼び出し）
     * 2. 「神速」タグ持ちにTimeSlot=1のIntentを追加
     * 3. TimeSlot設定済みのIntent配列を返却
     *
     * @param Enemies 敵アクター配列
     * @param OutIntents 生成されたIntent配列（TimeSlot設定済み）
     *
     * 使用例（Blueprint）:
     *   CoreThinkPhaseWithTimeSlots(CachedEnemies) → OutIntents
     *   EnemyTurnData.SaveIntents(OutIntents)
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreThinkPhaseWithTimeSlots(
        const TArray<AActor*>& Enemies,
        TArray<FEnemyIntent>& OutIntents);

    // ========================================================================
    // Helper Functions
    // ========================================================================

    /**
     * ActorからAbilitySystemComponentを解決
     * Lyra準拠：Actor → Pawn → Controller → PlayerState の順で探索
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Core")
    static UAbilitySystemComponent* ResolveASC(AActor* Actor);

    /**
     * Pawn所有ASCを確保（v2.5追加・非static）
     *
     * 処理フロー:
     * 1. 既存ASCを探す（UAbilitySystemGlobals, FindComponentByClass）
     * 2. なければPawn直下に新規作成（Lyra依存なし）
     * 3. RegisterComponent + InitAbilityActorInfo(Pawn, Pawn)
     * 4. レプリケーション設定（Mixed mode）
     *
     * 用途:
     * - AIController変更不可の制約下でPawn所有ASCを実現
     * - PlayerState不要（bWantsPlayerState=false でも動作）
     * - BeginPlayで呼び出すことで確実にASCを用意
     *
     * @param Pawn 対象Pawn（BP_EnemyUnitBase等）
     * @return 確保されたASC（既存または新規作成）
     *
     * 使用例（Blueprint）:
     *   Event BeginPlay
     *     → Get World Subsystem (TurnCorePhaseManager)
     *     → EnsurePawnASC(self)
     *     → RegisterEnemyAndAssignOrder(self)
     *
     * ※ 非staticなので World Subsystem 経由で呼び出す
     */

    // GAS 準備完了チェック
    UFUNCTION(BlueprintCallable, Category = "Turn System")
    static bool IsGASReady(AActor* Actor);

    // 全敵の準備完了チェック
    UFUNCTION(BlueprintCallable, Category = "Turn System")
    bool AllEnemiesReady(const TArray<AActor*>& Enemies) const;

private:
    // ========================================================================
    // Subsystem Dependencies
    // ========================================================================

    UPROPERTY()
    TObjectPtr<UDistanceFieldSubsystem> DistanceField = nullptr;

    UPROPERTY()
    TObjectPtr<UConflictResolverSubsystem> ConflictResolver = nullptr;

    UPROPERTY()
    TObjectPtr<UStableActorRegistry> ActorRegistry = nullptr;

    // ========================================================================
    // TimeSlot Internal Helpers
    // ========================================================================

    /**
     * GameplayTag静的キャッシュ（ループ内での再取得を防止）
     */
    static const FGameplayTag& Tag_Move();
    static const FGameplayTag& Tag_Attack();
    static const FGameplayTag& Tag_Wait();

    /**
     * バケット化ヘルパー（1回の走査で全スロットに振り分け）
     *
     * 処理:
     * 1. MaxSlot算出（1パス目）
     * 2. バケット配列確保（再割当なし）
     * 3. Intent振り分け（2パス目）
     *
     * @param All 全Intent配列
     * @param OutBuckets スロット別Intent配列（OutBuckets[Slot][N]）
     * @param OutMaxSlot 最大TimeSlot値
     */
    static void BucketizeIntentsBySlot(
        const TArray<FEnemyIntent>& All,
        TArray<TArray<FEnemyIntent>>& OutBuckets,
        int32& OutMaxSlot);
};
