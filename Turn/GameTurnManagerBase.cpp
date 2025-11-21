// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// CodeRevision: INC-2025-1135-R1 (Fix encoding issues: translate Japanese comments to English) (2025-11-19 17:25)
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
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
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

// CodeRevision: INC-2025-1204-R1 (Gate on-move intent regeneration behind console variable) (2025-11-21 16:30)
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

// CodeRevision: INC-2025-1120-R2 (Fix CSV log generation by registering the UDebugObserverCSV component at BeginPlay) (2025-11-20 23:45)
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

if (!HasAuthority())
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

// CodeRevision: INC-2025-00027-R1 (Migrate to UGridPathfindingSubsystem - Phase 2.3) (2025-11-16 00:00)
// HandleDungeonReady now initializes UGridPathfindingSubsystem instead of AGridPathfindingLibrary Actor
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

// CodeRevision: INC-2025-00032-R1 (Removed GetCachedPathFinder() - use GetGridPathfindingSubsystem() instead) (2025-01-XX XX:XX)

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

// CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1) (2025-11-16 15:00)
// Removed: BuildAllObservations() - unused wrapper function

// CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1 & 3) (2025-11-16 15:00)
// Removed: SendGameplayEventWithResult() - unused wrapper function
// Removed: GetPlayerController_TBS() - unused wrapper function
// Removed: GetPlayerPawnCachedOrFetch() - unused wrapper function
// Removed: GetPlayerPawn() - replaced with direct UGameplayStatics call
// Removed: GetPlayerActor() - unused wrapper function


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
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildAllyIntents: %d allies"), CurrentTurnId, AllyData->GetAllyCount());
    }
}

void AGameTurnManagerBase::ExecuteAllyActions_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (!AllyData || !AllyData->HasAllies())
    {
        return;
    }

    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ExecuteAllyActions: %d allies"), CurrentTurnId, AllyData->GetAllyCount());
}

void AGameTurnManagerBase::BuildSimulBatch_Implementation()
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildSimulBatch called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::CommitSimulStep_Implementation(const FSimulBatch& Batch)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CommitSimulStep called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::ResolveConflicts_Implementation(TArray<FPendingMove>& Moves)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ResolveConflicts called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CheckInterruptWindow called (Blueprint)"), CurrentTurnId);
}


// CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1) (2025-11-16 15:00)
// Removed: SendGameplayEvent() - unused wrapper function

void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnCombineSystemUpdate called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::OnBreedingSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnBreedingSystemUpdate called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::OnPotSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnPotSystemUpdate called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::OnTrapSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnTrapSystemUpdate called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::OnStatusEffectSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnStatusEffectSystemUpdate called (Blueprint)"), CurrentTurnId);
}

void AGameTurnManagerBase::OnItemSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnItemSystemUpdate called (Blueprint)"), CurrentTurnId);
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
    // CodeRevision: INC-2025-00030-R1 (Removed CurrentTurnIndex replication - use TurnFlowCoordinator instead) (2025-11-16 00:00)
    // Removed: DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnIndex);
    // CodeRevision: INC-2025-00030-R1 (Removed InputWindowId replication - use TurnFlowCoordinator instead) (2025-11-16 00:00)
    // Removed: DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);
    DOREPLIFETIME(AGameTurnManagerBase, bPlayerMoveInProgress);
}

void AGameTurnManagerBase::OnEnemyAttacksCompleted()
{
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemy attacks completed"), CurrentTurnId);
    OnAllAbilitiesCompleted();
}

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
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

// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnId);

OnTurnStartedHandler(CurrentTurnId);
}

// CodeRevision: INC-2025-1120-R9 (Switched to timestamp-based session filenames) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R6 (Rearchitected logger for one file per session and TurnID stamping) (2025-11-20 00:00)
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

// CodeRevision: INC-2025-1205-R1 (Add helper to copy cached enemies from UnitTurnStateSubsystem) (2025-11-21 23:08)
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

