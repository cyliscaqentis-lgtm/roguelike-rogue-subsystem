#pragma once

#include "CoreMinimal.h"
#include "GA_AttackBase.h"
#include "GA_MeleeAttack.generated.h"

// 前方宣言
class UGameplayEffect;
class AUnitBase;
class AGridPathfindingLibrary;

UCLASS(Blueprintable)
class LYRAGAME_API UGA_MeleeAttack : public UGA_AttackBase
{
    GENERATED_BODY()

public:
    UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility オーバ�EライチE
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

    /** 近接攻撁E�EGameplayEffect */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    TSubclassOf<UGameplayEffect> MeleeAttackEffect;

    /** 近接攻撁E��ンタージュ */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack|Animation")
    TObjectPtr<UAnimMontage> MeleeAttackMontage;

    /** ダメージ量！EetByCallerで設定！E*/
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    float Damage = 28.0f;

    //--------------------------------------------------------------------------
    // 冁E��状慁E
    //--------------------------------------------------------------------------

    /** 攻撁E��象ユニッチE*/
    UPROPERTY(BlueprintReadOnly, Category = "MeleeAttack|State")
    TObjectPtr<AActor> TargetUnit;

    //--------------------------------------------------------------------------
    // C++冁E��実裁E
    //--------------------------------------------------------------------------

    /** 隣接する敵ユニットを取征E*/
    AActor* FindAdjacentTarget();

    /** GameplayEffectを適用してダメージを与えめE*/
    void ApplyDamageToTarget(AActor* Target);

    /** モンタージュ完亁E��ールバック */
    UFUNCTION()
    void OnMontageCompleted();

    /** モンタージュブレンドアウチE*/
    UFUNCTION()
    void OnMontageBlendOut();

    /** モンタージュ中断 */
    UFUNCTION()
    void OnMontageCancelled();

    //--------------------------------------------------------------------------
    // Blueprint拡張ポインチE
    //--------------------------------------------------------------------------

    /**
     * モンタージュ再生�E�Elueprint実裁E��能�E�E
     *
     * チE��ォルトではC++でPlayMontageAndWaitを使用、E
     * プロジェクト固有�Eアニメーション制御が忁E��な場合�EBPでオーバ�Eライド、E
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
    
    // ☁E�E☁ETurnManagerのキャチE��ュ ☁E�E☁E
    UPROPERTY(Transient)
    mutable TWeakObjectPtr<class AGameTurnManagerBase> CachedTurnManager;
    
    /** TurnManager取得�Eルパ�E */
    class AGameTurnManagerBase* GetTurnManager() const;

    /** ���߂̃^�[�Q�b�g�ʒu�L���b�V�� */
    FVector CachedTargetLocation = FVector::ZeroVector;
    FIntPoint CachedTargetCell = FIntPoint(-1, -1);
    bool bHasCachedTargetCell = false;

    /** �^�[�Q�b�g�ʒu�L���b�V�����X�V */
    void UpdateCachedTargetLocation(const FVector& Location, const FIntPoint& ReservedCell, const AGridPathfindingLibrary* GridLib);
};

