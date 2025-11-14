// Copyright Epic Games, Inc. All Rights Reserved.

#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"  // ATBSLyraGameMode用
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/AABB.h"  // ☁E�E☁EPlayerStartRoom診断用 ☁E�E☁E
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnEventDispatcher.h"
#include "Turn/TurnDebugSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/PlayerInputProcessor.h"
#include "Player/PlayerControllerBase.h"  // ☁E�E☁EClient RPC用 (2025-11-09) ☁E�E☁E
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
#include "Turn/TurnActionBarrierSubsystem.h"          // UTurnActionBarrierSubsystem
#include "Player/LyraPlayerState.h"                   // ALyraPlayerState
#include "Player/LyraPlayerController.h"              // ALyraPlayerController
#include "GameFramework/SpectatorPawn.h"  // ☁E�E☁Eこれを追加 ☁E�E☁E
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
// ☁E�E☁E追加: DistanceFieldSubsystemのinclude ☁E�E☁E
#include "DistanceFieldSubsystem.h" 
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameModes/LyraGameState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "Utility/TurnCommandEncoding.h"
// ☁E�E☁EActionExecutorSubsystem と TurnPhaseManagerComponent のヘッダ
// 注愁E これら�Eクラスが存在しなぁE��合、Ecppファイルで該当コードをコメントアウトまた�E修正してください
// #include "Turn/ActionExecutorSubsystem.h"  // UActionExecutorSubsystem, FOnActionExecutorCompleted
// #include "Turn/TurnPhaseManagerComponent.h"  // UTurnPhaseManagerComponent

// ☁E�E☁EFOnActionExecutorCompleted が定義されてぁE��ぁE��合�E暫定定義
// ActionExecutorSubsystem.h に定義がある場合、上記�Eincludeを有効化してください
#if !defined(FOnActionExecutorCompleted_DEFINED)
DECLARE_DELEGATE(FOnActionExecutorCompleted);
#define FOnActionExecutorCompleted_DEFINED 1
#endif

// ☁E�E☁ELogTurnManager と LogTurnPhase は ProjectDiagnostics.cpp で既に定義されてぁE��ため、ここでは定義しなぁE
// DEFINE_LOG_CATEGORY(LogTurnManager);
// DEFINE_LOG_CATEGORY(LogTurnPhase);
using namespace RogueGameplayTags;

// ☁E�E☁E追加: CVar定義 ☁E�E☁E
TAutoConsoleVariable<int32> CVarTurnLog(
    TEXT("tbs.TurnLog"),
    1,
    TEXT("0:Off, 1:Key events only, 2:Verbose debug output"),
    ECVF_Default
     );
using namespace RogueGameplayTags;

//------------------------------------------------------------------------------
// 初期匁E
//------------------------------------------------------------------------------

AGameTurnManagerBase::AGameTurnManagerBase()
{
    PrimaryActorTick.bCanEverTick = false;
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;  // ネイチE��ブタグを使用

    // ☁E�E☁E2025-11-09: レプリケーション有効化（忁E��！E
    // WaitingForPlayerInput/InputWindowId等をクライアントに同期するため
    bReplicates = true;
    bAlwaysRelevant = true;         // 全クライアントに常に関連
    SetReplicateMovement(false);    // 移動しなぁE��琁E��クタなので不要E

    UE_LOG(LogTurnManager, Log, TEXT("[TurnManager] Constructor: Replication enabled (bReplicates=true, bAlwaysRelevant=true)"));
}

//------------------------------------------------------------------------------
// ☁E�E☁E新規追加: InitializeTurnSystem ☁E�E☁E
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// InitializeTurnSystemの修正牁E
//------------------------------------------------------------------------------


void AGameTurnManagerBase::InitializeTurnSystem()
{
    //==========================================================================
    // 1. 初期化済み回避
    //==========================================================================
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
    //==========================================================================
    // ☁E�E☁E新要E Subsystem初期化！E025-11-09リファクタリング�E�E☁E�E☁E
    //==========================================================================
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

    //==========================================================================
    // Step 1: CachedPlayerPawn
    //==========================================================================
    if (!IsValid(CachedPlayerPawn))
    {
        CachedPlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
        if (CachedPlayerPawn)
        {
            UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CachedPlayerPawn initialized: %s"), *CachedPlayerPawn->GetName());

            // SpectatorPawn ↁEBPPlayerUnitへPossess
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

    //==========================================================================
    // Step 2: CachedPathFinderは既に注入済みなので探索不要E
    //==========================================================================
    // PathFinderはHandleDungeonReady()で既に注入されてぁE��、またResolveOrSpawnPathFinder()で解決済み
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

    //==========================================================================
    // Step 3: Blueprint CollectEnemies
    //==========================================================================
    CollectEnemies();
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CollectEnemies completed (%d enemies)"), CachedEnemies.Num());

    //==========================================================================
    // 2. Subsystem
    //==========================================================================
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

    //==========================================================================
    // Step 4: SubsystemチェチE��
    //==========================================================================

    {
    // 既にline 113で宣言済みのWorldを�E利用
    if (World)
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            // ☁E�E☁E重褁E��止: 既存バインドを削除してから追加
            Barrier->OnAllMovesFinished.RemoveDynamic(this, &ThisClass::OnAllMovesFinished);
            Barrier->OnAllMovesFinished.AddDynamic(this, &ThisClass::OnAllMovesFinished);
            UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Barrier delegate bound"));
        }
        else
        {
            // 3回リトライ
            if (SubsystemRetryCount < 3)
            {
                SubsystemRetryCount++;
                UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Barrier not found, retrying... (%d/3)"), SubsystemRetryCount);
                bHasInitialized = false; // InitializeTurnSystemを�E呼び出し可能に

                World->GetTimerManager().SetTimer(SubsystemRetryHandle, this, &AGameTurnManagerBase::InitializeTurnSystem, 0.5f, false);
                return;
            }
            else
            {
                UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: TurnActionBarrierSubsystem not found after 3 retries"));
            }
        }
    }

        //======================================================================
        // ☁E�E☁E削除: UTurnInputGuard参�EはタグシスチE��で不要E
        //======================================================================
        // if (UTurnInputGuard* InputGuard = World->GetSubsystem<UTurnInputGuard>())
        // {
        //     UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: InputGuard registered TurnManager"));
        // }
        // else
        // {
        //     UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: UTurnInputGuard not found (non-critical)"));
        // }
    }

    //==========================================================================
    // ☁E�E☁E修正3: ASC Gameplay Eventの重褁E��止
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        // ☁E�E☁E既存�Eハンドルがあれ�E削除
        if (PlayerMoveCompletedHandle.IsValid())
        {
            if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(Tag_TurnAbilityCompleted))
            {
                Delegate->Remove(PlayerMoveCompletedHandle);
            }
            PlayerMoveCompletedHandle.Reset();
        }

        // ☁E�E☁E新規バインチE
        FGameplayEventMulticastDelegate& Delegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(Tag_TurnAbilityCompleted);
        PlayerMoveCompletedHandle = Delegate.AddUObject(this, &AGameTurnManagerBase::OnPlayerMoveCompleted);

        UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Bound to Gameplay.Event.Turn.Ability.Completed event"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: GetPlayerASC() returned null"));
    }

    //==========================================================================
    // ☁E�E☁E削除: BindAbilityCompletion()は重褁E��避けるため
    //==========================================================================
    // BindAbilityCompletion();

    //==========================================================================
    // 4. 完亁E
    //==========================================================================
    bHasInitialized = true;
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));

    //==========================================================================
    // ☁E�E☁EStartFirstTurnは削除: TryStartFirstTurnゲートから呼ぶ�E�E025-11-09 解析サマリ対応！E
    //==========================================================================
    // StartFirstTurn(); // 削除: 全条件が揃ぁE��で征E��
}







void AGameTurnManagerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 封E��皁E��拡張用。現在は空実裁E
}





// GameTurnManagerBase.cpp

void AGameTurnManagerBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::BeginPlay: START..."));

    // Authority
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager: Not authoritative, skipping"));
        return;
    }

    InitGameplayTags();

    // GameModeから参�Eを注入
    ATBSLyraGameMode* GM = GetWorld()->GetAuthGameMode<ATBSLyraGameMode>();
    if (!ensure(GM))
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: GameMode not found"));
        return;
    }

    // WorldSubsystemは忁E��World初期化時に作�Eされるため、直接Worldから取征E
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: World is null!"));
        return;
    }

    DungeonSys = World->GetSubsystem<URogueDungeonSubsystem>();

    UE_LOG(LogTurnManager, Warning, TEXT("TurnManager: ACQ_WORLD_V3 - DungeonSys=%p (NEW BINARY)"),
        static_cast<void*>(DungeonSys.Get()));

    // 準備完亁E��ベントにサブスクライチE
    if (DungeonSys)
    {
        DungeonSys->OnGridReady.AddDynamic(this, &AGameTurnManagerBase::HandleDungeonReady);
        UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Subscribed to DungeonSys->OnGridReady"));

        // ダンジョン生�Eをトリガー
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
    // DungeonSysは既にGameModeから注入済み
    if (!DungeonSys)
    {
        UE_LOG(LogTurnManager, Error, TEXT("HandleDungeonReady: DungeonSys not ready"));
        return;
    }

    // PathFinderとUnitManagerを生成�E初期化（ここで唯一の所有権を持つ�E�E
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

    // CachedPathFinderも更新
    CachedPathFinder = PathFinder;

    // ☁E�E☁EPathFinderを�E期化�E�E025-11-09 解析サマリ対応！E
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

        // 初期化検証: サンプルセルのスチE�Eタスを確誁E
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

        // ☁E�E☁E詳細ログ: BuildUnits後�EPlayerStartRoomを確誁E
        AAABB* PlayerRoom = UnitMgr->PlayerStartRoom.Get();
        UE_LOG(LogTurnManager, Warning, TEXT("[BuildUnits] Completed. PlayerStartRoom=%s at Location=%s"),
            PlayerRoom ? *PlayerRoom->GetName() : TEXT("NULL"),
            PlayerRoom ? *PlayerRoom->GetActorLocation().ToString() : TEXT("N/A"));

        bUnitsSpawned = true; // ☁E��愁E これは部屋選択完亁E��ラグ。実際の敵スポ�EンはOnTBSCharacterPossessedで行われる
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: HandleDungeonReady completed, initializing turn system..."));

    // ☁E�E☁EInitializeTurnSystem はここで呼ぶ
    InitializeTurnSystem();

    // ☁E�E☁Eゲート機槁E 全条件が揃ったらStartFirstTurnを試衁E
    TryStartFirstTurn();
}



// ☁E�E☁E赤ペン修正5: OnExperienceLoaded の実裁E☁E�E☁E
void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] ========== EXPERIENCE READY =========="));
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] Experience: %s"),
        Experience ? *Experience->GetName() : TEXT("NULL"));

    // ☁E�E☁EExperience完亁E��はInitializeTurnSystemを呼ばなぁE��EandleDungeonReadyからのみ呼ぶ
    // InitializeTurnSystem()はHandleDungeonReady()からのみ呼ばれる
}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    // ☁E�E☁Eクライアント�E: ターンUI更新/ログの同期 ☁E�E☁E
    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Client: TurnId replicated to %d"), CurrentTurnId);
    // Blueprintイベント発火は任せる。OnTurnAdvanced.Broadcast(CurrentTurnId);
}

void AGameTurnManagerBase::StartTurnMoves(int32 TurnId)
{
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::StartTurnMoves: Not authority"));
        return;
    }

    // Barrier / MoveBatch
    UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager::StartTurnMoves: Batch for Turn %d"), TurnId);

    if (UWorld* World = GetWorld())
    {
        // ☁E�E☁EREMOVED: StartMoveBatch は非推奨、EeginTurn() を使用 (2025-11-09) ☁E�E☁E
        // 以前�E実裁E
        //   int32 TotalUnits = 1 + Enemies.Num();
        //   Barrier->StartMoveBatch(TotalUnits, TurnId);
        // BeginTurn() がターン開始時に自動的にバリアを�E期化するため、この呼び出し�E不要、E

        // Move: Player
        if (APawn* PlayerPawn = GetPlayerPawn())
        {
            if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn))
            {
                FGameplayEventData EventData;
                EventData.EventTag = RogueGameplayTags::GameplayEvent_Turn_StartMove;  // ネイチE��ブタグを使用
                ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
                UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: Player GAS Turn.StartMove fired"));
            }
        }

        // Move Batch / Subsystem / CollectEnemies
        // ☁E�E☁Eエラー該当箁E��39: StartEnemyMoveBatch() 未実裁E�EコメントアウチE☁E�E☁E
        // if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        // {
        //     TArray<AActor*> EnemiesLocal;
        //     GetCachedEnemies(EnemiesLocal);
        //     // AI
        //     EnemyAI->StartEnemyMoveBatch(EnemiesLocal, TurnId);
        //     UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: EnemyAI StartEnemyMoveBatch for %d enemies"), EnemiesLocal.Num());
        // }

        // ☁E�E☁Eエラー該当箁E��47: PhaseManager->StartPhase(ETurnPhase::Move) 未実裁E�EコメントアウチE☁E�E☁E
        // PhaseManager / MovePhase
        // if (PhaseManager)
        // {
        //     PhaseManager->StartPhase(ETurnPhase::Move);
        //     UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: Move Phase started"));
        // }

        bTurnContinuing = true;
        WaitingForPlayerInput = true;
    }
}



// ☁E�E☁EOnExperienceLoadedの実裁E☁E�E☁E
// ☁E�E☁E新規関数: Experience完亁E��のコールバック ☁E�E☁E


