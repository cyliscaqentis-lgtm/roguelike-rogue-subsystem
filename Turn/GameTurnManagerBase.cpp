#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"  
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/AABB.h"  
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnEventDispatcher.h"
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
#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "Character/EnemyUnitBase.h"
#include "Character/UnitBase.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "Debug/DebugObserverCSV.h"
#include "Grid/GridPathfindingLibrary.h"
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
            Barrier->BeginTurn(CurrentTurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: Barrier::BeginTurn(%d) called"),
                CurrentTurnIndex, CurrentTurnIndex);
        }
    }
    
    UWorld* World = GetWorld();
    if (World)
    {
        CommandHandler = World->GetSubsystem<UTurnCommandHandler>();
        EventDispatcher = World->GetSubsystem<UTurnEventDispatcher>();
        DebugSubsystem = World->GetSubsystem<UTurnDebugSubsystem>();
        TurnFlowCoordinator = World->GetSubsystem<UTurnFlowCoordinator>();
        PlayerInputProcessor = World->GetSubsystem<UPlayerInputProcessor>();

        if (!CommandHandler)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UTurnCommandHandler subsystem"));
        }
        if (!EventDispatcher)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Failed to get UTurnEventDispatcher subsystem"));
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

        UE_LOG(LogTurnManager, Log, TEXT("Subsystems initialized: CommandHandler=%s, EventDispatcher=%s, DebugSubsystem=%s, TurnFlowCoordinator=%s, PlayerInputProcessor=%s"),
            CommandHandler ? TEXT("OK") : TEXT("FAIL"),
            EventDispatcher ? TEXT("OK") : TEXT("FAIL"),
            DebugSubsystem ? TEXT("OK") : TEXT("FAIL"),
            TurnFlowCoordinator ? TEXT("OK") : TEXT("FAIL"),
            PlayerInputProcessor ? TEXT("OK") : TEXT("FAIL"));
    }

if (!IsValid(CachedPlayerPawn))
    {
        CachedPlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
        if (CachedPlayerPawn)
        {
            UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CachedPlayerPawn initialized: %s"), *CachedPlayerPawn->GetName());

if (CachedPlayerPawn->IsA(ASpectatorPawn::StaticClass()))
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
                                CachedPlayerPawn = PlayerUnit;
                                UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Possessed PlayerUnit: %s"), *PlayerUnit->GetName());
                                break;
                            }
                        }
                    }
                }

                if (CachedPlayerPawn->IsA(ASpectatorPawn::StaticClass()))
                {
                    UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Could not find/possess BPPlayerUnit (may be possessed later)"));
                }
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get PlayerPawn"));
        }
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
    CachedPathFinder = PathFinder;
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: PathFinder ready: %s"), *GetNameSafe(PathFinder));

CollectEnemies();
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CollectEnemies completed (%d enemies)"), CachedEnemies.Num());

EnemyAISubsystem = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get EnemyAISubsystem!"));
        return;
    }

    EnemyTurnData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();
    if (!EnemyTurnData)
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
            
            Barrier->OnAllMovesFinished.RemoveDynamic(this, &ThisClass::OnAllMovesFinished);
            Barrier->OnAllMovesFinished.AddDynamic(this, &ThisClass::OnAllMovesFinished);
            UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Barrier delegate bound"));
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

void AGameTurnManagerBase::HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys)
{
    
    if (!DungeonSys)
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: DungeonSys not ready"));
        return;
    }

if (!PathFinder)
    {
        UE_LOG(LogTurnManager, Log, TEXT("HandleDungeonReady: Creating PathFinder..."));
        ResolveOrSpawnPathFinder();
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

CachedPathFinder = PathFinder;

if (ADungeonFloorGenerator* Floor = DungeonSys->GetFloorGenerator())
    {
        FGridInitParams InitParams;
        InitParams.GridCostArray = Floor->GridCells;
        InitParams.MapSize = FVector(Floor->GridWidth, Floor->GridHeight, 0.f);
        InitParams.TileSizeCM = Floor->CellSize;
        InitParams.Origin = FVector::ZeroVector;

        PathFinder->InitializeFromParams(InitParams);

        UE_LOG(LogTurnManager, Warning, TEXT("[PF.Init] Size=%dx%d Cell=%d Origin=(%.1f,%.1f,%.1f) Walkables=%d"),
            Floor->GridWidth, Floor->GridHeight, Floor->CellSize,
            InitParams.Origin.X, InitParams.Origin.Y, InitParams.Origin.Z,
            Floor->GridCells.Num());

FVector TestWorld(950.f, 3050.f, 0.f);
        int32 TestStatus = PathFinder->ReturnGridStatus(TestWorld);
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

AGridPathfindingLibrary* AGameTurnManagerBase::GetCachedPathFinder() const
{
    
    if (IsValid(PathFinder.Get()))
    {
        return PathFinder.Get();
    }

if (CachedPathFinder.IsValid())
    {
        AGridPathfindingLibrary* PF = CachedPathFinder.Get();
        
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        AGridPathfindingLibrary* PF = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

if (ATBSLyraGameMode* GM = GetWorld()->GetAuthGameMode<ATBSLyraGameMode>())
    {
        AGridPathfindingLibrary* PF = GM->GetPathFinder();
        if (IsValid(PF))
        {
            const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
            const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
            return PF;
        }
    }

    return nullptr;
}

void AGameTurnManagerBase::BuildAllObservations()
{
    SCOPED_NAMED_EVENT(BuildAllObservations, FColor::Orange);

    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: EnemyAISubsystem not found"), CurrentTurnIndex);
        return;
    }

TArray<AActor*> Enemies;
    Enemies.Reserve(CachedEnemies.Num());
    for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
    {
        if (Enemy)
        {
            Enemies.Add(Enemy.Get());
        }
    }

AGridPathfindingLibrary* CachedPF = GetCachedPathFinder();
    AActor* Player = GetPlayerActor();

    if (!CachedPF || !Player)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: PathFinder or Player not found"), CurrentTurnIndex);
        return;
    }

TArray<FEnemyObservation> Observations;
    EnemyAISubsystem->BuildObservations(Enemies, Player, CachedPF, Observations);

if (EnemyTurnData)
    {
        EnemyTurnData->Observations = Observations;
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] BuildAllObservations: Synced %d observations to EnemyTurnData"),
            CurrentTurnIndex, Observations.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: EnemyTurnData is null, cannot sync!"),
            CurrentTurnIndex);
    }
}

void AGameTurnManagerBase::NotifyPlayerInputReceived()
{
    UE_LOG(LogTurnManager, Log, TEXT("[Turn%d]NotifyPlayerInputReceived"), CurrentTurnIndex);

if (PlayerInputProcessor)
    {
        PlayerInputProcessor->NotifyPlayerInputReceived();
    }

    if (EventDispatcher)
    {
        EventDispatcher->BroadcastPlayerInputReceived();
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("UTurnEventDispatcher not available"));
    }

