#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/AABB.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnDebugSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/PlayerInputProcessor.h"
#include "Player/PlayerControllerBase.h"
#include "Utility/GridUtils.h"
#include "Utility/RogueActorUtils.h"
#include "Utility/RogueGameplayTags.h"
#include "Utility/TurnTagCleanupUtils.h"
#include "Turn/MoveReservationSubsystem.h"
#include "Debug/TurnSystemInterfaces.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "Turn/UnitTurnStateSubsystem.h"
#include "Turn/TurnSystemInitializer.h"
#include "Turn/TurnInitializationSubsystem.h"
#include "Turn/PlayerMoveHandlerSubsystem.h"
#include "Turn/TurnAdvanceGuardSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Character/UnitMovementComponent.h"
#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "Character/EnemyUnitBase.h"
#include "Character/UnitBase.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "Debug/DebugObserverCSV.h"
#include "Player/PlayerControllerBase.h" 
#include "Net/UnrealNetwork.h"           
#include "Turn/TurnActionBarrierSubsystem.h"          
#include "Player/LyraPlayerState.h"                   
#include "Player/LyraPlayerController.h"              
#include "GameFramework/SpectatorPawn.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#include "DistanceFieldSubsystem.h" 
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameModes/LyraGameState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Utility/TurnCommandEncoding.h"
#include "Character/UnitMovementComponent.h"

#if !defined(FOnActionExecutorCompleted_DEFINED)
DECLARE_DELEGATE(FOnActionExecutorCompleted);
#define FOnActionExecutorCompleted_DEFINED 1
#endif

using namespace RogueGameplayTags;

TAutoConsoleVariable<int32> CVarTurnLog(
    TEXT("tbs.TurnLog"),
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

    UE_LOG(LogTurnManager, Log, TEXT("[TurnManager] Constructor: Replication enabled (bReplicates=true, bAlwaysRelevant=true)"));
}

void AGameTurnManagerBase::InitializeTurnSystem()
{
	if (bHasInitialized)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Already initialized, skipping"));
		return;
	}

	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Starting..."));

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: World is null"));
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
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: UTurnSystemInitializer subsystem not available"));
		return;
	}

	bHasInitialized = true;
	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));
}

void AGameTurnManagerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

}

void AGameTurnManagerBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::BeginPlay: START..."));

	// Find and add the CSV debug observer to the list of observers.
	UDebugObserverCSV* CSVObserver = FindComponentByClass<UDebugObserverCSV>();
	if (CSVObserver)
	{
		DebugObservers.Add(CSVObserver);
        CachedCsvObserver = CSVObserver;
		UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager::BeginPlay: Found and added UDebugObserverCSV to DebugObservers."));
        
        // Start the logging session
        CachedCsvObserver->MarkSessionStart();
        CachedCsvObserver->SetCurrentTurnForLogging(0);
	}
	else
	{
		UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::BeginPlay: UDebugObserverCSV component not found on this actor. CSV logging will be disabled."));
	}

if (!IsAuthorityLike(GetWorld(), this))
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager: Not authoritative, skipping"));
        return;
    }

    InitGameplayTags();

ATBSLyraGameMode* GM = GetWorld()->GetAuthGameMode<ATBSLyraGameMode>();
    if (!ensure(GM))
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: GameMode not found"));
        return;
    }

UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: World is null!"));
        return;
    }

    DungeonSys = World->GetSubsystem<URogueDungeonSubsystem>();

    UE_LOG(LogTurnManager, Warning, TEXT("TurnManager: ACQ_WORLD_V3 - DungeonSys=%p (NEW BINARY)"),
        static_cast<void*>(DungeonSys.Get()));

if (DungeonSys)
    {
        DungeonSys->OnGridReady.AddDynamic(this, &AGameTurnManagerBase::HandleDungeonReady);
        UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Subscribed to DungeonSys->OnGridReady"));

UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Triggering dungeon generation..."));
        DungeonSys->StartGenerateFromLevel();
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: DungeonSys is null! GetSubsystem<URogueDungeonSubsystem>() failed. Check if subsystem is properly registered."));
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Ready for HandleDungeonReady"));
}

void AGameTurnManagerBase::HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys)
{
    
    if (!DungeonSys)
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: DungeonSys not ready"));
        return;
    }

    // Get GridPathfindingSubsystem (new subsystem-based approach)
    UGridPathfindingSubsystem* GridPathfindingSubsystem = GetGridPathfindingSubsystem();
    if (!GridPathfindingSubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: GridPathfindingSubsystem not available"));
        return;
    }

    // CodeRevision: INC-2025-00032-R1 (Removed legacy PathFinder Actor resolution - PathFinder is now Subsystem) (2025-01-XX XX:XX)
    // PathFinder is now a Subsystem, resolved automatically by engine
    if (!PathFinder)
    {
        PathFinder = GridPathfindingSubsystem;
    }

    if (!UnitMgr)
    {
        UE_LOG(LogTurnManager, Log, TEXT("HandleDungeonReady: Creating UnitManager..."));
        ResolveOrSpawnUnitManager();
    }

    if (!IsValid(PathFinder) || !IsValid(UnitMgr))
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: Dependencies not ready (PFL:%p UM:%p Dgn:%p)"),
            static_cast<void*>(PathFinder.Get()), static_cast<void*>(UnitMgr.Get()), static_cast<void*>(DungeonSys.Get()));
        return;
    }

    // CodeRevision: INC-2025-00032-R1 (Removed CachedPathFinder assignment - use PathFinder only) (2025-01-XX XX:XX)

    // Initialize GridPathfindingSubsystem with dungeon data
    if (ADungeonFloorGenerator* Floor = DungeonSys->GetFloorGenerator())
    {
        FGridInitParams InitParams;
        InitParams.GridCostArray = Floor->GridCells;
        InitParams.MapSize = FVector(Floor->GridWidth, Floor->GridHeight, 0.f);
        InitParams.TileSizeCM = Floor->CellSize;
        InitParams.Origin = FVector::ZeroVector;

        // Initialize Subsystem
        // CodeRevision: INC-2025-00032-R1 (Removed duplicate PathFinder initialization - PathFinder is same as GridPathfindingSubsystem) (2025-01-XX XX:XX)
        GridPathfindingSubsystem->InitializeFromParams(InitParams);

        UE_LOG(LogTurnManager, Warning, TEXT("[PF.Init] Size=%dx%d Cell=%d Origin=(%.1f,%.1f,%.1f) Walkables=%d"),
            Floor->GridWidth, Floor->GridHeight, Floor->CellSize,
            InitParams.Origin.X, InitParams.Origin.Y, InitParams.Origin.Z,
            Floor->GridCells.Num());

        FVector TestWorld(950.f, 3050.f, 0.f);
        int32 TestStatus = GridPathfindingSubsystem->ReturnGridStatus(TestWorld);
        UE_LOG(LogTurnManager, Warning, TEXT("[PF.Verify] World(950,3050) -> Status=%d (Expected: 3=Walkable, -1=Blocked/Uninitialized)"), TestStatus);

        bPathReady = true;
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: FloorGenerator not available for PathFinder initialization"));
    }

    if (IsValid(UnitMgr))
    {
        UnitMgr->PathFinder = PathFinder;

        if (ADungeonFloorGenerator* Floor = DungeonSys->GetFloorGenerator())
        {
            UnitMgr->MapSize = FVector(Floor->GridWidth, Floor->GridHeight, 0.f);
        }

        TArray<AAABB*> GeneratedRooms;
        DungeonSys->GetGeneratedRooms(GeneratedRooms);

        if (GeneratedRooms.Num() == 0)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("HandleDungeonReady: No generated rooms available for UnitManager."));
        }

        UnitMgr->BuildUnits(GeneratedRooms);

