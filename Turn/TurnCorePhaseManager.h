// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "TurnCorePhaseManager.generated.h"

// ログカチE��リの宣言
DECLARE_LOG_CATEGORY_EXTERN(LogTurnCore, Log, All);

// ☁E�E☁E前方宣言 ☁E�E☁E
class UDistanceFieldSubsystem;
class UConflictResolverSubsystem;
class UStableActorRegistry;
class UAbilitySystemComponent;
class AGameTurnManagerBase;

/**
 * UTurnCorePhaseManager
 * ターンフローの核�E�E2.5 Complete Edition�E�E
 *
 * 新機�E:
 * - ExecuteMovePhaseWithSlots: TimeSlot別の移動実行（神速対応！E
 * - ExecuteAttackPhaseWithSlots: TimeSlot別の攻撁E��行！E2.4追加�E�E
 * - CoreThinkPhaseWithTimeSlots: 神速対応Intent一括生�E�E�E2.4追加�E�E
 * - EnsurePawnASC: Pawn所有ASC確保！E2.5追加・非static�E�E
 * - バケチE��化による高速フィルタリング�E�Eパス処琁E��E
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
    void CoreObservationPhase(const FIntPoint& PlayerCell, const TArray<AActor*>& Enemies = TArray<AActor*>());

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FEnemyIntent> CoreThinkPhase(const TArray<AActor*>& Enemies);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FResolvedAction> CoreResolvePhase(const TArray<FEnemyIntent>& Intents);

    /**
     * Phase 4: Execute Phase
     * ☁E�E☁E演�E起動�EBlueprintへ完�E委譲、E++では何もしなぁE☁E�E☁E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreCleanupPhase();

    // ========================================================================
    // TimeSlot対応：高速実行ラチE���E�E2.4完�E版！E
    // ========================================================================

    /**
     * 移動フェーズをTimeSlot別に実衁E
     *
     * 処琁E��ロー:
     * 1. バケチE��化！Eパスで全Intentを振り�Eけ！E
     * 2. スロチE��0→MaxSlotの頁E��実衁E
     * 3. 吁E��ロチE��でCoreResolvePhase→CoreExecutePhase
     * 4. 占有更新はCoreExecutePhase冁E��実施�E�スロチE��間�E一貫性保証�E�E
     *
     * @param AllIntents 全Intent�E�EimeSlot混在OK�E�E
     * @param OutActions 実行されたResolvedActionの統合�E刁E
     * @return MaxTimeSlot値�E�E=通常速度、E=神速、E=3倍送E..�E�E
     *
     * 使用例！Elueprint�E�E
     *   ExecuteMovePhaseWithSlots(Intents) ↁEResolvedActions, MaxTimeSlot
     *   BP_ExecuteResolvedActions(ResolvedActions)  // 演�Eのみ
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteMovePhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * 攻撁E��ェーズをTimeSlot別に実行！E2.4追加�E�E
     *
     * 処琁E��ロー:
     * 1. バケチE��化！Eパスで全Intentを振り�Eけ！E
     * 2. 吁E��ロチE��で「AI.Intent.Attack」タグのみフィルタ
     * 3. ASCへ攻撁E��ベント送信�E�EendGameplayEventToActor�E�E
     * 4. 再帰・Delay不要E��ESC冁E��完結！E
     *
     * @param AllIntents 全Intent
     * @param OutActions 実行されたResolvedActionの統合�E刁E
     * @return MaxTimeSlot値
     *
     * 使用例！Elueprint�E�E
     *   ExecuteAttackPhaseWithSlots(Intents) ↁEResolvedActions, MaxTimeSlot
     *   BP_ExecuteResolvedActions(ResolvedActions)  // 演�E�E�任意！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteAttackPhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * 神速対応Intent一括生�E�E�E2.4追加�E�E
     *
     * 処琁E
     * 1. 全敵のTimeSlot=0のIntentを生成！EoreThinkPhase呼び出し！E
     * 2. 「神速」タグ持ちにTimeSlot=1のIntentを追加
     * 3. TimeSlot設定済みのIntent配�Eを返却
     *
     * @param Enemies 敵アクター配�E
     * @param OutIntents 生�EされたIntent配�E�E�EimeSlot設定済み�E�E
     *
     * 使用例！Elueprint�E�E
     *   CoreThinkPhaseWithTimeSlots(CachedEnemies) ↁEOutIntents
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
     * Lyra準拠�E�Actor ↁEPawn ↁEController ↁEPlayerState の頁E��探索
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Core")
    static UAbilitySystemComponent* ResolveASC(AActor* Actor);

    /**
     * Pawn所有ASCを確保！E2.5追加・非static�E�E
     *
     * 処琁E��ロー:
     * 1. 既存ASCを探す！EAbilitySystemGlobals, FindComponentByClass�E�E
     * 2. なければPawn直下に新規作�E�E�Eyra依存なし！E
     * 3. RegisterComponent + InitAbilityActorInfo(Pawn, Pawn)
     * 4. レプリケーション設定！Eixed mode�E�E
     *
     * 用送E
     * - AIController変更不可の制紁E��でPawn所有ASCを実現
     * - PlayerState不要E��EWantsPlayerState=false でも動作！E
     * - BeginPlayで呼び出すことで確実にASCを用愁E
     *
     * @param Pawn 対象Pawn�E�EP_EnemyUnitBase等！E
     * @return 確保されたASC�E�既存また�E新規作�E�E�E
     *
     * 使用例！Elueprint�E�E
     *   Event BeginPlay
     *     ↁEGet World Subsystem (TurnCorePhaseManager)
     *     ↁEEnsurePawnASC(self)
     *     ↁERegisterEnemyAndAssignOrder(self)
     *
     * ※ 非staticなので World Subsystem 経由で呼び出ぁE
     */

    // GAS 準備完亁E��ェチE��
    UFUNCTION(BlueprintCallable, Category = "Turn System")
    static bool IsGASReady(AActor* Actor);

    // 全敵の準備完亁E��ェチE��
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
    // Helper Methods
    // ========================================================================

    const FGameplayTag& Tag_Move();
    const FGameplayTag& Tag_Attack();
    const FGameplayTag& Tag_Wait();

    void BucketizeIntentsBySlot(
        const TArray<FEnemyIntent>& All,
        TArray<TArray<FEnemyIntent>>& OutBuckets,
        int32& OutMaxSlot);

    TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
    AGameTurnManagerBase* ResolveTurnManager();
};