if (WaitingForPlayerInput)
    {
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);

if (CommandHandler)
        {
            CommandHandler->EndInputWindow();
        }
    }
    ContinueTurnAfterInput();
}

bool AGameTurnManagerBase::SendGameplayEventWithResult(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload)
{
    if (!Target)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] SendGameplayEventWithResult: Target is nullptr"), CurrentTurnIndex);
        return false;
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
    {
        ASC->HandleGameplayEvent(EventTag, &Payload);
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] GameplayEvent sent: %s to %s"), CurrentTurnIndex, *EventTag.ToString(), *Target->GetName());
        return true;
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] SendGameplayEventWithResult: Target %s has no ASC"), CurrentTurnIndex, *Target->GetName());
    return false;
}

APlayerController* AGameTurnManagerBase::GetPlayerController_TBS() const
{
    return UGameplayStatics::GetPlayerController(GetWorld(), 0);
}

APawn* AGameTurnManagerBase::GetPlayerPawnCachedOrFetch()
{
    if (!IsValid(CachedPlayerPawn))
    {
        CachedPlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    }
    return CachedPlayerPawn;
}

APawn* AGameTurnManagerBase::GetPlayerPawn() const
{
    if (UWorld* World = GetWorld())
    {
        return UGameplayStatics::GetPlayerPawn(World, 0);
    }
    return nullptr;
}

AActor* AGameTurnManagerBase::GetPlayerActor() const
{
    if (CachedPlayerActor.IsValid())
    {
        return CachedPlayerActor.Get();
    }

    if (UWorld* World = GetWorld())
    {
        AActor* Player = UGameplayStatics::GetPlayerPawn(World, 0);
        if (Player)
        {
            CachedPlayerActor = Player;
            UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] Player cached: %s"), CurrentTurnIndex, *Player->GetName());
            return Player;
        }
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Player not found"), CurrentTurnIndex);
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

if (EventDispatcher)
    {
        EventDispatcher->BroadcastPhaseChanged(OldPhase, PhaseTag);
    }

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
            CurrentTurnIndex, InputWindowId);
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
            CurrentTurnIndex);
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

namespace
{
    FORCEINLINE UAbilitySystemComponent* TryGetASC(const AActor* Actor)
    {
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
            return ASI->GetAbilitySystemComponent();
        if (const APawn* P = Cast<APawn>(Actor))
                
            if (const IAbilitySystemInterface* C = Cast<IAbilitySystemInterface>(P->GetController()))
                return C->GetAbilitySystemComponent();
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

void AGameTurnManagerBase::CollectEnemies_Implementation()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] ==== START ===="));
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] Before: CachedEnemies.Num()=%d"), CachedEnemies.Num());

if (EnemyAISubsystem)
    {
        TArray<AActor*> CollectedEnemies;
        EnemyAISubsystem->CollectAllEnemies(CachedPlayerPawn, CollectedEnemies);

        CachedEnemies.Empty();
        CachedEnemies = CollectedEnemies;

        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemies.Num());
        return;
    }

UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));

TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);

    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

CachedEnemies.Empty();
    CachedEnemies.Reserve(Found.Num());

    for (AActor* A : Found)
    {
        
        if (!IsValid(A))
        {
            continue;
        }

if (A == CachedPlayerPawn)
        {
            UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            continue;
        }

        const int32 TeamId = GetTeamIdOf(A);

const bool bByTeam = (TeamId == 2 || TeamId == 255);

        const UAbilitySystemComponent* ASC = TryGetASC(A);
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));

        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

if (bByGTag || bByTeam || bByActorTag)
        {
            CachedEnemies.Add(A);

            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;

const int32 Index = CachedEnemies.Num() - 1;
            if (Index < 3 || Index == 31)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[CollectEnemies] Added[%d]: %s (TeamId=%d, GTag=%d, ByTeam=%d, ActorTag=%d)"),
                    Index, *A->GetName(), TeamId, bByGTag, bByTeam, bByActorTag);
            }
        }
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[CollectEnemies] ==== RESULT ==== found=%d  collected=%d  byGTag=%d  byTeam=%d  byActorTag=%d"),
        Found.Num(), CachedEnemies.Num(), NumByTag, NumByTeam, NumByActorTag);

if (CachedEnemies.Num() == 0 && Found.Num() > 1)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[CollectEnemies] ❌Failed to collect any enemies from %d Pawns!"),
            Found.Num());
        UE_LOG(LogTurnManager, Error,
            TEXT("[CollectEnemies] Troubleshooting:"));
        UE_LOG(LogTurnManager, Error,
            TEXT("  1. Check if TeamID assignment is working (expected: 2 or 255)"));
        UE_LOG(LogTurnManager, Error,
            TEXT("  2. Check if GameplayTag 'Faction.Enemy' is applied"));
        UE_LOG(LogTurnManager, Error,
            TEXT("  3. Check if ActorTag 'Enemy' is set in Blueprint"));
    }
    
    else if (CachedEnemies.Num() > 0 && CachedEnemies.Num() < 32 && Found.Num() >= 32)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] Collected %d enemies, but expected around 32 from %d Pawns"),
            CachedEnemies.Num(), Found.Num());
    }
}

void AGameTurnManagerBase::CollectIntents_Implementation()
{
    
    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (!EnemyAISys || !EnemyTurnDataSys)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Missing subsystem - EnemyAI=%d, EnemyTurnData=%d"),
            CurrentTurnIndex,
            EnemyAISys != nullptr,
            EnemyTurnDataSys != nullptr);
        return;
    }

if (EnemyTurnDataSys->Observations.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
            CurrentTurnIndex, CachedEnemies.Num());

if (CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
        {
            TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
            EnemyTurnDataSys->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Auto-generated %d observations from %d enemies"),
                CurrentTurnIndex, Observations.Num(), CachedEnemies.Num());
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] Cannot auto-generate observations: PathFinder=%d, PlayerPawn=%d, Enemies=%d"),
                CurrentTurnIndex,
                CachedPathFinder.IsValid(),
                CachedPlayerPawn != nullptr,
                CachedEnemies.Num());
            return;
        }
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents input: Observations=%d, Enemies=%d"),
        CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

if (EnemyTurnDataSys->Observations.Num() != CachedEnemies.Num())
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

if (CachedPathFinder.IsValid() && CachedPlayerPawn)
        {
            TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
            EnemyTurnDataSys->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Re-generated %d observations to match enemy count"),
                CurrentTurnIndex, Observations.Num());
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Cannot re-generate observations"), CurrentTurnIndex);
            return;
        }
    }

TArray<FEnemyIntent> Intents;
    EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemies, Intents);

EnemyTurnDataSys->Intents = Intents;

int32 AttackCount = 0, MoveCount = 0, WaitCount = 0, OtherCount = 0;