AAABB* PlayerRoom = UnitMgr->PlayerStartRoom.Get();
        UE_LOG(LogTurnManager, Warning, TEXT("[BuildUnits] Completed. PlayerStartRoom=%s at Location=%s"),
            PlayerRoom ? *PlayerRoom->GetName() : TEXT("NULL"),
            PlayerRoom ? *PlayerRoom->GetActorLocation().ToString() : TEXT("N/A"));

        bUnitsSpawned = true; 
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: HandleDungeonReady completed, initializing turn system..."));

InitializeTurnSystem();

TryStartFirstTurn();
}

void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] ========== EXPERIENCE READY =========="));
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] Experience: %s"),
        Experience ? *Experience->GetName() : TEXT("NULL"));

}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    
    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Client: TurnId replicated to %d"), CurrentTurnId);
    
}

void AGameTurnManagerBase::InitGameplayTags()
{
    
    Tag_AbilityMove = RogueGameplayTags::GameplayEvent_Intent_Move;  
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;   
    Phase_Turn_Init = RogueGameplayTags::Phase_Turn_Init;            
    Phase_Player_Wait = RogueGameplayTags::Phase_Player_WaitInput;   

const FGameplayTag TagGateInputOpen = RogueGameplayTags::Gate_Input_Open;
    const FGameplayTag TagStateInProgress = RogueGameplayTags::State_Action_InProgress;

UE_LOG(LogTurnManager, Log, TEXT("[TagCache] Initialized:"));
    UE_LOG(LogTurnManager, Log, TEXT("  Move: %s"), *Tag_AbilityMove.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  TurnAbilityCompleted: %s"), *Tag_TurnAbilityCompleted.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  PhaseInit: %s"), *Phase_Turn_Init.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  PhaseWait: %s"), *Phase_Player_Wait.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  Gate: %s"), *TagGateInputOpen.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  InProgress: %s"), *TagStateInProgress.ToString());

if (!Tag_AbilityMove.IsValid() || !Tag_TurnAbilityCompleted.IsValid())
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TagCache] INVALID TAGS! Check DefaultGameplayTags.ini"));
    }

    if (!Phase_Player_Wait.IsValid() || !TagGateInputOpen.IsValid() || !TagStateInProgress.IsValid())
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TagCache] INVALID 3-Tag System tags! Check RogueGameplayTags.cpp"));
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

UGridPathfindingSubsystem* AGameTurnManagerBase::GetGridPathfindingSubsystem() const
{
    if (UWorld* World = GetWorld())
    {
        return World->GetSubsystem<UGridPathfindingSubsystem>();
    }
    return nullptr;
}

void AGameTurnManagerBase::StartTurn()
{
    
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->StartTurn();
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[StartTurn] TurnFlowCoordinator not available!"));
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
        // CodeRevision: INC-2025-1155-R1 (Remove redundant ApplyWaitInputGate - handled in OpenInputWindow) (2025-11-20 11:30)
        // ApplyWaitInputGate(true); // REMOVED: Redundant, OpenInputWindow handles this via PlayerInputProcessor
        OpenInputWindow();
        UE_LOG(LogTurnManager, Log,
            TEXT("Turn%d:BeginPhase(Input) Id=%d, Gate=OPEN, Waiting=TRUE"),
            CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
    }

for (UObject* Obj : DebugObservers)
    {
        if (Obj && Obj->Implements<UDebugObserver>())
        {
            TArray<AActor*> EnemyActors;
            GetCachedEnemies(EnemyActors);
            IDebugObserver::Execute_OnPhaseStarted(Obj, PhaseTag, EnemyActors);
        }
    }
}

void AGameTurnManagerBase::EndPhase(FGameplayTag PhaseTag)
{
    const double Duration = FPlatformTime::Seconds() - PhaseStartTime;
    UE_LOG(LogTurnPhase, Log, TEXT("PhaseEnd:%s(%.2fms)"), *PhaseTag.ToString(), Duration*1000.0);

    CurrentPhase = FGameplayTag();

    if (PhaseTag == Phase_Player_Wait)
    {
        
        WaitingForPlayerInput = false;
        
        // CodeRevision: INC-2025-1155-R1 (Use PlayerInputProcessor to close window and remove tags) (2025-11-20 11:30)
        if (PlayerInputProcessor)
        {
            PlayerInputProcessor->CloseInputWindow();
        }
        else
        {
            ApplyWaitInputGate(false);
        }

        UE_LOG(LogTurnManager, Log, TEXT("Turn%d:[EndPhase] Gate=CLOSED, Waiting=FALSE"),
            CurrentTurnId);
    }

for (UObject* Obj : DebugObservers)
    {
        if (Obj && Obj->Implements<UDebugObserver>())
        {
            IDebugObserver::Execute_OnPhaseCompleted(Obj, PhaseTag, 0.0f);
        }
    }
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
        if (CVarTurnLog.GetValueOnGameThread() >= 2)
        {
            UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildAllyIntents: %d allies"), CurrentTurnId, AllyData->GetAllyCount());
        }
    }
}

