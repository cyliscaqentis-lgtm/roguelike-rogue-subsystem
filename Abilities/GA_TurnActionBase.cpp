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
    // CodeRevision: INC-2025-1142-R1 (Clear the cached TurnId before activation so completion payloads never reuse stale IDs) (2025-11-20 12:30)
    CompletionEventTurnId = INDEX_NONE;

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

    // 親クラスのEndAbilityを呼ぶ (ActivationOwnedTagsの削除など)
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

    // Defensive: 念のため、EndAbility後も残っている可能性のあるタグを強制削除
    // この時点では、ASCが有効であることを保証できない場合があるため、チェックを強化する。
    UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Defensive check: Attempting to get ASC after Super::EndAbility"));
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (ASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Defensive check: ASC found, checking tags"));
        
        bool bHasExecuting = ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing);
        bool bHasInProgress = ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress);
        
        UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Defensive check: State_Ability_Executing=%d, State_Action_InProgress=%d"), 
            bHasExecuting ? 1 : 0, bHasInProgress ? 1 : 0);
        
        if (bHasExecuting)
        {
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
            UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Defensive: Cleared lingering State_Ability_Executing on %s"), *GetNameSafe(GetAvatarActorFromActorInfo()));
        }
        if (bHasInProgress)
        {
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
            UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Defensive: Cleared lingering State_Action_InProgress on %s"), *GetNameSafe(GetAvatarActorFromActorInfo()));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GA_TurnActionBase] Defensive check: ASC is NULL after Super::EndAbility! Cannot clear tags."));
    }

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
    // CodeRevision: INC-2025-1142-R1 (Guard completion events so only the correct TurnId is broadcast) (2025-11-20 12:30)
    if (CompletionEventTurnId == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GA_TurnActionBase] Completion event skipped: TurnId is unset (TimedOut=%d)"),
            bTimedOut ? 1 : 0);
        return;
    }

    AActor* InstigatorActor = GetAvatarActorFromActorInfo();
    if (!InstigatorActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_TurnActionBase] Cannot send completion event: Avatar actor missing"));
        return;
    }

    if (bTimedOut)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GA_TurnActionBase] Sending timed-out completion event for Turn=%d"),
            CompletionEventTurnId);
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InstigatorActor))
    {
        FGameplayEventData Payload;
        Payload.Instigator = InstigatorActor;
        Payload.OptionalObject = this;
        Payload.EventMagnitude = static_cast<float>(CompletionEventTurnId);
        Payload.EventTag = CompletionEventTag;
        ASC->HandleGameplayEvent(CompletionEventTag, &Payload);
    }
}

void UGA_TurnActionBase::SetCompletionEventTurnId(int32 TurnId)
{
    CompletionEventTurnId = TurnId;
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