// GameTurnManagerBase.cpp
// ☁E�E☁EBeginPlay() の直後に追加 ☁E�E☁E

//------------------------------------------------------------------------------
// GameplayTag キャチE��ュ初期化（新規追加�E�E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::InitGameplayTags()
{
    //==========================================================================
    // RogueGameplayTagsから取征E
    //==========================================================================
    Tag_AbilityMove = RogueGameplayTags::GameplayEvent_Intent_Move;  // "GameplayEvent.Intent.Move"
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;   // "Gameplay.Event.Turn.Ability.Completed"
    Phase_Turn_Init = RogueGameplayTags::Phase_Turn_Init;            // "Phase.Turn.Init"
    Phase_Player_Wait = RogueGameplayTags::Phase_Player_WaitInput;   // "Phase.Player.WaitInput"

    //==========================================================================
    // 🌟 3-Tag System: 新タグのキャチE��ュ確誁E
    //==========================================================================
    const FGameplayTag TagGateInputOpen = RogueGameplayTags::Gate_Input_Open;
    const FGameplayTag TagStateInProgress = RogueGameplayTags::State_Action_InProgress;

    //==========================================================================
    // 検証ログ
    //==========================================================================
    UE_LOG(LogTurnManager, Log, TEXT("[TagCache] Initialized:"));
    UE_LOG(LogTurnManager, Log, TEXT("  Move: %s"), *Tag_AbilityMove.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  TurnAbilityCompleted: %s"), *Tag_TurnAbilityCompleted.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  PhaseInit: %s"), *Phase_Turn_Init.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  PhaseWait: %s"), *Phase_Player_Wait.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  Gate: %s"), *TagGateInputOpen.ToString());
    UE_LOG(LogTurnManager, Log, TEXT("  InProgress: %s"), *TagStateInProgress.ToString());

    //==========================================================================
    // タグの有効性チェチE��
    //==========================================================================
    if (!Tag_AbilityMove.IsValid() || !Tag_TurnAbilityCompleted.IsValid())
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TagCache] INVALID TAGS! Check DefaultGameplayTags.ini"));
    }

    if (!Phase_Player_Wait.IsValid() || !TagGateInputOpen.IsValid() || !TagStateInProgress.IsValid())
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TagCache] INVALID 3-Tag System tags! Check RogueGameplayTags.cpp"));
    }
}


//------------------------------------------------------------------------------
// Ability Completion Event Handling
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// Phase 2: パフォーマンス最適化実裁E
//------------------------------------------------------------------------------

AGridPathfindingLibrary* AGameTurnManagerBase::GetCachedPathFinder() const
{
    // メンバ変数PathFinderを優先、EameModeから注入されたもの
    if (IsValid(PathFinder.Get()))
    {
        return PathFinder.Get();
    }

    // CachedPathFinderを確誁E
    if (CachedPathFinder.IsValid())
    {
        AGridPathfindingLibrary* PF = CachedPathFinder.Get();
        // PathFinderにも設定（次回�E高速化�E�E
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // フォールバック: シーン冁E��探索
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        AGridPathfindingLibrary* PF = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // 最後�E手段: GameModeから取得を試みめE
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

// ========== 修正: Subsystemに委譲 ==========
void AGameTurnManagerBase::BuildAllObservations()
{
    SCOPED_NAMED_EVENT(BuildAllObservations, FColor::Orange);

    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: EnemyAISubsystem not found"), CurrentTurnIndex);
        return;
    }

    // 敵リスト�E変換: ObjectPtr ↁE生�Eインタ
    TArray<AActor*> Enemies;
    Enemies.Reserve(CachedEnemies.Num());
    for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
    {
        if (Enemy)
        {
            Enemies.Add(Enemy.Get());
        }
    }

    // PathFinder と Player の取征E
    AGridPathfindingLibrary* CachedPF = GetCachedPathFinder();
    AActor* Player = GetPlayerActor();

    if (!CachedPF || !Player)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: PathFinder or Player not found"), CurrentTurnIndex);
        return;
    }

    // ========== Subsystemに委譲 ==========
    TArray<FEnemyObservation> Observations;
    EnemyAISubsystem->BuildObservations(Enemies, Player, CachedPF, Observations);

    // EnemyTurnData に書き込み
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

    // ☁E�E☁EPhase 1: EventDispatcher経由でイベント�E信�E�E025-11-09�E�E☁E�E☁E
    // ☁E�E☁EWeek 1: UPlayerInputProcessorに委譲�E�E025-11-09リファクタリング�E�E
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

    // 二重進行防止: ここで入力ゲートを閉じてから継綁E
    if (WaitingForPlayerInput)
    {
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);

        // ☁E�E☁EコアシスチE��: CommandHandler経由でInput Window終亁E��E025-11-09�E�E☁E�E☁E
        if (CommandHandler)
        {
            CommandHandler->EndInputWindow();
        }
    }
    ContinueTurnAfterInput();
}






//------------------------------------------------------------------------------
// BP互換関数実裁E
//------------------------------------------------------------------------------

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

FEnemyObservation AGameTurnManagerBase::BP_BuildObservationForEnemy(AActor* Enemy)
{
    FEnemyObservation Observation;

    if (!Enemy)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] BP_BuildObservationForEnemy: Enemy is nullptr"), CurrentTurnIndex);
        return Observation;
    }

    AActor* Player = GetPlayerActor();
    if (!Player)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] BP_BuildObservationForEnemy: Player not found"), CurrentTurnIndex);
        return Observation;
    }

    AGridPathfindingLibrary* CachedPF = GetCachedPathFinder();

    if (CachedPF)
    {
        Observation.GridPosition = CachedPF->WorldToGrid(Enemy->GetActorLocation());
        Observation.PlayerGridPosition = CachedPF->WorldToGrid(Player->GetActorLocation());
        Observation.DistanceInTiles = FMath::Abs(Observation.GridPosition.X - Observation.PlayerGridPosition.X) +
            FMath::Abs(Observation.GridPosition.Y - Observation.PlayerGridPosition.Y);

        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BP_BuildObservationForEnemy: %s at Grid(%d,%d), Distance: %d tiles"),
            CurrentTurnIndex, *Enemy->GetName(), Observation.GridPosition.X, Observation.GridPosition.Y, Observation.DistanceInTiles);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] BP_BuildObservationForEnemy: PathFinder not found"), CurrentTurnIndex);
        Observation.GridPosition = FIntPoint::ZeroValue;
        Observation.PlayerGridPosition = FIntPoint::ZeroValue;
        Observation.DistanceInTiles = 0;
    }

    return Observation;
}

void AGameTurnManagerBase::BuildObservations_Implementation()
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] BuildObservations called (Blueprint should override)"), CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// Core API
//------------------------------------------------------------------------------

void AGameTurnManagerBase::StartTurn()
{
    // ☁E�E☁EWeek 1: UTurnFlowCoordinatorに転送E��E025-11-09リファクタリング�E�E
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->StartTurn();
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[StartTurn] TurnFlowCoordinator not available!"));
    }
}

void AGameTurnManagerBase::RunTurn()
{
    FTurnContext Context;
    Context.TurnIndex = CurrentTurnIndex;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Turn Start"), CurrentTurnIndex);

    BeginPhase(RogueGameplayTags::Phase_Turn_Init);
    OnCombineSystemUpdate(Context);
    OnStatusEffectSystemUpdate(Context);
    EndPhase(RogueGameplayTags::Phase_Turn_Init);

    BeginPhase(RogueGameplayTags::Phase_Player_WaitInput);
    EndPhase(RogueGameplayTags::Phase_Player_WaitInput);

    BeginPhase(RogueGameplayTags::Phase_Ally_IntentBuild);
    BuildAllyIntents(Context);
    EndPhase(RogueGameplayTags::Phase_Ally_IntentBuild);

    BeginPhase(RogueGameplayTags::Phase_Enemy_IntentBuild);
    CollectEnemies();
    BuildObservations();
    CollectIntents();
    EndPhase(RogueGameplayTags::Phase_Enemy_IntentBuild);

    BeginPhase(RogueGameplayTags::Phase_Simul_Move);
    if (!HasAnyAttackIntent())
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] No attack Intent: Simultaneous Move"), CurrentTurnIndex);
        ExecuteSimultaneousMoves();
    }
    else
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Attack Intent detected: Sequential"), CurrentTurnIndex);
        ExecuteEnemyAttacks();
        ExecuteEnemyMoves();
    }
    EndPhase(RogueGameplayTags::Phase_Simul_Move);

    OnTrapSystemUpdate(Context);

    BeginPhase(RogueGameplayTags::Phase_Ally_ActionExecute);
    ExecuteAllyActions(Context);
    EndPhase(RogueGameplayTags::Phase_Ally_ActionExecute);

    WaitForAbilityCompletion();

    OnItemSystemUpdate(Context);
    OnPotSystemUpdate(Context);
    OnBreedingSystemUpdate(Context);

    BeginPhase(RogueGameplayTags::Phase_Turn_Cleanup);
    EndPhase(RogueGameplayTags::Phase_Turn_Cleanup);

    BeginPhase(RogueGameplayTags::Phase_Turn_Advance);
    CurrentTurnIndex++;
    EndPhase(RogueGameplayTags::Phase_Turn_Advance);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Turn End"), CurrentTurnIndex - 1);
}

// ========== 修正: PhaseManagerに委譲 ==========
void AGameTurnManagerBase::BeginPhase(FGameplayTag PhaseTag)
{
    // ☁E�E☁EPhase 1: フェーズ変更イベント�E信�E�E025-11-09�E�E☁E�E☁E
    FGameplayTag OldPhase = CurrentPhase;
    CurrentPhase = PhaseTag;
    PhaseStartTime = FPlatformTime::Seconds();

    // EventDispatcher経由でフェーズ変更を通知
    if (EventDispatcher)
    {
        EventDispatcher->BroadcastPhaseChanged(OldPhase, PhaseTag);
    }

    // DebugSubsystem経由でログ記録
    if (DebugSubsystem)
    {
        DebugSubsystem->LogPhaseTransition(OldPhase, PhaseTag);
    }
    else
    {
        // Fallback: 直接ログ
        UE_LOG(LogTurnPhase, Log, TEXT("PhaseStart:%s"), *PhaseTag.ToString());
    }

    if (PhaseTag == Phase_Player_Wait)
    {
        // 入力フェーズ開始：フラグとゲートを**揁E��て**開く
        WaitingForPlayerInput = true;
        ApplyWaitInputGate(true);      // ☁E�E☁E追加。重要E☁E�E☁E
        OpenInputWindow();
        UE_LOG(LogTurnManager, Log,
            TEXT("Turn%d:BeginPhase(Input) Id=%d, Gate=OPEN, Waiting=TRUE"),
            CurrentTurnIndex, InputWindowId);
    }

    // ☁E�E☁EPhaseManager は存在しなぁE��めコメントアウチE
    /*
    if (PhaseManager)
    {
        PhaseManager->BeginPhase(PhaseTag);
    }
    */

    // DebugObserver への通知。既存�E処琁E
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
        // 入力フェーズ終亁E��フラグとゲートを**揁E��て**閉じめE
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);
        UE_LOG(LogTurnManager, Log, TEXT("Turn%d:[EndPhase] Gate=CLOSED, Waiting=FALSE"),
            CurrentTurnIndex);
    }

    // ☁E�E☁EPhaseManager は存在しなぁE��めコメントアウチE
    /*
    if (PhaseManager)
    {
        PhaseManager->EndPhase(PhaseTag);
    }
    */

    // DebugObserver通知
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

//------------------------------------------------------------------------------
// Enemy Pipeline
//------------------------------------------------------------------------------
namespace
{
    FORCEINLINE UAbilitySystemComponent* TryGetASC(const AActor* Actor)
    {
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
            return ASI->GetAbilitySystemComponent();
        if (const APawn* P = Cast<APawn>(Actor))
                // ☁E�E☁E修正: IAbilitySystemInterface に戻ぁE☁E�E☁E
            if (const IAbilitySystemInterface* C = Cast<IAbilitySystemInterface>(P->GetController()))
                return C->GetAbilitySystemComponent();
        return nullptr;
    }

    FORCEINLINE int32 GetTeamIdOf(const AActor* Actor)
    {
        // まずControllerを確誁E
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

        // 次にActor自身を確誁E
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

    // ☁E�E☁EPhase 4: UEnemyAISubsystem経由でEnemy収集�E�E025-11-09�E�E☁E�E☁E
    if (EnemyAISubsystem)
    {
        TArray<AActor*> CollectedEnemies;
        EnemyAISubsystem->CollectAllEnemies(CachedPlayerPawn, CollectedEnemies);

        CachedEnemies.Empty();
        CachedEnemies = CollectedEnemies;

        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemies.Num());
        return;
    }