void AGameTurnManagerBase::ExecuteAllyActions_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (!AllyData || !AllyData->HasAllies())
    {
        return;
    }

    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ExecuteAllyActions: %d allies"), CurrentTurnId, AllyData->GetAllyCount());
    }
}

void AGameTurnManagerBase::BuildSimulBatch_Implementation()
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildSimulBatch called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::CommitSimulStep_Implementation(const FSimulBatch& Batch)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CommitSimulStep called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::ResolveConflicts_Implementation(TArray<FPendingMove>& Moves)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ResolveConflicts called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CheckInterruptWindow called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnCombineSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnBreedingSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnBreedingSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnPotSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnPotSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnTrapSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnTrapSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnStatusEffectSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnStatusEffectSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::OnItemSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnItemSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

void AGameTurnManagerBase::GetCachedEnemies(TArray<AActor*>& OutEnemies) const
{
    CopyEnemyActors(OutEnemies);
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
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemy attacks completed"), CurrentTurnId);
    OnAllAbilitiesCompleted();
}

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnId);

    LogPlayerPositionBeforeAdvance();

	if (!CanAdvanceTurn(CurrentTurnId))
	{
		UE_LOG(LogTurnManager, Error,
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
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnId);

    bTurnStarted = true;
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnId);

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
            UE_LOG(LogTurnManager, Log, TEXT("[EndPlay] Saved session log to %s"), *Filename);
        }
    }
    
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        if (PlayerMoveCompletedHandle.IsValid())
        {
            ASC->GenericGameplayEventCallbacks
                .FindOrAdd(Tag_TurnAbilityCompleted)
                .Remove(PlayerMoveCompletedHandle);
            PlayerMoveCompletedHandle.Reset();

            UE_LOG(LogTurnManager, Log, TEXT("[EndPlay] PlayerMoveCompletedHandle removed"));
        }
    }

    Super::EndPlay(Reason);
}

UAbilitySystemComponent* AGameTurnManagerBase::GetPlayerASC() const
{
    
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
        {
            
            if (ALyraPlayerState* LyraPS = Cast<ALyraPlayerState>(PS))
            {
                return LyraPS->GetAbilitySystemComponent();
            }

if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PS))
            {
                return ASI->GetAbilitySystemComponent();
            }
        }
    }

    // CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
    // Fallback: Try to get ASC from PlayerPawn directly
    if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PlayerPawn))
        {
            return ASI->GetAbilitySystemComponent();
        }
    }

    UE_LOG(LogTurnManager, Error, TEXT("GetPlayerASC: Failed to find ASC"));
    return nullptr;
}

void AGameTurnManagerBase::CopyEnemyActors(TArray<AActor*>& OutEnemies) const
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

// CodeRevision: INC-2025-1122-R3 (Simplify to use EnemyAISubsystem and UnitTurnStateSubsystem) (2025-11-22)
void AGameTurnManagerBase::RefreshEnemyRoster(APawn* PlayerPawn, int32 TurnId, const TCHAR* ContextLabel)
{
    const TCHAR* Label = ContextLabel ? ContextLabel : TEXT("CollectEnemies");
    if (!UnitTurnStateSubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[%s] Turn %d: UnitTurnStateSubsystem missing"), Label, TurnId);
        return;
    }

    const int32 BeforeCount = UnitTurnStateSubsystem->GetCachedEnemyRefs().Num();
    TArray<AActor*> CollectedEnemies;

    if (EnemyAISubsystem)
    {
        EnemyAISubsystem->CollectAllEnemies(PlayerPawn, CollectedEnemies);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[%s] Turn %d: EnemyAISubsystem not available"), Label, TurnId);
    }

    UnitTurnStateSubsystem->UpdateEnemies(CollectedEnemies);
    const int32 AfterCount = UnitTurnStateSubsystem->GetCachedEnemyRefs().Num();

    if (CVarTurnLog.GetValueOnGameThread() >= 1)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[%s] Turn %d: Enemy roster updated: %d -> %d"),
            Label, TurnId, BeforeCount, AfterCount);
    }
}

void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
	if (!IsAuthorityLike(GetWorld(), this)) return;

	// Use TurnId directly (should match CurrentTurnId from TurnFlowCoordinator)
	CurrentTurnId = TurnId;

	if (CachedCsvObserver.IsValid())
	{
		CachedCsvObserver->SetCurrentTurnForLogging(TurnId);
	}

	// Get PlayerPawn locally instead of caching
	APawn* PlayerPawn = nullptr;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PlayerPawn = PC->GetPawn();
	}

	UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnId);

	// Clear residual tags
	ClearResidualInProgressTags();
	UE_LOG(LogTurnManager, Log,
		TEXT("[Turn %d] ClearResidualInProgressTags called at turn start"),
		TurnId);

	// Refresh enemy roster
	const int32 CachedBefore = UnitTurnStateSubsystem ? UnitTurnStateSubsystem->GetCachedEnemyRefs().Num() : 0;
	UE_LOG(LogTurnManager, Warning,
		TEXT("[Turn %d] Cached enemies before refresh = %d"),
		TurnId, CachedBefore);

	RefreshEnemyRoster(PlayerPawn, TurnId, TEXT("OnTurnStartedHandler"));
	TArray<AActor*> EnemyActors;
	CopyEnemyActors(EnemyActors);

	UE_LOG(LogTurnManager, Warning,
		TEXT("[Turn %d] Enemy roster refreshed: %d enemies"),
		TurnId, EnemyActors.Num());

	// Delegate turn initialization to TurnInitializationSubsystem
	if (UTurnInitializationSubsystem* TurnInit = GetWorld()->GetSubsystem<UTurnInitializationSubsystem>())
	{
		TurnInit->InitializeTurn(TurnId, PlayerPawn, EnemyActors);
		
		// Cache the generated intents
		if (UEnemyTurnDataSubsystem* EnemyData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())
		{
			CachedEnemyIntents = EnemyData->Intents;
		}
	}
	else
	{
		UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] TurnInitializationSubsystem not available!"), TurnId);
	}

	UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnId);
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
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command processing failed by CommandHandler"));
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] CommandHandler is null!"));
    }
}

