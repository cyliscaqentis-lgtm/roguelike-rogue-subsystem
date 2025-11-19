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

    // CodeRevision: INC-2025-1142-R1 (Track the TurnId associated with completion events so they carry valid payloads) (2025-11-20 12:30)
    void SetCompletionEventTurnId(int32 TurnId);

private:
    FTimerHandle TimeoutHandle;
    bool bCompletionSent = false;

    // CodeRevision: INC-2025-1142-R1 (Keep the most recent TurnId for completion notifications to avoid stale payloads) (2025-11-20 12:30)
    int32 CompletionEventTurnId = INDEX_NONE;

};
