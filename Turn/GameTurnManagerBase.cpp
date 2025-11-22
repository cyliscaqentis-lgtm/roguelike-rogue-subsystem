#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnEnemyPhaseSubsystem.h"
#include "Turn/TurnInitializationSubsystem.h"
#include "Turn/TurnSystemInitializer.h"
#include "Turn/TurnDebugSubsystem.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnAdvanceGuardSubsystem.h"
#include "Turn/PlayerInputProcessor.h"
#include "Turn/UnitTurnStateSubsystem.h"
#include "Turn/MoveReservationSubsystem.h"
#include "Turn/PlayerMoveHandlerSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "TBSLyraGameMode.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/AABB.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "Debug/DebugObserverCSV.h"
#include "Utility/TurnSystemUtils.h"
#include "Utility/TurnTagCleanupUtils.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameFramework/GameStateBase.h"

// Console variable for turn logging
TAutoConsoleVariable<int32> CVarTurnLog(
    TEXT("ts.TurnLog"),
    1,
    TEXT("0:Off, 1:Key events only, 2:Verbose debug output"),
    ECVF_Default
);

// 0: Do not regenerate DistanceField / enemy intents when a MOVE command is accepted (use preliminary intents from turn start)
// 1: Regenerate DistanceField / enemy intents on MOVE accept using predicted player destination (heavier but more predictive)
TAutoConsoleVariable<int32> CVarRegenIntentsOnMove(
    TEXT("ts.Enemy.RegenIntentsOnMove"),
    0,
    TEXT("Regenerate DistanceField and enemy intents when a player MOVE command is accepted.\n")
    TEXT("0: Off (use preliminary intents from turn start, faster)\n")
    TEXT("1: On  (rebuild DistanceField + intents using predicted player destination, heavier)\n"),
    ECVF_Default
);
AGameTurnManagerBase::AGameTurnManagerBase()
{
    PrimaryActorTick.bCanEverTick = false;
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;

    bReplicates = true;
    bAlwaysRelevant = true;         
    SetReplicateMovement(false);    

    LOG_TURN(Log, TEXT("[TurnManager] Constructor: Replication enabled (bReplicates=true, bAlwaysRelevant=true)"));
}

void AGameTurnManagerBase::BeginPlay()
{
    Super::BeginPlay();
    LOG_TURN(Log, TEXT("[TurnManager] BeginPlay"));

    // Bind to Experience loaded event and trigger initialization
    if (UWorld* World = GetWorld())
    {
        if (AGameStateBase* GameState = World->GetGameState())
        {
            if (ULyraExperienceManagerComponent* ExperienceManager = GameState->FindComponentByClass<ULyraExperienceManagerComponent>())
            {
                ExperienceManager->CallOrRegister_OnExperienceLoaded(
                    FOnLyraExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::OnExperienceLoaded));
                LOG_TURN(Log, TEXT("[TurnManager] Bound to Experience load event"));
            }
            else
            {
                LOG_TURN(Warning, TEXT("[TurnManager] ExperienceManagerComponent not found, initializing directly"));
                InitializeTurnSystem();
            }
        }
        else
        {
            LOG_TURN(Warning, TEXT("[TurnManager] GameState not found, initializing directly"));
            InitializeTurnSystem();
        }
    }
}

void AGameTurnManagerBase::HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys)
{
    LOG_TURN(Log, TEXT("[TurnManager] HandleDungeonReady received"));
    if (UTurnInitializationSubsystem* InitSys = GetWorld()->GetSubsystem<UTurnInitializationSubsystem>())
    {
        InitSys->HandleDungeonReady(this, InDungeonSys);
    }
}

void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    LOG_TURN(Log, TEXT("[TurnManager] OnExperienceLoaded"));

    UWorld* World = GetWorld();
    if (!World)
    {
        LOG_TURN(Error, TEXT("OnExperienceLoaded: World is null"));
        return;
    }

    // Trigger dungeon generation pipeline: InitializeGameTurnManager -> StartGenerateFromLevel -> OnGridReady -> HandleDungeonReady -> BuildUnits
    if (UTurnInitializationSubsystem* InitSys = World->GetSubsystem<UTurnInitializationSubsystem>())
    {
        InitSys->InitializeGameTurnManager(this);
    }
    else
    {
        // Fallback for non-Rogue maps
        LOG_TURN(Warning, TEXT("OnExperienceLoaded: TurnInitializationSubsystem not found. Calling InitializeTurnSystem directly."));
        InitializeTurnSystem();
    }
}