void AGameTurnManagerBase::ResolveOrSpawnPathFinder()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: World is null"));
        return;
    }

    if (IsValid(PathFinder.Get()))
    {
        return;
    }

    // Get UGridPathfindingSubsystem (subsystems are automatically created by engine, no spawning needed)
    PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

    if (IsValid(PathFinder.Get()))
    {
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Retrieved UGridPathfindingSubsystem"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: Failed to get UGridPathfindingSubsystem!"));
    }
}

void AGameTurnManagerBase::ResolveOrSpawnUnitManager()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnUnitManager: World is null"));
        return;
    }

if (IsValid(UnitMgr))
    {
        return;
    }

TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AUnitManager::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        UnitMgr = Cast<AUnitManager>(Found[0]);
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Found existing UnitManager: %s"), *GetNameSafe(UnitMgr));
        return;
    }

if (ATBSLyraGameMode* GM = World->GetAuthGameMode<ATBSLyraGameMode>())
    {
        UnitMgr = GM->GetUnitManager();
        if (IsValid(UnitMgr))
        {
            UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Got UnitManager from GameMode: %s"), *GetNameSafe(UnitMgr));
            return;
        }
    }

UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Spawning UnitManager"));
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    UnitMgr = World->SpawnActor<AUnitManager>(AUnitManager::StaticClass(), FTransform::Identity, Params);

    if (IsValid(UnitMgr))
    {
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Spawned UnitManager: %s"), *GetNameSafe(UnitMgr));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnUnitManager: Failed to spawn UnitManager!"));
    }
}

UGridPathfindingSubsystem* AGameTurnManagerBase::FindPathFinder(UWorld* World) const
{
    if (!World)
    {
        return nullptr;
    }

    return World->GetSubsystem<UGridPathfindingSubsystem>();
}

void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput is DEPRECATED. Logic moved to OnPlayerMoveCompleted."), CurrentTurnId);
}

// CodeRevision: INC-2025-1208-R2 (Refactor ExecuteMovePhase / ExecuteSimultaneousPhase dependencies and player-intent plumbing) (2025-11-22 01:30)
bool AGameTurnManagerBase::ResolveMovePhaseDependencies(const TCHAR* ContextLabel, bool bLogSubsystemPresence, UTurnCorePhaseManager*& OutPhaseManager, UEnemyTurnDataSubsystem*& OutEnemyData)
{
    OutPhaseManager = nullptr;
    OutEnemyData = nullptr;

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] %s: World is null"), CurrentTurnId, ContextLabel);
        return false;
    }

    OutPhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
    OutEnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (!OutPhaseManager || !OutEnemyData)
    {
        if (bLogSubsystemPresence)
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] %s: PhaseManager=%d or EnemyData=%d not found"),
                CurrentTurnId, ContextLabel,
                OutPhaseManager != nullptr,
                OutEnemyData != nullptr);
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] %s: PhaseManager or EnemyData not found"),
                CurrentTurnId, ContextLabel);
        }

        EndEnemyTurn();
        return false;
    }

    return true;
}

void AGameTurnManagerBase::AppendPlayerIntentIfPending(APawn* PlayerPawn, TArray<FEnemyIntent>& InOutIntents, bool bSimultaneousContext)
{
    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (!PlayerPawn || !IsValid(PathFinder))
    {
        return;
    }

    const FVector PlayerLoc = PlayerPawn->GetActorLocation();
    const FIntPoint PlayerCurrentCell = PathFinder->WorldToGrid(PlayerLoc);

    TWeakObjectPtr<AActor> PlayerKey(PlayerPawn);
    if (const FIntPoint* PlayerNextCell = PendingMoveReservations.Find(PlayerKey))
    {
        if (PlayerCurrentCell == *PlayerNextCell)
        {
            return;
        }

        FEnemyIntent PlayerIntent;
        PlayerIntent.Actor = PlayerPawn;
        PlayerIntent.CurrentCell = PlayerCurrentCell;
        PlayerIntent.NextCell = *PlayerNextCell;
        PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
        PlayerIntent.BasePriority = 200;

        InOutIntents.Add(PlayerIntent);

        if (bSimultaneousContext)
        {
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteSimultaneousPhase: Added Player intent (%d,%d)->(%d,%d)"),
                CurrentTurnId, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                PlayerNextCell->X, PlayerNextCell->Y);
        }
        else
        {
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] Added Player intent to ConflictResolver: (%d,%d)->(%d,%d)"),
                CurrentTurnId, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                PlayerNextCell->X, PlayerNextCell->Y);
        }
    }
}

void AGameTurnManagerBase::ExecuteSimultaneousPhase()
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Simultaneous Move Phase (No Attacks) ===="), CurrentTurnId);

    UTurnCorePhaseManager* PhaseManager = nullptr;
    UEnemyTurnDataSubsystem* EnemyData = nullptr;
    if (!ResolveMovePhaseDependencies(TEXT("ExecuteSimultaneousPhase"), /*bLogSubsystemPresence*/ false, PhaseManager, EnemyData))
    {
        return;
    }

    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

    // Add player intent
    if (UWorld* World = GetWorld())
    {
        APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
        AppendPlayerIntentIfPending(PlayerPawn, AllIntents, /*bSimultaneousContext*/ true);
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteSimultaneousPhase: Processing %d intents via CoreResolvePhase"),
        CurrentTurnId, AllIntents.Num());

    // Call CoreResolvePhase
    TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteSimultaneousPhase: CoreResolvePhase produced %d resolved actions"),
        CurrentTurnId, ResolvedActions.Num());

    // Immediately loop execute DispatchResolvedMove
    for (const FResolvedAction& Action : ResolvedActions)
    {
        DispatchResolvedMove(Action);
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteSimultaneousPhase complete - %d movements dispatched"),
        CurrentTurnId, ResolvedActions.Num());
}



void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnId);

    ExecuteAttacks();
}


