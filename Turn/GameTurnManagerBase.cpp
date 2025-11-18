// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
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
#include "Utility/RogueGameplayTags.h"
#include "Debug/TurnSystemInterfaces.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
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
#include "Utility/TurnCommandEncoding.h"
#include "Character/UnitMovementComponent.h"

// CodeRevision: INC-2025-00020-R1 (Declare helper utilities and limit enemy subsystem scopes so CollectEnemies/CollectIntents compile cleanly) (2025-11-17 14:00)
namespace
{
    FORCEINLINE UAbilitySystemComponent* TryGetASC(const AActor* Actor)
    {
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
        {
            return ASI->GetAbilitySystemComponent();
        }
        if (const APawn* P = Cast<APawn>(Actor))
        {
            if (const IAbilitySystemInterface* C = Cast<IAbilitySystemInterface>(P->GetController()))
            {
                return C->GetAbilitySystemComponent();
            }
        }
        return nullptr;
    }

    FORCEINLINE int32 GetTeamIdOf(const AActor* Actor)
    {
        if (const APawn* P = Cast<APawn>(Actor))
        {
            if (const IGenericTeamAgentInterface* C = Cast<IGenericTeamAgentInterface>(P->GetController()))
            {
                const int32 TeamID = C->GetGenericTeamId().GetId();
                UE_LOG(LogTurnManager, Log, TEXT("[GetTeamIdOf] %s -> Controller TeamID=%d"),
                    *Actor->GetName(), TeamID);
                return TeamID;
            }
        }

        if (const IGenericTeamAgentInterface* T = Cast<IGenericTeamAgentInterface>(Actor))
        {
            const int32 TeamID = T->GetGenericTeamId().GetId();
            UE_LOG(LogTurnManager, Log, TEXT("[GetTeamIdOf] %s -> Actor TeamID=%d"),
                *Actor->GetName(), TeamID);
            return TeamID;
        }

        UE_LOG(LogTurnManager, Warning, TEXT("[GetTeamIdOf] %s -> No TeamID found"),
            *Actor->GetName());
        return INDEX_NONE;
    }
}

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
using namespace RogueGameplayTags;

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

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnId);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: Barrier::BeginTurn(%d) called"),
                CurrentTurnId, CurrentTurnId);
        }
    }
    
    UWorld* World = GetWorld();
    if (World)
    {
        CommandHandler = World->GetSubsystem<UTurnCommandHandler>();
        DebugSubsystem = World->GetSubsystem<UTurnDebugSubsystem>();
        TurnFlowCoordinator = World->GetSubsystem<UTurnFlowCoordinator>();
        PlayerInputProcessor = World->GetSubsystem<UPlayerInputProcessor>();

        if (!CommandHandler)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UTurnCommandHandler subsystem"));
        }
        if (!DebugSubsystem)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UTurnDebugSubsystem subsystem"));
        }
        if (!TurnFlowCoordinator)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UTurnFlowCoordinator subsystem"));
        }
        if (!PlayerInputProcessor)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UPlayerInputProcessor subsystem"));
        }

        UE_LOG(LogTurnManager, Log, TEXT("Subsystems initialized: CommandHandler=%s, DebugSubsystem=%s, TurnFlowCoordinator=%s, PlayerInputProcessor=%s"),
            CommandHandler ? TEXT("OK") : TEXT("FAIL"),
            DebugSubsystem ? TEXT("OK") : TEXT("FAIL"),
            TurnFlowCoordinator ? TEXT("OK") : TEXT("FAIL"),
            PlayerInputProcessor ? TEXT("OK") : TEXT("FAIL"));
    }

// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
    // Initialize player pawn (no longer caching in CachedPlayerPawn member)
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (PlayerPawn)
    {
        UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: PlayerPawn found: %s"), *PlayerPawn->GetName());

        if (PlayerPawn->IsA(ASpectatorPawn::StaticClass()))
        {
            UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: SpectatorPawn detected, searching for BPPlayerUnit..."));
            TArray<AActor*> FoundActors;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), FoundActors);
            for (AActor* Actor : FoundActors)
            {
                if (Actor->GetName().Contains(TEXT("BPPlayerUnit_C")))
                {
                    if (APawn* PlayerUnit = Cast<APawn>(Actor))
                    {
                        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
                        {
                            PC->Possess(PlayerUnit);
                            PlayerPawn = PlayerUnit;  // Update local reference
                            UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Possessed PlayerUnit: %s"), *PlayerUnit->GetName());
                            break;
                        }
                    }
                }
            }

            if (PlayerPawn->IsA(ASpectatorPawn::StaticClass()))
            {
                UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Could not find/possess BPPlayerUnit (may be possessed later)"));
            }
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get PlayerPawn"));
    }

if (!PathFinder)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: PathFinder not injected, attempting resolve..."));
        ResolveOrSpawnPathFinder();
    }
    
    if (!PathFinder)
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: PathFinder not available after resolve"));
        return;
    }
    // CodeRevision: INC-2025-00032-R1 (Removed CachedPathFinder assignment - use PathFinder only) (2025-01-XX XX:XX)
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: PathFinder ready: %s"), *GetNameSafe(PathFinder));

// CodeRevision: INC-2025-00017-R1 (Replace CollectEnemies() wrapper - Phase 2) (2025-11-16 15:10)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// Direct call to EnemyAISubsystem with fallback
UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] ==== START ===="));
UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] Before: CachedEnemiesForTurn.Num()=%d"), CachedEnemiesForTurn.Num());

if (EnemyAISubsystem)
{
    TArray<AActor*> CollectedEnemies;
    EnemyAISubsystem->CollectAllEnemies(PlayerPawn, CollectedEnemies);
    CachedEnemiesForTurn.Empty();
    CachedEnemiesForTurn = CollectedEnemies;
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemiesForTurn.Num());
}
else
{
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());
    
    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;
    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;
    CachedEnemiesForTurn.Empty();
    CachedEnemiesForTurn.Reserve(Found.Num());
    
    for (AActor* A : Found)
    {
        if (!IsValid(A) || A == PlayerPawn)
        {
            if (A == PlayerPawn)
            {
                UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            }
            continue;
        }
        
        const int32 TeamId = GetTeamIdOf(A);
        const bool bByTeam = (TeamId == 2 || TeamId == 255);
        const UAbilitySystemComponent* ASC = TryGetASC(A);
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));
        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);
        
        if (bByGTag || bByTeam || bByActorTag)
        {
            CachedEnemiesForTurn.Add(A);
            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;
        }
    }
    
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ==== RESULT ==== found=%d  collected=%d  byGTag=%d  byTeam=%d  byActorTag=%d"),
            Found.Num(), CachedEnemiesForTurn.Num(), NumByTag, NumByTeam, NumByActorTag);
}

UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CollectEnemies completed (%d enemies)"), CachedEnemiesForTurn.Num());

EnemyAISubsystem = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get EnemyAISubsystem!"));
        return;
    }

    // CodeRevision: INC-2025-00030-R1 (Use GetWorld()->GetSubsystem<>() instead of cached member) (2025-11-16 00:00)
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();
    if (!EnemyTurnDataSys)
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get EnemyTurnDataSubsystem!"));
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Subsystems initialized (EnemyAI + EnemyTurnData)"));

{
    
    if (World)
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            // CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
            Barrier->OnAllMovesFinished.RemoveDynamic(this, &ThisClass::HandleMovePhaseCompleted);
            Barrier->OnAllMovesFinished.AddDynamic(this, &ThisClass::HandleMovePhaseCompleted);
            UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Barrier delegate bound to HandleMovePhaseCompleted"));

            if (UAttackPhaseExecutorSubsystem* ExecutorSubsystem = World->GetSubsystem<UAttackPhaseExecutorSubsystem>())
            {
                AttackExecutor = ExecutorSubsystem;
                AttackExecutor->OnFinished.RemoveDynamic(this, &ThisClass::HandleAttackPhaseCompleted);
                AttackExecutor->OnFinished.AddDynamic(this, &ThisClass::HandleAttackPhaseCompleted);
                UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: AttackExecutor delegate bound"));
            }
            else
            {
                UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: AttackPhaseExecutorSubsystem not found"));
            }
        }
        else
        {
            
            if (SubsystemRetryCount < 3)
            {
                SubsystemRetryCount++;
                UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Barrier not found, retrying... (%d/3)"), SubsystemRetryCount);
                bHasInitialized = false; 

                World->GetTimerManager().SetTimer(SubsystemRetryHandle, this, &AGameTurnManagerBase::InitializeTurnSystem, 0.5f, false);
                return;
            }
            else
            {
                UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: TurnActionBarrierSubsystem not found after 3 retries"));
            }
        }
    }

}