void AGameTurnManagerBase::InitializeTurnSystem()
{
	if (bHasInitialized)
	{
		LOG_TURN(Warning, TEXT("InitializeTurnSystem: Already initialized, skipping"));
		return;
	}

	LOG_TURN(Log, TEXT("InitializeTurnSystem: Starting..."));

	UWorld* World = GetWorld();
	if (!World)
	{
		LOG_TURN(Error, TEXT("InitializeTurnSystem: World is null"));
		return;
	}

	if (UTurnSystemInitializer* Initializer = World->GetSubsystem<UTurnSystemInitializer>())
	{
		if (!Initializer->InitializeTurnSystem(this))
		{
			// Subsystem will have logged detailed failure reasons.
			return;
		}
	}
	else
	{
		LOG_TURN(Error, TEXT("InitializeTurnSystem: UTurnSystemInitializer subsystem not available"));
		return;
	}

	bHasInitialized = true;
	LOG_TURN(Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));
}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    
    LOG_TURN(Log, TEXT("[GameTurnManager] Client: TurnId replicated to %d"), CurrentTurnId);
    
}

void AGameTurnManagerBase::InitGameplayTags()
{
    
    Tag_AbilityMove = RogueGameplayTags::GameplayEvent_Intent_Move;  
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;   
    Phase_Turn_Init = RogueGameplayTags::Phase_Turn_Init;            
    Phase_Player_Wait = RogueGameplayTags::Phase_Player_WaitInput;   

const FGameplayTag TagGateInputOpen = RogueGameplayTags::Gate_Input_Open;
    const FGameplayTag TagStateInProgress = RogueGameplayTags::State_Action_InProgress;

LOG_TURN(Log, TEXT("[TagCache] Initialized:"));
    LOG_TURN(Log, TEXT("  Move: %s"), *Tag_AbilityMove.ToString());
    LOG_TURN(Log, TEXT("  TurnAbilityCompleted: %s"), *Tag_TurnAbilityCompleted.ToString());
    LOG_TURN(Log, TEXT("  PhaseInit: %s"), *Phase_Turn_Init.ToString());
    LOG_TURN(Log, TEXT("  PhaseWait: %s"), *Phase_Player_Wait.ToString());
    LOG_TURN(Log, TEXT("  Gate: %s"), *TagGateInputOpen.ToString());
    LOG_TURN(Log, TEXT("  InProgress: %s"), *TagStateInProgress.ToString());

if (!Tag_AbilityMove.IsValid() || !Tag_TurnAbilityCompleted.IsValid())
    {
        LOG_TURN(Error, TEXT("[TagCache] INVALID TAGS! Check DefaultGameplayTags.ini"));
    }

    if (!Phase_Player_Wait.IsValid() || !TagGateInputOpen.IsValid() || !TagStateInProgress.IsValid())
    {
        LOG_TURN(Error, TEXT("[TagCache] INVALID 3-Tag System tags! Check RogueGameplayTags.cpp"));
    }
}

int32 AGameTurnManagerBase::GetCurrentTurnIndex() const
{
    return TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentTurnIndex() : CurrentTurnId;
}

int32 AGameTurnManagerBase::GetCurrentInputWindowId() const
{
    if (TurnFlowCoordinator)
    {
        return TurnFlowCoordinator->GetCurrentInputWindowId();
    }
    return 0;
}



void AGameTurnManagerBase::StartTurn()
{
    
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->StartTurn();
    }
    else
    {
        LOG_TURN(Error, TEXT("[StartTurn] TurnFlowCoordinator not available!"));
    }
}

void AGameTurnManagerBase::BeginPhase(FGameplayTag PhaseTag)
{
    FGameplayTag OldPhase = CurrentPhase;
    CurrentPhase = PhaseTag;
    PhaseStartTime = FPlatformTime::Seconds();

    if (DebugSubsystem)
    {
        DebugSubsystem->LogPhaseTransition(OldPhase, PhaseTag);
    }
    else
    {
        UE_LOG(LogTurnPhase, Log, TEXT("PhaseStart:%s"), *PhaseTag.ToString());
    }

    if (PhaseTag == Phase_Player_Wait)
    {
        WaitingForPlayerInput = true;
        OpenInputWindow();
    }
}