void AGameTurnManagerBase::ExecuteSequentialPhase()
{
	if (!IsAuthorityLike(GetWorld(), this))
	{
		return;
	}

	UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Sequential Phase (Attack -> Move) ===="), CurrentTurnId);
	// CodeRevision: INC-2025-1127-R1 (Ensure legacy sequential entry still sets move-phase flags) (2025-11-27 14:15)
	// Ensure the legacy sequential path still flags that a move phase should follow.
	bSequentialModeActive = true;
	bSequentialMovePhaseStarted = false;
	bIsInMoveOnlyPhase = false;

	ExecuteAttacks();
}


// CodeRevision: INC-2025-1122-R5 (Simplify OnPlayerMoveCompleted with extracted helper) (2025-11-22)
void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
	if (!IsAuthorityLike(GetWorld(), this)) return;

	UE_LOG(LogTurnManager, Log, TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
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
	UWorld* World = GetWorld();
	if (!World) return;

	UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
	UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
	if (!PhaseManager || !EnemyData) { EndEnemyTurn(); return; }

	const bool bHasAttack = DoesAnyIntentHaveAttack();

	if (bHasAttack)
	{
		UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Mode: SEQUENTIAL"), CurrentTurnId);
		bSequentialModeActive = true;
		bSequentialMovePhaseStarted = false;
		bIsInMoveOnlyPhase = false;

		CachedResolvedActions.Reset();
		CachedResolvedActions = PhaseManager->CoreResolvePhase(EnemyData->Intents);

		TArray<FResolvedAction> AttackActions;
		const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
		for (const FResolvedAction& Action : CachedResolvedActions)
		{
			if (Action.AbilityTag.MatchesTag(AttackTag) || Action.FinalAbilityTag.MatchesTag(AttackTag))
			{
				AttackActions.Add(Action);
			}
		}

		if (AttackActions.IsEmpty())
		{
			ExecuteEnemyMoves_Sequential();
		}
		else
		{
			ExecuteAttacks(AttackActions);
		}
	}
	else
	{
		UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Mode: SIMULTANEOUS"), CurrentTurnId);
		bSequentialModeActive = false;
		ExecuteSimultaneousPhase();
	}
}

void AGameTurnManagerBase::ExecuteEnemyMoves_Sequential()
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    TArray<FResolvedAction> EnemyMoveActions;
    int32 TotalActions = 0;
    int32 TotalEnemyMoves = 0;
    int32 TotalEnemyWaits = 0;
    int32 TotalEnemyNonMoves = 0;

    const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

    for (const FResolvedAction& Action : CachedResolvedActions)
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
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: No move actions found (Total=%d, Moves=%d, Waits=%d, Non=%d). Ending turn."),
            CurrentTurnId,
            TotalActions,
            TotalEnemyMoves,
            TotalEnemyWaits,
            TotalEnemyNonMoves);
        EndEnemyTurn();
        return;
    }

    bSequentialMovePhaseStarted = true;
    bIsInMoveOnlyPhase = true;

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] Dispatching %d cached enemy actions sequentially (Moves=%d, Waits=%d, NonMove=%d)"),
        CurrentTurnId,
        EnemyMoveActions.Num(),
        TotalEnemyMoves,
        TotalEnemyWaits,
        TotalEnemyNonMoves);

    DispatchMoveActions(EnemyMoveActions);
}


void AGameTurnManagerBase::DispatchMoveActions(const TArray<FResolvedAction>& ActionsToDispatch)
{
    if (!IsAuthorityLike(GetWorld(), this) || ActionsToDispatch.IsEmpty())
    {
        return;
    }

    for (const FResolvedAction& Action : ActionsToDispatch)
    {
        DispatchResolvedMove(Action);
    }
}


bool AGameTurnManagerBase::IsSequentialModeActive() const
{
    return bSequentialModeActive;
}


bool AGameTurnManagerBase::DoesAnyIntentHaveAttack() const
{
    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;

    for (const FEnemyIntent& Intent : CachedEnemyIntents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            if (CVarTurnLog.GetValueOnGameThread() >= 2)
            {
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[AttackScan] Turn %d: Attack intent found in %d intents"),
                    CurrentTurnId, CachedEnemyIntents.Num());
            }
            return true;
        }
    }

    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[AttackScan] Turn %d: No attack intents in %d intents"),
            CurrentTurnId, CachedEnemyIntents.Num());
    }
    return false;
}

void AGameTurnManagerBase::ExecuteAttacks(const TArray<FResolvedAction>& PreResolvedAttacks)
{
    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    // CodeRevision: INC-2025-1127-R1 (Ensure legacy sequential entry still sets move-phase flags) (2025-11-27 14:15)
    // Ensure flags are set if we came here directly (though usually set in ExecuteSequentialPhase)
    if (!bSequentialModeActive)
    {
        bSequentialModeActive = true;
        bSequentialMovePhaseStarted = false;
        bIsInMoveOnlyPhase = false;
    }

    TArray<FResolvedAction> AttacksToExecute = PreResolvedAttacks;

    // If no pre-resolved attacks provided, try to resolve them now (Legacy path)
    if (AttacksToExecute.IsEmpty())
    {
        UWorld* World = GetWorld();
        if (UTurnCorePhaseManager* PhaseManager = World ? World->GetSubsystem<UTurnCorePhaseManager>() : nullptr)
        {
            if (UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>())
            {
                // Resolve conflicts to determine order & winners
                CachedResolvedActions = PhaseManager->CoreResolvePhase(EnemyData->Intents);
                
                // Extract Attacks
                const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
                for (const FResolvedAction& Action : CachedResolvedActions)
                {
                    if (Action.AbilityTag.MatchesTag(AttackTag) || Action.FinalAbilityTag.MatchesTag(AttackTag))
                    {
                        AttacksToExecute.Add(Action);
                    }
                }
            }
        }
    }

    if (AttacksToExecute.IsEmpty())
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ExecuteAttacks: No attacks to execute. Proceeding to moves."), CurrentTurnId);
        ExecuteEnemyMoves_Sequential();
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ExecuteAttacks: Delegating %d attacks to AttackPhaseExecutor."), CurrentTurnId, AttacksToExecute.Num());

    if (AttackExecutor)
    {
        AttackExecutor->BeginSequentialAttacks(AttacksToExecute, CurrentTurnId);
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteAttacks: AttackExecutor is NULL! Skipping attacks."), CurrentTurnId);
        ExecuteEnemyMoves_Sequential();
    }
}


