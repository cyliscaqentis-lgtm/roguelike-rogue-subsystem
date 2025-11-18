// Copyright Epic Games, Inc. All Rights Reserved.

// AttackPhaseExecutorSubsystem.cpp

#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Utility/RogueGameplayTags.h"
#include "Turn/TurnActionBarrierSubsystem.h"

DEFINE_LOG_CATEGORY(LogAttackPhase);

//--------------------------------------------------------------------------
// Subsystem Lifecycle
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentIndex = 0;
	TurnId = -1;
	Queue.Empty();

	UE_LOG(LogAttackPhase, Log, TEXT("[AttackPhaseExecutor] Initialized"));
}

void UAttackPhaseExecutorSubsystem::Deinitialize()
{
	UnbindCurrentASC();

	Queue.Empty();
	CurrentIndex = 0;
	TurnId = -1;

	UE_LOG(LogAttackPhase, Log, TEXT("[AttackPhaseExecutor] Deinitialized"));
	Super::Deinitialize();
}

//--------------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::BeginSequentialAttacks(
	const TArray<FResolvedAction>& AttackActions,
	int32 InTurnId)
{
	// Ensure we are not still bound to a previous ASC
	UnbindCurrentASC();

	Queue = AttackActions;
	TurnId = InTurnId;
	CurrentIndex = 0;

	UE_LOG(LogAttackPhase, Log,
		TEXT("[Turn %d] BeginSequentialAttacks: %d attacks queued"),
		TurnId, Queue.Num());

	if (Queue.Num() == 0)
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] No attacks to execute"), TurnId);

		OnFinished.Broadcast(TurnId);
		return;
	}

	// Pre-register all attacks in the TurnActionBarrier so that abilities
	// can reference a stable ActionId during their lifetime.
	if (UWorld* World = GetWorld())
	{
		if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
		{
			UE_LOG(LogAttackPhase, Log,
				TEXT("[Turn %d] Pre-registering %d attacks with TurnActionBarrier"),
				TurnId, Queue.Num());

			for (int32 i = 0; i < Queue.Num(); ++i)
			{
				FResolvedAction& Action = Queue[i];
				if (Action.Actor.IsValid())
				{
					AActor* Attacker = Action.Actor.Get();
					FGuid ActionId = Barrier->RegisterAction(Attacker, TurnId);
					Action.BarrierActionId = ActionId;

					UE_LOG(LogAttackPhase, Verbose,
						TEXT("[Turn %d] Pre-registered attack %d/%d: %s (ActionId=%s)"),
						TurnId, i + 1, Queue.Num(),
						*GetNameSafe(Attacker), *ActionId.ToString());
				}
				else
				{
					UE_LOG(LogAttackPhase, Warning,
						TEXT("[Turn %d] Skip pre-registering attack %d/%d: Actor is invalid"),
						TurnId, i + 1, Queue.Num());
				}
			}
		}
		else
		{
			UE_LOG(LogAttackPhase, Error,
				TEXT("[Turn %d] TurnActionBarrierSubsystem not found! Cannot pre-register attacks"),
				TurnId);
		}
	}

	DispatchNext();
}

void UAttackPhaseExecutorSubsystem::NotifyAttackCompleted(AActor* Attacker)
{
	if (Attacker)
	{
		UE_LOG(LogAttackPhase, Verbose,
			TEXT("[Turn %d] NotifyAttackCompleted: %s"),
			TurnId, *GetNameSafe(Attacker));
	}

	// Reuse the same completion path as the ASC event callback
	OnAbilityCompleted(nullptr);
}

