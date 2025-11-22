#include "Turn/TurnEnemyPhaseSubsystem.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnCorePhaseManager.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "Turn/MoveReservationSubsystem.h"
#include "Utility/RogueGameplayTags.h"
#include "Utility/TurnAuthorityUtils.h"
#include "Kismet/GameplayStatics.h"

void UTurnEnemyPhaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PhaseManager = Collection.InitializeDependency<UTurnCorePhaseManager>();
	EnemyData = Collection.InitializeDependency<UEnemyTurnDataSubsystem>();
	AttackExecutor = Collection.InitializeDependency<UAttackPhaseExecutorSubsystem>();
}

void UTurnEnemyPhaseSubsystem::Deinitialize()
{
	PhaseManager = nullptr;
	EnemyData = nullptr;
	AttackExecutor = nullptr;
	Super::Deinitialize();
}

void UTurnEnemyPhaseSubsystem::EnterSequentialMode()
{
	bSequentialModeActive = true;
	bSequentialMovePhaseStarted = false;
	bIsInMoveOnlyPhase = false;
}

void UTurnEnemyPhaseSubsystem::ResetSequentialMode()
{
	bSequentialModeActive = false;
	bSequentialMovePhaseStarted = false;
	bIsInMoveOnlyPhase = false;
}

void UTurnEnemyPhaseSubsystem::MarkSequentialMoveOnlyPhase()
{
	bSequentialModeActive = true;
	bSequentialMovePhaseStarted = true;
	bIsInMoveOnlyPhase = true;
}

void UTurnEnemyPhaseSubsystem::ExecuteEnemyPhase(AGameTurnManagerBase* TurnManager, int32 TurnId)
{
	if (!TurnManager) return;

	UTurnCorePhaseManager* LocalPhaseManager = nullptr;
	UEnemyTurnDataSubsystem* LocalEnemyData = nullptr;
	if (!ResolveDependencies(TurnId, TEXT("ExecuteEnemyPhase"), LocalPhaseManager, LocalEnemyData))
	{
		TurnManager->EndEnemyTurn();
		return;
	}

    // Use intents from EnemyData (source of truth)
    const TArray<FEnemyIntent>& Intents = LocalEnemyData->Intents;
    const bool bHasAttack = DoesAnyIntentHaveAttack(Intents, TurnId);

	if (bHasAttack)
	{
		LOG_TURN(Log, TEXT("[Turn %d] Mode: SEQUENTIAL"), TurnId);
		EnterSequentialMode();

		// Resolve conflicts
		TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(Intents);
		StoredResolvedActions = ResolvedActions;

		TArray<FResolvedAction> AttackActions;
		const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
		for (const FResolvedAction& Action : ResolvedActions)
		{
			if (Action.AbilityTag.MatchesTag(AttackTag) || Action.FinalAbilityTag.MatchesTag(AttackTag))
			{
				AttackActions.Add(Action);
			}
		}

		if (AttackActions.IsEmpty())
		{
            ExecuteSequentialMovePhase(TurnManager, TurnId, ResolvedActions);
        }
        else
        {
            ExecuteAttacks(TurnManager, TurnId, AttackActions);
        }
	}
	else
	{
		LOG_TURN(Log, TEXT("[Turn %d] Mode: SIMULTANEOUS"), TurnId);
		ResetSequentialMode();
		ExecuteSimultaneousPhase(TurnManager, TurnId);
	}
}