void AGameTurnManagerBase::EndPhase(FGameplayTag PhaseTag)
{
    const double PhaseDuration = FPlatformTime::Seconds() - PhaseStartTime;

    if (DebugSubsystem)
    {
        DebugSubsystem->LogPhaseTransition(PhaseTag, FGameplayTag::EmptyTag);
    }
    else
    {
        UE_LOG(LogTurnPhase, Log, TEXT("PhaseEnd:%s (Duration: %.3fs)"), *PhaseTag.ToString(), PhaseDuration);
    }

    if (PhaseTag == Phase_Player_Wait)
    {
        WaitingForPlayerInput = false;
    }

    CurrentPhase = FGameplayTag::EmptyTag;
}

bool AGameTurnManagerBase::ShouldSimulMove_Implementation(const FPlayerCommand& Command, const FBoardSnapshot& Snapshot) const
{
    return false;
}

void AGameTurnManagerBase::BuildAllyIntents_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (AllyData && AllyData->HasAllies())
    {
        AllyData->BuildAllAllyIntents(Context);
        LOG_TURN_VERBOSE(CurrentTurnId, TEXT("BuildAllyIntents: %d allies"), AllyData->GetAllyCount());
    }
}

void AGameTurnManagerBase::ExecuteAllyActions_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (!AllyData || !AllyData->HasAllies())
    {
        return;
    }

    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("ExecuteAllyActions: %d allies"), AllyData->GetAllyCount());
}

#define SIMPLE_BP_HOOK(FuncName) \
void AGameTurnManagerBase::FuncName##_Implementation(const FTurnContext& Context) \
{ LOG_TURN_VERBOSE(CurrentTurnId, TEXT(#FuncName " called (Blueprint)")); }

SIMPLE_BP_HOOK(OnCombineSystemUpdate)
SIMPLE_BP_HOOK(OnBreedingSystemUpdate)
SIMPLE_BP_HOOK(OnPotSystemUpdate)
SIMPLE_BP_HOOK(OnTrapSystemUpdate)
SIMPLE_BP_HOOK(OnStatusEffectSystemUpdate)
SIMPLE_BP_HOOK(OnItemSystemUpdate)

#undef SIMPLE_BP_HOOK

void AGameTurnManagerBase::BuildSimulBatch_Implementation()
{
    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("BuildSimulBatch called (Blueprint)"));
}

void AGameTurnManagerBase::CommitSimulStep_Implementation(const FSimulBatch& Batch)
{
    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("CommitSimulStep called (Blueprint)"));
}

void AGameTurnManagerBase::ResolveConflicts_Implementation(TArray<FPendingMove>& Moves)
{
    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("ResolveConflicts called (Blueprint)"));
}

void AGameTurnManagerBase::CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove)
{
    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("CheckInterruptWindow called (Blueprint)"));
}

void AGameTurnManagerBase::GetCachedEnemies(TArray<AActor*>& OutEnemies) const
{
    if (UnitTurnStateSubsystem)
    {
        UnitTurnStateSubsystem->CopyEnemiesTo(OutEnemies);
    }
    else
    {
        OutEnemies.Reset();
    }
}

void AGameTurnManagerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AGameTurnManagerBase, WaitingForPlayerInput);
    DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnId);
    DOREPLIFETIME(AGameTurnManagerBase, bPlayerMoveInProgress);
}

void AGameTurnManagerBase::OnEnemyAttacksCompleted()
{
    LOG_TURN(Log, TEXT("[Turn %d] All enemy attacks completed"), CurrentTurnId);
    OnAllAbilitiesCompleted();
}

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    LOG_TURN(Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnId);



	if (!CanAdvanceTurn(CurrentTurnId))
	{
		LOG_TURN(Error,
			TEXT("[AdvanceTurnAndRestart] ABORT: Cannot advance turn %d (safety check failed)"),
			CurrentTurnId);

		if (UWorld* World = GetWorld())
		{
			if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
			{
				Barrier->DumpTurnState(CurrentTurnId);
			}
		}

		return;
	}

    PerformTurnAdvanceAndBeginNextTurn();
}