static const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
    static const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    static const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag))
            ++AttackCount;
        else if (Intent.AbilityTag.MatchesTag(MoveTag))
            ++MoveCount;
        else if (Intent.AbilityTag.MatchesTag(WaitTag))
            ++WaitCount;
        else
            ++OtherCount;
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents -> %d intents (Attack=%d, Move=%d, Wait=%d, Other=%d)"),
        CurrentTurnIndex, Intents.Num(), AttackCount, MoveCount, WaitCount, OtherCount);

if (Intents.Num() == 0 && EnemyTurnDataSys->Observations.Num() > 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: ❌Failed to generate intents from %d observations!"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() < EnemyTurnDataSys->Observations.Num())
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: Generated %d intents from %d observations (some enemies may have invalid intent)"),
            CurrentTurnIndex, Intents.Num(), EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() == EnemyTurnDataSys->Observations.Num() && Intents.Num() > 0)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] CollectIntents: Successfully generated all intents"),
            CurrentTurnIndex);
    }
}



void AGameTurnManagerBase::GetEnemyIntentsBP_Implementation(TArray<FEnemyIntent>& OutIntents) const
{
    OutIntents.Reset();
    if (EnemyTurnData)
    {
        OutIntents = EnemyTurnData->Intents;
    }
}

bool AGameTurnManagerBase::HasAnyAttackIntent() const
{
    TArray<FEnemyIntent> Intents;
    GetEnemyIntentsBP(Intents);

    if (Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] No intents found"), CurrentTurnIndex);
        return false;
    }

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;

    for (const FEnemyIntent& I : Intents)
    {
        if (I.AbilityTag.MatchesTag(AttackTag))
        {
            if (I.Actor.IsValid())
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Attack Intent found: %s"), CurrentTurnIndex, *I.Actor->GetName());
                return true;
            }
        }
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] No attack Intent: move only"), CurrentTurnIndex);
    return false;
}


void AGameTurnManagerBase::BuildAllyIntents_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (AllyData && AllyData->HasAllies())
    {
        AllyData->BuildAllAllyIntents(Context);
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildAllyIntents: %d allies"), CurrentTurnIndex, AllyData->GetAllyCount());
    }
}

void AGameTurnManagerBase::ExecuteAllyActions_Implementation(FTurnContext& Context)
{
    UAllyTurnDataSubsystem* AllyData = GetWorld()->GetSubsystem<UAllyTurnDataSubsystem>();
    if (!AllyData || !AllyData->HasAllies())
    {
        return;
    }

    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ExecuteAllyActions: %d allies"), CurrentTurnIndex, AllyData->GetAllyCount());
}

void AGameTurnManagerBase::BuildSimulBatch_Implementation()
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildSimulBatch called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::CommitSimulStep_Implementation(const FSimulBatch& Batch)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CommitSimulStep called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::ResolveConflicts_Implementation(TArray<FPendingMove>& Moves)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ResolveConflicts called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CheckInterruptWindow called (Blueprint)"), CurrentTurnIndex);
}


void AGameTurnManagerBase::SendGameplayEvent(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload)
{
    if (!Target)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] SendGameplayEvent: Target is nullptr"), CurrentTurnIndex);
        return;
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
    {
        ASC->HandleGameplayEvent(EventTag, &Payload);
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] GameplayEvent sent: %s to %s"), CurrentTurnIndex, *EventTag.ToString(), *Target->GetName());
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] SendGameplayEvent: Target %s has no ASC"), CurrentTurnIndex, *Target->GetName());
    }
}

void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnCombineSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::OnBreedingSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnBreedingSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::OnPotSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnPotSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::OnTrapSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnTrapSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::OnStatusEffectSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnStatusEffectSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::OnItemSystemUpdate_Implementation(const FTurnContext& Context)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnItemSystemUpdate called (Blueprint)"), CurrentTurnIndex);
}

void AGameTurnManagerBase::GetCachedEnemies(TArray<AActor*>& OutEnemies) const
{
    OutEnemies.Reset();
    OutEnemies.Reserve(CachedEnemies.Num());

    for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
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
    DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnIndex);  
    DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);
    DOREPLIFETIME(AGameTurnManagerBase, bPlayerMoveInProgress);
}

void AGameTurnManagerBase::OnEnemyAttacksCompleted()
{
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemy attacks completed"), CurrentTurnIndex);
    OnAllAbilitiesCompleted();
}

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnIndex);

if (APawn* PlayerPawn = GetPlayerPawn())
    {
        if (CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = PlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[AdvanceTurn] PLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y, *PlayerLoc.ToCompactString());
        }
    }

if (!CanAdvanceTurn(CurrentTurnIndex))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[AdvanceTurnAndRestart] ABORT: Cannot advance turn %d (safety check failed)"),
            CurrentTurnIndex);

if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
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

const int32 PreviousTurn = CurrentTurnIndex;

if (TurnFlowCoordinator)
    {
        
        TurnFlowCoordinator->EndTurn();
        TurnFlowCoordinator->AdvanceTurn();
    }

if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnEnded(PreviousTurn);
    }

    CurrentTurnIndex++;

bEndTurnPosted = false;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d -> %d (bEndTurnPosted reset)"),
        PreviousTurn, CurrentTurnIndex);

if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[AdvanceTurnAndRestart] Barrier::BeginTurn(%d) called"),
                CurrentTurnIndex);
        }
    }

int32 SavedCount = 0;
    for (UObject* Observer : DebugObservers)
    {
        if (UDebugObserverCSV* CSV = Cast<UDebugObserverCSV>(Observer))
        {
            FString Filename = FString::Printf(TEXT("TurnDebug_Turn%d.csv"), PreviousTurn);
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

if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("UTurnEventDispatcher not available"));
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnIndex);

OnTurnStartedHandler(CurrentTurnIndex);
}

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnIndex);

    bTurnStarted = true;

if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("UTurnEventDispatcher not available"));
    }

    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnIndex);

OnTurnStartedHandler(CurrentTurnIndex);
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

if (CachedPlayerPawn)
    {
        if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CachedPlayerPawn))
        {
            return ASI->GetAbilitySystemComponent();
        }
    }

    UE_LOG(LogTurnManager, Error, TEXT("GetPlayerASC: Failed to find ASC"));
    return nullptr;
}

void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnIndex)
{
    if (!HasAuthority()) return;

    CurrentTurnIndex = TurnIndex;

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnIndex);

ClearResidualInProgressTags();
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ClearResidualInProgressTags called at turn start"),
        TurnIndex);

if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            GridOccupancy->SetCurrentTurnId(TurnIndex);
            GridOccupancy->PurgeOutdatedReservations(TurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] PurgeOutdatedReservations called at turn start (before player input)"),
                TurnIndex);

