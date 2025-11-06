// GA_AttackBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GA_AttackBase.generated.h"

UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_AttackBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_AttackBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack")
    float BaseDamage = 10.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack")
    float Range = 200.0f;

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

    // 範囲取得（追加）
    UFUNCTION(BlueprintPure, Category = "Attack")
    float GetRange() const { return Range; }
};
