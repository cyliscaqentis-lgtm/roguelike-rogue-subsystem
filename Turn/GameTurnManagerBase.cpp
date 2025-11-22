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

    if (PhaseTag == RogueGameplayTags::Phase_Player_WaitInput)
    {
        WaitingForPlayerInput = true;
        if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
        {
            InputProc->OpenTurnInputWindow(this, CurrentTurnId);
        }
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

    if (PhaseTag == RogueGameplayTags::Phase_Player_WaitInput)
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
                .FindOrAdd(RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed)
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
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->OpenTurnInputWindow(this, CurrentTurnId);
    }
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









void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    LOG_TURN(Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnId);

    if (UWorld* World = GetWorld())
    {
        if (UTurnEnemyPhaseSubsystem* EnemyPhase = World->GetSubsystem<UTurnEnemyPhaseSubsystem>())
        {
            EnemyPhase->ExecuteAttacks(this, CurrentTurnId);
        }
        else
        {
            LOG_TURN(Error, TEXT("OnSimultaneousPhaseCompleted: UTurnEnemyPhaseSubsystem not found!"));
            EndEnemyTurn();
        }
    }
    else
    {
        LOG_TURN(Error, TEXT("OnSimultaneousPhaseCompleted: World is null, ending turn."));
        EndEnemyTurn();
    }
}

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
	if (!IsAuthorityLike(GetWorld(), this)) return;

	LOG_TURN(Log, TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
		CurrentTurnId, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

	AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
	if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
	{
		InputProc->OnPlayerMoveFinalized(this, CompletedActor);
	}

	UWorld* World = GetWorld();
	if (!World) { EndEnemyTurn(); return; }

    // CodeRevision: INC-2025-1122-PERF-R1 (Skip redundant AI calculations when enemy phase already executed) (2025-11-22)
    // If enemy phase was already triggered (simultaneous movement case), we've already:
    // - Collected enemies (RebuildEnemyList)
    // - Updated DistanceField
    // - Generated intents (RegenerateIntentsForPlayerPosition)
    // So we skip the duplicate AI calculations here.
    if (bEnemyPhaseExecutedThisTurn)
    {
        LOG_TURN(Log, TEXT("[Turn %d] Player action complete - enemy phase already in progress (skipping redundant AI calc)"), CurrentTurnId);
        return;
    }

    // Enemy phase not yet executed (player attacked without moving) - need full AI calculation
    LOG_TURN(Log, TEXT("[Turn %d] Player action complete - starting enemy phase"), CurrentTurnId);

	UPlayerMoveHandlerSubsystem* MoveHandler = World->GetSubsystem<UPlayerMoveHandlerSubsystem>();
	if (!MoveHandler) { EndEnemyTurn(); return; }

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
    TArray<AActor*> EnemyActors;
    if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
    {
        EnemyAI->CollectAllEnemies(PlayerPawn, EnemyActors);
    }

    if (UnitTurnStateSubsystem)
    {
        UnitTurnStateSubsystem->UpdateEnemies(EnemyActors);
        LOG_TURN(Log, TEXT("[Turn %d] Enemy roster refreshed (EnemyPhaseStart): %d enemies"), CurrentTurnId, EnemyActors.Num());
    }

    TArray<FEnemyIntent> FinalIntents;
    if (!MoveHandler->HandlePlayerMoveCompletion(Payload, CurrentTurnId, EnemyActors, FinalIntents))
    {
        EndEnemyTurn();
        return;
    }

    // CodeRevision: INC-2025-1122-ATTACK-SEQ-R2 (Trigger enemy phase after player action completes) (2025-11-22)
    // Player attacked without moving, so we need to start enemy phase now
    ExecuteEnemyPhase();
}

// CodeRevision: INC-2025-1122-SIMUL-R5 (Simplified - delegate intent regeneration to EnemyTurnDataSubsystem) (2025-11-22)
void AGameTurnManagerBase::ExecuteEnemyPhase()
{
    // Guard against double execution in the same turn
    if (bEnemyPhaseExecutedThisTurn)
    {
        LOG_TURN(Warning, TEXT("[Turn %d] ExecuteEnemyPhase SKIPPED - already executed this turn"), CurrentTurnId);
        return;
    }
    bEnemyPhaseExecutedThisTurn = true;
    bEnemyPhaseInProgress = true;

    UWorld* World = GetWorld();
    if (!World)
    {
        LOG_TURN(Error, TEXT("ExecuteEnemyPhase: World is null!"));
        EndEnemyTurn();
        return;
    }

    // Determine player's target cell for intent generation
    UGridPathfindingSubsystem* LocalPathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
    UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (EnemyData && LocalPathFinder && PlayerPawn)
    {
        // Use player's reserved target cell if available, otherwise use current position
        FIntPoint PlayerFinalCell;
        const FIntPoint ReservedCell = Occupancy ? Occupancy->GetReservedCellForActor(PlayerPawn) : FIntPoint(-1, -1);

        if (ReservedCell != FIntPoint(-1, -1))
        {
            PlayerFinalCell = ReservedCell;
            LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: Using RESERVED PlayerTargetCell=(%d,%d)"),
                CurrentTurnId, PlayerFinalCell.X, PlayerFinalCell.Y);
        }
        else
        {
            PlayerFinalCell = LocalPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
            LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: Using CURRENT player position=(%d,%d)"),
                CurrentTurnId, PlayerFinalCell.X, PlayerFinalCell.Y);
        }

        // Delegate intent regeneration to EnemyTurnDataSubsystem
        TArray<FEnemyIntent> FinalIntents;
        EnemyData->RegenerateIntentsForPlayerPosition(CurrentTurnId, PlayerFinalCell, FinalIntents);

        // CodeRevision: INC-2025-1122-ADJ-ATTACK-R3 (Upgrade adjacent enemies only if they can reach both positions) (2025-11-22)
        // After regenerating intents based on player's target position, upgrade any enemies
        // that are adjacent to BOTH the player's current AND target positions.
        // This ensures:
        // 1. Enemies adjacent to player's current position get a chance to attack
        // 2. Enemies that can't reach the player's target position won't attack thin air
        const FIntPoint PlayerCurrentCell = LocalPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        if (PlayerCurrentCell != PlayerFinalCell)
        {
            const int32 UpgradedCount = EnemyData->UpgradeIntentsForAdjacency(PlayerCurrentCell, PlayerFinalCell, 1);
            if (UpgradedCount > 0)
            {
                LOG_TURN(Log, TEXT("[Turn %d] ExecuteEnemyPhase: Upgraded %d enemies to ATTACK (adjacent to both PlayerCurrent=(%d,%d) and PlayerTarget=(%d,%d))"),
                    CurrentTurnId, UpgradedCount, PlayerCurrentCell.X, PlayerCurrentCell.Y, PlayerFinalCell.X, PlayerFinalCell.Y);
            }
        }
    }
    else
    {
        LOG_TURN(Warning, TEXT("[Turn %d] ExecuteEnemyPhase: Missing subsystems (EnemyData=%d, PathFinder=%d, Player=%d)"),
            CurrentTurnId, EnemyData != nullptr, LocalPathFinder != nullptr, PlayerPawn != nullptr);
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
        EnemyPhase->ExecuteSequentialMovePhase(this, CurrentTurnId);
    }
}

