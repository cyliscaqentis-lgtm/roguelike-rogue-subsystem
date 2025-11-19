// GA_WaitBase.cpp
#include "Abilities/GA_WaitBase.h"

#include "Utility/RogueGameplayTags.h"
#include "Turn/TurnActionBarrierSubsystem.h"
#include "Turn/GameTurnManagerBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "../Utility/ProjectDiagnostics.h"

UGA_WaitBase::UGA_WaitBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;

    MaxExecutionTime = 2.0f;

    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Wait);
    SetAssetTags(Tags);

    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Ability_Executing);
    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Action_InProgress);
    ActivationOwnedTags.AddTag(RogueGameplayTags::State_Action_InProgress);
}

void UGA_WaitBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    WaitTurnId = INDEX_NONE;
    WaitActionId = FGuid();
    bBarrierActionRegistered = false;
    bBarrierActionCompleted = false;
    bTurnManagerNotified = false;

    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    RegisterBarrierAction();
    NotifyTurnManagerStarted();

    DIAG_LOG(Verbose, TEXT("[GA_WaitBase] Wait ability activated"));

    ExecuteWait();
}

void UGA_WaitBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (HasAuthority(&ActivationInfo))
    {
        CompleteBarrierAction();
        NotifyTurnManagerCompleted();
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

    WaitTurnId = INDEX_NONE;
    WaitActionId = FGuid();
    bBarrierActionRegistered = false;
    bBarrierActionCompleted = false;
    bTurnManagerNotified = false;
}

void UGA_WaitBase::ExecuteWait_Implementation()
{
    OnWaitCompleted();
}

void UGA_WaitBase::OnWaitCompleted()
{
    if (!HasAuthority(&CurrentActivationInfo))
    {
        DIAG_LOG(Warning, TEXT("[GA_WaitBase] OnWaitCompleted called on client (ignored)"));
        return;
    }

    DIAG_LOG(Verbose, TEXT("[GA_WaitBase] Wait completed - ending ability"));

    // EndAbility内でCompleteBarrierActionとNotifyTurnManagerCompletedが呼ばれるため、
    // ここではEndAbilityのみを呼ぶ
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_WaitBase::RegisterBarrierAction()
{
    if (!HasAuthority(&CurrentActivationInfo))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
    {
        if (AActor* Avatar = GetAvatarActorFromActorInfo())
        {
            WaitTurnId = Barrier->GetCurrentTurnId();
            // CodeRevision: INC-2025-1142-R1 (Keep the wait completion event aligned with the current TurnId) (2025-11-20 12:30)
            SetCompletionEventTurnId(WaitTurnId);
            WaitActionId = Barrier->RegisterAction(Avatar, WaitTurnId);
            bBarrierActionRegistered = WaitActionId.IsValid();

            DIAG_LOG(Verbose,
                TEXT("[GA_WaitBase] Barrier register: Turn=%d ActionId=%s Registered=%d"),
                WaitTurnId,
                *WaitActionId.ToString(),
                bBarrierActionRegistered ? 1 : 0);
        }
    }
    else
    {
        DIAG_LOG(Warning, TEXT("[GA_WaitBase] Barrier subsystem not found; wait action untracked"));
    }
}

void UGA_WaitBase::CompleteBarrierAction()
{
    if (!HasAuthority(&CurrentActivationInfo))
    {
        return;
    }

    if (!bBarrierActionRegistered || bBarrierActionCompleted)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
    {
        if (AActor* Avatar = GetAvatarActorFromActorInfo())
        {
            Barrier->CompleteAction(Avatar, WaitTurnId, WaitActionId);
            DIAG_LOG(Verbose,
                TEXT("[GA_WaitBase] Barrier complete: Turn=%d ActionId=%s"),
                WaitTurnId,
                *WaitActionId.ToString());
        }
    }
    else
    {
        DIAG_LOG(Warning, TEXT("[GA_WaitBase] Barrier subsystem not found during completion"));
    }

    bBarrierActionCompleted = true;
}

void UGA_WaitBase::NotifyTurnManagerStarted()
{
    // NOTE: NotifyAbilityStarted() was removed - Barrier subsystem handles this now
    // This function is kept for compatibility but does nothing
    bTurnManagerNotified = true;
}

void UGA_WaitBase::NotifyTurnManagerCompleted()
{
    // NOTE: OnAbilityCompleted() was removed - Barrier subsystem handles this now
    // This function is kept for compatibility but does nothing
    bTurnManagerNotified = true;
}