TArray<AActor*> AllUnits;
            if (CachedPlayerPawn)
            {
                AllUnits.Add(CachedPlayerPawn);
            }
            AllUnits.Append(CachedEnemies);

            if (AllUnits.Num() > 0)
            {
                GridOccupancy->RebuildFromWorldPositions(AllUnits);
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] RebuildFromWorldPositions called - rebuilding occupancy from physical positions (%d units)"),
                    TurnIndex, AllUnits.Num());
            }
            else
            {
                
                GridOccupancy->EnforceUniqueOccupancy();
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] EnforceUniqueOccupancy called (fallback - no units cached yet)"),
                    TurnIndex);
            }
        }
    }

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        CachedPlayerPawn = PC->GetPawn();

if (CachedPlayerPawn && CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] PLAYER POSITION AT TURN START: Grid(%d,%d) World(%s)"),
                TurnIndex, PlayerGrid.X, PlayerGrid.Y, *PlayerLoc.ToCompactString());
        }

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] CachedPlayerPawn re-cached: %s"),
            TurnIndex, CachedPlayerPawn ? *CachedPlayerPawn->GetName() : TEXT("NULL"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] PlayerController not found!"), TurnIndex);
    }

BeginPhase(Phase_Turn_Init);
    BeginPhase(Phase_Player_Wait);
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] INPUT GATE OPENED EARLY (before DistanceField update)"), TurnIndex);

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CachedEnemies.Num() BEFORE CollectEnemies = %d"),
        TurnIndex, CachedEnemies.Num());

    CollectEnemies();

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CachedEnemies.Num() AFTER CollectEnemies = %d"),
        TurnIndex, CachedEnemies.Num());

    if (CachedEnemies.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectEnemies returned 0, trying fallback with ActorTag 'Enemy'"),
            TurnIndex);

        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("Enemy"), Found);

        for (AActor* A : Found)
        {
            if (IsValid(A) && A != CachedPlayerPawn)
            {
                CachedEnemies.Add(A);
            }
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Fallback collected %d enemies with ActorTag 'Enemy'"),
            TurnIndex, CachedEnemies.Num());
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Pre-DistanceField check: PlayerPawn=%s, PathFinder=%s, Enemies=%d"),
        TurnIndex,
        CachedPlayerPawn ? TEXT("Valid") : TEXT("NULL"),
        CachedPathFinder.IsValid() ? TEXT("Valid") : TEXT("Invalid"),
        CachedEnemies.Num());

    if (CachedPlayerPawn && CachedPathFinder.IsValid())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Attempting to update DistanceField..."), TurnIndex);

        UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>();
        if (DistanceField)
        {
            FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());

            TSet<FIntPoint> EnemyPositions;
            for (AActor* Enemy : CachedEnemies)
            {
                if (IsValid(Enemy))
                {
                    FIntPoint EnemyGrid = CachedPathFinder->WorldToGrid(Enemy->GetActorLocation());
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
                TurnIndex, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), MaxD, Margin);

DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

bool bAnyUnreached = false;
            for (const FIntPoint& C : EnemyPositions)
            {
                const int32 D = DistanceField->GetDistance(C);
                if (D < 0)
                {
                    bAnyUnreached = true;
                    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Enemy at (%d,%d) unreached! Dist=%d"),
                        TurnIndex, C.X, C.Y, D);
                }
            }

            if (bAnyUnreached)
            {
                UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ❌DistanceField has unreachable enemies with Margin=%d"),
                    TurnIndex, Margin);
            }
            else
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemies reachable with Margin=%d"),
                    TurnIndex, Margin);
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] DistanceFieldSubsystem is NULL!"), TurnIndex);
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Cannot update DistanceField:"), TurnIndex);
        if (!CachedPlayerPawn)
            UE_LOG(LogTurnManager, Error, TEXT("  - CachedPlayerPawn is NULL"));
        if (!CachedPathFinder.IsValid())
            UE_LOG(LogTurnManager, Error, TEXT("  - CachedPathFinder is Invalid"));
    }

UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Generating preliminary enemy intents at turn start..."),
        TurnIndex);

    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
    {
        
        TArray<FEnemyObservation> PreliminaryObs;
        EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), PreliminaryObs);
        EnemyTurnDataSys->Observations = PreliminaryObs;

TArray<FEnemyIntent> PreliminaryIntents;
        EnemyAISys->CollectIntents(PreliminaryObs, CachedEnemies, PreliminaryIntents);
        EnemyTurnDataSys->Intents = PreliminaryIntents;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Preliminary intents generated: %d intents from %d enemies (will be updated after player move)"),
            TurnIndex, PreliminaryIntents.Num(), CachedEnemies.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Cannot generate preliminary intents: EnemyAISys=%d, EnemyTurnDataSys=%d, PathFinder=%d, PlayerPawn=%d, Enemies=%d"),
            TurnIndex,
            EnemyAISys != nullptr,
            EnemyTurnDataSys != nullptr,
            CachedPathFinder.IsValid(),
            CachedPlayerPawn != nullptr,
            CachedEnemies.Num());
    }

UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnIndex);

}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    
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
        
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
            *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId, Command.TargetCell.X, Command.TargetCell.Y);

if (Command.TurnId != CurrentTurnIndex && Command.TurnId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
                Command.TurnId, CurrentTurnIndex);
            return;
        }

if (Command.WindowId != InputWindowId && Command.WindowId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GameTurnManager] Command REJECTED - WindowId mismatch (%d != %d) | Turn=%d"),
                Command.WindowId, InputWindowId, CurrentTurnIndex);
            return;
        }

if (bPlayerMoveInProgress)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Move in progress, ignoring command"));

if (APlayerController* PC = Cast<APlayerController>(GetPlayerPawn()->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    TPCB->Client_NotifyMoveRejected();
                }
            }

            return;
        }

        if (!WaitingForPlayerInput)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Not waiting for input"));

if (APlayerController* PC = Cast<APlayerController>(GetPlayerPawn()->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    TPCB->Client_NotifyMoveRejected();
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

APawn* PlayerPawn = GetPlayerPawn();
    if (!PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] PlayerPawn not found"));
        return;
    }

UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
    if (!ASC)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] Player ASC not found"));
        return;
    }

FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove; 
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;
    EventData.OptionalObject = this; 

const int32 DirX = FMath::RoundToInt(Command.Direction.X);
    const int32 DirY = FMath::RoundToInt(Command.Direction.Y);
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackDir(DirX, DirY));

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

if (CachedPathFinder.IsValid())
    {
        const FIntPoint CurrentCell = CachedPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        const FIntPoint TargetCell(
            CurrentCell.X + static_cast<int32>(Command.Direction.X),
            CurrentCell.Y + static_cast<int32>(Command.Direction.Y));

const bool bTerrainBlocked = !CachedPathFinder->IsCellWalkableIgnoringActor(TargetCell, PlayerPawn);

bool bCornerCutting = false;
        const int32 AbsDirX = FMath::Abs(FMath::RoundToInt(Command.Direction.X));
        const int32 AbsDirY = FMath::Abs(FMath::RoundToInt(Command.Direction.Y));
        const bool bIsDiagonalMove = (AbsDirX == 1 && AbsDirY == 1);

        if (bIsDiagonalMove)
        {
            
            const FIntPoint Side1 = CurrentCell + FIntPoint(static_cast<int32>(Command.Direction.X), 0);  
            const FIntPoint Side2 = CurrentCell + FIntPoint(0, static_cast<int32>(Command.Direction.Y));  
            const bool bSide1Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side1, PlayerPawn);
            const bool bSide2Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side2, PlayerPawn);