bool AGameTurnManagerBase::IsSequentialModeActive() const
{
    if (const UTurnEnemyPhaseSubsystem* EnemyPhase = GetWorld()->GetSubsystem<UTurnEnemyPhaseSubsystem>())
    {
        return EnemyPhase->IsSequentialModeActive();
    }
    return false;
}



void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
{
    UWorld* World = GetWorld();
    UTurnEnemyPhaseSubsystem* EnemyPhase = World ? World->GetSubsystem<UTurnEnemyPhaseSubsystem>() : nullptr;
    const bool bSequentialActive = EnemyPhase ? EnemyPhase->IsSequentialModeActive() : false;
    const bool bSequentialMoveStarted = EnemyPhase ? EnemyPhase->HasSequentialMovePhaseStarted() : false;

    if (bSequentialActive && !bSequentialMoveStarted)
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
        // If we receive a barrier completion but enemy phase was already executed,
        // it means this is a late callback from the enemy phase (e.g. simultaneous moves finishing).
        // We should ignore it to avoid double-execution logic or misleading logs.
        if (bEnemyPhaseExecutedThisTurn)
        {
             LOG_TURN(Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Late barrier completion received after Enemy Phase execution. Ignoring."), FinishedTurnId);
             return;
        }

        LOG_TURN(Log, TEXT("[Turn %d] Player move phase complete. Starting Enemy Phase..."), FinishedTurnId);

        bPlayerMoveInProgress = false;
        if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
        {
            InputProc->ApplyWaitInputGate(false);
        }

        // Start enemy phase
        bEnemyPhaseInProgress = true;
        ExecuteEnemyPhase();
        return;
    }

    // Case 2: Enemy phase just completed -> End turn
    LOG_TURN(Log, TEXT("[Turn %d] Enemy phase complete. Ending turn."), FinishedTurnId);

    bEnemyPhaseInProgress = false;
    bPlayerMoveInProgress = false;
    if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
    {
        InputProc->ApplyWaitInputGate(false);
    }

    if (EnemyPhase && EnemyPhase->IsSequentialModeActive())
    {
        EnemyPhase->ResetSequentialMode();
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

    UWorld* World = GetWorld();
    UTurnEnemyPhaseSubsystem* EnemyPhase = World ? World->GetSubsystem<UTurnEnemyPhaseSubsystem>() : nullptr;

    // Always start subsequent move phase after attack completion (soft lock fix)
    if (EnemyPhase && EnemyPhase->IsSequentialModeActive())
    {
        LOG_TURN(Warning,
            TEXT("[Turn %d] Sequential attack phase complete, dispatching move-only phase"), FinishedTurnId);

        EnemyPhase->MarkSequentialMoveOnlyPhase();

        ExecuteEnemyMoves_Sequential();
        return;
    }
}

void AGameTurnManagerBase::EndEnemyTurn()
{
    LOG_TURN(Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnId);

    UWorld* World = GetWorld();

    if (World)
    {
        if (UTurnAdvanceGuardSubsystem* TurnGuard = World->GetSubsystem<UTurnAdvanceGuardSubsystem>())
        {
            TurnGuard->HandleEndEnemyTurn(this);
            return;
        }
    }

    LOG_TURN(Error,
        TEXT("[EndEnemyTurn] TurnAdvanceGuardSubsystem missing; forcing direct advance"));

    // Fallback cleanup: clear residual in-progress tags before advancing.
    if (World)
    {
        APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
        TArray<AActor*> EnemyActors;
        if (UnitTurnStateSubsystem)
        {
            UnitTurnStateSubsystem->CopyEnemiesTo(EnemyActors);
        }
        TurnTagCleanupUtils::ClearResidualInProgressTags(World, PlayerPawn, EnemyActors);
    }
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
        if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
        {
            InputProc->ApplyWaitInputGate(true);
        }
        LOG_TURN(Log,
            TEXT("[Turn %d] Client: Gate OPENED after replication (WindowId=%d)"),
            CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
    }
    else
    {
        if (UPlayerInputProcessor* InputProc = GetWorld()->GetSubsystem<UPlayerInputProcessor>())
        {
            InputProc->ApplyWaitInputGate(false);
        }
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

