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

    // ★★★ FIX: Use ActivationOwnedTags for automatic GAS management (2025-11-11) ★★★
    ActivationOwnedTags.AddTag(RogueGameplayTags::State_Ability_Executing);
}

UGA_TurnActionBase::UGA_TurnActionBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TimeoutTag = RogueGameplayTags::Effect_Turn_AbilityTimeout;
    StartEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Started;
    CompletionEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;

    // ★★★ FIX: Use ActivationOwnedTags for automatic GAS management (2025-11-11) ★★★
    ActivationOwnedTags.AddTag(RogueGameplayTags::State_Ability_Executing);
}

void UGA_TurnActionBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    bCompletionSent = false;

    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // ★★★ REMOVED: Manual tag management (2025-11-11) ★★★
    // ActivationOwnedTags を使用するため、GASが自動でタグを管理する
    // Super::ActivateAbility() の中で State_Ability_Executing が自動追加される

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

    // ★★★ REMOVED: Manual tag removal (2025-11-11) ★★★
    // ActivationOwnedTags を使用するため、GASが自動でタグを削除する
    // Super::EndAbility() の中で State_Ability_Executing が自動削除される

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
            ASC->HandleGameplayEvent(CompletionEventTag, &Payload);
        }
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