// CodeRevision: INC-2025-1122-R3 (Delegate to EnemyTurnDataSubsystem) (2025-11-22)
void AGameTurnManagerBase::EnsureEnemyIntents(APawn* PlayerPawn)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    if (!EnemyData) return;

    // Prepare enemy actors for fallback
    TArray<AActor*> EnemyActors;
    if (IsValid(PathFinder) && PlayerPawn)
    {
        RefreshEnemyRoster(PlayerPawn, CurrentTurnId, TEXT("MovePhaseFallback"));
        CopyEnemyActors(EnemyActors);
    }

    // Delegate to subsystem
    TArray<FEnemyIntent> GeneratedIntents;
    if (EnemyData->EnsureIntentsFallback(CurrentTurnId, PlayerPawn, PathFinder.Get(), EnemyActors, GeneratedIntents))
    {
        CachedEnemyIntents = GeneratedIntents;
    }
}

void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{
    // Get PlayerPawn locally instead of using cached member
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteMovePhase: World is null"), CurrentTurnId);
        return;
    }

    UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (!PhaseManager || !EnemyData)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteMovePhase: PhaseManager=%d or EnemyData=%d not found"),
            CurrentTurnId,
            PhaseManager != nullptr,
            EnemyData != nullptr);

        EndEnemyTurn();
        return;
    }

    EnsureEnemyIntents(PlayerPawn);

    if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteMovePhase: Still no intents after fallback, skipping enemy turn"),
            CurrentTurnId);

        EndEnemyTurn();
        return;
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Fallback successful: Proceeding with %d intents"),
        CurrentTurnId, EnemyData->Intents.Num());

    // CodeRevision: INC-2025-00017-R1 (Replace HasAnyAttackIntent() wrapper - Phase 2) (2025-11-16 15:15)
    // CodeRevision: INC-2025-1125-R1 (Use DoesAnyIntentHaveAttack for fallback attack gating) (2025-11-25 12:00)
    const bool bHasAttack = DoesAnyIntentHaveAttack();
    UE_LOG(LogTurnManager, Verbose,
        TEXT("[Turn %d] Sequential attack check: AttackIntentPresent=%s (Intents=%d)"),
        CurrentTurnId, bHasAttack ? TEXT("TRUE") : TEXT("FALSE"), CachedEnemyIntents.Num());

    if (!bSkipAttackCheck)
    {
        if (bHasAttack)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ATTACK INTENT detected (%d intents) - Executing attack phase instead of move phase"),
                CurrentTurnId, EnemyData->Intents.Num());

            ExecuteAttacks();
            return;
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] Attack check skipped (called after attack completion)"),
            CurrentTurnId);
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] No attack intents - Proceeding with move phase"),
        CurrentTurnId);

    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

    AppendPlayerIntentIfPending(PlayerPawn, AllIntents, /*bSimultaneousContext*/ false);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase: Processing %d intents (%d enemies + Player) via ConflictResolver"),
        CurrentTurnId, AllIntents.Num(), EnemyData->Intents.Num());

    TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ConflictResolver produced %d resolved actions"),
        CurrentTurnId, ResolvedActions.Num());

    if (PlayerPawn)
    {
        for (const FResolvedAction& Action : ResolvedActions)
        {
            if (IsValid(Action.SourceActor.Get()) && Action.SourceActor.Get() == PlayerPawn)
            {
                if (Action.bIsWait)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[Turn %d] Player movement CANCELLED by ConflictResolver (Swap detected or other conflict)"),
                        CurrentTurnId);

                    TWeakObjectPtr<AActor> PlayerKey(PlayerPawn);
                    PendingMoveReservations.Remove(PlayerKey);

                    if (UAbilitySystemComponent* ASC = PlayerPawn->FindComponentByClass<UAbilitySystemComponent>())
                    {
                        ASC->CancelAllAbilities();
                        UE_LOG(LogTurnManager, Log,
                            TEXT("[Turn %d] Player abilities cancelled due to Swap conflict"),
                            CurrentTurnId);
                    }

                    EndEnemyTurn();
                    return;
                }
                break;
            }
        }
    }

    DispatchMoveActions(ResolvedActions);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase complete - movements dispatched"),
        CurrentTurnId);
}




void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
{
    if (bSequentialModeActive && !bSequentialMovePhaseStarted)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Ignoring barrier completion during sequential attack phase. Waiting for HandleAttackPhaseCompleted to start the move phase."), FinishedTurnId);
        return;
    }

    if (!IsAuthorityLike(GetWorld(), this))
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::HandleMovePhaseCompleted: Not authority"));
        return;
    }

    if (FinishedTurnId != CurrentTurnId)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::HandleMovePhaseCompleted: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnId);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier complete - all move actions finished"), FinishedTurnId);

    bPlayerMoveInProgress = false;
    ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

    if (bSequentialModeActive)
    {
        bSequentialModeActive = false;
        bSequentialMovePhaseStarted = false;
        bIsInMoveOnlyPhase = false;
        CachedResolvedActions.Reset();
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] Sequential move phase complete, ending enemy turn"), FinishedTurnId);
        EndEnemyTurn();
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Move phase complete, calling EndEnemyTurn directly"), FinishedTurnId);
    // CodeRevision: INC-2025-1117B (Remove timer delay, call EndEnemyTurn directly) (2025-11-17 17:30)
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
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::HandleAttackPhaseCompleted: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnId);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] AttackExecutor complete - attack phase finished"), FinishedTurnId);

    // CodeRevision: INC-2025-1117J (Always dispatch move phase after attacks) (2025-11-17 20:30)
    // Always start subsequent move phase after attack completion (soft lock fix)
    if (bSequentialModeActive)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Sequential attack phase complete, dispatching move-only phase"), FinishedTurnId);

        bSequentialMovePhaseStarted = true;
        bIsInMoveOnlyPhase = true;

        // CodeRevision: INC-2025-1117G-R1 (Sequential enemy turn helpers) (2025-11-17 19:30)
        ExecuteEnemyMoves_Sequential();
        return;
    }
}