// CodeRevision: INC-2025-1205-R1 (Route enemy roster updates through UnitTurnStateSubsystem) (2025-11-21 23:08)
void AGameTurnManagerBase::RefreshEnemyRoster(APawn* PlayerPawn, int32 TurnId, const TCHAR* ContextLabel)
{
    const TCHAR* Label = ContextLabel ? ContextLabel : TEXT("CollectEnemies");
    if (!UnitTurnStateSubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[%s] Turn %d: UnitTurnStateSubsystem missing; enemy cache not updated"), Label, TurnId);
        return;
    }

    const int32 BeforeCount = UnitTurnStateSubsystem->GetCachedEnemyRefs().Num();
    UE_LOG(LogTurnManager, Warning,
        TEXT("[%s] Turn %d: Cached enemies BEFORE collect = %d"),
        Label, TurnId, BeforeCount);

    TArray<AActor*> CollectedEnemies;
    if (EnemyAISubsystem)
    {
        EnemyAISubsystem->CollectAllEnemies(PlayerPawn, CollectedEnemies);
        UE_LOG(LogTurnManager, Warning,
            TEXT("[%s] Turn %d: EnemyAISubsystem collected %d enemies"),
            Label, TurnId, CollectedEnemies.Num());
    }
    else
    {
        CollectEnemiesViaPawnScan(PlayerPawn, TurnId, Label, CollectedEnemies);
    }

    if (CollectedEnemies.Num() == 0)
    {
        CollectEnemiesViaActorTag(PlayerPawn, TurnId, Label, CollectedEnemies);
    }

    UnitTurnStateSubsystem->UpdateEnemies(CollectedEnemies);
    const int32 AfterCount = UnitTurnStateSubsystem->GetCachedEnemyRefs().Num();
    UE_LOG(LogTurnManager, Warning,
        TEXT("[%s] Turn %d: Cached enemies AFTER collect = %d"),
        Label, TurnId, AfterCount);
}

// CodeRevision: INC-2025-1208-R1 (Split enemy roster fallback collection into helpers) (2025-11-22 01:10)
void AGameTurnManagerBase::CollectEnemiesViaPawnScan(APawn* PlayerPawn, int32 TurnId, const TCHAR* Label, TArray<AActor*>& OutEnemies)
{
    if (UWorld* World = GetWorld())
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[%s] Turn %d: EnemyAISubsystem unavailable, fallback scan initiated"),
            Label, TurnId);

        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Found);
        UE_LOG(LogTurnManager, Warning,
            TEXT("[%s] Turn %d: Fallback pawn scan found %d actors"),
            Label, TurnId, Found.Num());

        static const FName ActorTagEnemy(TEXT("Enemy"));
        static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;

        int32 NumByTag = 0;
        int32 NumByTeam = 0;
        int32 NumByActorTag = 0;
        OutEnemies.Reserve(Found.Num());

        for (AActor* Candidate : Found)
        {
            if (!IsValid(Candidate) || Candidate == PlayerPawn)
            {
                if (Candidate == PlayerPawn)
                {
                    UE_LOG(LogTurnManager, Log, TEXT("[%s] Turn %d: Skipping PlayerPawn fallback (Actor=%s)"),
                        Label, TurnId, *Candidate->GetName());
                }
                continue;
            }

            const int32 TeamId = GetTeamIdOf(Candidate);
            const bool bByTeam = (TeamId == 2 || TeamId == 255);
            const UAbilitySystemComponent* ASC = TryGetASC(Candidate);
            const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));
            const bool bByActorTag = Candidate->Tags.Contains(ActorTagEnemy);

            if (bByGTag || bByTeam || bByActorTag)
            {
                OutEnemies.Add(Candidate);
                if (bByGTag) ++NumByTag;
                if (bByTeam) ++NumByTeam;
                if (bByActorTag) ++NumByActorTag;
            }
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[%s] Turn %d: Fallback summary found=%d collected=%d byGTag=%d byTeam=%d byActorTag=%d"),
            Label, TurnId, Found.Num(), OutEnemies.Num(), NumByTag, NumByTeam, NumByActorTag);
    }
}

void AGameTurnManagerBase::CollectEnemiesViaActorTag(APawn* PlayerPawn, int32 TurnId, const TCHAR* Label, TArray<AActor*>& InOutEnemies)
{
    if (UWorld* World = GetWorld())
    {
        TArray<AActor*> TaggedEnemies;
        UGameplayStatics::GetAllActorsWithTag(World, FName("Enemy"), TaggedEnemies);
        for (AActor* Tagged : TaggedEnemies)
        {
            if (IsValid(Tagged) && Tagged != PlayerPawn)
            {
                InOutEnemies.AddUnique(Tagged);
            }
        }

        if (TaggedEnemies.Num() > 0)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[%s] Turn %d: ActorTag fallback added %d enemies"), Label, TurnId, InOutEnemies.Num());
        }
    }
}

// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// CodeRevision: INC-2025-1206-R1 (Delegate turn initialization to TurnInitializationSubsystem) (2025-11-22 00:11)
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
	if (!HasAuthority()) return;

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
    if (!HasAuthority())
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

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
UGridPathfindingSubsystem* AGameTurnManagerBase::FindPathFinder(UWorld* World) const
{
    if (!World)
    {
        return nullptr;
    }

    return World->GetSubsystem<UGridPathfindingSubsystem>();
}

// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
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
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Simultaneous Move Phase (No Attacks) ===="), CurrentTurnId);

    // CodeRevision: INC-2025-1117K (Fix Turn 0 simultaneous movement bug) (2025-11-17 20:45)
    // Call CoreResolvePhase and directly execute DispatchResolvedMove
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
    if (!HasAuthority())
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
	if (!HasAuthority())
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


// CodeRevision: INC-2025-1206-R2 (Delegate player move completion to PlayerMoveHandlerSubsystem) (2025-11-22 00:22)
void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTurnManager, Log,
		TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
		CurrentTurnId, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

	// Finalize player move
	AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
	FinalizePlayerMove(CompletedActor);

	// CodeRevision: INC-2025-1206-R6 (Unified Enemy Phase Entry)
	// This is now the SINGLE entry point for the enemy turn, regardless of player action type.

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] World not available"), CurrentTurnId);
		return;
	}

	// Get required subsystems
	UPlayerMoveHandlerSubsystem* MoveHandler = World->GetSubsystem<UPlayerMoveHandlerSubsystem>();
	UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
	UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();

	if (!MoveHandler || !PhaseManager || !EnemyData)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Critical Subsystem Missing! Aborting Enemy Turn."), CurrentTurnId);
		EndEnemyTurn();
		return;
	}

	// 1. Refresh Enemy List (Handle spawns/deaths during player action)
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	RefreshEnemyRoster(PlayerPawn, CurrentTurnId, TEXT("EnemyPhaseStart"));
	TArray<AActor*> EnemyActors;
	CopyEnemyActors(EnemyActors);

	UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Enemy Phase Start: Collected %d enemies"), CurrentTurnId, EnemyActors.Num());

	// 2. Handle move completion and update AI knowledge
	TArray<FEnemyIntent> FinalIntents;
	if (!MoveHandler->HandlePlayerMoveCompletion(Payload, CurrentTurnId, EnemyActors, FinalIntents))
	{
		UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Failed to handle player move completion"), CurrentTurnId);
		EndEnemyTurn();
		return;
	}

	// Cache intents
	CachedEnemyIntents = FinalIntents;

	// 3. Determine & Execute Phase
	const bool bHasAttack = DoesAnyIntentHaveAttack();

	if (bHasAttack)
	{
		// --- Sequential Mode (Pattern A) ---
		UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Mode: SEQUENTIAL (Attacks detected)"), CurrentTurnId);

		bSequentialModeActive = true;
		bSequentialMovePhaseStarted = false;
		bIsInMoveOnlyPhase = false;

		// Resolve conflicts to determine order & winners
		CachedResolvedActions.Reset();
		CachedResolvedActions = PhaseManager->CoreResolvePhase(EnemyData->Intents);

		// Extract Attacks
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
			// Should rarely happen if bHasAttack is true, but possible if resolution failed/blocked all attacks
			UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Sequential Mode: No resolved attacks found despite intents. Skipping to moves."), CurrentTurnId);
			ExecuteEnemyMoves_Sequential();
		}
		else
		{
			ExecuteAttacks(AttackActions);
		}
	}
	else
	{
		// --- Simultaneous Mode ---
		UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Mode: SIMULTANEOUS (No attacks)"), CurrentTurnId);

		bSequentialModeActive = false;
		ExecuteSimultaneousPhase();
	}
}