void AGameTurnManagerBase::StartFirstTurn()
{
    LOG_TURN(Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnId);

    bTurnStarted = true;
    LOG_TURN(Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnId);

OnTurnStartedHandler(CurrentTurnId);
}

void AGameTurnManagerBase::EndPlay(const EEndPlayReason::Type Reason)
{
    if (CachedCsvObserver.IsValid())
    {
        const FString Timestamp = CachedCsvObserver->GetSessionTimestamp();
        if (!Timestamp.IsEmpty())
        {
            FString Filename = FString::Printf(TEXT("Session_%s.csv"), *Timestamp);
            CachedCsvObserver->SaveToFile(Filename);
            LOG_TURN(Log, TEXT("[EndPlay] Saved session log to %s"), *Filename);
        }
    }
    
    if (UAbilitySystemComponent* ASC = TurnSystemUtils::GetPlayerASC(this))
    {
        if (PlayerMoveCompletedHandle.IsValid())
        {
            ASC->GenericGameplayEventCallbacks
                .FindOrAdd(Tag_TurnAbilityCompleted)
                .Remove(PlayerMoveCompletedHandle);
            PlayerMoveCompletedHandle.Reset();

            LOG_TURN(Log, TEXT("[EndPlay] PlayerMoveCompletedHandle removed"));
        }
    }

    Super::EndPlay(Reason);
}





void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
	if (!IsAuthorityLike(GetWorld(), this)) return;

	// Use TurnId directly (should match CurrentTurnId from TurnFlowCoordinator)
	CurrentTurnId = TurnId;

	// Reset phase flags for new turn
	bEnemyPhaseInProgress = false;
	bEnemyPhaseExecutedThisTurn = false;

	if (CachedCsvObserver.IsValid())
	{
		CachedCsvObserver->SetCurrentTurnForLogging(TurnId);
	}

	if (UTurnInitializationSubsystem* TurnInit = GetWorld()->GetSubsystem<UTurnInitializationSubsystem>())
	{
		TurnInit->OnTurnStarted(this, TurnId);
	}
	else
	{
		LOG_TURN(Error, TEXT("[Turn %d] TurnInitializationSubsystem not available!"), TurnId);
	}

	// Open player input window after turn initialization is complete
	OpenInputWindow();
	LOG_TURN(Log, TEXT("[Turn %d] OnTurnStartedHandler: Player input window opened"), TurnId);
}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    if (CommandHandler)
    {
        if (CommandHandler->ProcessPlayerCommand(Command))
        {
            if (PlayerInputProcessor)
            {
                PlayerInputProcessor->CloseInputWindow();
            }
            LastAcceptedCommandTag = Command.CommandTag;
        }
        else
        {
            LOG_TURN(Warning, TEXT("[GameTurnManager] Command processing failed by CommandHandler"));
        }
    }
    else
    {
        LOG_TURN(Error, TEXT("[GameTurnManager] CommandHandler is null!"));
    }
}