// CodeRevision: INC-2025-1208-R4 (Delegate EndEnemyTurn guards to TurnAdvanceGuardSubsystem) (2025-11-22 02:10)
void AGameTurnManagerBase::EndEnemyTurn()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnId);

    if (UWorld* World = GetWorld())
    {
        if (UTurnAdvanceGuardSubsystem* TurnGuard = World->GetSubsystem<UTurnAdvanceGuardSubsystem>())
        {
            TurnGuard->HandleEndEnemyTurn(this);
            return;
        }
    }

    UE_LOG(LogTurnManager, Error,
        TEXT("[EndEnemyTurn] TurnAdvanceGuardSubsystem missing; forcing direct advance"));

    ClearResidualInProgressTags();
    AdvanceTurnAndRestart();
}


void AGameTurnManagerBase::ClearResidualInProgressTags()
{
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    TArray<AActor*> Enemies;
    GetCachedEnemies(Enemies);
    TurnTagCleanupUtils::ClearResidualInProgressTags(GetWorld(), PlayerPawn, Enemies);
}

// CodeRevision: INC-2025-1122-R1 (Delegate to TurnTagCleanupUtils) (2025-11-22)
int32 AGameTurnManagerBase::RemoveGameplayTagLoose(UAbilitySystemComponent* ASC, const FGameplayTag& TagToRemove)
{
    return TurnTagCleanupUtils::RemoveGameplayTagLoose(ASC, TagToRemove);
}

int32 AGameTurnManagerBase::CleanseBlockingTags(UAbilitySystemComponent* ASC, AActor* ActorForLog)
{
    return TurnTagCleanupUtils::CleanseBlockingTags(ASC, ActorForLog);
}

// CodeRevision: INC-2025-1208-R3 (Extract advance-turn logging and core phase transition into helpers) (2025-11-22 01:45)
void AGameTurnManagerBase::LogPlayerPositionBeforeAdvance() const
{
    // CodeRevision: INC-2025-00017-R1 (Replace GetPlayerPawn() wrapper - Phase 2) (2025-11-16 15:05)
    if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        if (UWorld* World = GetWorld())
        {
            if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
            {
                const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
                // WorldLocation is not critical here, but can be retrieved from the grid for logging if needed.
                // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
                const FVector PlayerWorldLoc = IsValid(PathFinder) ? PathFinder->GridToWorld(PlayerGrid) : PlayerPawn->GetActorLocation();

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[AdvanceTurn] PLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
                    CurrentTurnId, PlayerGrid.X, PlayerGrid.Y, *PlayerWorldLoc.ToCompactString());
            }
        }
    }
}

void AGameTurnManagerBase::PerformTurnAdvanceAndBeginNextTurn()
{
    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* CorePhase = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            CorePhase->CoreCleanupPhase();
            UE_LOG(LogTurnManager, Log,
                TEXT("[AdvanceTurnAndRestart] CoreCleanupPhase completed"));
        }
    }

    // CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    // Use CurrentTurnId from TurnFlowCoordinator instead of local CurrentTurnIndex
    const int32 PreviousTurnId = CurrentTurnId;

    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->EndTurn();
        TurnFlowCoordinator->AdvanceTurn();
        // Update CurrentTurnId from TurnFlowCoordinator
        CurrentTurnId = TurnFlowCoordinator->GetCurrentTurnId();
        // CodeRevision: INC-2025-00030-R1 (Removed CurrentTurnIndex synchronization - use TurnFlowCoordinator directly) (2025-11-16 00:00)
    }

    bEndTurnPosted = false;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d -> %d (bEndTurnPosted reset)"),
        PreviousTurnId, CurrentTurnId);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnId);
            UE_LOG(LogTurnManager, Log,
                TEXT("[AdvanceTurnAndRestart] Barrier::BeginTurn(%d) called"),
                CurrentTurnId);
        }
    }

    bTurnContinuing = false;

    // CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnId);

    OnTurnStartedHandler(CurrentTurnId);
}

void AGameTurnManagerBase::OnRep_WaitingForPlayerInput()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Client] OnRep_WaitingForPlayerInput: %d"),
        WaitingForPlayerInput);

if (WaitingForPlayerInput)
    {
        ApplyWaitInputGate(true);
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] Client: Gate OPENED after replication (WindowId=%d)"),
            CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
    }
    else
    {
        ApplyWaitInputGate(false);
        UE_LOG(LogTurnManager, Log,
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
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        if (bOpen)
        {
            
            ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate OPENED (Phase+Gate tags added), WindowId=%d"),
                CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
        }
        else
        {

            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

                UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate CLOSED (Gate tag removed), WindowId=%d"),
                CurrentTurnId, TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0);
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ApplyWaitInputGate failed - Player ASC not found"),
            CurrentTurnId);
    }
}

void AGameTurnManagerBase::OpenInputWindow()
{

    if (!IsAuthorityLike(GetWorld(), this))
    {
        return;
    }

    // ① Open new input window (WindowId++)
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->OpenNewInputWindow();
    }

    const int32 CurrentTurnId_Local   = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentTurnId() : CurrentTurnId;
    const int32 CurrentWindowId = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;

    UE_LOG(LogTurnManager, Log,
        TEXT("[GTM] OpenInputWindow: TurnId=%d, WindowId=%d"),
        CurrentTurnId_Local, CurrentWindowId);

    // ② Notify PlayerInputProcessor to start accepting input for this turn/window
    if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(CurrentTurnId_Local, CurrentWindowId);
    }
    else
    {
        // Fallback: If PIP is missing, apply gate manually
        ApplyWaitInputGate(true);
    }

    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(CurrentWindowId);
    }

    // ③ Update internal state
    // CodeRevision: INC-2025-1155-R1 (Remove redundant ApplyWaitInputGate - handled by PIP or fallback above) (2025-11-20 11:30)
    // ApplyWaitInputGate(true); // REMOVED: Redundant
    WaitingForPlayerInput = true;

    OnRep_WaitingForPlayerInput();
    // CodeRevision: INC-2025-00030-R1 (Removed OnRep_InputWindowId call - use TurnFlowCoordinator instead) (2025-11-16 00:00)
}

