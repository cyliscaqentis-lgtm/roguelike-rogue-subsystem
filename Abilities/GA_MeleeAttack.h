#pragma once

#include "CoreMinimal.h"
#include "GA_AttackBase.h"
#include "GA_MeleeAttack.generated.h"

// 前方宣言
class UGameplayEffect;
class AUnitBase;

UCLASS(Blueprintable)
class LYRAGAME_API UGA_MeleeAttack : public UGA_AttackBase
{
    GENERATED_BODY()

public:
    UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility オーバーライド
    //--------------------------------------------------------------------------

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

protected:
    //--------------------------------------------------------------------------
    // 設定パラメータ
    //--------------------------------------------------------------------------

    /** 近接攻撃のGameplayEffect */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    TSubclassOf<UGameplayEffect> MeleeAttackEffect;

    /** 近接攻撃モンタージュ */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack|Animation")
    TObjectPtr<UAnimMontage> MeleeAttackMontage;

    /** ダメージ量（SetByCallerで設定） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    float Damage = 28.0f;

    //--------------------------------------------------------------------------
    // 内部状態
    //--------------------------------------------------------------------------

    /** 攻撃対象ユニット */
    UPROPERTY(BlueprintReadOnly, Category = "MeleeAttack|State")
    TObjectPtr<AActor> TargetUnit;

    /** 攻撃前の向き（攻撃完了後に元に戻すため） */
    FRotator OriginalRotation;

    //--------------------------------------------------------------------------
    // C++内部実装
    //--------------------------------------------------------------------------

    /** 隣接する敵ユニットを取得 */
    AActor* FindAdjacentTarget();

    /** GameplayEffectを適用してダメージを与える */
    void ApplyDamageToTarget(AActor* Target);

    /** モンタージュ完了コールバック */
    UFUNCTION()
    void OnMontageCompleted();

    /** モンタージュブレンドアウト */
    UFUNCTION()
    void OnMontageBlendOut();

    /** モンタージュ中断 */
    UFUNCTION()
    void OnMontageCancelled();

    //--------------------------------------------------------------------------
    // Blueprint拡張ポイント
    //--------------------------------------------------------------------------

    /**
     * モンタージュ再生（Blueprint実装可能）
     *
     * デフォルトではC++でPlayMontageAndWaitを使用。
     * プロジェクト固有のアニメーション制御が必要な場合はBPでオーバーライド。
     */
    UFUNCTION(BlueprintNativeEvent, Category = "MeleeAttack|Animation")
    void PlayAttackMontage();
    virtual void PlayAttackMontage_Implementation();

private:
    /** モンタージュタスクのハンドル */
    FDelegateHandle MontageCompletedHandle;
    FDelegateHandle MontageBlendOutHandle;
    FDelegateHandle MontageCancelledHandle;

    /** 入力無効化フラグ */
    bool bInputDisabled = false;
    
    // ★★★ TurnManagerのキャッシュ ★★★
    UPROPERTY(Transient)
    mutable TWeakObjectPtr<class AGameTurnManagerBase> CachedTurnManager;
    
    /** TurnManager取得ヘルパー */
    class AGameTurnManagerBase* GetTurnManager() const;
};