void AGameTurnManagerBase::ExecuteSimultaneousPhase()
{
    if (UTurnEnemyPhaseSubsystem* EnemyPhase = GetWorld()->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        EnemyPhase->ExecuteSimultaneousPhase(this, CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    LOG_TURN(Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnId);

    ExecuteAttacks();
}

void AGameTurnManagerBase::ExecuteSequentialPhase()
{
    ExecuteAttacks();
}

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
	if (!IsAuthorityLike(GetWorld(), this)) return;

	LOG_TURN(Log, TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
		CurrentTurnId, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

	AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
	FinalizePlayerMove(CompletedActor);

	UWorld* World = GetWorld();
	if (!World) { EndEnemyTurn(); return; }

	UPlayerMoveHandlerSubsystem* MoveHandler = World->GetSubsystem<UPlayerMoveHandlerSubsystem>();
	if (!MoveHandler) { EndEnemyTurn(); return; }

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	RefreshEnemyRoster(PlayerPawn, CurrentTurnId, TEXT("EnemyPhaseStart"));

	TArray<AActor*> EnemyActors;
	CopyEnemyActors(EnemyActors);

	TArray<FEnemyIntent> FinalIntents;
	if (!MoveHandler->HandlePlayerMoveCompletion(Payload, CurrentTurnId, EnemyActors, FinalIntents))
	{
		EndEnemyTurn();
		return;
	}

	CachedEnemyIntents = FinalIntents;
	ExecuteEnemyPhase();
}

void AGameTurnManagerBase::ExecuteEnemyPhase()
{
    // Guard against double execution in the same turn
    if (bEnemyPhaseExecutedThisTurn)
    {
        LOG_TURN(Warning, TEXT("[Turn %d] ExecuteEnemyPhase SKIPPED - already executed this turn"), CurrentTurnId);
        return;
    }
    bEnemyPhaseExecutedThisTurn = true;

    UWorld* World = GetWorld();
    if (!World)
    {
        LOG_TURN(Error, TEXT("ExecuteEnemyPhase: World is null!"));
        EndEnemyTurn();
        return;
    }

    // Regenerate enemy intents based on player's FINAL position (after player move)
    // This ensures enemies react to where the player actually ended up, not where they started
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>();
    UGridPathfindingSubsystem* LocalPathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (EnemyData && EnemyAI && LocalPathFinder && PlayerPawn)
    {
        const FIntPoint PlayerFinalCell = LocalPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: Regenerating intents for PlayerFinalCell=(%d,%d)"),
            CurrentTurnId, PlayerFinalCell.X, PlayerFinalCell.Y);

        // Ensure enemy list is up-to-date before regenerating intents
        EnemyData->RebuildEnemyList(FName("Enemy"));

        // Get enemy actors
        TArray<AActor*> EnemyActors = EnemyData->GetEnemiesSortedCopy();
        LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: Found %d enemies after RebuildEnemyList"), CurrentTurnId, EnemyActors.Num());

        if (EnemyActors.Num() > 0)
        {
            // Rebuild observations based on player's final position
            TArray<FEnemyObservation> FinalObservations;
            EnemyAI->BuildObservations(EnemyActors, PlayerFinalCell, LocalPathFinder, FinalObservations);

            // Regenerate intents based on new observations
            TArray<FEnemyIntent> FinalIntents;
            EnemyAI->CollectIntents(FinalObservations, EnemyActors, FinalIntents);

            // Count attack intents for logging
            int32 AttackCount = 0;
            int32 MoveCount = 0;
            int32 WaitCount = 0;
            static const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Attack"));
            static const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Move"));
            for (const FEnemyIntent& Intent : FinalIntents)
            {
                if (Intent.AbilityTag.MatchesTag(AttackTag)) AttackCount++;
                else if (Intent.AbilityTag.MatchesTag(MoveTag)) MoveCount++;
                else WaitCount++;
            }

            LOG_TURN(Warning, TEXT("[Turn %d] ExecuteEnemyPhase: Regenerated %d intents (Attack=%d, Move=%d, Wait=%d) based on PlayerFinalCell=(%d,%d)"),
                CurrentTurnId, FinalIntents.Num(), AttackCount, MoveCount, WaitCount, PlayerFinalCell.X, PlayerFinalCell.Y);

            // Update stored intents
            EnemyData->SetIntents(FinalIntents);
            CachedEnemyIntents = FinalIntents;
        }
        else
        {
            LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: No enemies to process"), CurrentTurnId);
        }
    }
    else
    {
        LOG_TURN(Warning, TEXT("[Turn %d] ExecuteEnemyPhase: Missing subsystems for intent regeneration (EnemyData=%d, EnemyAI=%d, PathFinder=%d, Player=%d)"),
            CurrentTurnId, EnemyData != nullptr, EnemyAI != nullptr, LocalPathFinder != nullptr, PlayerPawn != nullptr);
    }

    if (UTurnEnemyPhaseSubsystem* EnemyPhase = World->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        EnemyPhase->ExecuteEnemyPhase(this, CurrentTurnId);
    }
    else
    {
        LOG_TURN(Error, TEXT("ExecuteEnemyPhase: UTurnEnemyPhaseSubsystem not found!"));
        EndEnemyTurn();
    }
}

void AGameTurnManagerBase::ExecuteEnemyMoves_Sequential()
{
    if (UTurnEnemyPhaseSubsystem* EnemyPhase = GetWorld()->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        EnemyPhase->ExecuteSequentialMovePhase(this, CurrentTurnId, CachedResolvedActions);
    }
}

void AGameTurnManagerBase::DispatchMoveActions(const TArray<FResolvedAction>& ActionsToDispatch)
{
    if (!IsAuthorityLike(GetWorld(), this) || ActionsToDispatch.IsEmpty())
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            for (const FResolvedAction& Action : ActionsToDispatch)
            {
                MoveRes->DispatchResolvedMove(Action, this);
            }
        }
    }
}

