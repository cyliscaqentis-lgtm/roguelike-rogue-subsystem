// =============================================================================
// EnemyThinkerBase.h
// 敵AIの思考ロジックを管理するコンポーネント（マス単位対応版）
// =============================================================================

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"
#include "Turn/TurnSystemTypes.h"
#include "EnemyThinkerBase.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UEnemyThinkerBase : public UActorComponent
{
    GENERATED_BODY()

public:
    UEnemyThinkerBase();

    //--------------------------------------------------------------------------
    // 外部から変更可能なパラメータ（マス単位）
    //--------------------------------------------------------------------------

    /** 攻撃射程（マス数） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior",
        meta = (ClampMin = "1", ClampMax = "10"))
    int32 AttackRangeInTiles = 1;

    /** 敵の速度優先度（同時移動時の順序決定に使用） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float DefaultSpeedPriority = 0.5f;

    //--------------------------------------------------------------------------
    // AI思考関数（★★★ public ★★★）
    //--------------------------------------------------------------------------

    /**
     * v2.2 Final Edition用：意思決定
     * TurnCorePhaseManagerから呼び出される
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|AI")
    FEnemyIntent DecideIntent();
    virtual FEnemyIntent DecideIntent_Implementation();

    /**
     * Blueprint拡張可能な意図計算（旧版との互換性）
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI")
    FEnemyIntent ComputeIntent(const FEnemyObservation& Observation);
    virtual FEnemyIntent ComputeIntent_Implementation(const FEnemyObservation& Observation);

protected:
    //--------------------------------------------------------------------------
    // ライフサイクル
    //--------------------------------------------------------------------------

    virtual void BeginPlay() override;
    
    //--------------------------------------------------------------------------
    // キャッシュ
    //--------------------------------------------------------------------------
    
    /** PathFinderのキャッシュ（BeginPlayで取得） */
    UPROPERTY(Transient)
    TWeakObjectPtr<class AGridPathfindingLibrary> CachedPathFinder;

    //--------------------------------------------------------------------------
    // アビリティ射程取得（マス単位）
    //--------------------------------------------------------------------------

    /** 敵が持つ攻撃アビリティの最大射程を取得（マス数） */
    UFUNCTION(BlueprintCallable, Category = "AI|Ability")
    int32 GetMaxAttackRangeInTiles() const;

    /** 指定した距離で使用可能な攻撃アビリティを取得 */
    UFUNCTION(BlueprintCallable, Category = "AI|Ability")
    FGameplayTag GetAttackAbilityForRange(int32 DistanceInTiles) const;

    /** 敵のAbilitySystemComponentを取得 */
    UFUNCTION(BlueprintPure, Category = "AI|Ability")
    UAbilitySystemComponent* GetOwnerAbilitySystemComponent() const;

    //--------------------------------------------------------------------------
    // 内部ヘルパー
    //--------------------------------------------------------------------------

    /** 現在のグリッド座標を取得 */
    UFUNCTION(BlueprintCallable, Category = "AI|Grid")
    FIntPoint GetCurrentGridPosition() const;

    /** プレイヤーへの距離を取得（マス数） */
    UFUNCTION(BlueprintCallable, Category = "AI|Grid")
    int32 GetDistanceToPlayer() const;
};