    // ☁E�E☁EFallback: 既存�EロジチE���E�EnemyAISubsystemがなぁE��合！E☁E�E☁E
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));

        // ☁E�E☁EAPawnで検索、Encludeが不要E☁E�E☁E
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);

    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;  // ネイチE��ブタグを使用

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

        // ☁E�E☁E重要E CachedEnemiesを完�Eにクリアしてから再構篁E☁E�E☁E
    CachedEnemies.Empty();
    CachedEnemies.Reserve(Found.Num());

    for (AActor* A : Found)
    {
        // ☁E�E☁ENullチェチE�� ☁E�E☁E
        if (!IsValid(A))
        {
            continue;
        }

        // ☁E�E☁Eプレイヤーを除夁E☁E�E☁E
        if (A == CachedPlayerPawn)
        {
            UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            continue;
        }

        const int32 TeamId = GetTeamIdOf(A);

        // ☁E�E☁E修正: TeamId == 2 また�E 255 を敵として扱ぁE☁E�E☁E
        // ログから全ての敵がTeamID=255 であることが判昁E
        const bool bByTeam = (TeamId == 2 || TeamId == 255);

        const UAbilitySystemComponent* ASC = TryGetASC(A);
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));

        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

        // ☁E�E☁EぁE��れかの条件を満たせば敵として認譁E☁E�E☁E
        if (bByGTag || bByTeam || bByActorTag)
        {
            CachedEnemies.Add(A);

            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;

            // ☁E�E☁EチE��チE��: 最初�E3体と最後�E1体�Eみ詳細ログ ☁E�E☁E
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

    // ☁E�E☁Eエラー検�E: 敵ぁE体も見つからなぁE��吁E☁E�E☁E
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
    // ☁E�E☁E警告：予想より少なぁE☁E�E☁E
    else if (CachedEnemies.Num() > 0 && CachedEnemies.Num() < 32 && Found.Num() >= 32)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ⚠�E�ECollected %d enemies, but expected around 32 from %d Pawns"),
            CachedEnemies.Num(), Found.Num());
    }
}




// ========== 修正: Subsystemに委譲 ==========
// GameTurnManagerBase.cpp

void AGameTurnManagerBase::CollectIntents_Implementation()
{
    // ☁E�E☁E修正1: Subsystemを毎回取得（キャチE��ュ無効化対策！E☁E�E☁E
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

    // ☁E�E☁E修正2: Observationsの存在確誁E 自動リカバリー ☁E�E☁E
    if (EnemyTurnDataSys->Observations.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
            CurrentTurnIndex, CachedEnemies.Num());

        // ☁E�E☁E自動リカバリー: Observationsを生戁E☁E�E☁E
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

    // ☁E�E☁E修正3: CachedEnemiesを直接使用 ☁E�E☁E
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents input: Observations=%d, Enemies=%d"),
        CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

    // ☁E�E☁E修正4: サイズ一致チェチE�� ☁E�E☁E
    if (EnemyTurnDataSys->Observations.Num() != CachedEnemies.Num())
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

        // ☁E�E☁Eリカバリー: Observationsを�E生�E ☁E�E☁E
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

    // ☁E�E☁E修正5: EnemyAISubsystem::CollectIntents実裁E☁E�E☁E
    TArray<FEnemyIntent> Intents;
    EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemies, Intents);

    // ☁E�E☁E修正6: Subsystemに格紁E☁E�E☁E
    EnemyTurnDataSys->Intents = Intents;

    // ☁E�E☁EIntent数の計測 ☁E�E☁E
    int32 AttackCount = 0, MoveCount = 0, WaitCount = 0, OtherCount = 0;

    // ☁E�E☁E修正7: GameplayTagをキャチE��ュしてパフォーマンス改喁E☁E�E☁E
    static const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイチE��ブタグを使用
    static const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;  // ネイチE��ブタグを使用
    static const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;  // ネイチE��ブタグを使用

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

    // ☁E�E☁E修正8: ログレベルをWarningに変更。重要なイベンチE☁E�E☁E
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents -> %d intents (Attack=%d, Move=%d, Wait=%d, Other=%d)"),
        CurrentTurnIndex, Intents.Num(), AttackCount, MoveCount, WaitCount, OtherCount);

    // ☁E�E☁E修正9: Intent生�E失敗�E詳細ログ ☁E�E☁E
    if (Intents.Num() == 0 && EnemyTurnDataSys->Observations.Num() > 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: ❌Failed to generate intents from %d observations!"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() < EnemyTurnDataSys->Observations.Num())
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: ⚠�E�EGenerated %d intents from %d observations (some enemies may have invalid intent)"),
            CurrentTurnIndex, Intents.Num(), EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() == EnemyTurnDataSys->Observations.Num() && Intents.Num() > 0)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] CollectIntents: ✁ESuccessfully generated all intents"),
            CurrentTurnIndex);
    }
}




FEnemyIntent AGameTurnManagerBase::ComputeEnemyIntent_Implementation(AActor* Enemy, const FEnemyObservation& Observation)
{
    FEnemyIntent Intent;
    Intent.Actor = Enemy;

    if (Observation.DistanceInTiles <= 1)
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Attack;  // ネイチE��ブタグを使用
    }
    else
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;  // ネイチE��ブタグを使用
    }

    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ComputeEnemyIntent: %s -> %s (Distance=%d)"),
        CurrentTurnIndex, *Enemy->GetName(), *Intent.AbilityTag.ToString(), Observation.DistanceInTiles);

    return Intent;
}

// ========== 修正: Subsystemに委譲 ==========
void AGameTurnManagerBase::ExecuteEnemyMoves_Implementation()
{
    // ☁E�E☁EUActionExecutorSubsystem は存在しなぁE��めコメントアウチE
    UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyMoves: ActionExecutor not available (class not found)"), CurrentTurnIndex);
    return;
    /*
    if (!ActionExecutor || !EnemyTurnData)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyMoves: ActionExecutor or EnemyTurnData not found"), CurrentTurnIndex);
        return;
    }

    AGridPathfindingLibrary* CachedPF = GetCachedPathFinder();
    APawn* PlayerPawn = GetPlayerPawn();

    if (!CachedPF || !PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyMoves: PathFinder or PlayerPawn not found"), CurrentTurnIndex);
        return;
    }

    // ========== Subsystemに委譲 ==========
    ActionExecutor->ExecuteEnemyMoves(EnemyTurnData->Intents, CachedPF, PlayerPawn);
    */
}

// ========== 修正: Subsystemに委譲 ==========


//------------------------------------------------------------------------------
// Ability管琁E
//------------------------------------------------------------------------------

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

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイチE��ブタグを使用

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


void AGameTurnManagerBase::NotifyAbilityStarted()
{
    PendingAbilityCount++;
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Ability started. Pending: %d"), CurrentTurnIndex, PendingAbilityCount);
}

void AGameTurnManagerBase::OnAbilityCompleted()
{
    PendingAbilityCount = FMath::Max(0, PendingAbilityCount - 1);
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Ability completed. Remaining: %d"), CurrentTurnIndex, PendingAbilityCount);

    if (PendingAbilityCount == 0)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All abilities completed"), CurrentTurnIndex);

        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(AbilityWaitTimerHandle);
        }

        OnAllAbilitiesCompleted();
    }
}

//------------------------------------------------------------------------------
// Ally Turn
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// Move Pipeline
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// Interrupt System
//------------------------------------------------------------------------------

void AGameTurnManagerBase::CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove)
{
    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] CheckInterruptWindow called (Blueprint)"), CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// Utilities
//------------------------------------------------------------------------------

FBoardSnapshot AGameTurnManagerBase::CaptureSnapshot() const
{
    FBoardSnapshot Snapshot;
    return Snapshot;
}

void AGameTurnManagerBase::WaitForAbilityCompletion_Implementation()
{
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] WaitForAbilityCompletion"), CurrentTurnIndex);
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Waiting for %d abilities"), CurrentTurnIndex, PendingAbilityCount);

    if (PendingAbilityCount == 0)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] No abilities pending"), CurrentTurnIndex);
        OnAllAbilitiesCompleted();
        return;
    }

    if (!GetWorld())
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] World is null"), CurrentTurnIndex);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Starting ability wait timer"), CurrentTurnIndex);

    GetWorld()->GetTimerManager().SetTimer(
        AbilityWaitTimerHandle,
        [this]()
        {
            UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] Checking... Pending: %d"), CurrentTurnIndex, PendingAbilityCount);

            if (PendingAbilityCount == 0)
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Timer detected completion"), CurrentTurnIndex);

                if (GetWorld())
                {
                    GetWorld()->GetTimerManager().ClearTimer(AbilityWaitTimerHandle);
                }

                OnAllAbilitiesCompleted();
            }
        },
        0.1f,
        true
    );
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

//------------------------------------------------------------------------------
// System Hooks
//------------------------------------------------------------------------------

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


//------------------------------------------------------------------------------
// Blueprint Accessors
//------------------------------------------------------------------------------

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

bool AGameTurnManagerBase::TryGetEnemyIntent(AActor* Enemy, FEnemyIntent& OutIntent) const
{
    if (!Enemy) return false;

    for (const TPair<TWeakObjectPtr<AActor>, FEnemyIntent>& Pair : CachedIntents)
    {
        if (Pair.Key.Get() == Enemy)
        {
            OutIntent = Pair.Value;
            return true;
        }
    }

    return false;
}

void AGameTurnManagerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AGameTurnManagerBase, WaitingForPlayerInput);
    DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnId);
    DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnIndex);  // ☁E�E☁E2025-11-09: 追加�E�実際に使用されてぁE��変数�E�E
    DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);
    DOREPLIFETIME(AGameTurnManagerBase, bPlayerMoveInProgress);
}


void AGameTurnManagerBase::ExecuteEnemyAttacks_Implementation()
{
    // ☁E�E☁EUActionExecutorSubsystem は存在しなぁE��めコメントアウチE
    UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyAttacks: ActionExecutor not available (class not found)"), CurrentTurnIndex);
    return;
    /*
    if (!ActionExecutor || !EnemyTurnData)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyAttacks: ActionExecutor or EnemyTurnData not found"), CurrentTurnIndex);
        return;
    }

    // UFUNCTION をバインチE
    FOnActionExecutorCompleted OnComplete;
    OnComplete.BindUFunction(this, FName("OnEnemyAttacksCompleted"));

    ActionExecutor->ExecuteEnemyAttacksAsync(EnemyTurnData->Intents, OnComplete);
    */
}
void AGameTurnManagerBase::OnEnemyAttacksCompleted()
{
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All enemy attacks completed"), CurrentTurnIndex);
    OnAllAbilitiesCompleted();
}

// ════════════════════════════════════════════════════════════════════
// 🌟 統吁EPI - Lumina提言B3: AdvanceTurnAndRestart - 最終修正牁E
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnIndex);

    // ☁E�E☁EDEBUG: Log player position before turn advance (2025-11-09) ☁E�E☁E
    if (APawn* PlayerPawn = GetPlayerPawn())
    {
        if (CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = PlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[AdvanceTurn] ☁EPLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y, *PlayerLoc.ToCompactString());
        }
    }

    //==========================================================================
    // ☁E�E☁EPhase 4: 二重鍵チェチE��、EdvanceTurnAndRestartでも実施
    //==========================================================================
    if (!CanAdvanceTurn(CurrentTurnIndex))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[AdvanceTurnAndRestart] ABORT: Cannot advance turn %d (safety check failed)"),
            CurrentTurnIndex);

        // ☁E�E☁EチE��チE��: Barrierの状態をダンチE
        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
            }
        }

        return;  // ☁E�E☁E進行を中止
    }

    //==========================================================================
    // CoreCleanupPhase
    //==========================================================================
    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* CorePhase = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            CorePhase->CoreCleanupPhase();
            UE_LOG(LogTurnManager, Log,
                TEXT("[AdvanceTurnAndRestart] CoreCleanupPhase completed"));
        }
    }

    //==========================================================================
    // ターンインクリメンチE
    //==========================================================================
    const int32 PreviousTurn = CurrentTurnIndex;

    // ☁E�E☁EWeek 1: UTurnFlowCoordinatorに委譲�E�E025-11-09リファクタリング�E�E
    if (TurnFlowCoordinator)
    {
        // ターンを終亁E��てから進める
        TurnFlowCoordinator->EndTurn();
        TurnFlowCoordinator->AdvanceTurn();
    }

    // ☁E�E☁EコアシスチE��: OnTurnEnded配信�E�E025-11-09�E�E☁E�E☁E
    if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnEnded(PreviousTurn);
    }

    CurrentTurnIndex++;

    // ☁E�E☁EPhase 5補宁E EndTurnリトライフラグをリセチE���E�E025-11-09�E�E☁E�E☁E
    bEndTurnPosted = false;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d ↁE%d (bEndTurnPosted reset)"),
        PreviousTurn, CurrentTurnIndex);

    //==========================================================================
    // ☁E�E☁EPhase 4: Barrierに新しいターンを通知
    //==========================================================================
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

    //==========================================================================
    // チE��チE��Observerの保孁E
    //==========================================================================
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

    //==========================================================================
    // フラグリセチE��とイベント発火
    //==========================================================================
    bTurnContinuing = false;

    // ☁E�E☁Eリファクタリング: EventDispatcher経由でイベント�E信�E�E025-11-09�E�E☁E�E☁E
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

    // ☁E�E☁ECRITICAL FIX (2025-11-09): 入力フェーズを忁E��開姁E☁E�E☁E
    // Turn 1以降で入力ウィンドウが開かなぁE��題�E修正
    OnTurnStartedHandler(CurrentTurnIndex);
}