if (!bSide1Walkable || !bSide2Walkable)
            {
                bCornerCutting = true;
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] CORNER CUTTING BLOCKED: (%d,%d)->(%d,%d) - at least one shoulder blocked [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
            else
            {
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[MovePrecheck] Diagonal move OK: (%d,%d)->(%d,%d) - both shoulders clear [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
        }

        bool bOccupied = false;
        bool bSwapDetected = false;
        AActor* OccupyingActor = nullptr;
        UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>();
        if (OccSys)
        {
            OccupyingActor = OccSys->GetActorAtCell(TargetCell);
            if (OccupyingActor == PlayerPawn)
            {
                OccupyingActor = nullptr;
            }
        }

        if (!OccupyingActor && CachedPathFinder.IsValid())
        {
            for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
            {
        if (!Enemy || Enemy.Get() == PlayerPawn)
                {
                    continue;
                }

                const FIntPoint EnemyCell = CachedPathFinder->WorldToGrid(Enemy->GetActorLocation());
                if (EnemyCell == TargetCell)
                {
                    OccupyingActor = Enemy.Get();
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[MovePrecheck] Spatial occupancy detected: %s is at (%d,%d)"),
                        *GetNameSafe(OccupyingActor), TargetCell.X, TargetCell.Y);
                    break;
                }
            }
        }

        if (OccupyingActor && OccupyingActor != PlayerPawn)
        {
            bOccupied = true;

            if (UEnemyTurnDataSubsystem* EnemyTurnDataSys = World->GetSubsystem<UEnemyTurnDataSubsystem>())
            {
                const FEnemyIntent* Intent = nullptr;
                for (const FEnemyIntent& I : EnemyTurnDataSys->Intents)
                {
                    if (I.Actor.Get() == OccupyingActor)
                    {
                        Intent = &I;
                        break;
                    }
                }

                if (Intent && Intent->NextCell == CurrentCell)
                {
                    bSwapDetected = true;
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[MovePrecheck] SWAP DETECTED: Player (%d,%d)->(%d,%d), Enemy %s (%d,%d)->(%d,%d)"),
                        CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                        *GetNameSafe(OccupyingActor),
                        TargetCell.X, TargetCell.Y, Intent->NextCell.X, Intent->NextCell.Y);
                }
            }
        }

if (bTerrainBlocked || bOccupied || bCornerCutting)
        {
            const TCHAR* BlockReason = bCornerCutting ? TEXT("corner_cutting") :
                                       bTerrainBlocked ? TEXT("terrain") :
                                       bSwapDetected ? TEXT("swap") : TEXT("occupied");
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] BLOCKED by %s: Cell (%d,%d) | From=(%d,%d) | Applying FACING ONLY (No Turn)"),
                BlockReason, TargetCell.X, TargetCell.Y, CurrentCell.X, CurrentCell.Y);

const int32 TargetCost = CachedPathFinder->GetGridCost(TargetCell.X, TargetCell.Y);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Target cell (%d,%d) GridCost=%d (expected: 3=Walkable, -1=Blocked)"),
                TargetCell.X, TargetCell.Y, TargetCost);

if (!bTerrainBlocked && bOccupied && OccupyingActor)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] Occupied by: %s%s"),
                    *GetNameSafe(OccupyingActor),
                    bSwapDetected ? TEXT(" (SWAP detected)") : TEXT(""));
            }

const FIntPoint Directions[4] = {
                FIntPoint(1, 0),   
                FIntPoint(-1, 0),  
                FIntPoint(0, 1),   
                FIntPoint(0, -1)   
            };
            const TCHAR* DirNames[4] = { TEXT("Right"), TEXT("Left"), TEXT("Up"), TEXT("Down") };

            UE_LOG(LogTurnManager, Warning, TEXT("[MovePrecheck] Surrounding cells from (%d,%d):"), CurrentCell.X, CurrentCell.Y);
            for (int32 i = 0; i < 4; ++i)
            {
                const FIntPoint CheckCell = CurrentCell + Directions[i];
                const int32 Cost = CachedPathFinder->GetGridCost(CheckCell.X, CheckCell.Y);
                const bool bWalkable = CachedPathFinder->IsCellWalkableIgnoringActor(CheckCell, PlayerPawn);
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
                    
                    TPCB->Client_ApplyFacingNoTurn(InputWindowId, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_ApplyFacingNoTurn RPC (WindowId=%d, no turn consumed)"),
                        InputWindowId);

TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_NotifyMoveRejected to reset client latch"));
                }
            }

UE_LOG(LogTurnManager, Verbose, TEXT("[MovePrecheck] Server state after FACING ONLY:"));
            UE_LOG(LogTurnManager, Verbose, TEXT("  - WaitingForPlayerInput: %d (STAYS TRUE - no turn consumed)"), WaitingForPlayerInput);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - bPlayerMoveInProgress: %d (STAYS FALSE)"), bPlayerMoveInProgress);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - InputWindowId: %d (unchanged)"), InputWindowId);

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
                    TPCB->Client_ApplyFacingNoTurn(InputWindowId, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] Sent Client_ApplyFacingNoTurn RPC (WindowId=%d, reservation failed)"),
                        InputWindowId);

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

const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

    if (TriggeredCount > 0)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] GAS activated for Move (count=%d)"), TriggeredCount);
        bPlayerMoveInProgress = true;

if (CommandHandler)
        {
            CommandHandler->MarkCommandAsAccepted(Command);
            UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Command marked as accepted (prevents duplicates)"));
        }

if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
        {
            if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
            {
                TPCB->Client_ConfirmCommandAccepted(InputWindowId);
                UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Sent Client_ConfirmCommandAccepted ACK (WindowId=%d)"), InputWindowId);
            }
        }

        if (PlayerInputProcessor)
        {
            PlayerInputProcessor->CloseInputWindow();
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] PlayerInputProcessor not available for CloseInputWindow"));
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] GAS activation failed - No abilities triggered"));
        ApplyWaitInputGate(true);   
    }

CachedPlayerCommand = Command;

const FGameplayTag InputMove = RogueGameplayTags::InputTag_Move;

    if (Command.CommandTag.MatchesTag(InputMove))
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Player move command accepted - regenerating intents with predicted destination"),
            CurrentTurnIndex);