//--------------------------------------------------------------------------
// Internal execution pipeline
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::DispatchNext()
{
	if (CurrentIndex >= Queue.Num())
	{
		UE_LOG(LogAttackPhase, Log,
			TEXT("[Turn %d] All %d attacks completed"),
			TurnId, Queue.Num());

		UnbindCurrentASC();
		OnFinished.Broadcast(TurnId);
		return;
	}

	const FResolvedAction& Action = Queue[CurrentIndex];

	if (!Action.Actor.IsValid())
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] Invalid actor at index %d, skipping"),
			TurnId, CurrentIndex);

		++CurrentIndex;
		DispatchNext();
		return;
	}

	AActor* Attacker = Action.Actor.Get();
	UAbilitySystemComponent* ASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);

	if (!ASC)
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] %s has no ASC, skipping"),
			TurnId, *GetNameSafe(Attacker));

		++CurrentIndex;
		DispatchNext();
		return;
	}

	BindASC(ASC);

	FGameplayEventData Payload;
	Payload.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
	Payload.Instigator = Attacker;
	Payload.TargetData = Action.TargetData;

	UE_LOG(LogAttackPhase, Warning,
		TEXT("[Turn %d] %s: ASC has %d activatable abilities"),
		TurnId, *GetNameSafe(Attacker), ASC->GetActivatableAbilities().Num());

	for (int32 i = 0; i < ASC->GetActivatableAbilities().Num(); ++i)
	{
		const FGameplayAbilitySpec& Spec = ASC->GetActivatableAbilities()[i];
		if (Spec.Ability)
		{
			UE_LOG(LogAttackPhase, Warning,
				TEXT("  [%d] Ability=%s (Class=%s), Level=%d, InputID=%d, IsActive=%d"),
				i,
				*Spec.Ability->GetName(),
				*Spec.Ability->GetClass()->GetName(),
				Spec.Level,
				Spec.InputID,
				Spec.IsActive());

			const FGameplayTagContainer& AssetTags = Spec.Ability->GetAssetTags();
			UE_LOG(LogAttackPhase, Warning,
				TEXT("    AssetTags: %s"),
				*AssetTags.ToStringSimple());
		}
	}

	UE_LOG(LogAttackPhase, Warning,
		TEXT("[Turn %d] Sending GameplayEvent: Tag=%s (to %s)"),
		TurnId,
		TEXT("GameplayEvent.Intent.Attack"),
		*GetNameSafe(Attacker));

	const int32 TriggeredCount = ASC->HandleGameplayEvent(Payload.EventTag, &Payload);

	if (TriggeredCount > 0)
	{
		UE_LOG(LogAttackPhase, Log,
			TEXT("[Turn %d] Dispatched attack %d/%d: %s (Tag=%s)"),
			TurnId,
			CurrentIndex + 1,
			Queue.Num(),
			*GetNameSafe(Attacker),
			TEXT("GameplayEvent.Intent.Attack"));
	}
	else
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] %s: %s failed to trigger any abilities"),
			TurnId,
			*GetNameSafe(Attacker),
			TEXT("GameplayEvent.Intent.Attack"));

		// No ability fired; treat as completed and move on.
		UnbindCurrentASC();
		++CurrentIndex;
		DispatchNext();
	}
}

//--------------------------------------------------------------------------
// ASC binding helpers
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::BindASC(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		UE_LOG(LogAttackPhase, Error, TEXT("[BindASC] ASC is null"));
		return;
	}

	// Ensure we are not still bound to an old ASC
	UnbindCurrentASC();

	WaitingASC = ASC;

	AbilityCompletedHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(
		RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed
	).AddUObject(this, &UAttackPhaseExecutorSubsystem::OnAbilityCompleted);

	UE_LOG(LogAttackPhase, Verbose,
		TEXT("[Turn %d] Bound to ASC: %s"),
		TurnId, *GetNameSafe(ASC->GetOwner()));
}

void UAttackPhaseExecutorSubsystem::UnbindCurrentASC()
{
	if (WaitingASC.IsValid() && AbilityCompletedHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = WaitingASC.Get())
		{
			ASC->GenericGameplayEventCallbacks.FindOrAdd(
				RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed
			).Remove(AbilityCompletedHandle);

			UE_LOG(LogAttackPhase, Verbose,
				TEXT("[Turn %d] Unbound from ASC: %s"),
				TurnId, *GetNameSafe(ASC->GetOwner()));
		}

		AbilityCompletedHandle.Reset();
	}

	WaitingASC.Reset();
}

//--------------------------------------------------------------------------
// Callbacks
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::OnAbilityCompleted(
	const FGameplayEventData* Payload)
{
	UE_LOG(LogAttackPhase, Log,
		TEXT("[Turn %d] Ability completed at index %d"),
		TurnId, CurrentIndex);

	UnbindCurrentASC();
	++CurrentIndex;
	DispatchNext();
}

//--------------------------------------------------------------------------
// Barrier integration helpers
//--------------------------------------------------------------------------

FGuid UAttackPhaseExecutorSubsystem::GetActionIdForActor(AActor* Actor) const
{
	if (!Actor)
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[GetActionIdForActor] Actor is null"));
		return FGuid();
	}

	for (const FResolvedAction& Action : Queue)
	{
		if (Action.Actor.IsValid() && Action.Actor.Get() == Actor)
		{
			return Action.BarrierActionId;
		}
	}

	UE_LOG(LogAttackPhase, Warning,
		TEXT("[GetActionIdForActor] Actor %s not found in attack queue"),
		*GetNameSafe(Actor));

	return FGuid();
}