bool AGameTurnManagerBase::IsSequentialModeActive() const
{
    return bSequentialModeActive;
}



void AGameTurnManagerBase::ExecuteAttacks(const TArray<FResolvedAction>& PreResolvedAttacks)
{
    if (UTurnEnemyPhaseSubsystem* EnemyPhase = GetWorld()->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        EnemyPhase->ExecuteAttacks(this, CurrentTurnId, PreResolvedAttacks);
    }
    else
    {
        LOG_TURN(Error, TEXT("ExecuteAttacks: UTurnEnemyPhaseSubsystem not found!"));
        EndEnemyTurn();
    }
}

void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{
    if (UTurnEnemyPhaseSubsystem* EnemyPhase = GetWorld()->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        EnemyPhase->ExecuteMovePhase(this, CurrentTurnId, bSkipAttackCheck);
    }
    else
    {
        LOG_TURN(Error, TEXT("ExecuteMovePhase: UTurnEnemyPhaseSubsystem not found!"));
        EndEnemyTurn();
    }
}

void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
{
    if (bSequentialModeActive && !bSequentialMovePhaseStarted)
    {
        LOG_TURN(Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Ignoring barrier completion during sequential attack phase. Waiting for HandleAttackPhaseCompleted to start the move phase."), FinishedTurnId);
        return;
    }

    if (!IsAuthorityLike(GetWorld(), this))
    {
        LOG_TURN(Warning, TEXT("GameTurnManager::HandleMovePhaseCompleted: Not authority"));
        return;
    }

    if (FinishedTurnId != CurrentTurnId)
    {
        LOG_TURN(Warning, TEXT("GameTurnManager::HandleMovePhaseCompleted: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnId);
        return;
    }

    LOG_TURN(Log, TEXT("[Turn %d] Barrier complete - all move actions finished (EnemyPhaseInProgress=%d)"),
        FinishedTurnId, bEnemyPhaseInProgress ? 1 : 0);

    // Case 1: Player move phase just completed -> Start enemy phase
    if (!bEnemyPhaseInProgress)
    {
        LOG_TURN(Log, TEXT("[Turn %d] Player move phase complete. Starting Enemy Phase..."), FinishedTurnId);

        bPlayerMoveInProgress = false;
        ApplyWaitInputGate(false);

        // Start enemy phase
        bEnemyPhaseInProgress = true;
        ExecuteEnemyPhase();
        return;
    }

    // Case 2: Enemy phase just completed -> End turn
    LOG_TURN(Log, TEXT("[Turn %d] Enemy phase complete. Ending turn."), FinishedTurnId);

    bEnemyPhaseInProgress = false;
    bPlayerMoveInProgress = false;
    ApplyWaitInputGate(false);

    if (bSequentialModeActive)
    {
        bSequentialModeActive = false;
        bSequentialMovePhaseStarted = false;
        bIsInMoveOnlyPhase = false;
        CachedResolvedActions.Reset();
        LOG_TURN(Log, TEXT("[Turn %d] Sequential mode flags cleared"), FinishedTurnId);
    }

    EndEnemyTurn();
}

void AGameTurnManagerBase::HandleAttackPhaseCompleted(int32 FinishedTurnId)
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    if (FinishedTurnId != CurrentTurnId)
    {
        LOG_TURN(Warning, TEXT("GameTurnManager::HandleAttackPhaseCompleted: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnId);
        return;
    }

    LOG_TURN(Log, TEXT("[Turn %d] AttackExecutor complete - attack phase finished"), FinishedTurnId);

    // Always start subsequent move phase after attack completion (soft lock fix)
    if (bSequentialModeActive)
    {
        LOG_TURN(Warning,
            TEXT("[Turn %d] Sequential attack phase complete, dispatching move-only phase"), FinishedTurnId);

        bSequentialMovePhaseStarted = true;
        bIsInMoveOnlyPhase = true;

        ExecuteEnemyMoves_Sequential();
        return;
    }
}

void AGameTurnManagerBase::EndEnemyTurn()
{
    LOG_TURN(Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnId);

    if (UWorld* World = GetWorld())
    {
        if (UTurnAdvanceGuardSubsystem* TurnGuard = World->GetSubsystem<UTurnAdvanceGuardSubsystem>())
        {
            TurnGuard->HandleEndEnemyTurn(this);
            return;
        }
    }

    LOG_TURN(Error,
        TEXT("[EndEnemyTurn] TurnAdvanceGuardSubsystem missing; forcing direct advance"));

    ClearResidualInProgressTags();
    AdvanceTurnAndRestart();
}







void AGameTurnManagerBase::PerformTurnAdvanceAndBeginNextTurn()
{
    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* CorePhase = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            CorePhase->CoreCleanupPhase();
            LOG_TURN(Log,
                TEXT("[AdvanceTurnAndRestart] CoreCleanupPhase completed"));
        }
    }

    // Use CurrentTurnId from TurnFlowCoordinator instead of local CurrentTurnIndex
    const int32 PreviousTurnId = CurrentTurnId;

    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->EndTurn();
        TurnFlowCoordinator->AdvanceTurn();
        // Update CurrentTurnId from TurnFlowCoordinator
        CurrentTurnId = TurnFlowCoordinator->GetCurrentTurnId();
    }

    bEndTurnPosted = false;

    LOG_TURN(Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d -> %d (bEndTurnPosted reset)"),
        PreviousTurnId, CurrentTurnId);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnId);
            LOG_TURN(Log,
                TEXT("[AdvanceTurnAndRestart] Barrier::BeginTurn(%d) called"),
                CurrentTurnId);
        }
    }

    bTurnContinuing = false;

    LOG_TURN(Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnId);

    OnTurnStartedHandler(CurrentTurnId);
}