FIntPoint PlayerDestination = FIntPoint(0, 0);
        if (CachedPathFinder.IsValid() && PlayerPawn)
        {
            FIntPoint CurrentCell = CachedPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
            FIntPoint Direction = FIntPoint(
                FMath::RoundToInt(Command.Direction.X),
                FMath::RoundToInt(Command.Direction.Y)
            );
            PlayerDestination = CurrentCell + Direction;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Player destination predicted: (%d,%d)->(%d,%d)"),
                CurrentTurnIndex, CurrentCell.X, CurrentCell.Y, PlayerDestination.X, PlayerDestination.Y);
        }

if (UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            DF->UpdateDistanceField(PlayerDestination);

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] DistanceField updated with player destination (%d,%d)"),
                CurrentTurnIndex, PlayerDestination.X, PlayerDestination.Y);
        }

if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        {
            if (UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>())
            {

TArray<AActor*> Enemies;
                Enemies.Reserve(CachedEnemies.Num());
                for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
                {
                    if (Enemy)
                    {
                        Enemies.Add(Enemy.Get());
                    }
                }

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Intent regeneration: Using %d cached enemies"),
                    CurrentTurnIndex, Enemies.Num());

TArray<FEnemyObservation> Observations;
                Observations.Reserve(Enemies.Num());

                for (AActor* Enemy : Enemies)
                {
                    if (!Enemy || !CachedPathFinder.IsValid())
                    {
                        continue;
                    }

                    FEnemyObservation Obs;
                    Obs.GridPosition = CachedPathFinder->WorldToGrid(Enemy->GetActorLocation());
                    Obs.PlayerGridPosition = PlayerDestination;
                    Obs.DistanceInTiles = FMath::Abs(Obs.GridPosition.X - PlayerDestination.X) +
                        FMath::Abs(Obs.GridPosition.Y - PlayerDestination.Y);

                    Observations.Add(Obs);
                }

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Built %d observations with player destination (%d,%d)"),
                    CurrentTurnIndex, Observations.Num(), PlayerDestination.X, PlayerDestination.Y);

                TArray<FEnemyIntent> Intents;

EnemyAI->CollectIntents(Observations, Enemies, Intents);

                EnemyData->SaveIntents(Intents);

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Intents regenerated: %d intents (with player destination)"),
                    CurrentTurnIndex, Intents.Num());
            }
        }

const bool bHasAttack = HasAnyAttackIntent();

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] bHasAttack=%s (Simultaneous movement %s)"),
            CurrentTurnIndex,
            bHasAttack ? TEXT("TRUE") : TEXT("FALSE"),
            bHasAttack ? TEXT("DISABLED") : TEXT("ENABLED"));

        if (bHasAttack)
        {
            
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected (with player destination) -> Sequential mode"),
                CurrentTurnIndex);

            ExecuteSequentialPhase();  
        }
        else
        {
            
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] No attacks (with player destination) -> Simultaneous move mode"),
                CurrentTurnIndex);

            ExecuteSimultaneousPhase();  
        }
    }
    else if (Command.CommandTag == RogueGameplayTags::InputTag_Turn)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Turn command received but ExecuteTurnFacing not implemented"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[GameTurnManager] ❁ECommand tag does NOT match InputTag.Move or InputTag.Turn (Tag=%s)"),
            *Command.CommandTag.ToString());
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

if (IsValid(PathFinder))
    {
        return;
    }

TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AGridPathfindingLibrary::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        PathFinder = Cast<AGridPathfindingLibrary>(Found[0]);
        CachedPathFinder = PathFinder;
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Found existing PathFinder: %s"), *GetNameSafe(PathFinder));
        return;
    }

if (ATBSLyraGameMode* GM = World->GetAuthGameMode<ATBSLyraGameMode>())
    {
        PathFinder = GM->GetPathFinder();
        if (IsValid(PathFinder))
        {
            CachedPathFinder = PathFinder;
            UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Got PathFinder from GameMode: %s"), *GetNameSafe(PathFinder));
            return;
        }
    }

UE_LOG(LogTurnManager, Warning, TEXT("ResolveOrSpawnPathFinder: Spawning PathFinder as fallback (GameMode should have created it)"));
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PathFinder = World->SpawnActor<AGridPathfindingLibrary>(AGridPathfindingLibrary::StaticClass(), FTransform::Identity, Params);
    CachedPathFinder = PathFinder;

    if (IsValid(PathFinder))
    {
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Spawned PathFinder: %s"), *GetNameSafe(PathFinder));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: Failed to spawn PathFinder!"));
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

AGridPathfindingLibrary* AGameTurnManagerBase::FindPathFinder(UWorld* World) const
{
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
    {
        return *It;
    }

    return nullptr;
}

void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    if (!HasAuthority()) return;

if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnIndex);
        return;
    }

bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnIndex);

UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Collecting enemy intents AFTER player move"), CurrentTurnIndex);

CollectEnemies();

UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn)
    {
        
        if (UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>())
        {
            FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());
            DistanceField->UpdateDistanceField(PlayerGrid);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] DistanceField updated for PlayerGrid=(%d,%d)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y);
        }

TArray<FEnemyObservation> Observations;
        EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
        EnemyTurnDataSys->Observations = Observations;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] BuildObservations completed: Generated %d observations from %d enemies"),
            CurrentTurnIndex, Observations.Num(), CachedEnemies.Num());
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] Cannot build observations: EnemyAISys=%d, EnemyTurnDataSys=%d, PathFinder=%d, PlayerPawn=%d"),
            CurrentTurnIndex,
            EnemyAISys != nullptr,
            EnemyTurnDataSys != nullptr,
            CachedPathFinder.IsValid(),
            CachedPlayerPawn != nullptr);
    }

CollectIntents();

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Enemy intent collection complete"), CurrentTurnIndex);

const bool bHasAttack = HasAnyAttackIntent();

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

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Simultaneous Move Phase (No Attacks) ===="), CurrentTurnIndex);

ExecuteMovePhase();  

}


void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnIndex);

ExecuteAttacks();
}

void AGameTurnManagerBase::ExecuteSequentialPhase()
{
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Sequential Phase (Attack -> Move) ===="), CurrentTurnIndex);

ExecuteAttacks();  

}

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    
    const int32 NotifiedTurn = Payload ? static_cast<int32>(Payload->EventMagnitude) : -1;
    const int32 CurrentTurn = GetCurrentTurnIndex();

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
        CurrentTurnIndex, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
    FinalizePlayerMove(CompletedActor);

UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnIndex);

}

void AGameTurnManagerBase::ExecuteAttacks()
{
    if (!EnemyTurnData)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteAttacks: EnemyTurnData is null"),
            CurrentTurnIndex);
        return;
    }

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->BeginTurn(CurrentTurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: Barrier::BeginTurn(%d) called"),
                CurrentTurnIndex, CurrentTurnIndex);
        }
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] ExecuteAttacks: Closing input gate (attack phase starts)"), CurrentTurnIndex);
    ApplyWaitInputGate(false);

    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* PM = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            TArray<FResolvedAction> AttackActions;
            PM->ExecuteAttackPhaseWithSlots(
                EnemyTurnData->Intents,
                AttackActions
            );

            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: %d attack actions resolved"),
                CurrentTurnIndex, AttackActions.Num());

            if (UAttackPhaseExecutorSubsystem* AttackExecutor = World->GetSubsystem<UAttackPhaseExecutorSubsystem>())
            {

AttackExecutor->BeginSequentialAttacks(AttackActions, CurrentTurnIndex);
            }
        }
    }
}