if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        
        if (PlayerMoveCompletedHandle.IsValid())
        {
            if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(Tag_TurnAbilityCompleted))
            {
                Delegate->Remove(PlayerMoveCompletedHandle);
            }
            PlayerMoveCompletedHandle.Reset();
        }

FGameplayEventMulticastDelegate& Delegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(Tag_TurnAbilityCompleted);
        PlayerMoveCompletedHandle = Delegate.AddUObject(this, &AGameTurnManagerBase::OnPlayerMoveCompleted);

        UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Bound to Gameplay.Event.Turn.Ability.Completed event"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: GetPlayerASC() returned null"));
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
        ApplyWaitInputGate(true);
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
        ApplyWaitInputGate(false);
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
    OutEnemies.Reset();
    OutEnemies.Reserve(CachedEnemiesForTurn.Num());

    for (const TObjectPtr<AActor>& Enemy : CachedEnemiesForTurn)
    {
        if (Enemy)
        {
            OutEnemies.Add(Enemy);
        }
    }
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

int32 SavedCount = 0;
    for (UObject* Observer : DebugObservers)
    {
        if (UDebugObserverCSV* CSV = Cast<UDebugObserverCSV>(Observer))
        {
            FString Filename = FString::Printf(TEXT("TurnDebug_Turn%d.csv"), PreviousTurnId);
            CSV->SaveToFile(Filename);
            SavedCount++;

            UE_LOG(LogTurnManager, Verbose,
                TEXT("[AdvanceTurnAndRestart] CSV saved: %s"), *Filename);
        }
    }

    if (SavedCount > 0)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[AdvanceTurnAndRestart] %d Debug Observer(s) saved"), SavedCount);
    }

bTurnContinuing = false;

// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnId);

OnTurnStartedHandler(CurrentTurnId);
}

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnId);

    bTurnStarted = true;

// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnId);

OnTurnStartedHandler(CurrentTurnId);
}

void AGameTurnManagerBase::EndPlay(const EEndPlayReason::Type Reason)
{
    
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

// CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
    if (!HasAuthority()) return;

    // Use TurnId directly (should match CurrentTurnId from TurnFlowCoordinator)
    CurrentTurnId = TurnId;
    // CodeRevision: INC-2025-00030-R1 (Removed CurrentTurnIndex synchronization - use TurnFlowCoordinator directly) (2025-11-16 00:00)

    // Get PlayerPawn locally instead of caching
    APawn* PlayerPawn = nullptr;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        PlayerPawn = PC->GetPawn();
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnId);

ClearResidualInProgressTags();
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ClearResidualInProgressTags called at turn start"),
        TurnId);

if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            GridOccupancy->SetCurrentTurnId(TurnId);
            GridOccupancy->PurgeOutdatedReservations(TurnId);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] PurgeOutdatedReservations called at turn start (before player input)"),
                TurnId);

TArray<AActor*> AllUnits;
            if (PlayerPawn)
            {
                AllUnits.Add(PlayerPawn);
            }
            AllUnits.Append(CachedEnemiesForTurn);

            if (AllUnits.Num() > 0)
            {
                // GridOccupancy->RebuildFromWorldPositions(AllUnits); // ★★★ BUGFIX: This call is destructive and causes state loss. Disabled.
                // UE_LOG(LogTurnManager, Warning,
                //     TEXT("[Turn %d] RebuildFromWorldPositions called - rebuilding occupancy from physical positions (%d units)"),
                //     TurnId, AllUnits.Num());
            }
            else
            {
                
                GridOccupancy->EnforceUniqueOccupancy();
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] EnforceUniqueOccupancy called (fallback - no units cached yet)"),
                    TurnId);
            }
        }
    }

    // Log player pawn position
    if (PlayerPawn && GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
        {
            const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
            // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
            const FVector PlayerWorldLoc = IsValid(PathFinder) ? PathFinder->GridToWorld(PlayerGrid) : FVector::ZeroVector;
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] PLAYER POSITION AT TURN START: Grid(%d,%d) World(%s)"),
                TurnId, PlayerGrid.X, PlayerGrid.Y, *PlayerWorldLoc.ToCompactString());
        }
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] PlayerPawn found: %s"),
        TurnId, PlayerPawn ? *PlayerPawn->GetName() : TEXT("NULL"));

BeginPhase(Phase_Turn_Init);
    // CurrentInputWindowId++ は削除: OpenInputWindow() が TFC->OpenNewInputWindow() を呼ぶため不要
    BeginPhase(Phase_Player_Wait);
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] INPUT GATE OPENED EARLY (before DistanceField update)"), TurnId);

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CachedEnemiesForTurn.Num() BEFORE CollectEnemies = %d"),
        TurnId, CachedEnemiesForTurn.Num());

// CodeRevision: INC-2025-00017-R1 (Replace CollectEnemies() wrapper - Phase 2) (2025-11-16 15:10)
// CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
// Direct call to EnemyAISubsystem with fallback
UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] ==== START ===="));
UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] Before: CachedEnemiesForTurn.Num()=%d"), CachedEnemiesForTurn.Num());

if (EnemyAISubsystem)
{
    TArray<AActor*> CollectedEnemies;
    EnemyAISubsystem->CollectAllEnemies(PlayerPawn, CollectedEnemies);
    CachedEnemiesForTurn.Empty();
    CachedEnemiesForTurn = CollectedEnemies;
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemiesForTurn.Num());
}
else
{
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());
    
    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;
    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;
    CachedEnemiesForTurn.Empty();
    CachedEnemiesForTurn.Reserve(Found.Num());
    
    for (AActor* A : Found)
    {
        if (!IsValid(A) || A == PlayerPawn)
        {
            if (A == PlayerPawn)
            {
                UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            }
            continue;
        }
        
        const int32 TeamId = GetTeamIdOf(A);
        const bool bByTeam = (TeamId == 2 || TeamId == 255);
        const UAbilitySystemComponent* ASC = TryGetASC(A);
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));
        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);
        
        if (bByGTag || bByTeam || bByActorTag)
        {
            CachedEnemiesForTurn.Add(A);
            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;
        }
    }
    
    UE_LOG(LogTurnManager, Warning,
        TEXT("[CollectEnemies] ==== RESULT ==== found=%d  collected=%d  byGTag=%d  byTeam=%d  byActorTag=%d"),
        Found.Num(), CachedEnemiesForTurn.Num(), NumByTag, NumByTeam, NumByActorTag);
}

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CachedEnemiesForTurn.Num() AFTER CollectEnemies = %d"),
        TurnId, CachedEnemiesForTurn.Num());

    if (CachedEnemiesForTurn.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectEnemies returned 0, trying fallback with ActorTag 'Enemy'"),
            TurnId);

        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("Enemy"), Found);

        for (AActor* A : Found)
        {
            if (IsValid(A) && A != PlayerPawn)
            {
                CachedEnemiesForTurn.Add(A);
            }
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Fallback collected %d enemies with ActorTag 'Enemy'"),
            TurnId, CachedEnemiesForTurn.Num());
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Pre-DistanceField check: PlayerPawn=%s, PathFinder=%s, Enemies=%d"),
        TurnId,
        PlayerPawn ? TEXT("Valid") : TEXT("NULL"),
        // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
        IsValid(PathFinder) ? TEXT("Valid") : TEXT("Invalid"),
        CachedEnemiesForTurn.Num());

    if (PlayerPawn && IsValid(PathFinder))
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Attempting to update DistanceField..."), TurnId);

        UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>();
        if (DistanceField)
        {
            FIntPoint PlayerGrid = FIntPoint(-1, -1);
            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
            {
                PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
            }

            TSet<FIntPoint> EnemyPositions;
            for (AActor* Enemy : CachedEnemiesForTurn)
            {
                if (IsValid(Enemy))
                {
                    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
                    FIntPoint EnemyGrid = PathFinder->WorldToGrid(Enemy->GetActorLocation());
                    EnemyPositions.Add(EnemyGrid);
                }
            }

auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
                {
                    return FGridUtils::ManhattanDistance(A, B);
                };

int32 MaxD = 0;
            for (const FIntPoint& C : EnemyPositions)
            {
                int32 Dist = Manhattan(C, PlayerGrid);
                if (Dist > MaxD)
                {
                    MaxD = Dist;
                }
            }

const int32 Margin = FMath::Clamp(MaxD + 4, 8, 64);

            UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] DF: Player=(%d,%d) Enemies=%d MaxDist=%d Margin=%d"),
                TurnId, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), MaxD, Margin);

DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

bool bAnyUnreached = false;
            for (const FIntPoint& C : EnemyPositions)
            {
                const int32 D = DistanceField->GetDistance(C);
                if (D < 0)
                {
                    bAnyUnreached = true;
                    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Enemy at (%d,%d) unreached! Dist=%d"),
                        TurnId, C.X, C.Y, D);
                }
            }

            if (bAnyUnreached)
            {
                UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ❌DistanceField has unreachable enemies with Margin=%d"),
                    TurnId, Margin);
            }
            else
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemies reachable with Margin=%d"),
                    TurnId, Margin);
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] DistanceFieldSubsystem is NULL!"), TurnId);
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Cannot update DistanceField:"), TurnId);
        if (!PlayerPawn)
            UE_LOG(LogTurnManager, Error, TEXT("  - PlayerPawn is NULL"));
        // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
        if (!IsValid(PathFinder))
            UE_LOG(LogTurnManager, Error, TEXT("  - PathFinder is Invalid"));
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Generating preliminary enemy intents at turn start..."),
        TurnId);

    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (EnemyAISys && EnemyTurnDataSys && IsValid(PathFinder) && PlayerPawn && CachedEnemiesForTurn.Num() > 0)
    {
        // Get reliable player grid coordinates from GridOccupancySubsystem
        FIntPoint PlayerGrid = FIntPoint(-1, -1);
        if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
        {
            PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
        }

        TArray<FEnemyObservation> PreliminaryObs;
        EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, PathFinder.Get(), PreliminaryObs);
        EnemyTurnDataSys->Observations = PreliminaryObs;

TArray<FEnemyIntent> PreliminaryIntents;
        EnemyAISys->CollectIntents(PreliminaryObs, CachedEnemiesForTurn, PreliminaryIntents);
        EnemyTurnDataSys->Intents = PreliminaryIntents;
        CachedEnemyIntents = PreliminaryIntents;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Preliminary intents generated: %d intents from %d enemies (will be updated after player move)"),
            TurnId, PreliminaryIntents.Num(), CachedEnemiesForTurn.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Cannot generate preliminary intents: EnemyAISys=%d, EnemyTurnDataSys=%d, PathFinder=%d, PlayerPawn=%d, Enemies=%d"),
            TurnId,
            EnemyAISys != nullptr,
            EnemyTurnDataSys != nullptr,
            // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
            IsValid(PathFinder),
            PlayerPawn != nullptr,
            CachedEnemiesForTurn.Num());
    }

UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnId);

}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    // CodeRevision: INC-2025-1117E (Failsafe: Reset bPlayerMoveInProgress to prevent move drops) (2025-11-17 18:30)
    if (bPlayerMoveInProgress)
    {
        // 本来ここに来るべきではないが、来た場合は警告を出し、状態をリセットして処理を続行する
        UE_LOG(LogTurnManager, Warning, TEXT("[OnPlayerCommandAccepted] Failsafe: bPlayerMoveInProgress was true, forcing to false to prevent move drop."));
        bPlayerMoveInProgress = false;
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[ROUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Not authority, skipping"));
        return;
    }

    if (CommandHandler)
    {
        if (!CommandHandler->ProcessPlayerCommand(Command))
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command processing failed by CommandHandler"));
            return;
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
            *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId,
            Command.TargetCell.X, Command.TargetCell.Y);

        // CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
        if (Command.TurnId != CurrentTurnId && Command.TurnId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
                Command.TurnId, CurrentTurnId);
            return;
        }

        // CodeRevision: INC-2025-00030-R1 (Use TurnFlowCoordinator for InputWindowId) (2025-11-16 00:00)
        const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
        if (Command.WindowId != WindowIdForTurn && Command.WindowId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GameTurnManager] Command REJECTED - WindowId mismatch (%d != %d) | Turn=%d"),
                Command.WindowId, WindowIdForTurn, CurrentTurnId);
            return;
        }

        // CodeRevision: INC-2025-1117E (Removed redundant bPlayerMoveInProgress guard - replaced by failsafe at function start) (2025-11-17 18:30)
        if (!WaitingForPlayerInput)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Not waiting for input"));

            // CodeRevision: INC-2025-00017-R1 (Replace GetPlayerPawn() wrapper - Phase 2) (2025-11-16 15:05)
            if (APawn* PawnForPC = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
            {
                if (APlayerController* PC = Cast<APlayerController>(PawnForPC->GetController()))
                {
                    if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                    {
                        TPCB->Client_NotifyMoveRejected();
                    }
                }
            }

            return;
        }
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] World is null"));
        return;
    }

    // CodeRevision: INC-2025-00017-R1 (Replace GetPlayerPawn() wrapper - Phase 2) (2025-11-16 15:05)
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] PlayerPawn not found"));
        return;
    }

    FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove;
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;
    EventData.OptionalObject = this;

    const int32 DirX = FMath::RoundToInt(Command.Direction.X);
    const int32 DirY = FMath::RoundToInt(Command.Direction.Y);

    // Calculate target cell before packing
    FIntPoint TargetCell = Command.TargetCell;
    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (IsValid(PathFinder) && (TargetCell.X == 0 && TargetCell.Y == 0))
    {
        const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        TargetCell = FIntPoint(CurrentCell.X + DirX, CurrentCell.Y + DirY);
    }

    // CodeRevision: INC-2025-00030-R8 (Refactor GA_MoveBase SSOT) (2025-11-17 01:50)
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(TargetCell.X, TargetCell.Y));

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (IsValid(PathFinder))
    {
        const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        TargetCell = FIntPoint(
            CurrentCell.X + static_cast<int32>(Command.Direction.X),
            CurrentCell.Y + static_cast<int32>(Command.Direction.Y));

        // ★★★ New: Use unified validation API (2025-11-16 refactoring) ★★★
        // CodeRevision: INC-2025-00015-R1 (Refactored to use IsMoveValid) (2025-11-16 13:55)
        FString FailureReason;
        const bool bMoveValid = PathFinder->IsMoveValid(CurrentCell, TargetCell, PlayerPawn, FailureReason);

        // Swap detection handled separately (ConflictResolver processes it, but we detect here for logging)
        bool bSwapDetected = false;
        if (!bMoveValid && FailureReason.Contains(TEXT("occupied")))
        {
            if (UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>())
            {
                AActor* OccupyingActor = OccSys->GetActorAtCell(TargetCell);
                if (OccupyingActor && OccupyingActor != PlayerPawn)
                {
                    if (UEnemyTurnDataSubsystem* EnemyTurnDataSys = World->GetSubsystem<UEnemyTurnDataSubsystem>())
                    {
                        for (const FEnemyIntent& Intent : EnemyTurnDataSys->Intents)
                        {
                            if (Intent.Actor.Get() == OccupyingActor && Intent.NextCell == CurrentCell)
                            {
                                bSwapDetected = true;
                                UE_LOG(LogTurnManager, Warning,
                                    TEXT("[MovePrecheck] SWAP DETECTED: Player (%d,%d)->(%d,%d), Enemy %s (%d,%d)->(%d,%d)"),
                                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                                    *GetNameSafe(OccupyingActor),
                                    TargetCell.X, TargetCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!bMoveValid)
        {
            // FailureReasonから原因を判定
            const TCHAR* BlockReason = FailureReason.Contains(TEXT("Corner cutting")) ? TEXT("corner_cutting") :
                FailureReason.Contains(TEXT("terrain")) ? TEXT("terrain") :
                FailureReason.Contains(TEXT("distance")) ? TEXT("invalid_distance") :
                bSwapDetected ? TEXT("swap") : TEXT("occupied");

            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] BLOCKED: %s | Cell (%d,%d) | From=(%d,%d) | Applying FACING ONLY (No Turn)"),
                *FailureReason, TargetCell.X, TargetCell.Y, CurrentCell.X, CurrentCell.Y);

            const int32 TargetCost = PathFinder->GetGridCost(TargetCell.X, TargetCell.Y);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Target cell (%d,%d) GridCost=%d (expected: 3=Walkable, -1=Blocked)"),
                TargetCell.X, TargetCell.Y, TargetCost);

            const FIntPoint Directions[4] = {
                FIntPoint(1, 0),
                FIntPoint(-1, 0),
                FIntPoint(0, 1),
                FIntPoint(0, -1)
            };
            const TCHAR* DirNames[4] = { TEXT("Right"), TEXT("Left"), TEXT("Up"), TEXT("Down") };

            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Surrounding cells from (%d,%d):"),
                CurrentCell.X, CurrentCell.Y);

            for (int32 i = 0; i < 4; ++i)
            {
                const FIntPoint CheckCell = CurrentCell + Directions[i];
                const int32 Cost = PathFinder->GetGridCost(CheckCell.X, CheckCell.Y);
                const bool bWalkable = PathFinder->IsCellWalkableIgnoringActor(CheckCell, PlayerPawn);
                UE_LOG(LogTurnManager, Warning,
                    TEXT("  %s (%d,%d): Cost=%d Walkable=%d"),
                    DirNames[i], CheckCell.X, CheckCell.Y, Cost, bWalkable ? 1 : 0);
            }

            const float Yaw = FMath::Atan2(Command.Direction.Y, Command.Direction.X) * 180.f / PI;
            FRotator NewRotation = PlayerPawn->GetActorRotation();
            NewRotation.Yaw = Yaw;
            PlayerPawn->SetActorRotation(NewRotation);

            UE_LOG(LogTurnManager, Log,
                TEXT("[MovePrecheck] SERVER: Applied FACING ONLY - Direction=(%.1f,%.1f), Yaw=%.1f"),
                Command.Direction.X, Command.Direction.Y, Yaw);

            if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
                    TPCB->Client_ApplyFacingNoTurn(WindowIdForTurn, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_ApplyFacingNoTurn RPC (WindowId=%d, no turn consumed)"),
                        WindowIdForTurn);

                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            UE_LOG(LogTurnManager, Verbose, TEXT("[MovePrecheck] Server state after FACING ONLY:"));
            UE_LOG(LogTurnManager, Verbose,
                TEXT("  - WaitingForPlayerInput: %d (STAYS TRUE - no turn consumed)"),
                WaitingForPlayerInput);
            UE_LOG(LogTurnManager, Verbose,
                TEXT("  - bPlayerMoveInProgress: %d (STAYS FALSE)"),
                bPlayerMoveInProgress);
            {
                const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("  - InputWindowId: %d (unchanged)"),
                    WindowIdForTurn);
            }

            return;
        }

        const bool bReserved = RegisterResolvedMove(PlayerPawn, TargetCell);
        if (!bReserved)
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[MovePrecheck] RESERVATION FAILED: (%d,%d)->(%d,%d) - Cell already reserved by another actor"),
                CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y);

            const float Yaw = FMath::Atan2(Command.Direction.Y, Command.Direction.X) * 180.f / PI;
            FRotator NewRotation = PlayerPawn->GetActorRotation();
            NewRotation.Yaw = Yaw;
            PlayerPawn->SetActorRotation(NewRotation);

            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Reservation failed - Applied FACING ONLY - Direction=(%.1f,%.1f), Yaw=%.1f"),
                Command.Direction.X, Command.Direction.Y, Yaw);

            if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
                    TPCB->Client_ApplyFacingNoTurn(WindowIdForTurn, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_ApplyFacingNoTurn RPC (WindowId=%d, reservation failed)"),
                        WindowIdForTurn);

                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            return;
        }

        UE_LOG(LogTurnManager, Log,
            TEXT("[MovePrecheck] OK: (%d,%d)->(%d,%d) | Walkable=true | Reservation SUCCESS"),
            CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[GameTurnManager] PathFinder not available - Player reservation skipped"));
    }

    // CodeRevision: INC-2025-00030-R8 (Refactor GA_MoveBase SSOT) (2025-11-17 01:50)
    // Player move ability now triggers after ConflictResolver dispatch, so simply ack the command here.
    if (CommandHandler)
    {
        CommandHandler->MarkCommandAsAccepted(Command);
        UE_LOG(LogTurnManager, Log,
            TEXT("[GameTurnManager] Command marked as accepted (prevents duplicates)"));
    }

    if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
    {
        if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
        {
            const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
            TPCB->Client_ConfirmCommandAccepted(WindowIdForTurn);
            UE_LOG(LogTurnManager, Log,
                TEXT("[GameTurnManager] Sent Client_ConfirmCommandAccepted ACK (WindowId=%d)"),
                WindowIdForTurn);
        }
    }

    if (PlayerInputProcessor)
    {
        PlayerInputProcessor->CloseInputWindow();
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[GameTurnManager] PlayerInputProcessor not available for CloseInputWindow"));
    }

    CachedPlayerCommand = Command;

    const FGameplayTag InputMove = RogueGameplayTags::InputTag_Move;

    if (Command.CommandTag.MatchesTag(InputMove))
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Player move command accepted - regenerating intents with predicted destination"),
            CurrentTurnId);

        FIntPoint PlayerDestination = FIntPoint(0, 0);
        if (IsValid(PathFinder) && PlayerPawn)
        {
            // Get current cell from GridOccupancySubsystem (reliable source)
            FIntPoint CurrentCell = FIntPoint(-1, -1);
            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
            {
                CurrentCell = Occupancy->GetCellOfActor(PlayerPawn);
            }

            FIntPoint Direction = FIntPoint(
                FMath::RoundToInt(Command.Direction.X),
                FMath::RoundToInt(Command.Direction.Y));
            PlayerDestination = CurrentCell + Direction;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Player destination predicted: (%d,%d)->(%d,%d)"),
                CurrentTurnId, CurrentCell.X, CurrentCell.Y,
                PlayerDestination.X, PlayerDestination.Y);
        }

        if (UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            DF->UpdateDistanceField(PlayerDestination);

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] DistanceField updated with player destination (%d,%d)"),
                CurrentTurnId, PlayerDestination.X, PlayerDestination.Y);
        }

        if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        {
            if (UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>())
            {
                TArray<AActor*> Enemies;
                Enemies.Reserve(CachedEnemiesForTurn.Num());
                for (const TObjectPtr<AActor>& Enemy : CachedEnemiesForTurn)
                {
                    if (Enemy)
                    {
                        Enemies.Add(Enemy.Get());
                    }
                }

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Intent regeneration: Using %d cached enemies"),
                    CurrentTurnId, Enemies.Num());

                TArray<FEnemyObservation> Observations;
                Observations.Reserve(Enemies.Num());

                for (AActor* Enemy : Enemies)
                {
                    if (!Enemy || !IsValid(PathFinder))
                    {
                        continue;
                    }

                    FEnemyObservation Obs;
                    Obs.GridPosition = PathFinder->WorldToGrid(Enemy->GetActorLocation());
                    Obs.PlayerGridPosition = PlayerDestination;
                    Obs.DistanceInTiles = FGridUtils::ChebyshevDistance(Obs.GridPosition, PlayerDestination);

                    Observations.Add(Obs);
                }

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Built %d observations with player destination (%d,%d)"),
                    CurrentTurnId, Observations.Num(),
                    PlayerDestination.X, PlayerDestination.Y);

                TArray<FEnemyIntent> Intents;
                EnemyAI->CollectIntents(Observations, Enemies, Intents);

                EnemyData->SaveIntents(Intents);
                CachedEnemyIntents = Intents;

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Intents regenerated: %d intents (with player destination)"),
                    CurrentTurnId, Intents.Num());
            }
        }

        // CodeRevision: INC-2025-00017-R1 (Replace HasAnyAttackIntent() wrapper - Phase 2) (2025-11-16 15:15)
        // CodeRevision: INC-2025-1125-R1 (Use DoesAnyIntentHaveAttack helper) (2025-11-25 12:00)
        const bool bHasAttack = DoesAnyIntentHaveAttack();

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] bHasAttack=%s (Simultaneous movement %s)"),
            CurrentTurnId,
            bHasAttack ? TEXT("TRUE") : TEXT("FALSE"),
            bHasAttack ? TEXT("DISABLED") : TEXT("ENABLED"));

        if (bHasAttack)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected (with player destination) -> Sequential mode"),
                CurrentTurnId);

            // CodeRevision: INC-2025-00023-R1 (Sequential move phase after attacks) (2025-11-17 19:15)
            bSequentialModeActive = true;
            bSequentialMovePhaseStarted = false;

            // CodeRevision: INC-2025-1117G-R1 (Cache resolved actions for sequential enemy flow) (2025-11-17 19:30)
            CachedResolvedActions.Reset();

            if (UWorld* LocalWorld = GetWorld())
            {
                UEnemyTurnDataSubsystem* EnemyData = LocalWorld->GetSubsystem<UEnemyTurnDataSubsystem>();
                UTurnCorePhaseManager* PhaseManager = LocalWorld->GetSubsystem<UTurnCorePhaseManager>();

                if (!EnemyData || !PhaseManager)
                {
                    UE_LOG(LogTurnManager, Error,
                        TEXT("[Turn %d] Sequential cache: PhaseManager=%d, EnemyData=%d (missing subsystem)"),
                        CurrentTurnId,
                        PhaseManager != nullptr,
                        EnemyData != nullptr);
                    EndEnemyTurn();
                }
                else
                {
                    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

                    if (APawn* LocalPlayerPawn = UGameplayStatics::GetPlayerPawn(LocalWorld, 0))
                    {
                        TWeakObjectPtr<AActor> PlayerKey(LocalPlayerPawn);
                        if (const FIntPoint* PlayerNextCell = PendingMoveReservations.Find(PlayerKey))
                        {
                            if (IsValid(PathFinder))
                            {
                                const FVector PlayerLoc = LocalPlayerPawn->GetActorLocation();
                                const FIntPoint PlayerCurrentCell = PathFinder->WorldToGrid(PlayerLoc);

                                if (PlayerCurrentCell != *PlayerNextCell)
                                {
                                    FEnemyIntent PlayerIntent;
                                    PlayerIntent.Actor = LocalPlayerPawn;
                                    PlayerIntent.CurrentCell = PlayerCurrentCell;
                                    PlayerIntent.NextCell = *PlayerNextCell;
                                    PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
                                    PlayerIntent.BasePriority = 200;

                                    AllIntents.Add(PlayerIntent);

                                    UE_LOG(LogTurnManager, Log,
                                        TEXT("[Turn %d] Added Player intent to cached resolver: (%d,%d)->(%d,%d)"),
                                        CurrentTurnId, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                                        PlayerNextCell->X, PlayerNextCell->Y);
                                }
                            }
                        }
                    }

                    UE_LOG(LogTurnManager, Log,
                        TEXT("[Turn %d] Sequential cache: %d intents (%d enemies + player)"),
                        CurrentTurnId, AllIntents.Num(), EnemyData->Intents.Num());

                    CachedResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

                    UE_LOG(LogTurnManager, Log,
                        TEXT("[Turn %d] CoreResolvePhase produced %d cached actions"),
                        CurrentTurnId, CachedResolvedActions.Num());

                    if (APawn* LocalPlayerPawn = UGameplayStatics::GetPlayerPawn(LocalWorld, 0))
                    {
                        for (const FResolvedAction& Action : CachedResolvedActions)
                        {
                            if (Action.SourceActor && Action.SourceActor.Get() == LocalPlayerPawn)
                            {
                                DispatchResolvedMove(Action);
                                break;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] No attacks (with player destination) -> Simultaneous move mode"),
                CurrentTurnId);

            ExecuteSimultaneousPhase();
        }
    }
    else if (Command.CommandTag == RogueGameplayTags::InputTag_Turn)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[GameTurnManager] Turn command received but ExecuteTurnFacing not implemented"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[GameTurnManager] ❁ECommand tag does NOT match InputTag.Move or InputTag.Turn (Tag=%s)"),
            *Command.CommandTag.ToString());
    }
}


// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// CodeRevision: INC-2025-00032-R1 (Cleaned up ResolveOrSpawnPathFinder - PathFinder is now Subsystem, not Actor) (2025-01-XX XX:XX)
// Note: Function name suggests "Spawn" but PathFinder is now a Subsystem (automatically created by engine)
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
    if (!HasAuthority()) return;

    // Get PlayerPawn locally instead of using cached member
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnId);
        return;
    }

    bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnId);

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Collecting enemy intents AFTER player move"), CurrentTurnId);

    // CodeRevision: INC-2025-00017-R1 (Replace CollectEnemies() wrapper - Phase 2) (2025-11-16 15:10)
    // CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
    // Direct call to EnemyAISubsystem with fallback
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] ==== START ===="));
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] Before: CachedEnemiesForTurn.Num()=%d"), CachedEnemiesForTurn.Num());

    if (EnemyAISubsystem)
    {
        TArray<AActor*> CollectedEnemies;
        EnemyAISubsystem->CollectAllEnemies(PlayerPawn, CollectedEnemies);
        CachedEnemiesForTurn.Empty();
        CachedEnemiesForTurn = CollectedEnemies;
        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemiesForTurn.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);
        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

        static const FName ActorTagEnemy(TEXT("Enemy"));
        static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;
        int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;
        CachedEnemiesForTurn.Empty();
        CachedEnemiesForTurn.Reserve(Found.Num());

        for (AActor* A : Found)
        {
            if (!IsValid(A) || A == PlayerPawn)
            {
                if (A == PlayerPawn)
                {
                    UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
                }
                continue;
            }

            const int32 TeamId = GetTeamIdOf(A);
            const bool bByTeam = (TeamId == 2 || TeamId == 255);
            const UAbilitySystemComponent* ASC = TryGetASC(A);
            const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));
            const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

            if (bByGTag || bByTeam || bByActorTag)
            {
                CachedEnemiesForTurn.Add(A);
                if (bByGTag) ++NumByTag;
                if (bByTeam) ++NumByTeam;
                if (bByActorTag) ++NumByActorTag;
            }
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ==== RESULT ==== found=%d  collected=%d  byGTag=%d  byTeam=%d  byActorTag=%d"),
            Found.Num(), CachedEnemiesForTurn.Num(), NumByTag, NumByTeam, NumByActorTag);
    }

    // CodeRevision: INC-2025-00017-R1 (Replace CollectIntents() and HasAnyAttackIntent() wrappers - Phase 2) (2025-11-16 15:15)
    // CodeRevision: INC-2025-00029-R1 (Replace CachedPlayerPawn with GetPlayerPawn() - Phase 3.2) (2025-11-16 00:00)
    // Direct call to EnemyAISubsystem
    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys)
    {
        // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
        if (IsValid(PathFinder) && PlayerPawn)
        {
            // Get player grid from GridOccupancySubsystem (reliable source)
            FIntPoint PlayerGrid = FIntPoint(-1, -1);
            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
            {
                PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
            }

            if (UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>())
            {
                DistanceField->UpdateDistanceField(PlayerGrid);
                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] DistanceField updated for PlayerGrid=(%d,%d)"),
                    CurrentTurnId, PlayerGrid.X, PlayerGrid.Y);
            }

            TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, PathFinder.Get(), Observations);
            EnemyTurnDataSys->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] BuildObservations completed: Generated %d observations from %d enemies"),
                CurrentTurnId, Observations.Num(), CachedEnemiesForTurn.Num());
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] BuildObservations: PathFinder=%d PlayerPawn=%d"),
                CurrentTurnId,
                IsValid(PathFinder),
                PlayerPawn != nullptr);
        }

        if (EnemyTurnDataSys->Observations.Num() == 0)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
                CurrentTurnId, CachedEnemiesForTurn.Num());

            if (IsValid(PathFinder) && PlayerPawn && CachedEnemiesForTurn.Num() > 0)
            {
                // Get reliable player grid coordinates from GridOccupancySubsystem
                FIntPoint PlayerGrid = FIntPoint(-1, -1);
                if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
                {
                    PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
                }

                TArray<FEnemyObservation> Observations;
                EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, PathFinder.Get(), Observations);
                EnemyTurnDataSys->Observations = Observations;

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Auto-generated %d observations from %d enemies"),
                    CurrentTurnId, Observations.Num(), CachedEnemiesForTurn.Num());
            }
        }

        if (EnemyTurnDataSys->Observations.Num() != CachedEnemiesForTurn.Num())
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
                CurrentTurnId, EnemyTurnDataSys->Observations.Num(), CachedEnemiesForTurn.Num());

            if (IsValid(PathFinder) && PlayerPawn)
            {
                // Get reliable player grid coordinates from GridOccupancySubsystem
                FIntPoint PlayerGrid = FIntPoint(-1, -1);
                if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
                {
                    PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
                }

                TArray<FEnemyObservation> Observations;
                EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, PathFinder.Get(), Observations);
                EnemyTurnDataSys->Observations = Observations;

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Re-generated %d observations to match enemy count"),
                    CurrentTurnId, Observations.Num());
            }
        }

        TArray<FEnemyIntent> Intents;
        EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemiesForTurn, Intents);
        EnemyTurnDataSys->Intents = Intents;
        CachedEnemyIntents = Intents;
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] Cannot build observations/intents: EnemyAISys=%d, EnemyTurnDataSys=%d"),
            CurrentTurnId,
            EnemyAISys != nullptr,
            EnemyTurnDataSys != nullptr);
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Enemy intent collection complete"), CurrentTurnId);

    const bool bHasAttack = DoesAnyIntentHaveAttack();

    if (bHasAttack)
    {
        ExecuteSequentialPhase();
    }
    else
    {
        ExecuteSimultaneousPhase();
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
    // CoreResolvePhaseの呼び出しとDispatchResolvedMoveの直接実行
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteSimultaneousPhase: World is null"), CurrentTurnId);
        return;
    }

    UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (!PhaseManager || !EnemyData)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteSimultaneousPhase: PhaseManager or EnemyData not found"),
            CurrentTurnId);
        EndEnemyTurn();
        return;
    }

    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

    // プレイヤーのインテントを追加
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (PlayerPawn && IsValid(PathFinder))
    {
        const FVector PlayerLoc = PlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = PathFinder->WorldToGrid(PlayerLoc);

        TWeakObjectPtr<AActor> PlayerKey(PlayerPawn);
        if (const FIntPoint* PlayerNextCell = PendingMoveReservations.Find(PlayerKey))
        {
            if (PlayerCurrentCell != *PlayerNextCell)
            {
                FEnemyIntent PlayerIntent;
                PlayerIntent.Actor = PlayerPawn;
                PlayerIntent.CurrentCell = PlayerCurrentCell;
                PlayerIntent.NextCell = *PlayerNextCell;
                PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
                PlayerIntent.BasePriority = 200;

                AllIntents.Add(PlayerIntent);

                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] ExecuteSimultaneousPhase: Added Player intent (%d,%d)->(%d,%d)"),
                    CurrentTurnId, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                    PlayerNextCell->X, PlayerNextCell->Y);
            }
        }
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteSimultaneousPhase: Processing %d intents via CoreResolvePhase"),
        CurrentTurnId, AllIntents.Num());

    // CoreResolvePhaseを呼び出し
    TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteSimultaneousPhase: CoreResolvePhase produced %d resolved actions"),
        CurrentTurnId, ResolvedActions.Num());

    // 直後にDispatchResolvedMoveをループ実行
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