void AGameTurnManagerBase::ExecuteEnemyMoves_Sequential()
{
    if (!HasAuthority())
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


// CodeRevision: INC-2025-1117H-R1 (Centralized move dispatch) (2025-11-17 19:10)
// CodeRevision: INC-2025-1117H-R1 (Centralized move dispatch) (2025-11-17 19:10)
void AGameTurnManagerBase::DispatchMoveActions(const TArray<FResolvedAction>& ActionsToDispatch)
{
    if (!HasAuthority() || ActionsToDispatch.IsEmpty())
    {
        return;
    }

    for (const FResolvedAction& Action : ActionsToDispatch)
    {
        DispatchResolvedMove(Action);
    }
}


// CodeRevision: INC-2025-1125-R1 (Expose sequential flag for resolver checks) (2025-11-25 12:00)
// CodeRevision: INC-2025-1125-R1 (Expose sequential flag for resolver checks) (2025-11-25 12:00)
bool AGameTurnManagerBase::IsSequentialModeActive() const
{
    return bSequentialModeActive;
}


bool AGameTurnManagerBase::DoesAnyIntentHaveAttack() const
{
    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;

    // INC-2025-0002: Enhanced logging - detailed output of Intents array contents used for bHasAttack determination
    UE_LOG(LogTurnManager, Warning,
        TEXT("[AttackScan] Turn=%d, Scanning CachedEnemyIntents (Count=%d)"),
        CurrentTurnId, CachedEnemyIntents.Num());

    int32 AttackCount = 0;
    int32 MoveCount = 0;
    int32 WaitCount = 0;
    const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

    for (int32 i = 0; i < CachedEnemyIntents.Num(); ++i)
    {
        const FEnemyIntent& Intent = CachedEnemyIntents[i];

        // Count
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            ++AttackCount;
        }
        else if (Intent.AbilityTag.MatchesTag(MoveTag))
        {
            ++MoveCount;
        }
        else if (Intent.AbilityTag.MatchesTag(WaitTag))
        {
            ++WaitCount;
        }

        // Log details of each Intent
        UE_LOG(LogTurnManager, Warning,
            TEXT("[AttackScan] Turn=%d, Intent[%d]: Actor=%s, Ability=%s, From=(%d,%d), To=(%d,%d)"),
            CurrentTurnId,
            i,
            *GetNameSafe(Intent.Actor.Get()),
            *Intent.AbilityTag.ToString(),
            Intent.CurrentCell.X, Intent.CurrentCell.Y,
            Intent.NextCell.X, Intent.NextCell.Y);
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[AttackScan] Turn=%d, Summary: Attack=%d, Move=%d, Wait=%d, Total=%d"),
        CurrentTurnId, AttackCount, MoveCount, WaitCount, CachedEnemyIntents.Num());

    const bool bHasAttack = (AttackCount > 0);

    UE_LOG(LogTurnManager, Warning,
        TEXT("[AttackScan] Turn=%d, bHasAttack=%s"),
        CurrentTurnId, bHasAttack ? TEXT("TRUE") : TEXT("FALSE"));

    return bHasAttack;
}

void AGameTurnManagerBase::ExecuteAttacks(const TArray<FResolvedAction>& PreResolvedAttacks)
{
    if (!HasAuthority())
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


// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
void AGameTurnManagerBase::EnsureEnemyIntents(APawn* PlayerPawn)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    if (!EnemyData) return;

    if (EnemyData->Intents.Num() > 0) return;

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] EnsureEnemyIntents: No enemy intents detected, attempting fallback generation..."),
        CurrentTurnId);

    UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
    TArray<AActor*> EnemyActors;

    if (EnemyAISys && IsValid(PathFinder) && PlayerPawn)
    {
        RefreshEnemyRoster(PlayerPawn, CurrentTurnId, TEXT("MovePhaseFallback"));
        CopyEnemyActors(EnemyActors);
    }

    if (EnemyActors.Num() > 0)
    {
        // Get reliable player grid coordinates from GridOccupancySubsystem
        FIntPoint PlayerGrid = FIntPoint(-1, -1);
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
        }

        if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            DistanceField->UpdateDistanceField(PlayerGrid);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] Fallback: DistanceField updated for PlayerGrid=(%d,%d)"),
                CurrentTurnId, PlayerGrid.X, PlayerGrid.Y);
        }

        TArray<FEnemyObservation> Observations;
        EnemyAISys->BuildObservations(EnemyActors, PlayerGrid, PathFinder.Get(), Observations);
        EnemyData->Observations = Observations;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Fallback: Generated %d observations"),
            CurrentTurnId, Observations.Num());

        TArray<FEnemyIntent> Intents;
        EnemyAISys->CollectIntents(Observations, EnemyActors, Intents);
        EnemyData->Intents = Intents;
        CachedEnemyIntents = Intents;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Fallback: Generated %d intents"),
            CurrentTurnId, Intents.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] Fallback failed: EnemyAISys=%d, PathFinder=%d, PlayerPawn=%d, Enemies=%d"),
            CurrentTurnId,
            EnemyAISys != nullptr,
            IsValid(PathFinder),
            PlayerPawn != nullptr,
            EnemyActors.Num());
    }
}

// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
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




// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
{
    // ★★★ START: New guard processing ★★★
    // CodeRevision: INC-2025-1117F (Prevent move phase from triggering during sequential attack phase)
    if (bSequentialModeActive && !bSequentialMovePhaseStarted)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Ignoring barrier completion during sequential attack phase. Waiting for HandleAttackPhaseCompleted to start the move phase."), FinishedTurnId);
        return;
    }
    // ★★★ END: New guard processing ★★★

    if (!HasAuthority())
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


// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
void AGameTurnManagerBase::HandleAttackPhaseCompleted(int32 FinishedTurnId)
{
    if (!HasAuthority())
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
static const FGameplayTag InProgressTag = FGameplayTag::RequestGameplayTag(FName("State.Action.InProgress"));
    static const FGameplayTag ExecutingTag = FGameplayTag::RequestGameplayTag(FName("State.Ability.Executing"));
    static const FGameplayTag MovingTag = FGameplayTag::RequestGameplayTag(FName("State.Moving"));
    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));

    if (!InProgressTag.IsValid() || !ExecutingTag.IsValid() || !MovingTag.IsValid())
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TagCleanup] One or more state tags not found"));
        return;
    }

    TArray<AActor*> AllUnits;

// CodeRevision: INC-2025-00017-R1 (Replace GetPlayerPawn() wrapper - Phase 2) (2025-11-16 15:05)
if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        AllUnits.Add(PlayerPawn);
    }

TArray<AActor*> Enemies;
    GetCachedEnemies(Enemies);
    AllUnits.Append(Enemies);

    int32 TotalInProgress = 0;
    int32 TotalExecuting = 0;
    int32 TotalMoving = 0;
    int32 TotalFalling = 0;

    for (AActor* Actor : AllUnits)
    {
        if (!Actor)
        {
            continue;
        }

        UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
        if (!ASC)
        {
            continue;
        }

        const int32 InProgressCount = RemoveGameplayTagLoose(ASC, InProgressTag);
        TotalInProgress += InProgressCount;

        const int32 ExecutingCount = RemoveGameplayTagLoose(ASC, ExecutingTag);
        TotalExecuting += ExecutingCount;

        const int32 MovingCount = RemoveGameplayTagLoose(ASC, MovingTag);
        TotalMoving += MovingCount;

        int32 FallingCount = 0;
        if (FallingTag.IsValid())
        {
            FallingCount = RemoveGameplayTagLoose(ASC, FallingTag);
            TotalFalling += FallingCount;
        }

        if (InProgressCount > 0 || ExecutingCount > 0 || MovingCount > 0 || FallingCount > 0)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[TagCleanup] %s cleared: InProgress=%d, Executing=%d, Moving=%d, Falling=%d"),
                *GetNameSafe(Actor), InProgressCount, ExecutingCount, MovingCount, FallingCount);
        }
    }

    if (TotalInProgress > 0 || TotalExecuting > 0 || TotalMoving > 0 || TotalFalling > 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TagCleanup] Total residual tags cleared: InProgress=%d, Executing=%d, Moving=%d, Falling=%d"),
            TotalInProgress, TotalExecuting, TotalMoving, TotalFalling);
    }
}

// CodeRevision: INC-2025-1208-R1 (Centralize loose tag removal and player move blocking-tag cleanup) (2025-11-22 01:10)
int32 AGameTurnManagerBase::RemoveGameplayTagLoose(UAbilitySystemComponent* ASC, const FGameplayTag& TagToRemove)
{
    if (!ASC || !TagToRemove.IsValid())
    {
        return 0;
    }

    const int32 Count = ASC->GetTagCount(TagToRemove);
    for (int32 i = 0; i < Count; ++i)
    {
        ASC->RemoveLooseGameplayTag(TagToRemove);
    }
    return Count;
}

int32 AGameTurnManagerBase::CleanseBlockingTags(UAbilitySystemComponent* ASC, AActor* ActorForLog)
{
    if (!ASC)
    {
        return 0;
    }

    int32 ClearedCount = 0;

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ++ClearedCount;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�E�E�ECleared blocking State.Ability.Executing tag from %s"),
            *GetNameSafe(ActorForLog));
    }

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        ++ClearedCount;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�E�E�ECleared blocking State.Action.InProgress tag from %s"),
            *GetNameSafe(ActorForLog));
    }

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Moving))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Moving);
        ++ClearedCount;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] Cleared residual State.Moving tag from %s (Gemini fix)"),
            *GetNameSafe(ActorForLog));
    }

    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));
    if (FallingTag.IsValid() && ASC->HasMatchingGameplayTag(FallingTag))
    {
        ASC->RemoveLooseGameplayTag(FallingTag);
        ++ClearedCount;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] Cleared residual Movement.Mode.Falling tag from %s (Gemini fix)"),
            *GetNameSafe(ActorForLog));
    }

    return ClearedCount;
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

    if (!HasAuthority())
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

// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
void AGameTurnManagerBase::NotifyPlayerPossessed(APawn* NewPawn)
{
    if (!HasAuthority()) return;

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
    if (!HasAuthority()) return;

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

// CodeRevision: INC-2025-00017-R1 (Remove wrapper functions - Phase 4) (2025-11-16 15:25)
// Removed: GetFloorGenerator() - use DungeonSystem->GetFloorGenerator() directly

// CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1) (2025-11-16 15:00)
// Removed: EnsureFloorGenerated() - unused wrapper function
// Removed: NextFloor() - unused wrapper function
// Removed: WarpPlayerToStairUp() - unimplemented wrapper function


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

void AGameTurnManagerBase::RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback)
{
    if (!Unit)
    {
        return;
    }

    if (bIsPlayerFallback)
    {
        PendingPlayerFallbackMoves.Add(Unit);
    }

    if (ActiveMoveDelegates.Contains(Unit))
    {
        return;
    }

        if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
        {
        MovementComp->OnMoveFinished.AddDynamic(this, &AGameTurnManagerBase::HandleManualMoveFinished);
        ActiveMoveDelegates.Add(Unit);

        UE_LOG(LogTurnManager, Verbose,
            TEXT("[Turn %d] Bound move-finished delegate for %s (via MovementComp)"),
            CurrentTurnId, *GetNameSafe(Unit));
        return;
    }

    UE_LOG(LogTurnManager, Error,
        TEXT("[Turn %d] Failed to bind move-finished delegate for %s: MovementComp missing!"),
        CurrentTurnId, *GetNameSafe(Unit));
}

void AGameTurnManagerBase::HandleManualMoveFinished(AUnitBase* Unit)
{
    if (!HasAuthority() || !Unit)
    {
        return;
    }

    if (ActiveMoveDelegates.Contains(Unit))
    {
        if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
        {
            MovementComp->OnMoveFinished.RemoveDynamic(this, &AGameTurnManagerBase::HandleManualMoveFinished);
        }
        ActiveMoveDelegates.Remove(Unit);
    }

    if (FManualMoveBarrierInfo* BarrierInfo = PendingMoveActionRegistrations.Find(Unit))
    {
        if (BarrierInfo->ActionId.IsValid() && BarrierInfo->TurnId != INDEX_NONE)
        {
            if (UWorld* World = GetWorld())
            {
                if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
                {
                    Barrier->CompleteAction(Unit, BarrierInfo->TurnId, BarrierInfo->ActionId);
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[HandleManualMoveFinished] Completed barrier action for %s TurnId=%d ActionId=%s"),
                        *GetNameSafe(Unit), BarrierInfo->TurnId, *BarrierInfo->ActionId.ToString());
                }
            }
        }

        PendingMoveActionRegistrations.Remove(Unit);
    }

    const bool bPlayerFallback = PendingPlayerFallbackMoves.Remove(Unit) > 0;
    if (bPlayerFallback)
    {
        FinalizePlayerMove(Unit);
    }
    else
    {
        ReleaseMoveReservation(Unit);
    }
}

void AGameTurnManagerBase::ClearResolvedMoves()
{
    PendingMoveReservations.Reset();
    PendingPlayerFallbackMoves.Reset();
    PendingMoveActionRegistrations.Reset();

    for (const TWeakObjectPtr<AUnitBase>& UnitPtr : ActiveMoveDelegates)
    {
        if (AUnitBase* Unit = UnitPtr.Get())
        {
            if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
            {
                MovementComp->OnMoveFinished.RemoveDynamic(this, &AGameTurnManagerBase::HandleManualMoveFinished);
            }
        }
    }
    ActiveMoveDelegates.Reset();

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            Occupancy->ClearAllReservations();
        }
    }
}

bool AGameTurnManagerBase::RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[RegisterResolvedMove] FAILED: Actor is null"));
        return false;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    PendingMoveReservations.FindOrAdd(ActorKey) = Cell;

if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            const bool bReserved = Occupancy->ReserveCellForActor(Actor, Cell);
            if (!bReserved)
            {
                UE_LOG(LogTurnManager, Error,
                    TEXT("[RegisterResolvedMove] FAILED: %s cannot reserve (%d,%d) - already reserved by another actor"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y);
                return false;  
            }

            UE_LOG(LogTurnManager, Verbose,
                TEXT("[RegisterResolvedMove] SUCCESS: %s reserved (%d,%d)"),
                *GetNameSafe(Actor), Cell.X, Cell.Y);
            return true;  
        }
    }

    UE_LOG(LogTurnManager, Error,
        TEXT("[RegisterResolvedMove] FAILED: GridOccupancySubsystem not available"));
    return false;
}

bool AGameTurnManagerBase::IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor || PendingMoveReservations.Num() == 0)
    {
        return true;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
    {
        return *ReservedCell == Cell;
    }

    return true;
}