void UTurnEnemyPhaseSubsystem::ExecuteMovePhase(AGameTurnManagerBase* TurnManager, int32 TurnId, bool bSkipAttackCheck)
{
	if (!TurnManager) return;

	if (!bSkipAttackCheck)
	{
		if (EnemyData && EnemyData->HasAttackIntent())
		{
			LOG_TURN(Warning, TEXT("[Turn %d] ATTACK INTENT detected - Executing attack phase instead of move phase"), TurnId);
			ExecuteAttacks(TurnManager, TurnId);
			return;
		}
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	if (PhaseManager)
	{
		bool bCancelledPlayer = false;
		TArray<FResolvedAction> Actions = PhaseManager->ExecuteMovePhaseWithResolution(TurnId, PlayerPawn, bCancelledPlayer);

		if (bCancelledPlayer)
		{
			LOG_TURN(Warning, TEXT("[Turn %d] Player move was cancelled, ending enemy turn."), TurnId);
			TurnManager->EndEnemyTurn();
			return;
		}

		if (Actions.Num() > 0)
		{
			DispatchMoveActions(TurnManager, Actions);
		}
		else
		{
			LOG_TURN(Log, TEXT("[Turn %d] No actions were resolved, ending enemy turn."), TurnId);
			TurnManager->EndEnemyTurn();
		}
	}
	else
	{
		LOG_TURN(Error, TEXT("[Turn %d] ExecuteMovePhase: Could not find TurnCorePhaseManager subsystem!"), TurnId);
		TurnManager->EndEnemyTurn();
	}
}

void UTurnEnemyPhaseSubsystem::ExecuteSimultaneousPhase(AGameTurnManagerBase* TurnManager, int32 TurnId)
{
	if (!TurnManager) return;

	LOG_TURN(Log, TEXT("[Turn %d] ==== Simultaneous Move Phase (No Attacks) ===="), TurnId);

	UTurnCorePhaseManager* LocalPhaseManager = nullptr;
	UEnemyTurnDataSubsystem* LocalEnemyData = nullptr;
	if (!ResolveDependencies(TurnId, TEXT("ExecuteSimultaneousPhase"), LocalPhaseManager, LocalEnemyData))
	{
		return;
	}

	TArray<FEnemyIntent> AllIntents = LocalEnemyData->Intents;

	// Add player intent
	if (UWorld* World = GetWorld())
	{
		// Player intent logic removed (PendingMoveReservations is dead code)
	}

	LOG_TURN(Log, TEXT("[Turn %d] ExecuteSimultaneousPhase: Processing %d intents via CoreResolvePhase"), TurnId, AllIntents.Num());

	TArray<FResolvedAction> ResolvedActions = LocalPhaseManager->CoreResolvePhase(AllIntents);

	LOG_TURN(Log, TEXT("[Turn %d] ExecuteSimultaneousPhase: CoreResolvePhase produced %d resolved actions"), TurnId, ResolvedActions.Num());

	DispatchMoveActions(TurnManager, ResolvedActions);

	LOG_TURN(Log, TEXT("[Turn %d] ExecuteSimultaneousPhase complete - %d movements dispatched"), TurnId, ResolvedActions.Num());
}

void UTurnEnemyPhaseSubsystem::ExecuteSequentialMovePhase(AGameTurnManagerBase* TurnManager, int32 TurnId, const TArray<FResolvedAction>& CachedActions)
{
	if (!TurnManager) return;

	MarkSequentialMoveOnlyPhase();

	const TArray<FResolvedAction>& ActionsToUse = CachedActions.Num() > 0 ? CachedActions : StoredResolvedActions;

	TArray<FResolvedAction> EnemyMoveActions;
	int32 TotalActions = 0;
	int32 TotalEnemyMoves = 0;
	int32 TotalEnemyWaits = 0;
	int32 TotalEnemyNonMoves = 0;

	const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
	const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

	for (const FResolvedAction& Action : ActionsToUse)
	{
		TotalActions++;
		if (Action.AbilityTag.MatchesTag(MoveTag) || Action.FinalAbilityTag.MatchesTag(MoveTag))
		{
			EnemyMoveActions.Add(Action);
			TotalEnemyMoves++;
		}
		else if (Action.AbilityTag.MatchesTag(WaitTag))
		{
			TotalEnemyWaits++;
		}
		else
		{
			TotalEnemyNonMoves++;
		}
	}

	if (EnemyMoveActions.IsEmpty())
	{
		LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: No move actions found (Total=%d, Moves=%d, Waits=%d, Non=%d). Ending turn."),
			TurnId, TotalActions, TotalEnemyMoves, TotalEnemyWaits, TotalEnemyNonMoves);
		TurnManager->EndEnemyTurn();
		return;
	}

	TurnManager->bEnemyPhaseInProgress = true;

	LOG_TURN(Log, TEXT("[Turn %d] Dispatching %d cached enemy actions sequentially (Moves=%d, Waits=%d, NonMove=%d)"),
		TurnId, EnemyMoveActions.Num(), TotalEnemyMoves, TotalEnemyWaits, TotalEnemyNonMoves);

	DispatchMoveActions(TurnManager, EnemyMoveActions);
}