void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    const int32 NotifiedTurn = Payload ? static_cast<int32>(Payload->EventMagnitude) : -1;
    const int32 CurrentTurn = CurrentTurnId;

    if (NotifiedTurn != CurrentTurn)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("Server: IGNORE stale move notification (notified=%d, current=%d)"),
            NotifiedTurn, CurrentTurn);
        return;
    }

    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
        CurrentTurnId, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

    AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
    FinalizePlayerMove(CompletedActor);

    if (bSequentialModeActive)
    {
        // CodeRevision: INC-2025-1117H-R1 (Consolidate sequential attack dispatch) (2025-11-17 19:10)
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] OnPlayerMoveCompleted: Sequential flow - dispatching cached enemy attacks"),
            CurrentTurnId);

        const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
        TArray<FResolvedAction> AttackActions;
        AttackActions.Reserve(CachedResolvedActions.Num());
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

        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnId);
}


void AGameTurnManagerBase::ExecuteEnemyMoves_Sequential()
{
	// CodeRevision: INC-2025-1127-R1 (Align sequential move dispatch filtering with CoreResolvePhase and surface diagnostics) (2025-11-27 14:15)
	if (!HasAuthority())
	{
		return;
	}

	if (CachedResolvedActions.Num() == 0)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: No cached actions available"),
			CurrentTurnId);
		EndEnemyTurn();
		return;
	}

	const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
	const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
	TArray<FResolvedAction> EnemyMoveActions;
	EnemyMoveActions.Reserve(CachedResolvedActions.Num());

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	int32 TotalActions = 0;
	int32 TotalEnemyMoves = 0;
	int32 TotalEnemyWaits = 0;
	int32 TotalEnemyNonMoves = 0;

	for (const FResolvedAction& Action : CachedResolvedActions)
	{
		++TotalActions;

		AActor* SourceActor = Action.SourceActor.Get();
		if (!SourceActor)
		{
			UE_LOG(LogTurnManager, VeryVerbose,
				TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: Skip action with null SourceActor"),
				CurrentTurnId);
			continue;
		}

		if (PlayerPawn && SourceActor == PlayerPawn)
		{
			UE_LOG(LogTurnManager, VeryVerbose,
				TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: Skip player action %s"),
				CurrentTurnId, *GetNameSafe(SourceActor));
			continue;
		}

		const bool bIsMoveLike =
			Action.FinalAbilityTag.MatchesTag(MoveTag) ||
			Action.AbilityTag.MatchesTag(MoveTag);

		const bool bIsAttackLike =
			Action.FinalAbilityTag.MatchesTag(AttackTag) ||
			Action.AbilityTag.MatchesTag(AttackTag);

		if (bIsMoveLike && !Action.bIsWait)
		{
			++TotalEnemyMoves;
			UE_LOG(LogTurnManager, VeryVerbose,
				TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: Enqueue MOVE for %s (FinalTag=%s, Next=(%d,%d))"),
				CurrentTurnId,
				*GetNameSafe(SourceActor),
				*Action.FinalAbilityTag.ToString(),
				Action.NextCell.X, Action.NextCell.Y);
			EnemyMoveActions.Add(Action);
			continue;
		}

		if (Action.bIsWait)
		{
			++TotalEnemyWaits;
			UE_LOG(LogTurnManager, VeryVerbose,
				TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: Skip WAIT action for %s (FinalTag=%s, AttackLike=%d)"),
				CurrentTurnId,
				*GetNameSafe(SourceActor),
				*Action.FinalAbilityTag.ToString(),
				bIsAttackLike ? 1 : 0);
			continue;
		}

		++TotalEnemyNonMoves;
		UE_LOG(LogTurnManager, VeryVerbose,
			TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: Skip non-move action for %s (FinalTag=%s, AttackLike=%d, Current=(%d,%d), Next=(%d,%d))"),
			CurrentTurnId,
			*GetNameSafe(SourceActor),
			*Action.FinalAbilityTag.ToString(),
			bIsAttackLike ? 1 : 0,
			Action.CurrentCell.X, Action.CurrentCell.Y,
			Action.NextCell.X, Action.NextCell.Y);
	}

	if (EnemyMoveActions.IsEmpty())
	{
		UE_LOG(LogTurnManager, Log,
			TEXT("[Turn %d] ExecuteEnemyMoves_Sequential: No enemy actions to dispatch (Total=%d, Moves=%d, Waits=%d, NonMove=%d)"),
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

    // INC-2025-0002: ログ強化 - bHasAttack判定に使用されるIntents配列の内容を詳細出力
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

        // カウント
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

        // 各Intentの詳細をログ出力
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
    // CodeRevision: INC-2025-00030-R1 (Use GetWorld()->GetSubsystem<>() instead of cached member) (2025-11-16 00:00)
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld() ? GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>() : nullptr;
    if (!EnemyTurnDataSys)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteAttacks: EnemyTurnDataSubsystem is null"),
            CurrentTurnId);
        return;
    }

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnId);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: Barrier::BeginTurn(%d) called"),
                CurrentTurnId, CurrentTurnId);
        }
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] ExecuteAttacks: Closing input gate (attack phase starts)"), CurrentTurnId);
    ApplyWaitInputGate(false);

    UWorld* World = GetWorld();
    const TArray<FResolvedAction>* AttackActionsPtr = nullptr;
    TArray<FResolvedAction> GeneratedActions;

    if (!PreResolvedAttacks.IsEmpty())
    {
        AttackActionsPtr = &PreResolvedAttacks;
    }
    else
    {
        if (World)
        {
            UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
            if (PhaseManager)
            {
                PhaseManager->ExecuteAttackPhaseWithSlots(
                    EnemyTurnDataSys->Intents,
                    GeneratedActions
                );
                AttackActionsPtr = &GeneratedActions;
            }
            else
            {
                UE_LOG(LogTurnManager, Error,
                    TEXT("[Turn %d] ExecuteAttacks: TurnCorePhaseManager not found"),
                    CurrentTurnId);
            }
        }
    }

    if (!AttackActionsPtr)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteAttacks: No attack actions available"),
            CurrentTurnId);
        return;
    }

    const TArray<FResolvedAction>& AttackActions = *AttackActionsPtr;
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteAttacks: Dispatching %d attack actions"),
        CurrentTurnId, AttackActions.Num());

    if (World)
    {
        if (UAttackPhaseExecutorSubsystem* AttackExecutorSubsystem = World->GetSubsystem<UAttackPhaseExecutorSubsystem>())
        {
            AttackExecutorSubsystem->BeginSequentialAttacks(AttackActions, CurrentTurnId);
        }
        else
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ExecuteAttacks: AttackExecutorSubsystem missing"),
                CurrentTurnId);
        }
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

    if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteMovePhase: No enemy intents detected, attempting fallback generation..."),
            CurrentTurnId);

        UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
        // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
        if (EnemyAISys && IsValid(PathFinder) && PlayerPawn && CachedEnemiesForTurn.Num() > 0)
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
            EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, PathFinder.Get(), Observations);
            EnemyData->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Fallback: Generated %d observations"),
                CurrentTurnId, Observations.Num());

            TArray<FEnemyIntent> Intents;
            EnemyAISys->CollectIntents(Observations, CachedEnemiesForTurn, Intents);
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
                CachedEnemiesForTurn.Num());
        }

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
    }

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

    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (PlayerPawn && IsValid(PathFinder))
    {
        const FVector PlayerLoc = PlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = PathFinder->WorldToGrid(PlayerLoc);

        TWeakObjectPtr<AActor> PlayerKey(PlayerPawn);
        if (const FIntPoint* PlayerNextCell = PendingMoveReservations.Find(PlayerKey))
        {
            if (PlayerCurrentCell != *PlayerNextCell)
            {
                FEnemyIntent PlayerIntent;
                PlayerIntent.Actor = PlayerPawn;
                PlayerIntent.CurrentCell = PlayerCurrentCell;
                PlayerIntent.NextCell = *PlayerNextCell;
                PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
                PlayerIntent.BasePriority = 200;

                AllIntents.Add(PlayerIntent);

                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] Added Player intent to ConflictResolver: (%d,%d)->(%d,%d)"),
                    CurrentTurnId, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                    PlayerNextCell->X, PlayerNextCell->Y);
            }
        }
    }

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


