// GA_WaitBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GA_WaitBase.generated.h"

/**
 * UGA_WaitBase
 * 待機アビリティの基底クラス（抽象）
 * ターンを消費して待機（何もしない）
 */
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_WaitBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_WaitBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

    // 待機処理（Blueprint側でオーバーライド可能）
    UFUNCTION(BlueprintNativeEvent, Category = "Wait")
    void ExecuteWait();
    virtual void ExecuteWait_Implementation();

public:
    // 待機完了通知（Blueprint側から呼び出し専用・サーバーのみ）
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wait")
    void OnWaitCompleted();

private:
    void RegisterBarrierAction();
    void CompleteBarrierAction();
    void NotifyTurnManagerStarted();
    void NotifyTurnManagerCompleted();

private:
    int32 WaitTurnId = INDEX_NONE;
    FGuid WaitActionId;
    bool bBarrierActionRegistered = false;
    bool bBarrierActionCompleted = false;
    bool bTurnManagerNotified = false;
};