void UTurnEnemyPhaseSubsystem::ExecuteAttacks(AGameTurnManagerBase* TurnManager, int32 TurnId, const TArray<FResolvedAction>& PreResolvedAttacks)
{
	if (!TurnManager) return;

	if (!bSequentialModeActive)
	{
		EnterSequentialMode();
	}

	TArray<FResolvedAction> AttacksToExecute = PreResolvedAttacks;

	if (AttacksToExecute.IsEmpty())
	{
		// Legacy path: resolve if not provided
		if (PhaseManager && EnemyData)
		{
			TArray<FResolvedAction> Resolved = PhaseManager->CoreResolvePhase(EnemyData->Intents);
			StoredResolvedActions = Resolved;

			const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
			for (const FResolvedAction& Action : Resolved)
			{
				if (Action.AbilityTag.MatchesTag(AttackTag) || Action.FinalAbilityTag.MatchesTag(AttackTag))
				{
					AttacksToExecute.Add(Action);
				}
			}
		}
	}

	if (AttacksToExecute.IsEmpty())
	{
		LOG_TURN(Log, TEXT("[Turn %d] ExecuteAttacks: No attacks to execute. Proceeding to moves."), TurnId);
		ExecuteSequentialMovePhase(TurnManager, TurnId, StoredResolvedActions);
		return;
	}

	LOG_TURN(Log, TEXT("[Turn %d] ExecuteAttacks: Delegating %d attacks to AttackPhaseExecutor."), TurnId, AttacksToExecute.Num());

	if (AttackExecutor)
	{
		AttackExecutor->BeginSequentialAttacks(AttacksToExecute, TurnId);
	}
	else
	{
		LOG_TURN(Error, TEXT("[Turn %d] ExecuteAttacks: AttackExecutor is NULL! Skipping attacks."), TurnId);
		ExecuteSequentialMovePhase(TurnManager, TurnId, StoredResolvedActions);
	}
}

bool UTurnEnemyPhaseSubsystem::ResolveDependencies(int32 TurnId, const TCHAR* ContextLabel, UTurnCorePhaseManager*& OutPhaseManager, UEnemyTurnDataSubsystem*& OutEnemyData)
{
	OutPhaseManager = PhaseManager;
	OutEnemyData = EnemyData;

	if (!OutPhaseManager || !OutEnemyData)
	{
		LOG_TURN(Error, TEXT("[Turn %d] %s: PhaseManager or EnemyData not found"), TurnId, ContextLabel);
		return false;
	}
	return true;
}

bool UTurnEnemyPhaseSubsystem::DoesAnyIntentHaveAttack(const TArray<FEnemyIntent>& Intents, int32 TurnId) const
{
	const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;

	for (const FEnemyIntent& Intent : Intents)
	{
		if (Intent.AbilityTag.MatchesTag(AttackTag))
		{
			LOG_TURN_VERBOSE(TurnId, TEXT("[AttackScan] Attack intent found in %d intents"), Intents.Num());
			return true;
		}
	}

	LOG_TURN_VERBOSE(TurnId, TEXT("[AttackScan] No attack intents in %d intents"), Intents.Num());
	return false;
}

void UTurnEnemyPhaseSubsystem::DispatchMoveActions(AGameTurnManagerBase* TurnManager, const TArray<FResolvedAction>& Actions)
{
	if (!TurnManager || Actions.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !IsAuthorityLike(World, TurnManager))
	{
		return;
	}

	if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
	{
		for (const FResolvedAction& Action : Actions)
		{
			MoveRes->DispatchResolvedMove(Action, TurnManager);
		}
	}
	else
	{
		LOG_TURN(Error, TEXT("[Turn %d] DispatchMoveActions: MoveReservationSubsystem not available"), TurnManager->GetCurrentTurnId());
	}
}