// ════════════════════════════════════════════════════════════════════
// StartFirstTurn - 初回ターン開始用
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnIndex);

    bTurnStarted = true;

    // ☁E�E☁EREMOVED: StartMoveBatch は非推奨、EeginTurn() ぁEAdvanceTurnAndRestart で既に呼ばれてぁE�� (2025-11-09) ☁E�E☁E
    // 以前�E実裁E Barrier->StartMoveBatch(1, CurrentTurnIndex);
    // BeginTurn() がターン開始とバリア初期化を一括処琁E��るため、この呼び出し�E冗長、E

    // ☁E�E☁Eリファクタリング: EventDispatcher経由でイベント�E信�E�E025-11-09�E�E☁E�E☁E
    if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("UTurnEventDispatcher not available"));
    }

    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnIndex);

    // ☁E�E☁ECRITICAL FIX (2025-11-09): 統一された�E力フェーズ開姁E☁E�E☁E
    // OnTurnStartedHandler ぁEBeginPhase(Phase_Player_Wait) を呼ぶ
    OnTurnStartedHandler(CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// ☁E�E☁EC++統合：ターンフロー制御 ☁E�E☁E
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// EndPlayの修正牁E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::EndPlay(const EEndPlayReason::Type Reason)
{
    // ☁E�E☁ENon-DynamicチE��ゲート解除 ☁E�E☁E
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
    // PlayerController から PlayerState 経由で ASC を取征E
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
        {
            // LyraPlayerState の場吁E
            if (ALyraPlayerState* LyraPS = Cast<ALyraPlayerState>(PS))
            {
                return LyraPS->GetAbilitySystemComponent();
            }

            // IAbilitySystemInterface を実裁E��てぁE��場吁E
            if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PS))
            {
                return ASI->GetAbilitySystemComponent();
            }
        }
    }

    // フォールバック: CachedPlayerPawn から直接取得を試みめE
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



// GameTurnManagerBase.cpp

//------------------------------------------------------------------------------
// OnTurnStartedHandlerのホットフィチE��ス適用牁E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnIndex)
{
    if (!HasAuthority()) return;

    CurrentTurnIndex = TurnIndex;

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnIndex);

    // ☁E�E☁ECRITICAL FIX (2025-11-11): ターン開始時に残留タグをクリア ☁E�E☁E
    // 前�EターンでアビリチE��が正しく終亁E��てぁE��ぁE��合、ブロチE��ングタグが残り続けめE
    // ターン開始直後に強制クリーンアチE�Eして、新しいアビリチE��が起動できるようにする
    ClearResidualInProgressTags();
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ☁EClearResidualInProgressTags called at turn start"),
        TurnIndex);

    // ☁E�E☁ECRITICAL FIX (2025-11-11): ターン開始時に古ぁE��紁E��即座にパ�Eジ ☁E�E☁E
    // プレイヤー入力がExecuteMovePhaseより先に来る可能性があるため、E
    // ターン開始直後にクリーンアチE�EしてPlayerの予紁E��確実に受け入れる
    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            GridOccupancy->SetCurrentTurnId(TurnIndex);
            GridOccupancy->PurgeOutdatedReservations(TurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ☁EPurgeOutdatedReservations called at turn start (before player input)"),
                TurnIndex);

            // ☁E�E☁ECRITICAL FIX (2025-11-11): 物琁E��標�Eースの占有�EチE�E再構築！Eemini診断より�E�E☁E�E☁E
            // 論理マップ！EctorToCell�E�ではなく、物琁E��標から占有�EチE�Eを完�E再構篁E
            // これにより、コミット失敗等で発生した論理/物琁E�E不整合を強制修正
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
                    TEXT("[Turn %d] ☁ERebuildFromWorldPositions called - rebuilding occupancy from physical positions (%d units)"),
                    TurnIndex, AllUnits.Num());
            }
            else
            {
                // フォールバック�E�ユニットリストが空の場合�E論理マップ�EースのチェチE��
                GridOccupancy->EnforceUniqueOccupancy();
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] ☁EEnforceUniqueOccupancy called (fallback - no units cached yet)"),
                    TurnIndex);
            }
        }
    }

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        CachedPlayerPawn = PC->GetPawn();

        // ☁E�E☁EDEBUG: Log player position at turn start (2025-11-09) ☁E�E☁E
        if (CachedPlayerPawn && CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ☁EPLAYER POSITION AT TURN START: Grid(%d,%d) World(%s)"),
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

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-11): ゲート開放を早期に実行（最適化！E☁E�E☁E
    // 琁E��: DistanceField更新は重い処琁E��が、�Eレイヤー入力前には不要E
    //       �E�敵AI判断にのみ忁E��で、それ�Eプレイヤー行動後に実行される�E�E
    //       ゲートを先に開くことで、�E力受付までの遁E��を最小化
    //==========================================================================
    BeginPhase(Phase_Turn_Init);
    BeginPhase(Phase_Player_Wait);
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ☁EINPUT GATE OPENED EARLY (before DistanceField update)"), TurnIndex);

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

    // ☁E�E☁EホットフィチE��スC: 動的Margin計算（�Eンハッタン距離ベ�Eス�E�E☁E�E☁E
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

            // ☁E�E☁E最適匁E GridUtils使用�E�重褁E��ード削除 2025-11-09�E�E
            // マンハッタン距離計算�EGridUtilsを使用
            auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
                {
                    return FGridUtils::ManhattanDistance(A, B);
                };

            // ☁E�E☁E最遠の敵までの距離を計測 ☁E�E☁E
            int32 MaxD = 0;
            for (const FIntPoint& C : EnemyPositions)
            {
                int32 Dist = Manhattan(C, PlayerGrid);
                if (Dist > MaxD)
                {
                    MaxD = Dist;
                }
            }

            // ☁E�E☁E動的Margin計算（最遠距離 + バッファ4、篁E��8-64�E�E☁E�E☁E
            const int32 Margin = FMath::Clamp(MaxD + 4, 8, 64);

            UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] DF: Player=(%d,%d) Enemies=%d MaxDist=%d Margin=%d"),
                TurnIndex, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), MaxD, Margin);

            // DistanceField更新
            DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

            // ☁E�E☁E到達確認ループ（診断用�E�E☁E�E☁E
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
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ✁Ell enemies reachable with Margin=%d"),
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

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-12): ターン開始時に仮インチE��トを生�E ☁E�E☁E
    // 琁E��: 「インチE��トが空のまま同時移動フェーズに突�E」を防ぐためE
    //       ターン開始時に仮インチE��トを生�Eし、�Eレイヤー移動後に再計画で上書ぁE
    //       これにより、ExecuteMovePhaseで確実にインチE��トが存在する
    //==========================================================================
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Generating preliminary enemy intents at turn start..."),
        TurnIndex);

    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
    {
        // 仮のObservationsを生成（現在のプレイヤー位置で�E�E
        TArray<FEnemyObservation> PreliminaryObs;
        EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), PreliminaryObs);
        EnemyTurnDataSys->Observations = PreliminaryObs;

        // 仮のIntentsを収雁E
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

    // ☁E�E☁E削除: BuildObservations の実行（�Eレイヤー行動後に移動！E
    // UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    // UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();
    //
    // if (EnemyAISys && CachedPathFinder.IsValid() && CachedPlayerPawn)
    // {
    //     TArray<FEnemyObservation> Observations;
    //     EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
    //
    //     UE_LOG(LogTurnManager, Warning,
    //         TEXT("[Turn %d] BuildObservations completed: %d observations (input enemies=%d)"),
    //         TurnIndex, Observations.Num(), CachedEnemies.Num());
    //
    //     if (EnemyTurnDataSys)
    //     {
    //         EnemyTurnDataSys->Observations = Observations;
    //         UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Observations assigned to EnemyTurnDataSubsystem"), TurnIndex);
    //
    //         // CollectIntents は ContinueTurnAfterInput() で呼ぶ
    //         UE_LOG(LogTurnManager, Warning,
    //             TEXT("[Turn %d] CollectIntents SKIPPED - will be called after player move"),
    //             TurnIndex);
    //     }
    //     else
    //     {
    //         UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] EnemyTurnDataSubsystem is NULL!"), TurnIndex);
    //     }
    // }
    // else
    // {
    //     if (!EnemyAISys)
    //         UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] EnemyAISubsystem is NULL!"), TurnIndex);
    //     if (!CachedPathFinder.IsValid())
    //         UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] CachedPathFinder is invalid!"), TurnIndex);
    //     if (!CachedPlayerPawn)
    //         UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] CachedPlayerPawn is NULL!"), TurnIndex);
    // }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnIndex);

    // ☁E�E☁E削除: BeginPhase は早期実行済み�E�ゲート開放の最適化！E
    // BeginPhase(Phase_Turn_Init);
    // BeginPhase(Phase_Player_Wait);
}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    //==========================================================================
    // ☁E�E☁EPhase 2: CommandHandler経由でコマンド�E琁E��E025-11-09�E�E☁E�E☁E
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[✁EOUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

    //==========================================================================
    // (1) 権威チェチE��
    //==========================================================================
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Not authority, skipping"));
        return;
    }

    // ☁E�E☁EコアシスチE��: CommandHandler経由で完�E処琁E��E025-11-09�E�E☁E�E☁E
    if (CommandHandler)
    {
        // ProcessPlayerCommandで検証と受理を一括処琁E
        if (!CommandHandler->ProcessPlayerCommand(Command))
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command processing failed by CommandHandler"));
            return;
        }
        // 検証・受理成功 ↁE既存�EロジチE��で実際の処琁E��継綁E
    }
    else
    {
        // Fallback: 既存�E検証ロジチE��
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
            *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId, Command.TargetCell.X, Command.TargetCell.Y);

        //==========================================================================
        // (2) TurnId検証。等価匁E
        //==========================================================================
    // ������ FIX (2025-11-14): ignore stale OnAttacksFinished notifications from previous turns ������
        if (Command.TurnId != CurrentTurnIndex && Command.TurnId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
                Command.TurnId, CurrentTurnIndex);
            return;
        }

        //==========================================================================
        // ☁E�E☁E(2.5) WindowId検証�E�E025-11-09 CRITICAL FIX�E�E☁E�E☁E
        //==========================================================================
        if (Command.WindowId != InputWindowId && Command.WindowId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GameTurnManager] Command REJECTED - WindowId mismatch (%d != %d) | Turn=%d"),
                Command.WindowId, InputWindowId, CurrentTurnIndex);
            return;
        }

        //==========================================================================
        // (3) 二重移動チェチE���E�E025-11-10 FIX: クライアント通知追加�E�E
        //==========================================================================
        if (bPlayerMoveInProgress)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Move in progress, ignoring command"));

            // ☁E�E☁Eクライアントに拒否を通知 (2025-11-10) ☁E�E☁E
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

            // ☁E�E☁Eクライアントに拒否を通知 (2025-11-10) ☁E�E☁E
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

    //==========================================================================
    // ☁E�E☁E(4) 早期Gate閉鎖を削除�E�E025-11-09 FIX�E�E☁E�E☁E
    // CloseInputWindowForPlayer() で一括管琁E
    //==========================================================================

    //==========================================================================
    // (5) World取征E
    //==========================================================================
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] World is null"));
        return;
    }

    //==========================================================================
    // (6) PlayerPawn取征E
    //==========================================================================
    APawn* PlayerPawn = GetPlayerPawn();
    if (!PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] PlayerPawn not found"));
        return;
    }

    //==========================================================================
    // (7) ASC取征E
    //==========================================================================
    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
    if (!ASC)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] Player ASC not found"));
        return;
    }

    //==========================================================================
    // (8) EventData構築、Eirection をエンコーチE
    //==========================================================================
    FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove; // GameplayEvent.Intent.Move
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;
    EventData.OptionalObject = this; // TurnId 取得用

    // ☁E�E☁EDirection をエンコード！EurnCommandEncoding 統一�E�E☁E�E☁E
    const int32 DirX = FMath::RoundToInt(Command.Direction.X);
    const int32 DirY = FMath::RoundToInt(Command.Direction.Y);
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackDir(DirX, DirY));

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

    //==========================================================================
    // ☁E�E☁EPhase 5: Register Player Reservation with Pre-Validation (2025-11-09) ☁E�E☁E
    // Calculate target cell and VALIDATE BEFORE reservation
    //==========================================================================
    if (CachedPathFinder.IsValid())
    {
        const FIntPoint CurrentCell = CachedPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        const FIntPoint TargetCell(
            CurrentCell.X + static_cast<int32>(Command.Direction.X),
            CurrentCell.Y + static_cast<int32>(Command.Direction.Y));

        // ☁E�E☁EPRE-VALIDATION: Check if target cell is walkable (2025-11-09 FIX) ☁E�E☁E
        // ☁E�E☁E2025-11-10: 地形チェチE�� + 占有チェチE��の二層構造 ☁E�E☁E

        // 地形ブロチE��チェチE��
        const bool bTerrainBlocked = !CachedPathFinder->IsCellWalkableIgnoringActor(TargetCell, PlayerPawn);

        // ☁E�E☁EFIX (2025-11-11): 角抜け禁止チェチE���E�斜め移動時�E��E☁E�E
        // 重要E��目皁E��の地形に関係なく、斜め移動�E場合�E常にチェチE��する
        bool bCornerCutting = false;
        const int32 AbsDirX = FMath::Abs(FMath::RoundToInt(Command.Direction.X));
        const int32 AbsDirY = FMath::Abs(FMath::RoundToInt(Command.Direction.Y));
        const bool bIsDiagonalMove = (AbsDirX == 1 && AbsDirY == 1);

        if (bIsDiagonalMove)
        {
            // 斜め移動�E場合、両肩が塞がってぁE��ぁE��チェチE��
            const FIntPoint Side1 = CurrentCell + FIntPoint(static_cast<int32>(Command.Direction.X), 0);  // 横の肩
            const FIntPoint Side2 = CurrentCell + FIntPoint(0, static_cast<int32>(Command.Direction.Y));  // 縦の肩
            const bool bSide1Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side1, PlayerPawn);
            const bool bSide2Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side2, PlayerPawn);

            // ☁E�E☁EFIX (2025-11-11): 正しいルール - 牁E��の肩でも壁なら禁止 ☁E�E☁E
            // 角をすり抜けて移動することを防ぐため、両方の肩が通行可能な場合�Eみ許可
            if (!bSide1Walkable || !bSide2Walkable)
            {
                bCornerCutting = true;
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] CORNER CUTTING BLOCKED: (%d,%d)ↁE%d,%d) - at least one shoulder blocked [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
            else
            {
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[MovePrecheck] Diagonal move OK: (%d,%d)ↁE%d,%d) - both shoulders clear [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
        }

        // 占有チェチE���E��E刁E��外�EユニットがぁE��か！E
        bool bOccupied = false;
        bool bSwapDetected = false;
        AActor* OccupyingActor = nullptr;
        if (UGridOccupancySubsystem* OccSys = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
        {
            OccupyingActor = OccSys->GetActorAtCell(TargetCell);
            // 自刁E�E身は除外（予紁E��みの場合！E
            if (OccupyingActor && OccupyingActor != PlayerPawn)
            {
                bOccupied = true;

                // ☁E�E☁E2025-11-10: スワチE�E検�E�E�相手が自刁E�E開始セルに向かってぁE��か！E☁E�E☁E
                if (const FEnemyIntent* Intent = CachedIntents.Find(OccupyingActor))
                {
                    if (Intent->NextCell == CurrentCell)
                    {
                        // 入れ替わり検�E�E�E
                        bSwapDetected = true;
                        UE_LOG(LogTurnManager, Warning,
                            TEXT("[MovePrecheck] ☁ESWAP DETECTED: Player (%d,%d)ↁE%d,%d), Enemy %s (%d,%d)ↁE%d,%d)"),
                            CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                            *GetNameSafe(OccupyingActor),
                            TargetCell.X, TargetCell.Y, Intent->NextCell.X, Intent->NextCell.Y);
                    }
                }
            }
        }

        // ☁E�E☁E2025-11-10: ブロチE��時�E回転のみ適用�E�ターン不消費�E�E☁E�E☁E
        // ☁E�E☁E2025-11-11: 角抜けチェチE��も追加 ☁E�E☁E
        if (bTerrainBlocked || bOccupied || bCornerCutting)
        {
            const TCHAR* BlockReason = bCornerCutting ? TEXT("corner_cutting") :
                                       bTerrainBlocked ? TEXT("terrain") :
                                       bSwapDetected ? TEXT("swap") : TEXT("occupied");
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] BLOCKED by %s: Cell (%d,%d) | From=(%d,%d) | Applying FACING ONLY (No Turn)"),
                BlockReason, TargetCell.X, TargetCell.Y, CurrentCell.X, CurrentCell.Y);

            // ☁E�E☁EDEBUG: 周辺セルの状態を診断 (2025-11-09) ☁E�E☁E
            const int32 TargetCost = CachedPathFinder->GetGridCost(TargetCell.X, TargetCell.Y);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Target cell (%d,%d) GridCost=%d (expected: 3=Walkable, -1=Blocked)"),
                TargetCell.X, TargetCell.Y, TargetCost);

            // ☁E�E☁E2025-11-10: 占有情報はoccupied/swapの場合�Eみ出力！Eerrainの場合�E不要E��E☁E�E☁E
            if (!bTerrainBlocked && bOccupied && OccupyingActor)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] Occupied by: %s%s"),
                    *GetNameSafe(OccupyingActor),
                    bSwapDetected ? TEXT(" (SWAP detected)") : TEXT(""));
            }

            // 周辺4方向�E状態を出劁E
            const FIntPoint Directions[4] = {
                FIntPoint(1, 0),   // 右
                FIntPoint(-1, 0),  // 左
                FIntPoint(0, 1),   // 丁E
                FIntPoint(0, -1)   // 丁E
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

            // ☁E�E☁E2025-11-10: サーバ�E側でプレイヤーを回転 ☁E�E☁E
            const float Yaw = FMath::Atan2(Command.Direction.Y, Command.Direction.X) * 180.f / PI;
            FRotator NewRotation = PlayerPawn->GetActorRotation();
            NewRotation.Yaw = Yaw;
            PlayerPawn->SetActorRotation(NewRotation);

            UE_LOG(LogTurnManager, Log,
                TEXT("[MovePrecheck] ☁ESERVER: Applied FACING ONLY - Direction=(%.1f,%.1f), Yaw=%.1f"),
                Command.Direction.X, Command.Direction.Y, Yaw);

            // ☁E�E☁E2025-11-11: クライアントに回転専用RPC + ラチE��リセチE�� ☁E�E☁E
            if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    // 回転のみ適用
                    TPCB->Client_ApplyFacingNoTurn(InputWindowId, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ☁ESent Client_ApplyFacingNoTurn RPC (WindowId=%d, no turn consumed)"),
                        InputWindowId);

                    // ☁E�E☁ECRITICAL FIX (2025-11-11): REJECT送信でラチE��をリセチE�� ☁E�E☁E
                    // ACK は送らなぁE��ターン不消費だから�E�が、REJECT を送って
                    // クライアント�Eの bSentThisInputWindow をリセチE��する、E
                    // これにより、�Eレイヤーは別の方向に移動を試みることができる、E
                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ☁ESent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            // ☁E�E☁Eサーバ�E側の状態確認ログ (2025-11-10) ☁E�E☁E
            UE_LOG(LogTurnManager, Verbose, TEXT("[MovePrecheck] Server state after FACING ONLY:"));
            UE_LOG(LogTurnManager, Verbose, TEXT("  - WaitingForPlayerInput: %d (STAYS TRUE - no turn consumed)"), WaitingForPlayerInput);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - bPlayerMoveInProgress: %d (STAYS FALSE)"), bPlayerMoveInProgress);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - InputWindowId: %d (unchanged)"), InputWindowId);

            // ☁Eウィンドウを閉じずに継続（�Eレイヤーが�E入力可能、ターン不消費�E�E
            // WaitingForPlayerInput は true のまま、Gate も開ぁE��まま
            // コマンド�E"消費"マ�EクしなぁE��EarkCommandAsAccepted()を呼ばなぁE��E
            return;
        }

        // ☁E�E☁ECRITICAL FIX (2025-11-11): 予紁E�E劁E失敗をチェチE�� ☁E�E☁E
        const bool bReserved = RegisterResolvedMove(PlayerPawn, TargetCell);

        if (!bReserved)
        {
            // 予紁E��敁E- 他�EActorが既に予紁E��み�E�通常はPurgeで削除される�Eずだが、丁E��の場合！E
            UE_LOG(LogTurnManager, Error,
                TEXT("[MovePrecheck] RESERVATION FAILED: (%d,%d) ↁE(%d,%d) - Cell already reserved by another actor"),
                CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y);

            // 回転のみ適用�E�ターン不消費�E�E
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

                    // ☁E�E☁ECRITICAL FIX (2025-11-11): REJECT送信でラチE��をリセチE�� ☁E�E☁E
                    // ACK は送らなぁE��ターン不消費だから�E�が、REJECT を送って
                    // クライアント�Eの bSentThisInputWindow をリセチE��する、E
                    // これにより、�Eレイヤーは別の方向に移動を試みることができる、E
                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ☁ESent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            // ターン不消費で継綁E
            return;
        }

        UE_LOG(LogTurnManager, Log,
            TEXT("[MovePrecheck] OK: (%d,%d) ↁE(%d,%d) | Walkable=true | Reservation SUCCESS"),
            CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[GameTurnManager] PathFinder not available - Player reservation skipped"));
    }

    //==========================================================================
    // (9) GAS起動！Ebility.Moveを発動！E
    //==========================================================================
    const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

    if (TriggeredCount > 0)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] GAS activated for Move (count=%d)"), TriggeredCount);
        bPlayerMoveInProgress = true;

        // ☁E�E☁ECRITICAL FIX (2025-11-10): Precheck成功後にコマンドを"消費"マ�Eク ☁E�E☁E
        if (CommandHandler)
        {
            CommandHandler->MarkCommandAsAccepted(Command);
            UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] ☁ECommand marked as accepted (prevents duplicates)"));
        }

        // ☁E�E☁ECRITICAL FIX (2025-11-10): コマンド受琁E�E劁EↁEACKをクライアントに送信 ☁E�E☁E
        if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
        {
            if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
            {
                TPCB->Client_ConfirmCommandAccepted(InputWindowId);
                UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] ☁ESent Client_ConfirmCommandAccepted ACK (WindowId=%d)"), InputWindowId);
            }
        }

        // ☁Eコマンド受琁E�E劁EↁE入力窓をクローズ�E�多重入力防止�E�E
        CloseInputWindowForPlayer();
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] GAS activation failed - No abilities triggered"));
        ApplyWaitInputGate(true);   // 失敗時はゲートを再度開く
    }

    //==========================================================================
    // (10) CachedPlayerCommand保孁E
    //==========================================================================
    CachedPlayerCommand = Command;

    //==========================================================================
    // ☁E�E☁EFIX (2025-11-12): プレイヤー移動�EでインチE��ト�E生�E→敵ターン開姁E
    // 琁E��: プレイヤーの移動�Eを見てからインチE��トを決める�E��Eタンを押した瞬間！E
    //==========================================================================
    const FGameplayTag InputMove = RogueGameplayTags::InputTag_Move;  // ネイチE��ブタグを使用

    if (Command.CommandTag.MatchesTag(InputMove))
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] Player move command accepted - regenerating intents with predicted destination"),
            CurrentTurnIndex);

        // ☁E�E☁Eプレイヤーの移動�Eを計箁E☁E�E☁E
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
                TEXT("[Turn %d] Player destination predicted: (%d,%d) ↁE(%d,%d)"),
                CurrentTurnIndex, CurrentCell.X, CurrentCell.Y, PlayerDestination.X, PlayerDestination.Y);
        }

        // ☁E�E☁EDistanceFieldを移動�Eで更新 ☁E�E☁E
        if (UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            DF->UpdateDistanceField(PlayerDestination);

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] DistanceField updated with player destination (%d,%d)"),
                CurrentTurnIndex, PlayerDestination.X, PlayerDestination.Y);
        }

        // ☁E�E☁EインチE��ト�E生�E�E��Eレイヤー移動�Eで�E�E☁E�E☁E
        if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        {
            if (UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>())
            {
                // ☁E�E☁EFIX (2025-11-12): GetEnemiesSortedCopy()ではなくCachedEnemiesを使用 ☁E�E☁E
                // OnPlayerCommandAcceptedのタイミングではEnemyDataがまだ空の可能性があめE
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

                // ☁E�E☁EFIX (2025-11-12): BuildObservationsを呼ばずに手動でObservationsを構篁E☁E�E☁E
                // BuildObservationsはPlayerPawn->GetActorLocation()を使ってDistanceFieldを上書きしてしまぁE��めE
                // 予測位置(PlayerDestination)を維持するために、手動でObservationsを構築すめE
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

                // CollectIntents: void function with output parameter
                EnemyAI->CollectIntents(Observations, Enemies, Intents);

                EnemyData->SaveIntents(Intents);

                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] Intents regenerated: %d intents (with player destination)"),
                    CurrentTurnIndex, Intents.Num());
            }
        }

        // ☁E�E☁E攻撁E��定による刁E��E☁E�E☁E
        const bool bHasAttack = HasAnyAttackIntent();

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] bHasAttack=%s (同時移勁E%s)"),
            CurrentTurnIndex,
            bHasAttack ? TEXT("TRUE") : TEXT("FALSE"),
            bHasAttack ? TEXT("無効") : TEXT("有効"));

        if (bHasAttack)
        {
            // ☁E�E☁E攻撁E��めE 同時移動しなぁE��逐次実衁E
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected (with player destination) ↁESequential mode"),
                CurrentTurnIndex);

            ExecuteSequentialPhase();  // 攻撁E�E移動を頁E��に実裁E
        }
        else
        {
            // ☁E�E☁E攻撁E��ぁE 同時移動すめE
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] No attacks (with player destination) ↁESimultaneous move mode"),
                CurrentTurnIndex);

            ExecuteSimultaneousPhase();  // 同時移勁E
        }
    }
    else if (Command.CommandTag == RogueGameplayTags::InputTag_Turn)  // ネイチE��ブタグを使用
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