void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{

UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteMovePhase: World is null"), CurrentTurnIndex);
        return;
    }

    UTurnCorePhaseManager* PhaseManager = World->GetSubsystem<UTurnCorePhaseManager>();
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (!PhaseManager || !EnemyData)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteMovePhase: PhaseManager=%d or EnemyData=%d not found"),
            CurrentTurnIndex,
            PhaseManager != nullptr,
            EnemyData != nullptr);

        EndEnemyTurn();
        return;
    }

if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteMovePhase: No enemy intents detected, attempting fallback generation..."),
            CurrentTurnIndex);

UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
        if (EnemyAISys && CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
        {
            
            if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
            {
                FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());
                DistanceField->UpdateDistanceField(PlayerGrid);
                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] Fallback: DistanceField updated for PlayerGrid=(%d,%d)"),
                    CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y);
            }

TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
            EnemyData->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Fallback: Generated %d observations"),
                CurrentTurnIndex, Observations.Num());

TArray<FEnemyIntent> Intents;
            EnemyAISys->CollectIntents(Observations, CachedEnemies, Intents);
            EnemyData->Intents = Intents;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Fallback: Generated %d intents"),
                CurrentTurnIndex, Intents.Num());
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] Fallback failed: EnemyAISys=%d, PathFinder=%d, PlayerPawn=%d, Enemies=%d"),
                CurrentTurnIndex,
                EnemyAISys != nullptr,
                CachedPathFinder.IsValid(),
                CachedPlayerPawn != nullptr,
                CachedEnemies.Num());
        }

if (EnemyData->Intents.Num() == 0)
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[Turn %d] ExecuteMovePhase: Still no intents after fallback, skipping enemy turn"),
                CurrentTurnIndex);

            EndEnemyTurn();
            return;
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Fallback successful: Proceeding with %d intents"),
            CurrentTurnIndex, EnemyData->Intents.Num());
    }

const bool bHasAttack = HasAnyAttackIntent();

if (!bSkipAttackCheck)
    {
        if (bHasAttack)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ATTACK INTENT detected (%d intents) - Executing attack phase instead of move phase"),
                CurrentTurnIndex, EnemyData->Intents.Num());

ExecuteAttacks();

return;
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] Attack check skipped (called after attack completion)"),
            CurrentTurnIndex);
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] No attack intents - Proceeding with move phase"),
        CurrentTurnIndex);

TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

if (CachedPlayerPawn && CachedPathFinder.IsValid())
    {
        const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = CachedPathFinder->WorldToGrid(PlayerLoc);

TWeakObjectPtr<AActor> PlayerKey(CachedPlayerPawn);
        if (const FIntPoint* PlayerNextCell = PendingMoveReservations.Find(PlayerKey))
        {
            if (PlayerCurrentCell != *PlayerNextCell)
            {
                FEnemyIntent PlayerIntent;
                PlayerIntent.Actor = CachedPlayerPawn;
                PlayerIntent.CurrentCell = PlayerCurrentCell;
                PlayerIntent.NextCell = *PlayerNextCell;
                PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
                PlayerIntent.BasePriority = 200;  

                AllIntents.Add(PlayerIntent);

                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] Added Player intent to ConflictResolver: (%d,%d)->(%d,%d)"),
                    CurrentTurnIndex, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                    PlayerNextCell->X, PlayerNextCell->Y);
            }
        }
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase: Processing %d intents (%d enemies + Player) via ConflictResolver"),
        CurrentTurnIndex, AllIntents.Num(), EnemyData->Intents.Num());

TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ConflictResolver produced %d resolved actions"),
        CurrentTurnIndex, ResolvedActions.Num());

if (CachedPlayerPawn)
    {
        
        for (const FResolvedAction& Action : ResolvedActions)
        {
            if (IsValid(Action.SourceActor.Get()) && Action.SourceActor.Get() == CachedPlayerPawn)
            {
                if (Action.bIsWait)
                {
                    
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[Turn %d] Player movement CANCELLED by ConflictResolver (Swap detected or other conflict)"),
                        CurrentTurnIndex);

TWeakObjectPtr<AActor> PlayerKey(CachedPlayerPawn);
                    PendingMoveReservations.Remove(PlayerKey);

if (UAbilitySystemComponent* ASC = CachedPlayerPawn->FindComponentByClass<UAbilitySystemComponent>())
                    {
                        
                        ASC->CancelAllAbilities();
                        UE_LOG(LogTurnManager, Log,
                            TEXT("[Turn %d] Player abilities cancelled due to Swap conflict"),
                            CurrentTurnIndex);
                    }

EndEnemyTurn();
                    return;
                }
                break;
            }
        }
    }

PhaseManager->CoreExecutePhase(ResolvedActions);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase complete - movements dispatched"),
        CurrentTurnIndex);

}

void AGameTurnManagerBase::ExecutePlayerMove()
{
    
    if (!HasAuthority())
    {
        return;
    }

APawn* PlayerPawnNow = GetPlayerPawn();
    if (!PlayerPawnNow)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ExecutePlayerMove: PlayerPawn is NULL"),
            CurrentTurnIndex);
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: ExecutePlayerMove: Pawn=%s, CommandTag=%s, Direction=%s"),
        CurrentTurnIndex,
        *PlayerPawnNow->GetName(),
        *CachedPlayerCommand.CommandTag.ToString(),
        *CachedPlayerCommand.Direction.ToString());

if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {

UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ==== ASC DIAGNOSTIC ==== Checking granted abilities for %s"),
            CurrentTurnIndex, *PlayerPawnNow->GetName());
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

        UE_LOG(LogTurnManager, Warning,
            TEXT("Turn %d: Sending GameplayEvent %s (Magnitude=%.2f, Direction=(%.0f,%.0f))"),
            CurrentTurnIndex,
            *EventData.EventTag.ToString(),
            EventData.EventMagnitude,
            CachedPlayerCommand.Direction.X,
            CachedPlayerCommand.Direction.Y);

        const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

        if (TriggeredCount > 0)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("Turn %d: GA_MoveBase activated (count=%d)"),
                CurrentTurnIndex, TriggeredCount);
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("Turn %d: ❌No abilities triggered for %s (Expected trigger tag: %s)"),
                CurrentTurnIndex, *EventData.EventTag.ToString(), *Tag_AbilityMove.ToString());
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ExecutePlayerMove: ASC not found"),
            CurrentTurnIndex);
    }
}

