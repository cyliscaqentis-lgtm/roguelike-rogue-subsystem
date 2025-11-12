// GA_TurnActionBase.cpp
#include "Abilities/GA_TurnActionBase.h"
#include "Utility/RogueGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ProjectDiagnostics.h"

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

    // ★★★ FIX (2025-11-12): InProgressタグを明示的に削除してBarrier通知前にクリア ★★★
    // ActivationOwnedTagsの自動削除はSuper::EndAbility()内で行われるが、
    // 遅延する可能性があるため、SendCompletionEvent()が呼ばれる前に確実に削除する
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const int32 CountBefore = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress);
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        const int32 CountAfter = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress);

        UE_LOG(LogTurnManager, Warning,
            TEXT("[GA_TurnActionBase] ★ EndAbility: InProgress tag REMOVED BEFORE SendCompletionEvent: Actor=%s, Count %d → %d"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), CountBefore, CountAfter);
    }

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