//------------------------------------------------------------------------------
// Helper: ResolveOrSpawnPathFinderのフォールバック用
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnPathFinder()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: World is null"));
        return;
    }

    // 既に設定済みならそのまま
    if (IsValid(PathFinder))
    {
        return;
    }

    // シーンに既にあるも�Eを使ぁE
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AGridPathfindingLibrary::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        PathFinder = Cast<AGridPathfindingLibrary>(Found[0]);
        CachedPathFinder = PathFinder;
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Found existing PathFinder: %s"), *GetNameSafe(PathFinder));
        return;
    }

    // 無ければGameModeから取得を試みめE
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

    // 最後�E手段: スポ�Eン。通常はGameModeが生成する�Eで、ここ�Eフォールバック
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

//------------------------------------------------------------------------------
// Helper: ResolveOrSpawnUnitManagerのフォールバック用
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnUnitManager()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnUnitManager: World is null"));
        return;
    }

    // 既に設定済みならそのまま
    if (IsValid(UnitMgr))
    {
        return;
    }

    // シーンに既にあるも�Eを使ぁE
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AUnitManager::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        UnitMgr = Cast<AUnitManager>(Found[0]);
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Found existing UnitManager: %s"), *GetNameSafe(UnitMgr));
        return;
    }

    // 無ければGameModeから取得を試みる（後方互換性のため�E�E
    if (ATBSLyraGameMode* GM = World->GetAuthGameMode<ATBSLyraGameMode>())
    {
        UnitMgr = GM->GetUnitManager();
        if (IsValid(UnitMgr))
        {
            UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Got UnitManager from GameMode: %s"), *GetNameSafe(UnitMgr));
            return;
        }
    }

    // 最後�E手段: スポ�Eン。通常はここで生�EされめE
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