void AGameTurnManagerBase::ExecutePlayerMove()
{
    
    if (!HasAuthority())
    {
        return;
    }

// CodeRevision: INC-2025-00017-R1 (Replace GetPlayerPawn() wrapper - Phase 2) (2025-11-16 15:05)
APawn* PlayerPawnNow = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!PlayerPawnNow)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ExecutePlayerMove: PlayerPawn is NULL"),
            CurrentTurnId);
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: ExecutePlayerMove: Pawn=%s, CommandTag=%s, Direction=%s"),
        CurrentTurnId,
        *PlayerPawnNow->GetName(),
        *CachedPlayerCommand.CommandTag.ToString(),
        *CachedPlayerCommand.Direction.ToString());

if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {

UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ==== ASC DIAGNOSTIC ==== Checking granted abilities for %s"),
            CurrentTurnId, *PlayerPawnNow->GetName());
        UE_LOG(LogTurnManager, Warning,
            TEXT("  OwnedTags: %s"),
            *ASC->GetOwnedGameplayTags().ToStringSimple());
        UE_LOG(LogTurnManager, Warning,
            TEXT("  Sending EventTag: %s (Valid: %s)"),
            *Tag_AbilityMove.ToString(),
            Tag_AbilityMove.IsValid() ? TEXT("YES") : TEXT("NO"));

        int32 AbilityIndex = 0;
        int32 PotentialMoveAbilities = 0;
        for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
        {
            if (Spec.Ability)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("  Ability[%d]: %s (Class: %s)"),
                    AbilityIndex,
                    *Spec.Ability->GetName(),
                    *Spec.Ability->GetClass()->GetName());

FGameplayTagContainer FailureTags;
                const bool bCanActivate = Spec.Ability->CanActivateAbility(
                    Spec.Handle,
                    ASC->AbilityActorInfo.Get(),
                    nullptr,
                    nullptr,
                    &FailureTags
                );

                const FString FailureInfo = FailureTags.Num() > 0
                    ? FString::Printf(TEXT(" (FailureTags: %s)"), *FailureTags.ToStringSimple())
                    : TEXT("");

                UE_LOG(LogTurnManager, Warning,
                    TEXT("    - CanActivate: %s%s"),
                    bCanActivate ? TEXT("YES") : TEXT("NO"),
                    *FailureInfo);

const bool bIsMoveAbility = Spec.Ability->GetClass()->GetName().Contains(TEXT("MoveBase"));
                if (bIsMoveAbility)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("    - This is a MOVE ability! CanActivate=%s"),
                        bCanActivate ? TEXT("YES") : TEXT("NO"));
                    if (bCanActivate)
                    {
                        ++PotentialMoveAbilities;
                    }
                }

const FGameplayTagContainer DynamicTags = Spec.GetDynamicSpecSourceTags();
                if (DynamicTags.Num() > 0)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("    - DynamicAbilityTags: %s"),
                        *DynamicTags.ToStringSimple());
                }
            }
            ++AbilityIndex;
        }
        UE_LOG(LogTurnManager, Warning,
            TEXT("==== END ASC DIAGNOSTIC ==== Total=%d, PotentialMoveAbilities=%d"),
            AbilityIndex, PotentialMoveAbilities);

        FGameplayEventData EventData;
        EventData.EventTag = Tag_AbilityMove;
        EventData.Instigator = PlayerPawnNow;
        EventData.Target = PlayerPawnNow;
        EventData.OptionalObject = this; 

