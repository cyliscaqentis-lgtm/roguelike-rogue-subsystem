// EnemyUnitBase.h
// Location: Rogue/Character/EnemyUnitBase.h

#pragma once

#include "CoreMinimal.h"
#include "UnitBase.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemInterface.h"          // 既に実装済みなら不要
#include "AI/Enemy/EnemyThinkerBase.h"
#include "EnemyUnitBase.generated.h"

// ===== 前方宣言 =====
class ULyraPawnData;
class ULyraAbilitySystemComponent;

/**
 * Enemy unit class with Lyra GAS initialization
 * Uses Pawn-owned ASC (not PlayerState) for simpler enemy AI setup
 */
UCLASS()
class LYRAGAME_API AEnemyUnitBase : public AUnitBase
{
    GENERATED_BODY()

public:
    AEnemyUnitBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|AI")
    TObjectPtr<UEnemyThinkerBase> EnemyThinkerComponent;

    /** Assigns PawnData from code before possession */
    void SetEnemyPawnData(ULyraPawnData* InPawnData);
    ULyraPawnData* GetEnemyPawnData() const { return EnemyPawnData; }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ IAbilitySystemInterface Override
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * Returns the AbilitySystemComponent owned by this Pawn
     * Overrides UnitBase which returns PlayerState's ASC
     */
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

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
     * Called after all components have been initialized
     * Registers ASC initialization callbacks
     */
    virtual void PostInitializeComponents() override;

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
    // ★AbilitySystem初期化コールバック
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * Called when the ability system is initialized (from PawnExtComponent)
     * Initializes HealthComponent with the ASC
     */
    virtual void OnAbilitySystemInitialized();

    /**
     * Called when the ability system is uninitialized
     * Uninitializes HealthComponent
     */
    virtual void OnAbilitySystemUninitialized();

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

    /**
     * If true, BeginPlay will NOT auto-spawn a controller
     * Used by UnitManager to set PawnData before controller possession
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Spawn")
    bool bDeferredControllerSpawn = false;

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

    /**
     * PawnDataに含まれるAbilitySetを一度だけ付与
     */
    void GrantAbilitySetsIfNeeded();
    bool bGrantedAbilitySets = false;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ Ability System Component (Pawn-owned)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * The AbilitySystemComponent owned by this enemy pawn
     * This is separate from player units which use PlayerState's ASC
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AbilitySystem", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULyraAbilitySystemComponent> AbilitySystemComponent;
};