void AGameTurnManagerBase::NotifyPlayerPossessed(APawn* NewPawn)
{
    if (!IsAuthorityLike(GetWorld(), this)) return;

    // No longer caching PlayerPawn - use UGameplayStatics::GetPlayerPawn() instead
    UE_LOG(LogTurnManager, Log, TEXT("[Turn] NotifyPlayerPossessed: %s"), *GetNameSafe(NewPawn));

bPlayerPossessed = true;

if (bTurnStarted && !WaitingForPlayerInput)
    {
        OpenInputWindow();
        bDeferOpenOnPossess = false; 
    }
    
    else if (bTurnStarted && bDeferOpenOnPossess)
    {
        OpenInputWindow();
        bDeferOpenOnPossess = false; 
    }
    else if (!bTurnStarted)
    {
        
        bDeferOpenOnPossess = true;
        UE_LOG(LogTurnManager, Log, TEXT("[Turn] Defer input window open until turn starts"));
        TryStartFirstTurn(); 
    }
}

void AGameTurnManagerBase::TryStartFirstTurn()
{
    if (!IsAuthorityLike(GetWorld(), this)) return;

if (bPathReady && bUnitsSpawned && bPlayerPossessed && !bTurnStarted)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[TryStartFirstTurn] All conditions met: PathReady=%d UnitsSpawned=%d PlayerPossessed=%d -> Starting Turn 0"),
            bPathReady, bUnitsSpawned, bPlayerPossessed);

        StartFirstTurn();
    }
    else
    {
        UE_LOG(LogTurnManager, Log, TEXT("[TryStartFirstTurn] Waiting: PathReady=%d UnitsSpawned=%d PlayerPossessed=%d TurnStarted=%d"),
            bPathReady, bUnitsSpawned, bPlayerPossessed, bTurnStarted);
    }
}

// CodeRevision: INC-2025-00030-R1 (Removed OnRep_InputWindowId implementation - use TurnFlowCoordinator instead) (2025-11-16 00:00)

bool AGameTurnManagerBase::IsInputOpen_Server() const
{
    if (!WaitingForPlayerInput)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[IsInputOpen] ❁EFALSE: WaitingForPlayerInput=FALSE (Turn=%d)"),
            CurrentTurnId);
        return false;
    }

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        const bool bGateOpen = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        if (!bGateOpen)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[IsInputOpen] ❁EFALSE: Gate_Input_Open=FALSE (Turn=%d)"),
                CurrentTurnId);
        }
        return bGateOpen;
    }

    UE_LOG(LogTurnManager, Error, TEXT("[IsInputOpen] ❁EFALSE: Player ASC not found"));
    return false;
}

// CodeRevision: INC-2025-1208-R4 (Delegate CanAdvanceTurn guards to TurnAdvanceGuardSubsystem) (2025-11-22 02:10)
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

    UE_LOG(LogTurnManager, Error,
        TEXT("[CanAdvanceTurn] TurnAdvanceGuardSubsystem missing"));
    return false;
}



void AGameTurnManagerBase::MarkMoveInProgress(bool bInProgress)
{
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        if (bInProgress)
        {
            ASC->AddLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        }
        else
        {
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        }
    }
}

void AGameTurnManagerBase::TryStartFirstTurnAfterASCReady()
{
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        if (ASC)
        {
            StartFirstTurn();
        }
    }
}

void AGameTurnManagerBase::FinalizePlayerMove(AActor* CompletedActor)
{
    WaitingForPlayerInput = false;
    OnRep_WaitingForPlayerInput();
    bPlayerMoveInProgress = false;

    ReleaseMoveReservation(CompletedActor);

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
    }

    if (CompletedActor)
    {
        UE_LOG(LogTurnManager, VeryVerbose,
            TEXT("[TurnNotify] Player %s completed move - relying on ActionId barrier"),
            *GetNameSafe(CompletedActor));
    }
}

// CodeRevision: INC-2025-1122-R2 (Delegate to MoveReservationSubsystem) (2025-11-22)
void AGameTurnManagerBase::RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            MoveRes->RegisterManualMoveDelegate(Unit, bIsPlayerFallback);
        }
    }
}

void AGameTurnManagerBase::HandleManualMoveFinished(AUnitBase* Unit)
{
    // Delegate handling is now in MoveReservationSubsystem
    // This function is kept for legacy compatibility but should not be called directly
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            MoveRes->HandleManualMoveFinished(Unit);
        }
    }
}

// CodeRevision: INC-2025-1122-R2 (Delegate to MoveReservationSubsystem) (2025-11-22)
void AGameTurnManagerBase::ClearResolvedMoves()
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            MoveRes->ClearResolvedMoves();
        }
    }
}

bool AGameTurnManagerBase::RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->RegisterResolvedMove(Actor, Cell);
        }
    }
    return false;
}

bool AGameTurnManagerBase::IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->IsMoveAuthorized(Actor, Cell);
        }
    }
    return true;
}

bool AGameTurnManagerBase::HasReservationFor(AActor* Actor, const FIntPoint& Cell) const
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->HasReservationFor(Actor, Cell);
        }
    }
    return false;
}

void AGameTurnManagerBase::ReleaseMoveReservation(AActor* Actor)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            MoveRes->ReleaseMoveReservation(Actor);
        }
    }
}

// CodeRevision: INC-2025-1122-R2 (Delegate to MoveReservationSubsystem) (2025-11-22)
bool AGameTurnManagerBase::DispatchResolvedMove(const FResolvedAction& Action)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->DispatchResolvedMove(Action, this);
        }
    }
    return false;
}

// CodeRevision: INC-2025-1122-R4 (Delegate to MoveReservationSubsystem) (2025-11-22)
bool AGameTurnManagerBase::TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit)
{
    if (UWorld* World = GetWorld())
    {
        if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
        {
            return MoveRes->TriggerPlayerMoveAbility(Action, Unit, this);
        }
    }
    return false;
}

void AGameTurnManagerBase::SetPlayerMoveState(const FVector& Direction, bool bInProgress)
{
    CachedPlayerCommand.Direction = Direction;
    bPlayerMoveInProgress = bInProgress;
}

