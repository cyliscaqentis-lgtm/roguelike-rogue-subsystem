// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../../Turn/TurnSystemTypes.h"
#include "EnemyTurnDataSubsystem.generated.h"

// ログカテゴリ宣言（.cppでDEFINE）
DECLARE_LOG_CATEGORY_EXTERN(LogEnemyTurnDataSys, Log, All);

/**
 * UEnemyTurnDataSubsystem
 * - 敵ユニットのGenerationOrder（スポーン順）を管理
 * - Intent配列/EnemiesSorted配列の一元管理（唯一の真実源）
 * - 完全決定論のための採番システム
 *
 * 【Lumina提言対応】
 * - A4: RebuildEnemyList（敵リスト自動再構成）
 * - B1: HasAttackIntent（攻撃Intent判定）
 */
UCLASS()
class LYRAGAME_API UEnemyTurnDataSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //--------------------------------------------------------------------------
    // ★★★ Public Data Members ★★★
    //--------------------------------------------------------------------------

    UPROPERTY(BlueprintReadWrite, Category = "Enemy Turn Data")
    TArray<FEnemyObservation> Observations;

    UPROPERTY(BlueprintReadWrite, Category = "Enemy Turn Data")
    TArray<FEnemyIntent> Intents;

    //--------------------------------------------------------------------------
    // Lifecycle
    //--------------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //--------------------------------------------------------------------------
    // GenerationOrder管理
    //--------------------------------------------------------------------------

    /**
     * 敵を登録してGenerationOrderを採番
     * BeginPlay（Server）から呼び出す
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void RegisterEnemyAndAssignOrder(AActor* Enemy);

    /**
     * 指定した敵のGenerationOrderを取得
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    int32 GetEnemyGenerationOrder(AActor* Enemy) const;

    /**
     * すべての登録をクリア（ワールドリセット時）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void ClearAllRegistrations();

    //--------------------------------------------------------------------------
    // 🌟 統合API（Lumina提言A4: RebuildEnemyList）
    //--------------------------------------------------------------------------

    /**
     * 敵リストの自動再構成
     *
     * 【Lumina提言A4対応】
     * - BeginPlayのDelay(3s) + GetAllActorsOfClass + ForEachを削除
     * - スポーン/デスペーン時に手動更新不要
     * - 決定論的なソート（GenerationOrder昇順）
     *
     * 処理フロー:
     * 1. GetAllActorsWithTag でタグフィルタ取得
     * 2. 未登録の敵に GenerationOrder 自動採番
     * 3. EnemiesSorted 配列を再構成（ソート済み）
     *
     * @param TagFilter フィルタタグ（デフォルト："Enemy"）
     *
     * 使用例（Blueprint）:
     *   Event BeginPlay
     *     └─ Get World Subsystem: EnemyTurnDataSubsystem
     *         └─ RebuildEnemyList(TagFilter="Enemy")
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void RebuildEnemyList(FName TagFilter = TEXT("Enemy"));

    //--------------------------------------------------------------------------
    // 🌟 統合API（Lumina提言B1: HasAttackIntent）
    //--------------------------------------------------------------------------

    /**
     * 攻撃Intentが存在するか判定
     *
     * 【Lumina提言B1対応】
     * - BP側の「ForEach: Intent in Intents → If Tag.MatchesTag("Attack")」を削除
     * - C++でタグ走査を実施（1パス処理）
     *
     * @return 攻撃Intentが1つ以上ある場合true
     *
     * 使用例（Blueprint）:
     *   Event ContinueTurnAfterInput
     *     ├─ HasAttack = EnemyTurnData.HasAttackIntent()
     *     └─ Branch: HasAttack
     *         ├─ True → Sequential Phase（攻撃あり）
     *         └─ False → Simultaneous Phase（攻撃なし）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    bool HasAttackIntent() const;

    //--------------------------------------------------------------------------
    // Intent配列の管理
    //--------------------------------------------------------------------------

    /**
     * Intent配列の保存
     * CoreThinkPhaseWithTimeSlotsで生成された配列をここに保存
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void SaveIntents(const TArray<FEnemyIntent>& NewIntents);

    /**
     * Intent配列の取得（C++専用・参照返し）
     * Execute Move Phase/ExecuteAttacksで使用
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    const TArray<FEnemyIntent>& GetIntents() const;

    /**
     * Intent配列のクリア
     * ターン終了時に呼び出し
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void ClearIntents();

    /**
     * Intent配列の取得（Blueprint用・コピー返し）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    TArray<FEnemyIntent> GetIntentsCopy() const;

    /**
     * Intent配列の設定（Blueprint用・サーバー権威）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void SetIntents(const TArray<FEnemyIntent>& NewIntents);

    /**
     * Intent が存在するか（軽量チェック）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    bool HasIntents() const;

    /**
     * 指定TimeSlotのIntentを抽出（BPループ削減）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void GetIntentsForSlot(int32 TargetSlot, TArray<FEnemyIntent>& OutIntents) const;

    //--------------------------------------------------------------------------
    // EnemiesSorted配列の管理
    //--------------------------------------------------------------------------

    /**
     * EnemiesSorted配列の取得（Blueprint用・コピー返し）
     * TArray<AActor*> で返す（UFUNCTION制約）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    TArray<AActor*> GetEnemiesSortedCopy() const;

    /**
     * EnemiesSorted配列の設定（Blueprint用・サーバー権威）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void SetEnemiesSorted(const TArray<AActor*>& NewEnemies);

    //--------------------------------------------------------------------------
    // C++専用API（UFUNCTION ではない・高速）
    //--------------------------------------------------------------------------

    /**
     * EnemiesSorted配列の直接参照（C++専用）
     * UFUNCTIONではないため、TObjectPtrを直接返せる
     */
    const TArray<TObjectPtr<AActor>>& GetEnemiesSortedDirect() const { return EnemiesSorted; }

private:
    //--------------------------------------------------------------------------
    // Internal Helpers
    //--------------------------------------------------------------------------

    /**
     * ソート済み配列の再構成（内部用）
     * RebuildEnemyList から呼ばれる
     */
    void RebuildSortedArray();

    //--------------------------------------------------------------------------
    // Internal Data
    //--------------------------------------------------------------------------

    /** 次に採番する番号 */
    UPROPERTY()
    int32 NextGenerationOrder = 0;

    /** 敵アクター → GenerationOrder のマップ */
    UPROPERTY()
    TMap<TObjectPtr<AActor>, int32> EnemyToOrder;

    // ★★★ 削除：Intentsはpublicで既に定義済み ★★★
    // UPROPERTY()
    // TArray<FEnemyIntent> Intents;  // ← この行を削除

    /** 敵ユニットのソート済み配列（TObjectPtr<AActor> で保持） */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> EnemiesSorted;

    /** データ更新カウンタ（デバッグ用） */
    UPROPERTY()
    int32 DataRevision = 0;
};