const int32 DirX = FMath::RoundToInt(CachedPlayerCommand.Direction.X);
        const int32 DirY = FMath::RoundToInt(CachedPlayerCommand.Direction.Y);
        EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackDir(DirX, DirY));

    // Calculate target cell from cached command
    FIntPoint TargetCell = CachedPlayerCommand.TargetCell;
    // CodeRevision: INC-2025-00032-R1 (Replace CachedPathFinder with PathFinder) (2025-01-XX XX:XX)
    if (IsValid(PathFinder) && (TargetCell.X == 0 && TargetCell.Y == 0))
    {
        const FIntPoint CurrentCell = PathFinder->WorldToGrid(GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation());
        TargetCell = FIntPoint(CurrentCell.X + DirX, CurrentCell.Y + DirY);
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("Turn %d: Sending GameplayEvent %s (TargetCell=(%d,%d), Magnitude=%.0f)"),
        CurrentTurnId,
        *EventData.EventTag.ToString(),
        TargetCell.X,
        TargetCell.Y,
        EventData.EventMagnitude);

        const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

        if (TriggeredCount > 0)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("Turn %d: GA_MoveBase activated (count=%d)"),
                CurrentTurnId, TriggeredCount);
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("Turn %d: ❌No abilities triggered for %s (Expected trigger tag: %s)"),
                CurrentTurnId, *EventData.EventTag.ToString(), *Tag_AbilityMove.ToString());
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ExecutePlayerMove: ASC not found"),
            CurrentTurnId);
    }
}

// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
// CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
{
    // ★★★ START: 新しいガード処理 ★★★
    // CodeRevision: INC-2025-1117F (Prevent move phase from triggering during sequential attack phase)
    if (bSequentialModeActive && !bSequentialMovePhaseStarted)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Ignoring barrier completion during sequential attack phase. Waiting for HandleAttackPhaseCompleted to start the move phase."), FinishedTurnId);
        return;
    }
    // ★★★ END: 新しいガード処理 ★★★

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
    // 攻撃完了後は常に後続の移動フェーズを開始する（ソフトロック修正）
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


void AGameTurnManagerBase::EndEnemyTurn()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnId);

    bool bBarrierQuiet = false;
    int32 InProgressCount = 0;

    // CodeRevision: INC-2025-00028-R1 (Replace CurrentTurnIndex with CurrentTurnId - Phase 3.1) (2025-11-16 00:00)
    if (!CanAdvanceTurn(CurrentTurnId, &bBarrierQuiet, &InProgressCount))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[EndEnemyTurn] ABORT: Cannot end turn %d (actions still in progress)"),
            CurrentTurnId);

        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnId);
            }
        }

        ClearResidualInProgressTags();

        if (bBarrierQuiet && InProgressCount > 0)
        {
            if (CanAdvanceTurn(CurrentTurnId))
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[EndEnemyTurn] Residual InProgress tags cleared, advancing turn without retry"));
                AdvanceTurnAndRestart();
                return;
            }
        }

        if (!bEndTurnPosted)
        {
            bEndTurnPosted = true;

            if (UWorld* World = GetWorld())
            {
                FTimerHandle RetryHandle;
                World->GetTimerManager().SetTimer(
                    RetryHandle,
                    [this]() { EndEnemyTurn(); },
                    0.5f,
                    false
                );

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[EndEnemyTurn] Retry scheduled in 0.5s"));
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Log,
                TEXT("[EndEnemyTurn] Retry suppressed (already posted)"));
        }

        return;
    }

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

const int32 InProgressCount = ASC->GetTagCount(InProgressTag);
        for (int32 i = 0; i < InProgressCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(InProgressTag);
        }
        TotalInProgress += InProgressCount;

const int32 ExecutingCount = ASC->GetTagCount(ExecutingTag);
        for (int32 i = 0; i < ExecutingCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(ExecutingTag);
        }
        TotalExecuting += ExecutingCount;

const int32 MovingCount = ASC->GetTagCount(MovingTag);
        for (int32 i = 0; i < MovingCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(MovingTag);
        }
        TotalMoving += MovingCount;

int32 FallingCount = 0;
        if (FallingTag.IsValid())
        {
            FallingCount = ASC->GetTagCount(FallingTag);
            for (int32 i = 0; i < FallingCount; ++i)
            {
                ASC->RemoveLooseGameplayTag(FallingTag);
            }
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

    // ① 新しい入力ウィンドウを開く（WindowId++）
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->OpenNewInputWindow();
    }

    const int32 CurrentTurnId_Local   = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentTurnId() : CurrentTurnId;
    const int32 CurrentWindowId = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;

    UE_LOG(LogTurnManager, Log,
        TEXT("[GTM] OpenInputWindow: TurnId=%d, WindowId=%d"),
        CurrentTurnId_Local, CurrentWindowId);

    // ② PlayerInputProcessor に「このターン／ウィンドウで受付開始」と伝える
    if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(CurrentTurnId_Local, CurrentWindowId);
    }

    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(CurrentWindowId);
    }

    // ③ 内部状態更新
    ApplyWaitInputGate(true);
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

bool AGameTurnManagerBase::CanAdvanceTurn(int32 TurnId, bool* OutBarrierQuiet, int32* OutInProgressCount) const
{
    
    bool bBarrierQuiet = false;
    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            bBarrierQuiet = Barrier->IsQuiescent(TurnId);

            if (!bBarrierQuiet)
            {
                int32 PendingCount = Barrier->GetPendingActionCount(TurnId);
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[CanAdvanceTurn] Barrier NOT quiescent: Turn=%d PendingActions=%d"),
                    TurnId, PendingCount);
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[CanAdvanceTurn] Barrier subsystem not found"));
            if (OutBarrierQuiet)
            {
                *OutBarrierQuiet = false;
            }
            if (OutInProgressCount)
            {
                *OutInProgressCount = 0;
            }
            return false;
        }
    }
    else if (OutBarrierQuiet)
    {
        *OutBarrierQuiet = false;
    }

    int32 InProgressCount = 0;
    bool bNoInProgressTags = false;
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        InProgressCount = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress);
        if (OutInProgressCount)
        {
            *OutInProgressCount = InProgressCount;
        }
        bNoInProgressTags = (InProgressCount == 0);

        if (!bNoInProgressTags)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[CanAdvanceTurn] InProgress tags still active: Count=%d"),
                InProgressCount);
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[CanAdvanceTurn] Player ASC not found"));
        if (OutInProgressCount)
        {
            *OutInProgressCount = InProgressCount;
        }
        return false;
    }

    if (OutBarrierQuiet)
    {
        *OutBarrierQuiet = bBarrierQuiet;
    }

    const bool bCanAdvance = bBarrierQuiet && bNoInProgressTags;

    if (bCanAdvance)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[CanAdvanceTurn] OK: Turn=%d (Barrier=Quiet, InProgress=0)"),
            TurnId);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CanAdvanceTurn] ❌BLOCKED: Turn=%d (Barrier=%s, InProgress=%d)"),
            TurnId,
            bBarrierQuiet ? TEXT("Quiet") : TEXT("Busy"),
            InProgressCount);
    }

    return bCanAdvance;
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
    // P1: Register wait actions with the barrier and complete them immediately.
    if (Action.NextCell == FIntPoint(-1, -1) || Action.NextCell == Action.CurrentCell)
    {
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

    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = LocalPathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, bIsPlayerUnit);

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

    int32 ClearedCount = 0;
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�E�E�ECleared blocking State.Ability.Executing tag from %s"),
            *GetNameSafe(Unit));
    }
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�E�E�ECleared blocking State.Action.InProgress tag from %s"),
            *GetNameSafe(Unit));
    }

    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Moving))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Moving);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] Cleared residual State.Moving tag from %s (Gemini fix)"),
            *GetNameSafe(Unit));
    }

    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));
    if (FallingTag.IsValid() && ASC->HasMatchingGameplayTag(FallingTag))
    {
        ASC->RemoveLooseGameplayTag(FallingTag);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] Cleared residual Movement.Mode.Falling tag from %s (Gemini fix)"),
            *GetNameSafe(Unit));
    }

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