bool AGameTurnManagerBase::HasReservationFor(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor)
    {
        return false;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
    {
        return *ReservedCell == Cell;
    }

    return false;
}

void AGameTurnManagerBase::ReleaseMoveReservation(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    PendingMoveReservations.Remove(ActorKey);

    if (AUnitBase* Unit = Cast<AUnitBase>(Actor))
    {
        PendingPlayerFallbackMoves.Remove(Unit);
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            Occupancy->ReleaseReservationForActor(Actor);
        }
    }
}

bool AGameTurnManagerBase::DispatchResolvedMove(const FResolvedAction& Action)
{
    if (!HasAuthority())
    {
        return false;
    }

    AActor* SourceActor = Action.SourceActor ? Action.SourceActor.Get() : nullptr;
    if (!SourceActor && Action.Actor.IsValid())
    {
        SourceActor = Action.Actor.Get();
    }

    if (!SourceActor)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[ResolvedMove] Missing source actor"));
        return false;
    }

    AUnitBase* Unit = Cast<AUnitBase>(SourceActor);
    if (!Unit)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[ResolvedMove] %s is not a UnitBase"), *GetNameSafe(SourceActor));
        ReleaseMoveReservation(SourceActor);
        return false;
    }

    // CodeRevision: INC-2025-00030-R4 (Fix Barrier desync for Wait actions) (2025-11-17 01:00)
    // CodeRevision: INC-2025-1208-R1 (Split wait handling into dedicated helper) (2025-11-22 01:10)
    if (HandleWaitResolvedAction(Unit, Action))
    {
        return false;
    }

    if (!IsMoveAuthorized(Unit, Action.NextCell))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[ResolvedMove] AUTHORIZATION FAILED: %s tried to move to (%d,%d) but reservation is for a different cell!"),
            *GetNameSafe(Unit), Action.NextCell.X, Action.NextCell.Y);

        TWeakObjectPtr<AActor> ActorKey(Unit);
        if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[ResolvedMove] Reserved cell for %s is (%d,%d), but Action.NextCell is (%d,%d) - MISMATCH!"),
                *GetNameSafe(Unit), ReservedCell->X, ReservedCell->Y, Action.NextCell.X, Action.NextCell.Y);
        }

        ReleaseMoveReservation(Unit);
        return false;
    }

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // CodeRevision: INC-2025-00032-R1 (Remove GetCachedPathFinder() - use GetGridPathfindingSubsystem() instead) (2025-01-XX XX:XX)
    UGridPathfindingSubsystem* LocalPathFinder = GetGridPathfindingSubsystem();
    if (!LocalPathFinder)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[ResolvedMove] PathFinder missing"));
        ReleaseMoveReservation(Unit);
        return false;
    }

    const bool bIsPlayerUnit = (Unit->Team == 0);
    if (bIsPlayerUnit)
    {
        return HandlePlayerResolvedMove(Unit, Action);
    }

    return HandleEnemyResolvedMove(Unit, Action, LocalPathFinder);
}

// CodeRevision: INC-2025-1208-R1 (Split DispatchResolvedMove responsibilities into helpers) (2025-11-22 01:10)
bool AGameTurnManagerBase::HandleWaitResolvedAction(AUnitBase* Unit, const FResolvedAction& Action)
{
    if (!Unit)
    {
        return false;
    }

    // Wait or "no-op" move: register with barrier and complete immediately.
    if (Action.NextCell != FIntPoint(-1, -1) && Action.NextCell != Action.CurrentCell)
    {
        return false;
    }

    ReleaseMoveReservation(Unit);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            const int32 RegisteredTurnId = Barrier->GetCurrentTurnId();
            const FGuid ActionId = Barrier->RegisterAction(Unit, RegisteredTurnId);
            if (ActionId.IsValid())
            {
                Barrier->CompleteAction(Unit, RegisteredTurnId, ActionId);
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[DispatchResolvedMove] Registered+Completed WAIT action: Actor=%s TurnId=%d ActionId=%s"),
                    *GetNameSafe(Unit), RegisteredTurnId, *ActionId.ToString());
            }
            else
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[DispatchResolvedMove] Failed to register WAIT action for %s"), *GetNameSafe(Unit));
            }
        }
    }

    return true;
}

bool AGameTurnManagerBase::HandlePlayerResolvedMove(AUnitBase* Unit, const FResolvedAction& Action)
{
    if (!Unit)
    {
        return false;
    }

    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[DispatchResolvedMove] Player move already in progress, skipping duplicate GA trigger for %s"),
            *GetNameSafe(Unit));
        return true;
    }

    if (TriggerPlayerMoveAbility(Action, Unit))
    {
        return true;
    }

    UE_LOG(LogTurnManager, Error,
        TEXT("[ResolvedMove] ❁EPlayer GA trigger failed - Move BLOCKED (GAS-only path enforced)"));
    ReleaseMoveReservation(Unit);
    return false;
}

