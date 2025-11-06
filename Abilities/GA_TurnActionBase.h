// GA_TurnActionBase.h
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"
#include "GA_TurnActionBase.generated.h"

UCLASS(Abstract)
class LYRAGAME_API UGA_TurnActionBase : public ULyraGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_TurnActionBase();
    UGA_TurnActionBase(const FObjectInitializer& ObjectInitializer);  // ← 追加

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
    UPROPERTY(EditDefaultsOnly, Category = "Turn|Timeout")
    float MaxExecutionTime = 5.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Turn|Timeout")
    FGameplayTag TimeoutTag;

    UPROPERTY(EditDefaultsOnly, Category = "Turn|Event")
    FGameplayTag StartEventTag;

    UPROPERTY(EditDefaultsOnly, Category = "Turn|Event")
    FGameplayTag CompletionEventTag;

    UFUNCTION()
    void OnTimeout();

    UFUNCTION(BlueprintCallable, Category = "Turn")
    virtual void SendCompletionEvent(bool bTimedOut = false);

    UFUNCTION(BlueprintCallable, Category = "Turn")
    void SendStartEvent();

private:
    FTimerHandle TimeoutHandle;
    bool bCompletionSent = false;
};