//------------------------------------------------------------------------------
// Helper: FindPathFinder
//------------------------------------------------------------------------------

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










// GameTurnManagerBase.cpp

// GameTurnManagerBase.cpp - line 999の既存実裁E
void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    if (!HasAuthority()) return;

    // ☁E�E☁E修正: bPlayerMoveInProgressのチェチE��に変更 ☁E�E☁E
    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnIndex);
        return;
    }

    // ☁E�E☁E修正: bTurnContinuingの削除。PlayerMoveInProgressを使用 ☁E�E☁E
    bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnIndex);

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-11): プレイヤー行動後に敵の意思決定を実衁E☁E�E☁E
    // 琁E��: 不思議のダンジョン型�E交互ターン制では、�Eレイヤーが行動してから敵が判断する
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Collecting enemy intents AFTER player move"), CurrentTurnIndex);

    // 敵リストを更新
    CollectEnemies();

    //==========================================================================
    // ☁E�E☁EFIX (2025-11-12): BuildObservationsを直接実裁E☁E�E☁E
    // 琁E��: BuildObservations_Implementation()は空なので、直接EnemyAISubsystemを呼ぶ
    //==========================================================================
    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn)
    {
        // DistanceFieldを更新�E�敵AIの判断に忁E��E��E
        if (UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>())
        {
            FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());
            DistanceField->UpdateDistanceField(PlayerGrid);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] DistanceField updated for PlayerGrid=(%d,%d)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y);
        }

        // プレイヤーの新しい位置を�Eに観測チE�Eタを構篁E
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

    // 敵の意図を収雁E��この時点の盤面で判断�E�E
    CollectIntents();

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Enemy intent collection complete"), CurrentTurnIndex);

    // ☁E�E☁E攻撁E��宁E☁E�E☁E
    const bool bHasAttack = HasAnyAttackIntent();

    if (bHasAttack)
    {
        ExecuteSequentialPhase(); // 冁E��でExecutePlayerMove()を呼ぶ
    }
    else
    {
        ExecuteSimultaneousPhase(); // 冁E��でExecutePlayerMove()を呼ぶ
    }
}




void AGameTurnManagerBase::ExecuteSimultaneousPhase()
{
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Simultaneous Move Phase (No Attacks) ===="), CurrentTurnIndex);

    //==========================================================================
    // ☁E�E☁Eプレイヤーの移動�E既に OnPlayerCommandAccepted_Implementation で実行済み
    //==========================================================================
    // ExecutePlayerMove();  // ☁E�E☁E削除: 二重実行を防ぁE

    //==========================================================================
    // (1) 敵の移動フェーズ。同時実衁E
    //==========================================================================
    ExecuteMovePhase();  // ☁E�E☁E既存�E実裁E��使用、Ector 参�Eが正しい

    // ☁E�E☁E注愁E 移動完亁E�E OnAllMovesFinished() チE��ゲートで通知されめE
}

void AGameTurnManagerBase::ExecuteSimultaneousMoves()
{
    // ExecuteSimultaneousPhase() を呼び出す（エイリアス�E�E
    ExecuteSimultaneousPhase();
}




void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    // ☁E�E☁E権限チェチE��。サーバ�E限宁E☁E�E☁E
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnIndex);

    // ☁E�E☁E確実に動作する方法：既存�E ExecuteAttacks() を呼び出ぁE☁E�E☁E
    ExecuteAttacks();
}


//------------------------------------------------------------------------------
// ExecuteSequentialPhaseの修正牁E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::ExecuteSequentialPhase()
{
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Sequential Phase (Attack ↁEMove) ===="), CurrentTurnIndex);

    //==========================================================================
    // (1) 攻撁E��ェーズ
    //==========================================================================
    ExecuteAttacks();  // 既存�E攻撁E��行関数

    // ☁E�E☁E注愁E 攻撁E��亁E�E OnAttacksFinished() チE��ゲートで通知されめE
    // ここでは移動フェーズは開始しなぁE
}





//------------------------------------------------------------------------------
// OnPlayerMoveCompleted - Non-Dynamic - Gameplay.Event.Turn.Ability.Completed 受信晁E
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    //==========================================================================
    // (1) TurnId検証、EventMagnitudeから取征E
    //==========================================================================
    const int32 NotifiedTurn = Payload ? static_cast<int32>(Payload->EventMagnitude) : -1;
    const int32 CurrentTurn = GetCurrentTurnIndex();

    if (NotifiedTurn != CurrentTurn)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("Server: IGNORE stale move notification (notified=%d, current=%d)"),
            NotifiedTurn, CurrentTurn);
        return;
    }

    //==========================================================================
    // (2) Authority チェチE��
    //==========================================================================
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
        CurrentTurnIndex, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

    //==========================================================================
    // Get the completed actor from the payload
    AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
    FinalizePlayerMove(CompletedActor);

    // ☁E�E☁EPhase 5: AP残量確認とGate再オープン
    //==========================================================================

    // ☁E�E☁E封E��皁E��拡張: APシスチE��統合！E025-11-09�E�E☁E�E☁E
    // APシスチE��が実裁E��れたら、以下�Eコードを有効化してください
    // - PlayerAttributeSetにAPアトリビュートを追加
    // - AP消費/回復のロジチE��を実裁E
    // - Gate再オープンの条件をAP残量に基づぁE��制御
    /*
    int32 PlayerAP = 0;
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        // APの取得方法（例：AttributeSetから�E�E
        const UPlayerAttributeSet* Attrs = ASC->GetSet<UPlayerAttributeSet>();
        PlayerAP = Attrs ? Attrs->GetActionPoints() : 0;
    }

    if (PlayerAP > 0 && CurrentPhase == Phase_Player_Wait)
    {
        //======================================================================
        // (8a) Gate.Input.Openを付与（連続移動許可�E�E
        //======================================================================
        if (UAbilitySystemComponent* ASC = GetPlayerASC())
        {
            // ☁E�E☁E修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate.Input.Open REOPENED (AP=%d remaining)"),
                CurrentTurnIndex, PlayerAP);
        }

        //======================================================================
        // (8b) Phase.Player.WaitInputを付丁E
        //======================================================================
        if (UAbilitySystemComponent* ASC = GetPlayerASC())
        {
            // ☁E�E☁E修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Phase.Player.WaitInput RESTORED for continuous move"),
                CurrentTurnIndex);
        }

        //======================================================================
        // (8c) WaitingForPlayerInputを�E度trueに設宁E
        //======================================================================
        WaitingForPlayerInput = true;
   OnRep_WaitingForPlayerInput();

        UE_LOG(LogTurnManager, Log,
            TEXT("Turn %d: Continuous move enabled (AP=%d, Phase=%s)"),
            CurrentTurnIndex, PlayerAP, *CurrentPhase.ToString());
    }
    else
    {
        //======================================================================
        // (8d) AP残量なし：フェーズ終亁E
        //======================================================================
        UE_LOG(LogTurnManager, Log,
            TEXT("Turn %d: No AP remaining or not in WaitInput phase (AP=%d, Phase=%s)"),
            CurrentTurnIndex, PlayerAP, *CurrentPhase.ToString());

        // フェーズ終亁E��シスチE��の状態に応じて
        // EndPlayerPhase();
    }
    */

    //==========================================================================
    // ☁E�E☁E暫定実裁E APシスチE��がなぁE��合フェーズ終亁E
    //==========================================================================
    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnIndex);

    // ☁E�E☁E現在はAPシスチE��未実裁E�Eため、フェーズ遷移は別のロジチE��で制御 ☁E�E☁E
    // APシスチE��実裁E���E、AP残量に基づぁE��フェーズ遷移を制御する

    // ☁E�E☁E注愁E 敵ターン開始�EOnPlayerCommandAcceptedで既に実行済み ☁E�E☁E
    // プレイヤーが�Eタンを押した瞬間（移動�E予測時）にインチE��ト生成と敵ターン開姁E
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

    // ☁E�E☁EFIX (2025-11-12): 攻撁E��ェーズ中は入力ゲートを閉じめE☁E�E☁E
    // プレイヤー移動と攻撁E�E競合を防ぐため、攻撁E��行中は入力を受け付けなぁE��E
    // 入力�E OnAttacksFinished で再度開く、E
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
    ApplyWaitInputGate(false);  // Close input gate

    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* PM = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            // ☁E�E☁EOut パラメータで受け取る ☁E�E☁E
            TArray<FResolvedAction> AttackActions;
            PM->ExecuteAttackPhaseWithSlots(
                EnemyTurnData->Intents,
                AttackActions  // Out パラメータ
            );

            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ExecuteAttacks: %d attack actions resolved"),
                CurrentTurnIndex, AttackActions.Num());

            if (UAttackPhaseExecutorSubsystem* AttackExecutor = World->GetSubsystem<UAttackPhaseExecutorSubsystem>())
            {
                if (AttackExecutor->OnFinished.Contains(this, FName("OnAttacksFinished")))
                {
                    // 既にバインド済み
                }
                else
                {
                    AttackExecutor->OnFinished.AddDynamic(this, &AGameTurnManagerBase::OnAttacksFinished);
                }

                AttackExecutor->BeginSequentialAttacks(AttackActions, CurrentTurnIndex);
            }
        }
    }
}



void AGameTurnManagerBase::OnAttacksFinished(int32 TurnId)
{
    if (!HasAuthority())
    {
        return;
    }

    // ☁E�E☁EFIX (2025-11-12): TurnId検証を削除 ☁E�E☁E
    // 攻撁E��ニメーション完亁E��次のターンで通知されることがあるため、E
    // TurnId mismatchでスキチE�Eすると、ConvertAttacksToWait()が呼ばれず、E
    // 攻撁E��た敵が移動してしまぁE��題を修正
    // ������ FIX (2025-11-14): ignore stale OnAttacksFinished notifications from previous turns ������
    if (TurnId != CurrentTurnIndex)
    {
        // CodeRevision: INC-2025-00004-R1 (Stale OnAttacksFinished notifications are ignored) (2025-11-14 22:50)
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] OnAttacksFinished: Stale notification from Turn %d - IGNORED"),
            CurrentTurnIndex, TurnId);
        return;
    }

    // ☁E�E☁EFIX (2025-11-12): 攻撁E��亁E��に入力ゲートを再度開く ☁E�E☁E
    // ExecuteAttacks で閉じたゲートを再度開き、�Eレイヤーが次の入力をできるようにする、E
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] OnAttacksFinished: Re-opening input gate (attack phase complete)"), TurnId);
    ApplyWaitInputGate(true);  // Re-open input gate

    //==========================================================================
    // (2) 移動フェーズ。攻撁E��E
    //==========================================================================
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Starting Move Phase (after attacks)"), TurnId);

    // ☁E�E☁EFIX (2025-11-12): 攻撁E��亁E���E呼び出しなので、攻撁E��ンチE��トチェチE��をスキチE�E ☁E�E☁E
    ExecuteMovePhase(true);  // bSkipAttackCheck=true で無限ループを防止

    // ☁E�E☁E注愁E 移動完亁E�E OnAllMovesFinished() チE��ゲートで通知されめE
}