void AGameTurnManagerBase::OnAllMovesFinished(int32 FinishedTurnId)
{
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::OnAllMovesFinished: Not authority"));
        return;
    }

if (FinishedTurnId != CurrentTurnIndex)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::OnAllMovesFinished: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnIndex);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier complete - all moves finished"), FinishedTurnId);

bPlayerMoveInProgress = false;

ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

EndEnemyTurn();  
}

void AGameTurnManagerBase::EndEnemyTurn()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnIndex);

    bool bBarrierQuiet = false;
    int32 InProgressCount = 0;

    if (!CanAdvanceTurn(CurrentTurnIndex, &bBarrierQuiet, &InProgressCount))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[EndEnemyTurn] ABORT: Cannot end turn %d (actions still in progress)"),
            CurrentTurnIndex);

        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
            }
        }

        ClearResidualInProgressTags();

        if (bBarrierQuiet && InProgressCount > 0)
        {
            if (CanAdvanceTurn(CurrentTurnIndex))
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

if (APawn* PlayerPawn = GetPlayerPawn())
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
            CurrentTurnIndex, InputWindowId);
    }
    else
    {
        ApplyWaitInputGate(false);
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] Client: Gate CLOSED after replication"),
            CurrentTurnIndex);
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
                CurrentTurnIndex, InputWindowId);
        }
        else
        {
            
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate CLOSED (Gate tag removed), WindowId=%d"),
                CurrentTurnIndex, InputWindowId);
        }
    }
    else
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("Turn %d: ApplyWaitInputGate failed - Player ASC not found"),
            CurrentTurnIndex);
    }
}

void AGameTurnManagerBase::OpenInputWindow()
{
    
    if (!HasAuthority())
    {
        return;
    }

++InputWindowId;

    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Opened: Turn=%d WindowId=%d"),
        CurrentTurnIndex, InputWindowId);

if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(TurnFlowCoordinator->GetCurrentTurnId());
    }

    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(InputWindowId);
    }

ApplyWaitInputGate(true);

WaitingForPlayerInput = true;

OnRep_WaitingForPlayerInput();
    OnRep_InputWindowId();
}

void AGameTurnManagerBase::NotifyPlayerPossessed(APawn* NewPawn)
{
    if (!HasAuthority()) return;

    CachedPlayerPawn = NewPawn;
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



void AGameTurnManagerBase::OnRep_InputWindowId()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Client OnRep: WindowId=%d"),
        InputWindowId);

}

bool AGameTurnManagerBase::IsInputOpen_Server() const
{
    if (!WaitingForPlayerInput)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[IsInputOpen] ❁EFALSE: WaitingForPlayerInput=FALSE (Turn=%d)"),
            CurrentTurnIndex);
        return false;
    }

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        const bool bGateOpen = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        if (!bGateOpen)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[IsInputOpen] ❁EFALSE: Gate_Input_Open=FALSE (Turn=%d)"),
                CurrentTurnIndex);
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

ADungeonFloorGenerator* AGameTurnManagerBase::GetFloorGenerator() const
{
    if (DungeonSystem)
    {
        return DungeonSystem->GetFloorGenerator();
    }
    return nullptr;
}

bool AGameTurnManagerBase::EnsureFloorGenerated(int32 FloorIndex)
{
    if (!DungeonSystem)
    {
        return false;
    }
    DungeonSystem->TransitionToFloor(FloorIndex);
    return true; 
}

bool AGameTurnManagerBase::NextFloor()
{
    if (!DungeonSystem)
    {
        return false;
    }
    CurrentFloorIndex++;
    DungeonSystem->TransitionToFloor(CurrentFloorIndex);
    return true; 
}


void AGameTurnManagerBase::WarpPlayerToStairUp(AActor* Player)
{
    if (!Player || !DungeonSystem)
    {
        return;
    }

UE_LOG(LogTurnManager, Warning, TEXT("[WarpPlayerToStairUp] Not implemented yet - requires dungeon stair tracking"));
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

    FDelegateHandle Handle = Unit->OnMoveFinished.AddUObject(this, &AGameTurnManagerBase::HandleManualMoveFinished);
    ActiveMoveDelegates.Add(Unit, Handle);

    UE_LOG(LogTurnManager, Verbose,
        TEXT("[Turn %d] Bound move-finished delegate for %s"),
        CurrentTurnIndex, *GetNameSafe(Unit));
}

void AGameTurnManagerBase::HandleManualMoveFinished(AUnitBase* Unit)
{
    if (!HasAuthority() || !Unit)
    {
        return;
    }

    if (FDelegateHandle* Handle = ActiveMoveDelegates.Find(Unit))
    {
        Unit->OnMoveFinished.Remove(*Handle);
        ActiveMoveDelegates.Remove(Unit);
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

    for (const TPair<AUnitBase*, FDelegateHandle>& Entry : ActiveMoveDelegates)
    {
        if (Entry.Key)
        {
            Entry.Key->OnMoveFinished.Remove(Entry.Value);
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

if (Action.bIsWait)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[DispatchResolvedMove] Skipping wait action for %s (conflict loser)"),
            *GetNameSafe(Action.Actor.Get()));
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

    if (Action.NextCell == FIntPoint(-1, -1) || Action.NextCell == Action.CurrentCell)
    {
        ReleaseMoveReservation(Unit);
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

    AGridPathfindingLibrary* LocalPathFinder = GetCachedPathFinder();
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

if (EventDispatcher)
    {
        const FGameplayTag MoveActionTag = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.Intent.Move"));
        const int32 UnitID = Unit->GetUniqueID();
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[DispatchResolvedMove] Broadcasting move start notification for Unit %d at (%d,%d)->(%d,%d)"),
            UnitID, Action.CurrentCell.X, Action.CurrentCell.Y, Action.NextCell.X, Action.NextCell.Y);
        EventDispatcher->BroadcastActionExecuted(UnitID, MoveActionTag, true);
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
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackDir(DirX, DirY));
    EventData.OptionalObject = this;

    const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
    if (TriggeredCount > 0)
    {
        CachedPlayerCommand.Direction = FVector(static_cast<double>(DirX), static_cast<double>(DirY), 0.0);
        UE_LOG(LogTurnManager, Log,
            TEXT("[TriggerPlayerMove] Player move ability triggered toward (%d,%d)"),
            Action.NextCell.X, Action.NextCell.Y);

if (EventDispatcher)
        {
            const int32 UnitID = Unit->GetUniqueID();
            UE_LOG(LogTurnManager, Verbose,
                TEXT("[TriggerPlayerMove] Broadcasting move start notification for Unit %d at (%d,%d)->(%d,%d)"),
                UnitID, Action.CurrentCell.X, Action.CurrentCell.Y, Action.NextCell.X, Action.NextCell.Y);
            EventDispatcher->BroadcastActionExecuted(UnitID, EventData.EventTag, true);
        }

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