bool AGameTurnManagerBase::HandleEnemyResolvedMove(AUnitBase* Unit, const FResolvedAction& Action, UGridPathfindingSubsystem* InPathFinder)
{
    if (!Unit || !InPathFinder)
    {
        if (!Unit)
        {
            UE_LOG(LogTurnManager, Error, TEXT("[HandleEnemyResolvedMove] Unit is null"));
        }
        if (!InPathFinder)
        {
            UE_LOG(LogTurnManager, Error, TEXT("[HandleEnemyResolvedMove] PathFinder missing"));
        }
        if (Unit)
        {
            ReleaseMoveReservation(Unit);
        }
        return false;
    }

    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = InPathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, /*bIsPlayerFallback*/ false);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            const int32 RegisteredTurnId = Barrier->GetCurrentTurnId();
            const FGuid ActionId = Barrier->RegisterAction(Unit, RegisteredTurnId);
            if (ActionId.IsValid())
            {
                FManualMoveBarrierInfo Info;
                Info.TurnId = RegisteredTurnId;
                Info.ActionId = ActionId;
                PendingMoveActionRegistrations.Add(Unit, Info);
                UE_LOG(LogTurnManager, Log,
                    TEXT("[DispatchResolvedMove] Registered enemy move with barrier: Actor=%s, TurnId=%d, ActionId=%s"),
                    *GetNameSafe(Unit), RegisteredTurnId, *ActionId.ToString());
            }
            else
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[DispatchResolvedMove] Failed to register barrier action for %s"), *GetNameSafe(Unit));
            }
        }
    }

    return true;
}

bool AGameTurnManagerBase::TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit)
{
    if (!Unit)
    {
        return false;
    }

    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Unit);
    if (!ASC)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[TriggerPlayerMove] Player ASC missing"));
        return false;
    }

    const int32 ClearedCount = CleanseBlockingTags(ASC, Unit);

    const int32 AbilityCount = ASC->GetActivatableAbilities().Num();
    if (AbilityCount == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�E�E�ENo abilities granted to %s - ASC may not be initialized"),
            *GetNameSafe(Unit));
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[TriggerPlayerMove] %s has %d abilities in ASC (cleared %d blocking tags)"),
            *GetNameSafe(Unit), AbilityCount, ClearedCount);

        const TArray<FGameplayAbilitySpec>& Specs = ASC->GetActivatableAbilities();
        for (int32 i = 0; i < Specs.Num(); ++i)
        {
            const FGameplayAbilitySpec& Spec = Specs[i];
            if (Spec.Ability)
            {
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[TriggerPlayerMove]   Ability[%d]: %s (Level=%d, InputID=%d)"),
                    i, *Spec.Ability->GetName(), Spec.Level, Spec.InputID);
            }
        }
    }

    const FIntPoint Delta = Action.NextCell - Action.CurrentCell;
    const int32 DirX = FMath::Clamp(Delta.X, -1, 1);
    const int32 DirY = FMath::Clamp(Delta.Y, -1, 1);

    if (DirX == 0 && DirY == 0)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[TriggerPlayerMove] Invalid delta (0,0)"));
        return false;
    }

    FGameplayEventData EventData;
    EventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Move;
    EventData.Instigator = Unit;
    EventData.Target = Unit;
    // CodeRevision: INC-2025-00030-R6 (Refactor GA_MoveBase SSOT) (2025-11-17 01:50)
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(Action.NextCell.X, Action.NextCell.Y));
    EventData.OptionalObject = this;

    const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
    if (TriggeredCount > 0)
    {
        CachedPlayerCommand.Direction = FVector(static_cast<double>(DirX), static_cast<double>(DirY), 0.0);
        bPlayerMoveInProgress = true;
        UE_LOG(LogTurnManager, Log,
            TEXT("[TriggerPlayerMove] Player move ability triggered toward (%d,%d)"),
            Action.NextCell.X, Action.NextCell.Y);

        return true;
    }

    FGameplayTagContainer CurrentTags;
    ASC->GetOwnedGameplayTags(CurrentTags);
    UE_LOG(LogTurnManager, Error,
        TEXT("[TriggerPlayerMove] ❁EHandleGameplayEvent returned 0 for %s (AbilityCount=%d, OwnedTags=%s)"),
        *GetNameSafe(Unit),
        AbilityCount,
        *CurrentTags.ToStringSimple());

    return false;
}