void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{
    // ☁E�E☁EPhase 5: ConflictResolver Integration (2025-11-09) ☁E�E☁E
    // Use TurnCorePhaseManager instead of non-existent ActionExecutorSubsystem

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

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-12): フェイルセーチE- インチE��トが空なら�E生�E ☁E�E☁E
    //==========================================================================
    if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteMovePhase: No enemy intents detected, attempting fallback generation..."),
            CurrentTurnIndex);

        // フェイルセーチE Observations と Intents を�E生�E
        UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
        if (EnemyAISys && CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
        {
            // DistanceFieldを更新
            if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
            {
                FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());
                DistanceField->UpdateDistanceField(PlayerGrid);
                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] Fallback: DistanceField updated for PlayerGrid=(%d,%d)"),
                    CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y);
            }

            // Observationsを生戁E
            TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
            EnemyData->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Fallback: Generated %d observations"),
                CurrentTurnIndex, Observations.Num());

            // Intentsを収雁E
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

        // 再チェチE��: まだ空ならスキチE�E
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

    //==========================================================================
    // FIX (INC-2025-00003): capture attack intent before regen
    const bool bHasAttack = HasAnyAttackIntent();
    // ☁E�E☁EATTACK PRIORITY (2025-11-12): 攻撁E��ンチE��トがあれば攻撁E��ェーズへ ☁E�E☁E
    // 琁E��: プレイヤー移動後�E再計画で攻撁E��昁E��する可能性があるため、E
    //       Execute直前に再チェチE��して攻撁E��先で処琁E
    // ☁E�E☁EFIX (2025-11-12): 攻撁E��亁E���E呼び出し時はスキチE�E�E�無限ループ防止�E�E
    //==========================================================================
    if (!bSkipAttackCheck)
    {
        if (bHasAttack)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ☁EATTACK INTENT detected (%d intents) - Executing attack phase instead of move phase"),
                CurrentTurnIndex, EnemyData->Intents.Num());

            // 攻撁E��ェーズを実行（既存�E実裁E��使用�E�E
            ExecuteAttacks();

            // 注愁E 攻撁E��亁E��に OnAttacksFinished() が呼ばれ、その征EExecuteMovePhase(true) が呼ばれる
            //       そ�E時�E bSkipAttackCheck=true なので、このチェチE��がスキチE�Eされて移動フェーズが実行される
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

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-11): Playerの移動もConflictResolverに追加 ☁E�E☁E
    // Swap検�Eを機�Eさせるため、PlayerとEnemyの移動情報を統合すめE
    //==========================================================================
    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

    // Playerが移動してぁE��場合、Intentリストに追加
    if (CachedPlayerPawn && CachedPathFinder.IsValid())
    {
        const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = CachedPathFinder->WorldToGrid(PlayerLoc);

        // PendingMoveReservationsからPlayerの移動�Eを取征E
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
                PlayerIntent.BasePriority = 200;  // Playerの優先度を高く設宁E

                AllIntents.Add(PlayerIntent);

                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] ☁EAdded Player intent to ConflictResolver: (%d,%d) ↁE(%d,%d)"),
                    CurrentTurnIndex, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                    PlayerNextCell->X, PlayerNextCell->Y);
            }
        }
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase: Processing %d intents (%d enemies + Player) via ConflictResolver"),
        CurrentTurnIndex, AllIntents.Num(), EnemyData->Intents.Num());

    //==========================================================================
    // (1) Conflict Resolution: Convert Intents ↁEResolvedActions
    //==========================================================================
    TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ConflictResolver produced %d resolved actions"),
        CurrentTurnIndex, ResolvedActions.Num());

    //==========================================================================
    // ☁E�E☁ECRITICAL FIX (2025-11-11): Playerの移動がSwap検�Eでキャンセルされた場合�E処琁E☁E�E☁E
    //==========================================================================
    if (CachedPlayerPawn)
    {
        // ResolvedActions冁E��PlayerがbIsWait=trueにマ�EクされてぁE��かチェチE��
        for (const FResolvedAction& Action : ResolvedActions)
        {
            if (IsValid(Action.SourceActor.Get()) && Action.SourceActor.Get() == CachedPlayerPawn)
            {
                if (Action.bIsWait)
                {
                    // PlayerのIntentがSwap検�Eで拒否されぁE
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[Turn %d] ☁EPlayer movement CANCELLED by ConflictResolver (Swap detected or other conflict)"),
                        CurrentTurnIndex);

                    // Playerの予紁E��解放
                    TWeakObjectPtr<AActor> PlayerKey(CachedPlayerPawn);
                    PendingMoveReservations.Remove(PlayerKey);

                    // PlayerのアビリチE��をキャンセル�E�実行中なら中止�E�E
                    if (UAbilitySystemComponent* ASC = CachedPlayerPawn->FindComponentByClass<UAbilitySystemComponent>())
                    {
                        // 移動アビリチE��をキャンセル
                        ASC->CancelAllAbilities();
                        UE_LOG(LogTurnManager, Log,
                            TEXT("[Turn %d] Player abilities cancelled due to Swap conflict"),
                            CurrentTurnIndex);
                    }

                    // ターン終亁E�E琁E��進む�E�Enemyの移動�E実行しなぁE��E
                    EndEnemyTurn();
                    return;
                }
                break;
            }
        }
    }

    //==========================================================================
    // (2) Execute Resolved Actions: Trigger GAS abilities
    //==========================================================================
    PhaseManager->CoreExecutePhase(ResolvedActions);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase complete - movements dispatched"),
        CurrentTurnIndex);

    // Note: Movement completion is handled via Barrier callbacks
    // EndEnemyTurn() will be called when all movements finish
}




//------------------------------------------------------------------------------
// ExecutePlayerMoveの修正牁E
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ExecutePlayerMoveの完�E修正牁E
//------------------------------------------------------------------------------
// GameTurnManagerBase.cpp - line 1170の既存実裁E
// ☁E�E☁E変更なし（参照のため全斁E��載！E☁E�E☁E

void AGameTurnManagerBase::ExecutePlayerMove()
{
    //==========================================================================
    // Step 1: 権限チェチE��
    //==========================================================================
    if (!HasAuthority())
    {
        return;
    }

    //==========================================================================
    // Step 2: PlayerPawn取征E
    //==========================================================================
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

    //==========================================================================
    // ☁E�E☁Eルーチン: Subsystem経由。推奨・型安�E
    // ☁E�E☁EUActionExecutorSubsystem は存在しなぁE��めコメントアウチE
    //==========================================================================
    /*
    if (UWorld* World = GetWorld())
    {
        if (UActionExecutorSubsystem* Exec = World->GetSubsystem<UActionExecutorSubsystem>())
        {
            const bool bSent = Exec->SendPlayerMove(CachedPlayerCommand);
            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: SendPlayerMove: %s"),
                CurrentTurnIndex, bSent ? TEXT("✁EK") : TEXT("❌FAILED"));

            if (bSent)
            {
                return; // 成功したらここで終亁E
            }
        }
    }
    */

    //==========================================================================
    // ☁E�E☁Eルーチン: 直接GAS経由。フォールバック
    // 問顁E EventMagnitude に TurnId だけを設定すると、GA_MoveBase で
    // Direction が取得できなぁE��以前�E実裁E��は Direction をエンコードしてぁE��
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        //======================================================================
        // ☁E�E☁EDIAGNOSTIC (2025-11-12): ASCのアビリチE��とトリガータグを診断 ☁E�E☁E
        //======================================================================
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

                // CanActivateAbility をテスチE
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

                // GA_MoveBaseかどぁE��チェチE��
                const bool bIsMoveAbility = Spec.Ability->GetClass()->GetName().Contains(TEXT("MoveBase"));
                if (bIsMoveAbility)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("    - ☁EThis is a MOVE ability! CanActivate=%s"),
                        bCanActivate ? TEXT("YES") : TEXT("NO"));
                    if (bCanActivate)
                    {
                        ++PotentialMoveAbilities;
                    }
                }

                // DynamicAbilityTags (using new API GetDynamicSpecSourceTags)
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
        EventData.OptionalObject = this; // TurnId 取得用

        //======================================================================
        // ☁E�E☁EDirection をエンコード！EurnCommandEncoding 統一�E�E☁E�E☁E
        //======================================================================
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
                TEXT("Turn %d: ✁EA_MoveBase activated (count=%d)"),
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



//------------------------------------------------------------------------------ 
// OnAllMovesFinished - Barrierコールバック
//------------------------------------------------------------------------------ 

void AGameTurnManagerBase::OnAllMovesFinished(int32 FinishedTurnId)
{
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::OnAllMovesFinished: Not authority"));
        return;
    }

    // TurnId検証
    if (FinishedTurnId != CurrentTurnIndex)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("GameTurnManager::OnAllMovesFinished: Stale TurnId (%d != %d)"),
            FinishedTurnId, CurrentTurnIndex);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier complete - all moves finished"), FinishedTurnId);

    //==========================================================================
    // ☁E�E☁Eセーフティ: 実行中フラグ/ゲートを確実に解除
    //==========================================================================
    bPlayerMoveInProgress = false;
 

    ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

    //==========================================================================
    // ☁E�E☁E次ターンへ進行（これが OnTurnStarted をBroadcastする�E�E
    //==========================================================================
    EndEnemyTurn();  // また�E AdvanceTurnAndRestart(); (実裁E��応じて)
}








void AGameTurnManagerBase::EndEnemyTurn()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnIndex);

    //==========================================================================
    // ☁E�E☁EPhase 4: 二重鍵チェチE��
    //==========================================================================
    if (!CanAdvanceTurn(CurrentTurnIndex))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[EndEnemyTurn] ABORT: Cannot end turn %d (actions still in progress)"),
            CurrentTurnIndex);

        //======================================================================
        // ☁E�E☁EチE��チE��: Barrierの状態をダンチE
        //======================================================================
        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
            }
        }

        //======================================================================
        // ☁E�E☁EPhase 5補宁E リトライ連打防止�E�E025-11-09�E�E☁E�E☁E
        // 最初�E1回だけリトライをスケジュール、以降�E抑止
        //======================================================================
        // Force-clear lingering InProgress tags before retrying.
        ClearResidualInProgressTags();

        if (!bEndTurnPosted)
        {
            bEndTurnPosted = true;  // フラグを立てめE

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

        return;  // ☁E�E☁E進行を中止
    }

    //==========================================================================
    // ☁E�E☁EPhase 5補宁E 残留タグクリーンアチE�E�E�E025-11-09�E�E☁E�E☁E
    // Barrier完亁E��、ターン進行前に残留InProgressタグを掃除
    //==========================================================================
    ClearResidualInProgressTags();

    //==========================================================================
    // (1) 次のターンへ進む
    //==========================================================================
    AdvanceTurnAndRestart();
}

//------------------------------------------------------------------------------
// ☁E�E☁EPhase 5補宁E 残留InProgressタグの強制クリーンアチE�E�E�E025-11-09�E�E☁E�E☁E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::ClearResidualInProgressTags()
{
    // ☁E�E☁ECRITICAL FIX (2025-11-11): 全てのブロチE��ングタグをクリア ☁E�E☁E
    // ☁E�E☁EEXTENDED FIX (2025-11-11): Movement.Mode.Fallingも追加 ☁E�E☁E
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

    // プレイヤー追加
    if (APawn* PlayerPawn = GetPlayerPawn())
    {
        AllUnits.Add(PlayerPawn);
    }

    // 敵追加
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

        // State.Action.InProgress をクリア
        const int32 InProgressCount = ASC->GetTagCount(InProgressTag);
        for (int32 i = 0; i < InProgressCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(InProgressTag);
        }
        TotalInProgress += InProgressCount;

        // State.Ability.Executing をクリア
        const int32 ExecutingCount = ASC->GetTagCount(ExecutingTag);
        for (int32 i = 0; i < ExecutingCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(ExecutingTag);
        }
        TotalExecuting += ExecutingCount;

        // State.Moving をクリア
        const int32 MovingCount = ASC->GetTagCount(MovingTag);
        for (int32 i = 0; i < MovingCount; ++i)
        {
            ASC->RemoveLooseGameplayTag(MovingTag);
        }
        TotalMoving += MovingCount;

        // ☁E�E☁ECRITICAL FIX (2025-11-11): Movement.Mode.Falling をクリア ☁E�E☁E
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

//------------------------------------------------------------------------------
// Replication Callbacks
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnRep_WaitingForPlayerInput()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Client] OnRep_WaitingForPlayerInput: %d"),
        WaitingForPlayerInput);

    //==========================================================================
    // 🌟 3-Tag System: クライアント�EでGateを開く（レプリケーション後！E
    //==========================================================================
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

    //==========================================================================
    // Standalone + Network両対応：ゲートリセチE��。既存�E処琁E
    //==========================================================================
    if (UWorld* World = GetWorld())
    {
        if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(World->GetFirstPlayerController()))
        {
            if (WaitingForPlayerInput)
            {
                // 入力ウィンドウが開ぁE��めEↁEゲートをリセチE��
                
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
            // ☁E�E☁E修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate OPENED (Phase+Gate tags added), WindowId=%d"),
                CurrentTurnIndex, InputWindowId);
        }
        else
        {
            // ☁E�E☁EGate だけを閉じる。Phase は維持E
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);
            // ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);  // Phaseは維持E

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
    // サーバ�E専用
    if (!HasAuthority())
    {
        return;
    }

    // ☁E�E☁EWindowIdをインクリメンチE
    ++InputWindowId;

    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Opened: Turn=%d WindowId=%d"),
        CurrentTurnIndex, InputWindowId);

    // ☁E�E☁EコアシスチE��: CommandHandler経由でInput Window開始！E025-11-09�E�E☁E�E☁E
    // ☁E�E☁EWeek 1: UPlayerInputProcessorに委譲�E�E025-11-09リファクタリング�E�E
    if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(TurnFlowCoordinator->GetCurrentTurnId());
    }

    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(InputWindowId);
    }

    // ☁E�E☁EGate/Phaseタグを付与（既存�EApplyWaitInputGateを流用�E�E
    ApplyWaitInputGate(true);

    // WaitingForPlayerInputフラグを立てめE
    WaitingForPlayerInput = true;

    // ☁E�E☁EStandalone では OnRep が呼ばれなぁE�Eで手動実裁E
    OnRep_WaitingForPlayerInput();
    OnRep_InputWindowId();
}

//------------------------------------------------------------------------------
// Possess通知 & 入力窓オープン/クローズ
//------------------------------------------------------------------------------

void AGameTurnManagerBase::NotifyPlayerPossessed(APawn* NewPawn)
{
    if (!HasAuthority()) return;

    CachedPlayerPawn = NewPawn;
    UE_LOG(LogTurnManager, Log, TEXT("[Turn] NotifyPlayerPossessed: %s"), *GetNameSafe(NewPawn));

    // プレイヤー所持完亁E��ラグを立てめE
    bPlayerPossessed = true;

    // すでにターン開始済みで、まだ窓が開いてぁE��ければ再オープン
    if (bTurnStarted && !WaitingForPlayerInput)
    {
        OpenInputWindowForPlayer();
        bDeferOpenOnPossess = false; // フラグをクリア
    }
    // また�E、E��延オープンフラグが立ってぁE��場合も開く
    else if (bTurnStarted && bDeferOpenOnPossess)
    {
        OpenInputWindowForPlayer();
        bDeferOpenOnPossess = false; // フラグをクリア
    }
    else if (!bTurnStarted)
    {
        // ターン未開始なら、ゲート機構で開始を試衁E
        bDeferOpenOnPossess = true;
        UE_LOG(LogTurnManager, Log, TEXT("[Turn] Defer input window open until turn starts"));
        TryStartFirstTurn(); // ☁E�E☁Eゲート機槁E
    }
}

