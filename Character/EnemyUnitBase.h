// EnemyUnitBase.h
// Location: Rogue/Character/EnemyUnitBase.h

#pragma once

#include "CoreMinimal.h"
#include "UnitBase.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemInterface.h"          // 既に実装済みなら不要
#include "EnemyUnitBase.generated.h"

// ===== 前方宣言 =====
class ULyraPawnData;

/**
 * Enemy unit class with Lyra GAS initialization
 * Uses Lyra's InitState system for proper ASC setup from PlayerState
 */
UCLASS()
class LYRAGAME_API AEnemyUnitBase : public AUnitBase
{
    GENERATED_BODY()

public:
    AEnemyUnitBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★コアメソッド
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * Called when this Pawn is possessed by a Controller
     * Sets PawnData and triggers InitState progression
     */
    virtual void PossessedBy(AController* NewController) override;

    /**
     * Called when the game starts or when spawned
     * Schedules GameplayReady check
     */
    virtual void BeginPlay() override;

    /**
     * Called when controller replicates to clients (insurance)
     */
    virtual void OnRep_Controller() override;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★初期化待機メソッド
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * Called repeatedly until ASC is ready with Abilities and AttributeSets
     * Retries every 0.1 seconds, up to MaxRetryCount times
     */
    UFUNCTION()
    void OnGameplayReady();

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★設定プロパティ
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * PawnData to use for this enemy unit
     * Set this in Blueprint Class Defaults (e.g., DA_Enemy_Pawn)
     * Contains AbilitySets, AttributeSets, and other gameplay data
     */
    UPROPERTY(EditDefaultsOnly, Category = "Enemy|Pawn")
    TObjectPtr<ULyraPawnData> EnemyPawnData;

private:
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★リトライ管理
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * Current retry count for GameplayReady check
     */
    int32 RetryCount = 0;

    /**
     * Maximum retry count (5 seconds @ 0.1s interval = 50 retries)
     */
    static constexpr int32 MaxRetryCount = 50;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★敵タグ管理
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * 一度だけ敵タグをASCに付与
     */
    void ApplyEnemyGameplayTags();
    bool bEnemyTagsApplied = false;
};
