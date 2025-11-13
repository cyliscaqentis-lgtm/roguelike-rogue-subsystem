// GA_TurnActionBase.cpp
#include "Abilities/GA_TurnActionBase.h"
#include "Utility/RogueGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

UGA_TurnActionBase::UGA_TurnActionBase()
{
    TimeoutTag = RogueGameplayTags::Effect_Turn_AbilityTimeout;
    StartEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Started;
    CompletionEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;
}

UGA_TurnActionBase::UGA_TurnActionBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TimeoutTag = RogueGameplayTags::Effect_Turn_AbilityTimeout;
    StartEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Started;
    CompletionEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;
}

void UGA_TurnActionBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    bCompletionSent = false;

    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    GetAbilitySystemComponentFromActorInfo()->AddLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
    SendStartEvent();

    if (MaxExecutionTime > 0.0f)
    {
        GetWorld()->GetTimerManager().SetTimer(
            TimeoutHandle,
            this,
            &UGA_TurnActionBase::OnTimeout,
            MaxExecutionTime
        );
    }

    UE_LOG(LogTemp, Verbose, TEXT("[GA_TurnActionBase] %s activated"), *GetName());
}

void UGA_TurnActionBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (TimeoutHandle.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(TimeoutHandle);
        TimeoutHandle.Invalidate();
    }

    GetAbilitySystemComponentFromActorInfo()->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

    if (!bCompletionSent)
    {
        SendCompletionEvent(false);
        bCompletionSent = true;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[GA_TurnActionBase] %s ended (Cancelled: %d)"), *GetName(), bWasCancelled);
}

void UGA_TurnActionBase::OnTimeout()
{
    UE_LOG(LogTemp, Error, TEXT("[GA_TurnActionBase] %s timed out!"), *GetName());

    GetAbilitySystemComponentFromActorInfo()->AddLooseGameplayTag(TimeoutTag);

    if (!bCompletionSent)
    {
        SendCompletionEvent(true);
        bCompletionSent = true;
    }

    CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGA_TurnActionBase::SendCompletionEvent(bool bTimedOut)
{
    // ★★★ DIAGNOSTIC (2025-11-13): 完了イベント送信を追跡 ★★★
    UE_LOG(LogTemp, Error,
        TEXT("[GA_TurnActionBase] SendCompletionEvent CALLED: Actor=%s, TimedOut=%d, Tag=%s"),
        *GetNameSafe(GetAvatarActorFromActorInfo()), bTimedOut, *CompletionEventTag.ToString());

    FGameplayEventData Payload;
    Payload.Instigator = GetAvatarActorFromActorInfo();
    Payload.OptionalObject = this;
    Payload.EventMagnitude = bTimedOut ? -1.0f : 1.0f;

    AActor* InstigatorActor = GetAvatarActorFromActorInfo();
    if (InstigatorActor)
    {
        UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InstigatorActor);
        if (ASC)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[GA_TurnActionBase] Calling ASC->HandleGameplayEvent with tag=%s"),
                *CompletionEventTag.ToString());

            ASC->HandleGameplayEvent(CompletionEventTag, &Payload);

            UE_LOG(LogTemp, Error,
                TEXT("[GA_TurnActionBase] HandleGameplayEvent RETURNED"));
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[GA_TurnActionBase] SendCompletionEvent: ASC is NULL!"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[GA_TurnActionBase] SendCompletionEvent: InstigatorActor is NULL!"));
    }
}

void UGA_TurnActionBase::SendStartEvent()
{
    if (!StartEventTag.IsValid())
    {
        return;
    }

    AActor* InstigatorActor = GetAvatarActorFromActorInfo();
    if (!InstigatorActor)
    {
        return;
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InstigatorActor))
    {
        FGameplayEventData Payload;
        Payload.Instigator = InstigatorActor;
        Payload.OptionalObject = this;
        Payload.EventTag = StartEventTag;
        Payload.EventMagnitude = 1.0f;
        ASC->HandleGameplayEvent(StartEventTag, &Payload);
    }
}
