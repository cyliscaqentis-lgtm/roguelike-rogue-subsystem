// GA_AttackBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GA_AttackBase.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogAttackAbility, Log, All);

UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_AttackBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_AttackBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    // ★★★ FIX (2025-11-12): BP上書き対策 - PostInitPropertiesで強制設定 ★★★
    virtual void PostInitProperties() override;

    // ★★★ Barrier統合 (2025-11-12) ★★★
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack")
    float BaseDamage = 10.0f;

    /** 攻撃射程（マス数）- 斜め移動を考慮したマンハッタン距離 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack")
    int32 RangeInTiles = 1;

    UFUNCTION(BlueprintNativeEvent, Category = "Attack")
    void ExecuteAttack(AActor* Target);
    virtual void ExecuteAttack_Implementation(AActor* Target);

    UFUNCTION(BlueprintNativeEvent, Category = "Attack")
    float CalculateDamage(AActor* Target) const;
    virtual float CalculateDamage_Implementation(AActor* Target) const;

    UFUNCTION(BlueprintNativeEvent, Category = "Attack")
    void OnAttackHit(AActor* Target, float DamageDealt);
    virtual void OnAttackHit_Implementation(AActor* Target, float DamageDealt);

    UFUNCTION(BlueprintNativeEvent, Category = "Attack")
    void OnAttackMiss(AActor* Target);
    virtual void OnAttackMiss_Implementation(AActor* Target);

public:
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack")
    void OnAttackCompleted();

    UFUNCTION(BlueprintCallable, Category = "Attack")
    TArray<AActor*> FindTargetsInRange(float SearchRange = 0.0f) const;

    // 射程取得（マス数）
    UFUNCTION(BlueprintPure, Category = "Attack")
    int32 GetRangeInTiles() const { return RangeInTiles; }

private:
    // ★★★ Barrier統合用の内部状態 (2025-11-12) ★★★
    FGuid AttackActionId;
    int32 AttackTurnId = -1;
    bool bBarrierRegistered = false;

    // CodeRevision: INC-2025-1144-R1 (Idempotency guard moved into GA_AttackBase so duplicate EndAbility calls skip barrier logic) (2025-11-20 13:30)
    bool bAbilityHasEnded = false;
};