void AGameTurnManagerBase::OnRep_WaitingForPlayerInput()
{
    LOG_TURN(Warning, TEXT("[Client] OnRep_WaitingForPlayerInput: %d"), WaitingForPlayerInput);

    if (WaitingForPlayerInput)
    {
        ApplyWaitInputGate(true);
        LOG_TURN(Log,
            TEXT("[Turn %d] Client: Gate OPENED after replication (WindowId=%d)"),
            CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
    }
    else
    {
        ApplyWaitInputGate(false);
        LOG_TURN(Log,
            TEXT("[Turn %d] Client: Gate CLOSED after replication"),
            CurrentTurnId);
    }

    if (UWorld* World = GetWorld())
    {
        if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(World->GetFirstPlayerController()))
        {
            if (WaitingForPlayerInput)
            {
                UE_LOG(LogTemp, Warning, TEXT("[Client] OnRep: Window OPEN, gate reset"));
            }
        }
    }
}

void AGameTurnManagerBase::ApplyWaitInputGate(bool bOpen)
{
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->ApplyWaitInputGate(bOpen);
    }
    else
    {
        LOG_TURN(Error, TEXT("ApplyWaitInputGate: PlayerInputProcessor not found!"));
    }
}

void AGameTurnManagerBase::OpenInputWindow()
{
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->OpenTurnInputWindow(this, CurrentTurnId);
    }
}

void AGameTurnManagerBase::NotifyPlayerPossessed(APawn* NewPawn)
{
    LOG_TURN(Log, TEXT("[TurnManager] NotifyPlayerPossessed: %s"), NewPawn ? *NewPawn->GetName() : TEXT("nullptr"));

    bPlayerPossessed = true;

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (UTurnInitializationSubsystem* InitSys = World->GetSubsystem<UTurnInitializationSubsystem>())
    {
        InitSys->NotifyPlayerPossessed(NewPawn);
    }

    if (UPlayerInputProcessor* InputProc = World->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->OnPlayerPossessed(this, NewPawn);
    }

    TryStartFirstTurn();
}