//==========================================================================
// ☁E�E☁ETryStartFirstTurn: 全条件が揃ったらStartFirstTurnを実行！E025-11-09 解析サマリ対応！E
//==========================================================================
void AGameTurnManagerBase::TryStartFirstTurn()
{
    if (!HasAuthority()) return;

    // 全条件チェチE��
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

void AGameTurnManagerBase::OpenInputWindowForPlayer()
{
    if (!HasAuthority()) return;
    if (!ensure(CachedPlayerPawn)) return;

    WaitingForPlayerInput = true;
    ++InputWindowId;

    // ☁E�E☁ETurnCommandHandlerに通知�E�E025-11-09 修正�E�E
    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(InputWindowId);
    }

    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CachedPlayerPawn))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            // 入力ゲートを"開く"
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);
        }
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn] InputWindow OPEN: Turn=%d WinId=%d Gate=OPEN Pawn=%s"),
        GetCurrentTurnIndex(), InputWindowId, *GetNameSafe(CachedPlayerPawn));
}

void AGameTurnManagerBase::CloseInputWindowForPlayer()
{
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[CloseInputWindow] REJECT: Not authority"));
        return;
    }

    if (!CachedPlayerPawn)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[CloseInputWindow] REJECT: No player pawn"));
        return;
    }

    if (!WaitingForPlayerInput)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CloseInputWindow] REJECT: Already closed (WPI=false) | Turn=%d WindowId=%d"),
            CurrentTurnIndex, InputWindowId);
        return;
    }

    // ☁E�E☁Eフラグとゲートを同時に閉じる！E025-11-09 FIX�E�E☁E�E☁E
    WaitingForPlayerInput = false;

    // ☁E�E☁ETurnCommandHandlerに通知�E�E025-11-09 修正�E�E
    if (CommandHandler)
    {
        CommandHandler->EndInputWindow();
    }

    // ☁E�E☁EGate タグを削除�E�E025-11-09 FIX�E�E
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CachedPlayerPawn))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);
        }
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[CloseInputWindow] ✁ESUCCESS: Turn=%d WindowId=%d Reason=AcceptedValidPlayerCmd"),
        CurrentTurnIndex, InputWindowId);
}


void AGameTurnManagerBase::OnRep_InputWindowId()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Client OnRep: WindowId=%d"),
        InputWindowId);

    // ☁E�E☁E重要E クライアント�Eではタグを触らなぁE
    // タグはサーバ�Eからレプリケートされる
    // ここではUIの更新のみを行う

    // UI更新チE��ゲートがあれば発火
    // OnInputWindowChanged.Broadcast(InputWindowId);

    // ☁E�E☁E削除: ResetInputWindowGateは呼ばなぁE
    // if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(GetPlayerControllerTBS()))
    // {
    //     PC->ResetInputWindowGate();  // 削除
    // }
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



bool AGameTurnManagerBase::CanAdvanceTurn(int32 TurnId) const
{
    //==========================================================================
    // (1) Barrier::IsQuiescent チェチE��
    //==========================================================================
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
            return false;
        }
    }

    //==========================================================================
    // (2) State.Action.InProgressタグカウントチェチE��
    //==========================================================================
    bool bNoInProgressTags = false;
    int32 InProgressCount = 0;

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        InProgressCount = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress);
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
        return false;
    }

    //==========================================================================
    // (3) 二重鍵の判定（両方がtrueで進行可能�E�E
    //==========================================================================
    const bool bCanAdvance = bBarrierQuiet && bNoInProgressTags;

    if (bCanAdvance)
    {
            UE_LOG(LogTurnManager, Log,
                TEXT("[CanAdvanceTurn] ✁EK: Turn=%d (Barrier=Quiet, InProgress=0)"),
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

//------------------------------------------------------------------------------
// ダンジョン生�EシスチE��統吁EPIの実裁E
//------------------------------------------------------------------------------

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
    return true; // Assume success as new function is void
}

bool AGameTurnManagerBase::NextFloor()
{
    if (!DungeonSystem)
    {
        return false;
    }
    CurrentFloorIndex++;
    DungeonSystem->TransitionToFloor(CurrentFloorIndex);
    return true; // Assume success
}

int32 AGameTurnManagerBase::TM_ReturnGridStatus(FVector World) const
{
    if (ADungeonFloorGenerator* FloorGen = GetFloorGenerator())
    {
        return FloorGen->ReturnGridStatus(World);
    }
    return -999;
}

void AGameTurnManagerBase::TM_GridChangeVector(FVector World, int32 Value)
{
    if (ADungeonFloorGenerator* FloorGen = GetFloorGenerator())
    {
        FloorGen->GridChangeVector(World, Value);
    }
}

void AGameTurnManagerBase::WarpPlayerToStairUp(AActor* Player)
{
    if (!Player || !DungeonSystem)
    {
        return;
    }

    // ☁E�E☁E封E��皁E��実裁E 階段ワープ機�E�E�E025-11-09�E�E☁E�E☁E
    // RogueDungeonSubsystemから階段の位置を取得してワーチE
    // 実裁E��！E
    // - URogueDungeonSubsystem::GetStairUpLocation() を追加
    // - PlayerPawnをその位置にチE��ポ�EチE
    // - カメラを更新
    UE_LOG(LogTurnManager, Warning, TEXT("[WarpPlayerToStairUp] Not implemented yet - requires dungeon stair tracking"));
}

//------------------------------------------------------------------------------
// グリチE��コスト設定、Elueprint用の実裁E
//------------------------------------------------------------------------------

void AGameTurnManagerBase::SetWallCell(int32 X, int32 Y)
{
    if (ADungeonFloorGenerator* FloorGen = GetFloorGenerator())
    {
        FVector WorldPos(X * FloorGen->CellSize, Y * FloorGen->CellSize, 0.0f);
        FloorGen->GridChangeVector(WorldPos, -1); // -1 = Wall
    }
}

void AGameTurnManagerBase::SetWallAtWorldPosition(FVector WorldPosition)
{
    if (ADungeonFloorGenerator* FloorGen = GetFloorGenerator())
    {
        FloorGen->GridChangeVector(WorldPosition, -1); // -1 = Wall
    }
}

void AGameTurnManagerBase::SetWallRect(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY)
{
    if (ADungeonFloorGenerator* FloorGen = GetFloorGenerator())
    {
        for (int32 X = MinX; X <= MaxX; X++)
        {
            for (int32 Y = MinY; Y <= MaxY; Y++)
            {
                SetWallCell(X, Y);
            }
        }
    }
}

//------------------------------------------------------------------------------
// そ�E他�E未定義関数の実裁E
//------------------------------------------------------------------------------

bool AGameTurnManagerBase::HasAnyMoveIntent() const
{
    // CachedPlayerCommandの方向�Eクトルが非ゼロなら移動意図あり
    const FVector Dir = CachedPlayerCommand.Direction;
    const bool bHasIntent = !Dir.IsNearlyZero(0.01);

    UE_LOG(LogTurnManager, Verbose, TEXT("[HasAnyMoveIntent] Direction=(%.2f,%.2f,%.2f) -> %s"),
        Dir.X, Dir.Y, Dir.Z, bHasIntent ? TEXT("TRUE") : TEXT("FALSE"));

    return bHasIntent;
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

    // ☁E�E☁ECRITICAL FIX (2025-11-11): 予紁E�E劁E失敗をチェチE�� ☁E�E☁E
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
                return false;  // 予紁E��敁E
            }

            UE_LOG(LogTurnManager, Verbose,
                TEXT("[RegisterResolvedMove] SUCCESS: %s reserved (%d,%d)"),
                *GetNameSafe(Actor), Cell.X, Cell.Y);
            return true;  // 予紁E�E劁E
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

    // ☁E�E☁ECRITICAL FIX (2025-11-10): 敗老E�E移動を完�EブロチE�� ☁E�E☁E
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

    // ☁E�E☁ECRITICAL FIX (2025-11-13): 予紁E��コミット�Eセル一致を検証 ☁E�E☁E
    // Gemini刁E��: 「予紁E��たセル」と「コミット時に移動させるセル」が異なるバグを防止
    if (!IsMoveAuthorized(Unit, Action.NextCell))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[ResolvedMove] ✁EAUTHORIZATION FAILED: %s tried to move to (%d,%d) but reservation is for a different cell!"),
            *GetNameSafe(Unit), Action.NextCell.X, Action.NextCell.Y);
        
        // 予紁E��れてぁE��セルをログ出劁E
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

        // ☁E�E☁EFIX (2025-11-12): プレイヤー移動�EGAS経路のみに統一 ☁E�E☁E
        // Direct MoveUnit フォールバックを廁E��、EA起動失敗時はエラー終亁E��E
        // これにより、移動経路が「コマンド受仁EↁEGAS起動」に一本化され、E
        // 攻撁E��ェーズとの競合や二重実行を防止、E
        UE_LOG(LogTurnManager, Error,
            TEXT("[ResolvedMove] ❁EPlayer GA trigger failed - Move BLOCKED (GAS-only path enforced)"));
        ReleaseMoveReservation(Unit);
        return false;
    }

    // ☁E�E☁E以下�EDirect MoveUnitパスは敵専用 ☁E�E☁E
    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = LocalPathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, bIsPlayerUnit);

    // ☁E�E☁EコアシスチE��: OnActionExecuted配信�E�E025-11-09�E�E☁E�E☁E
    // NOTE: This notification is sent when move STARTS, not when it completes
    // Listeners should not expect the unit to be at the final position yet
    if (EventDispatcher)
    {
        const FGameplayTag MoveActionTag = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.Intent.Move"));
        const int32 UnitID = Unit->GetUniqueID();
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[DispatchResolvedMove] Broadcasting move start notification for Unit %d at (%d,%d) ↁE(%d,%d)"),
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

    // ☁E�E☁ECRITICAL FIX (2025-11-11): アビリチE��起動前に古ぁE��ロチE��タグを強制削除 ☁E�E☁E
    // ☁E�E☁EEXTENDED FIX (2025-11-11): State.MovingとMovement.Mode.Fallingも追加 ☁E�E☁E
    // ターン開始時のクリーンアチE�Eが間に合わなかった場合�E保険
    // これにより、前のターンで残ったタグによるアビリチE��ブロチE��を回避
    //
    // Gemini刁E��により判明：OwnedTagsに State.Moving が残留してぁE��と
    // ActivationOwnedTags に State.Moving が含まれるGA_MoveBaseは二重起動防止で拒否されめE
    int32 ClearedCount = 0;
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�ECleared blocking State.Ability.Executing tag from %s"),
            *GetNameSafe(Unit));
    }
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�ECleared blocking State.Action.InProgress tag from %s"),
            *GetNameSafe(Unit));
    }

    // ☁E�E☁ECRITICAL FIX (2025-11-11): State.Moving 残留対筁E☁E�E☁E
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Moving))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Moving);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�ECleared residual State.Moving tag from %s (Gemini刁E��)"),
            *GetNameSafe(Unit));
    }

    // ☁E�E☁ECRITICAL FIX (2025-11-11): Movement.Mode.Falling 残留対筁E☁E�E☁E
    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));
    if (FallingTag.IsValid() && ASC->HasMatchingGameplayTag(FallingTag))
    {
        ASC->RemoveLooseGameplayTag(FallingTag);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�ECleared residual Movement.Mode.Falling tag from %s (Gemini刁E��)"),
            *GetNameSafe(Unit));
    }

    // ☁E�E☁EDIAGNOSTIC: Log ASC ability count (2025-11-11) ☁E�E☁E
    const int32 AbilityCount = ASC->GetActivatableAbilities().Num();
    if (AbilityCount == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠�E�ENo abilities granted to %s - ASC may not be initialized"),
            *GetNameSafe(Unit));
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[TriggerPlayerMove] %s has %d abilities in ASC (cleared %d blocking tags)"),
            *GetNameSafe(Unit), AbilityCount, ClearedCount);

        // ☁E�E☁EDIAGNOSTIC: List all granted abilities (2025-11-11) ☁E�E☁E
        // Note: AbilityTriggers is protected, so we can only log ability names
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

        // ☁E�E☁EコアシスチE��: OnActionExecuted配信�E�E025-11-09�E�E☁E�E☁E
        // NOTE: This notification is sent when ability triggers, not when move completes
        // Listeners should not expect the unit to be at the final position yet
        if (EventDispatcher)
        {
            const int32 UnitID = Unit->GetUniqueID();
            UE_LOG(LogTurnManager, Verbose,
                TEXT("[TriggerPlayerMove] Broadcasting move start notification for Unit %d at (%d,%d) ↁE(%d,%d)"),
                UnitID, Action.CurrentCell.X, Action.CurrentCell.Y, Action.NextCell.X, Action.NextCell.Y);
            EventDispatcher->BroadcastActionExecuted(UnitID, EventData.EventTag, true);
        }

        return true;
    }

    // ☁E�E☁EFIX: Better diagnostic when trigger fails (2025-11-11) ☁E�E☁E
    FGameplayTagContainer CurrentTags;
    ASC->GetOwnedGameplayTags(CurrentTags);
    UE_LOG(LogTurnManager, Error,
        TEXT("[TriggerPlayerMove] ❁EHandleGameplayEvent returned 0 for %s (AbilityCount=%d, OwnedTags=%s)"),
        *GetNameSafe(Unit),
        AbilityCount,
        *CurrentTags.ToStringSimple());

    return false;
}