void AGameTurnManagerBase::TryStartFirstTurn()
{
    if (!IsAuthorityLike(GetWorld(), this)) return;

    if (bPathReady && bUnitsSpawned && bPlayerPossessed && !bTurnStarted)
    {
        LOG_TURN(Warning, TEXT("[TryStartFirstTurn] All conditions met: PathReady=%d UnitsSpawned=%d PlayerPossessed=%d -> Starting Turn 0"),
            bPathReady, bUnitsSpawned, bPlayerPossessed);

        StartFirstTurn();
    }
    else
    {
        LOG_TURN(Log, TEXT("[TryStartFirstTurn] Waiting: PathReady=%d UnitsSpawned=%d PlayerPossessed=%d TurnStarted=%d"),
            bPathReady, bUnitsSpawned, bPlayerPossessed, bTurnStarted);
    }
}

bool AGameTurnManagerBase::IsInputOpen_Server() const
{
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        return InputProc->IsInputOpen_Server(this);
    }
    return false;
}

bool AGameTurnManagerBase::CanAdvanceTurn(int32 TurnId, bool* OutBarrierQuiet, int32* OutInProgressCount) const
{
    if (UWorld* World = GetWorld())
    {
        if (UTurnAdvanceGuardSubsystem* TurnGuard = World->GetSubsystem<UTurnAdvanceGuardSubsystem>())
        {
            return TurnGuard->CanAdvanceTurn(this, TurnId, OutBarrierQuiet, OutInProgressCount);
        }
    }

    if (OutBarrierQuiet)
    {
        *OutBarrierQuiet = false;
    }
    if (OutInProgressCount)
    {
        *OutInProgressCount = 0;
    }

    LOG_TURN(Error,
        TEXT("[CanAdvanceTurn] TurnAdvanceGuardSubsystem missing"));
    return false;
}

void AGameTurnManagerBase::MarkMoveInProgress(bool bInProgress)
{
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->MarkMoveInProgress(bInProgress);
    }
}



void AGameTurnManagerBase::FinalizePlayerMove(AActor* CompletedActor)
{
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->OnPlayerMoveFinalized(this, CompletedActor);
    }
}

void AGameTurnManagerBase::SetPlayerMoveState(const FVector& Direction, bool bInProgress)
{
    CachedPlayerCommand.Direction = Direction;
    bPlayerMoveInProgress = bInProgress;

    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->SetPlayerMoveState(Direction, bInProgress);
    }
}

void AGameTurnManagerBase::RefreshEnemyRoster(APawn* PlayerPawn, int32 TurnId, const TCHAR* Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<AActor*> CollectedEnemies;
    if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
    {
        EnemyAI->CollectAllEnemies(PlayerPawn, CollectedEnemies);
    }

    if (UnitTurnStateSubsystem)
    {
        UnitTurnStateSubsystem->UpdateEnemies(CollectedEnemies);
        LOG_TURN(Log, TEXT("[Turn %d] RefreshEnemyRoster (%s): Updated with %d enemies"),
            TurnId, Context, CollectedEnemies.Num());
    }
}

void AGameTurnManagerBase::CopyEnemyActors(TArray<AActor*>& OutEnemies)
{
    if (UnitTurnStateSubsystem)
    {
        UnitTurnStateSubsystem->CopyEnemiesTo(OutEnemies);
    }
    else
    {
        OutEnemies.Reset();
    }
}

void AGameTurnManagerBase::ClearResidualInProgressTags()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
    TArray<AActor*> EnemyActors;
    CopyEnemyActors(EnemyActors);

    TurnTagCleanupUtils::ClearResidualInProgressTags(World, PlayerPawn, EnemyActors);
    LOG_TURN(Log, TEXT("[Turn %d] ClearResidualInProgressTags: Cleared via TurnTagCleanupUtils (Player + %d enemies)"),
        CurrentTurnId, EnemyActors.Num());
}

UAbilitySystemComponent* AGameTurnManagerBase::GetPlayerASC() const
{
    return TurnSystemUtils::GetPlayerASC(const_cast<AGameTurnManagerBase*>(this));
}

bool AGameTurnManagerBase::RegisterResolvedMove(AActor* SourceActor, const FIntPoint& TargetCell)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->RegisterResolvedMove(SourceActor, TargetCell);
        }
    }
    return false;
}

bool AGameTurnManagerBase::DispatchResolvedMove(const FResolvedAction& Action)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            MoveRes->DispatchResolvedMove(Action, this);
            return true;
        }
    }
    return false;
}
