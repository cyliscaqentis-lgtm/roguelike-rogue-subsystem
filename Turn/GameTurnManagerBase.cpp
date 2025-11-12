// Copyright Epic Games, Inc. All Rights Reserved.

#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"  // ATBSLyraGameMode用
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/AABB.h"  // ★★★ PlayerStartRoom診断用 ★★★
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnEventDispatcher.h"
#include "Turn/TurnDebugSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/PlayerInputProcessor.h"
#include "Player/PlayerControllerBase.h"  // ★★★ Client RPC用 (2025-11-09) ★★★
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
#include "GameFramework/SpectatorPawn.h"  // ★★★ これを追加 ★★★
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
// ★★★ 追加: DistanceFieldSubsystemのinclude ★★★
#include "DistanceFieldSubsystem.h" 
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameModes/LyraGameState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "Utility/TurnCommandEncoding.h"
// ★★★ ActionExecutorSubsystem と TurnPhaseManagerComponent のヘッダ
// 注意: これらのクラスが存在しない場合、.cppファイルで該当コードをコメントアウトまたは修正してください
// #include "Turn/ActionExecutorSubsystem.h"  // UActionExecutorSubsystem, FOnActionExecutorCompleted
// #include "Turn/TurnPhaseManagerComponent.h"  // UTurnPhaseManagerComponent

// ★★★ FOnActionExecutorCompleted が定義されていない場合の暫定定義
// ActionExecutorSubsystem.h に定義がある場合、上記のincludeを有効化してください
#if !defined(FOnActionExecutorCompleted_DEFINED)
DECLARE_DELEGATE(FOnActionExecutorCompleted);
#define FOnActionExecutorCompleted_DEFINED 1
#endif

// ★★★ LogTurnManager と LogTurnPhase は ProjectDiagnostics.cpp で既に定義されているため、ここでは定義しない
// DEFINE_LOG_CATEGORY(LogTurnManager);
// DEFINE_LOG_CATEGORY(LogTurnPhase);
using namespace RogueGameplayTags;

// ★★★ 追加: CVar定義 ★★★
TAutoConsoleVariable<int32> CVarTurnLog(
    TEXT("tbs.TurnLog"),
    1,
    TEXT("0:Off, 1:Key events only, 2:Verbose debug output"),
    ECVF_Default
     );
using namespace RogueGameplayTags;

//------------------------------------------------------------------------------
// 初期化
//------------------------------------------------------------------------------

AGameTurnManagerBase::AGameTurnManagerBase()
{
    PrimaryActorTick.bCanEverTick = false;
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;  // ネイティブタグを使用

    // ★★★ 2025-11-09: レプリケーション有効化（必須）
    // WaitingForPlayerInput/InputWindowId等をクライアントに同期するため
    bReplicates = true;
    bAlwaysRelevant = true;         // 全クライアントに常に関連
    SetReplicateMovement(false);    // 移動しない管理アクタなので不要

    UE_LOG(LogTurnManager, Log, TEXT("[TurnManager] Constructor: Replication enabled (bReplicates=true, bAlwaysRelevant=true)"));
}

//------------------------------------------------------------------------------
// ★★★ 新規追加: InitializeTurnSystem ★★★
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// InitializeTurnSystemの修正版
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

    //==========================================================================
    // ★★★ 新規: Subsystem初期化（2025-11-09リファクタリング） ★★★
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

            // SpectatorPawn → BPPlayerUnitへPossess
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
    // Step 2: CachedPathFinderは既に注入済みなので探索不要
    //==========================================================================
    // PathFinderはHandleDungeonReady()で既に注入されている、またResolveOrSpawnPathFinder()で解決済み
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
    // Step 4: Subsystemチェック
    //==========================================================================

    {
    // 既にline 113で宣言済みのWorldを再利用
    if (World)
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            // ★★★ 重複防止: 既存バインドを削除してから追加
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
                bHasInitialized = false; // InitializeTurnSystemを再呼び出し可能に

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
        // ★★★ 削除: UTurnInputGuard参照はタグシステムで不要
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
    // ★★★ 修正3: ASC Gameplay Eventの重複防止
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        // ★★★ 既存のハンドルがあれば削除
        if (PlayerMoveCompletedHandle.IsValid())
        {
            if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(Tag_TurnAbilityCompleted))
            {
                Delegate->Remove(PlayerMoveCompletedHandle);
            }
            PlayerMoveCompletedHandle.Reset();
        }

        // ★★★ 新規バインド
        FGameplayEventMulticastDelegate& Delegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(Tag_TurnAbilityCompleted);
        PlayerMoveCompletedHandle = Delegate.AddUObject(this, &AGameTurnManagerBase::OnPlayerMoveCompleted);

        UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Bound to Gameplay.Event.Turn.Ability.Completed event"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: GetPlayerASC() returned null"));
    }

    //==========================================================================
    // ★★★ 削除: BindAbilityCompletion()は重複を避けるため
    //==========================================================================
    // BindAbilityCompletion();

    //==========================================================================
    // 4. 完了
    //==========================================================================
    bHasInitialized = true;
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));

    //==========================================================================
    // ★★★ StartFirstTurnは削除: TryStartFirstTurnゲートから呼ぶ（2025-11-09 解析サマリ対応）
    //==========================================================================
    // StartFirstTurn(); // 削除: 全条件が揃うまで待つ
}







void AGameTurnManagerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 将来的な拡張用。現在は空実装
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

    // GameModeから参照を注入
    ATBSLyraGameMode* GM = GetWorld()->GetAuthGameMode<ATBSLyraGameMode>();
    if (!ensure(GM))
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: GameMode not found"));
        return;
    }

    // WorldSubsystemは必ずWorld初期化時に作成されるため、直接Worldから取得
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: World is null!"));
        return;
    }

    DungeonSys = World->GetSubsystem<URogueDungeonSubsystem>();

    UE_LOG(LogTurnManager, Warning, TEXT("★★★ TurnManager: ACQ_WORLD_V3 - DungeonSys=%p (NEW BINARY) ★★★"),
        static_cast<void*>(DungeonSys.Get()));

    // 準備完了イベントにサブスクライブ
    if (DungeonSys)
    {
        DungeonSys->OnGridReady.AddDynamic(this, &AGameTurnManagerBase::HandleDungeonReady);
        UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Subscribed to DungeonSys->OnGridReady"));

        // ダンジョン生成をトリガー
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

    // PathFinderとUnitManagerを生成・初期化（ここで唯一の所有権を持つ）
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

    // ★★★ PathFinderを初期化（2025-11-09 解析サマリ対応）
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

        // 初期化検証: サンプルセルのステータスを確認
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

        // ★★★ 詳細ログ: BuildUnits後のPlayerStartRoomを確認
        AAABB* PlayerRoom = UnitMgr->PlayerStartRoom.Get();
        UE_LOG(LogTurnManager, Warning, TEXT("[BuildUnits] Completed. PlayerStartRoom=%s at Location=%s"),
            PlayerRoom ? *PlayerRoom->GetName() : TEXT("NULL"),
            PlayerRoom ? *PlayerRoom->GetActorLocation().ToString() : TEXT("N/A"));

        bUnitsSpawned = true; // ★注意: これは部屋選択完了フラグ。実際の敵スポーンはOnTBSCharacterPossessedで行われる
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: HandleDungeonReady completed, initializing turn system..."));

    // ★★★ InitializeTurnSystem はここで呼ぶ
    InitializeTurnSystem();

    // ★★★ ゲート機構: 全条件が揃ったらStartFirstTurnを試行
    TryStartFirstTurn();
}



// ★★★ 赤ペン修正5: OnExperienceLoaded の実装 ★★★
void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] ========== EXPERIENCE READY =========="));
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] Experience: %s"),
        Experience ? *Experience->GetName() : TEXT("NULL"));

    // ★★★ Experience完了時はInitializeTurnSystemを呼ばない。HandleDungeonReadyからのみ呼ぶ
    // InitializeTurnSystem()はHandleDungeonReady()からのみ呼ばれる
}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    // ★★★ クライアント側: ターンUI更新/ログの同期 ★★★
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
        // ★★★ REMOVED: StartMoveBatch は非推奨。BeginTurn() を使用 (2025-11-09) ★★★
        // 以前の実装:
        //   int32 TotalUnits = 1 + Enemies.Num();
        //   Barrier->StartMoveBatch(TotalUnits, TurnId);
        // BeginTurn() がターン開始時に自動的にバリアを初期化するため、この呼び出しは不要。

        // Move: Player
        if (APawn* PlayerPawn = GetPlayerPawn())
        {
            if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn))
            {
                FGameplayEventData EventData;
                EventData.EventTag = RogueGameplayTags::GameplayEvent_Turn_StartMove;  // ネイティブタグを使用
                ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
                UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: Player GAS Turn.StartMove fired"));
            }
        }

        // Move Batch / Subsystem / CollectEnemies
        // ★★★ エラー該当箇所39: StartEnemyMoveBatch() 未実装→コメントアウト ★★★
        // if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        // {
        //     TArray<AActor*> EnemiesLocal;
        //     GetCachedEnemies(EnemiesLocal);
        //     // AI
        //     EnemyAI->StartEnemyMoveBatch(EnemiesLocal, TurnId);
        //     UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: EnemyAI StartEnemyMoveBatch for %d enemies"), EnemiesLocal.Num());
        // }

        // ★★★ エラー該当箇所47: PhaseManager->StartPhase(ETurnPhase::Move) 未実装→コメントアウト ★★★
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



// ★★★ OnExperienceLoadedの実装 ★★★
// ★★★ 新規関数: Experience完了時のコールバック ★★★


// GameTurnManagerBase.cpp
// ★★★ BeginPlay() の直後に追加 ★★★

//------------------------------------------------------------------------------
// GameplayTag キャッシュ初期化（新規追加）
//------------------------------------------------------------------------------
void AGameTurnManagerBase::InitGameplayTags()
{
    //==========================================================================
    // RogueGameplayTagsから取得
    //==========================================================================
    Tag_AbilityMove = RogueGameplayTags::GameplayEvent_Intent_Move;  // "GameplayEvent.Intent.Move"
    Tag_TurnAbilityCompleted = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;   // "Gameplay.Event.Turn.Ability.Completed"
    Phase_Turn_Init = RogueGameplayTags::Phase_Turn_Init;            // "Phase.Turn.Init"
    Phase_Player_Wait = RogueGameplayTags::Phase_Player_WaitInput;   // "Phase.Player.WaitInput"

    //==========================================================================
    // 🌟 3-Tag System: 新タグのキャッシュ確認
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
    // タグの有効性チェック
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
// Phase 2: パフォーマンス最適化実装
//------------------------------------------------------------------------------

AGridPathfindingLibrary* AGameTurnManagerBase::GetCachedPathFinder() const
{
    // メンバ変数PathFinderを優先。GameModeから注入されたもの
    if (IsValid(PathFinder.Get()))
    {
        return PathFinder.Get();
    }

    // CachedPathFinderを確認
    if (CachedPathFinder.IsValid())
    {
        AGridPathfindingLibrary* PF = CachedPathFinder.Get();
        // PathFinderにも設定（次回の高速化）
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // フォールバック: シーン内を探索
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        AGridPathfindingLibrary* PF = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // 最後の手段: GameModeから取得を試みる
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

    // 敵リストの変換: ObjectPtr → 生ポインタ
    TArray<AActor*> Enemies;
    Enemies.Reserve(CachedEnemies.Num());
    for (const TObjectPtr<AActor>& Enemy : CachedEnemies)
    {
        if (Enemy)
        {
            Enemies.Add(Enemy.Get());
        }
    }

    // PathFinder と Player の取得
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

    // ★★★ Phase 1: EventDispatcher経由でイベント配信（2025-11-09） ★★★
    // ★★★ Week 1: UPlayerInputProcessorに委譲（2025-11-09リファクタリング）
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

    // 二重進行防止: ここで入力ゲートを閉じてから継続
    if (WaitingForPlayerInput)
    {
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);

        // ★★★ コアシステム: CommandHandler経由でInput Window終了（2025-11-09） ★★★
        if (CommandHandler)
        {
            CommandHandler->EndInputWindow();
        }
    }
    ContinueTurnAfterInput();
}






//------------------------------------------------------------------------------
// BP互換関数実装
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
    // ★★★ Week 1: UTurnFlowCoordinatorに転送（2025-11-09リファクタリング）
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
    // ★★★ Phase 1: フェーズ変更イベント配信（2025-11-09） ★★★
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
        // 入力フェーズ開始：フラグとゲートを**揃えて**開く
        WaitingForPlayerInput = true;
        ApplyWaitInputGate(true);      // ★★★ 追加。重要 ★★★
        OpenInputWindow();
        UE_LOG(LogTurnManager, Log,
            TEXT("Turn%d:BeginPhase(Input) Id=%d, Gate=OPEN, Waiting=TRUE"),
            CurrentTurnIndex, InputWindowId);
    }

    // ★★★ PhaseManager は存在しないためコメントアウト
    /*
    if (PhaseManager)
    {
        PhaseManager->BeginPhase(PhaseTag);
    }
    */

    // DebugObserver への通知。既存の処理
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
        // 入力フェーズ終了：フラグとゲートを**揃えて**閉じる
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);
        UE_LOG(LogTurnManager, Log, TEXT("Turn%d:[EndPhase] Gate=CLOSED, Waiting=FALSE"),
            CurrentTurnIndex);
    }

    // ★★★ PhaseManager は存在しないためコメントアウト
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
                // ★★★ 修正: IAbilitySystemInterface に戻す ★★★
            if (const IAbilitySystemInterface* C = Cast<IAbilitySystemInterface>(P->GetController()))
                return C->GetAbilitySystemComponent();
        return nullptr;
    }

    FORCEINLINE int32 GetTeamIdOf(const AActor* Actor)
    {
        // まずControllerを確認
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

        // 次にActor自身を確認
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

    // ★★★ Phase 4: UEnemyAISubsystem経由でEnemy収集（2025-11-09） ★★★
    if (EnemyAISubsystem)
    {
        TArray<AActor*> CollectedEnemies;
        EnemyAISubsystem->CollectAllEnemies(CachedPlayerPawn, CollectedEnemies);

        CachedEnemies.Empty();
        CachedEnemies = CollectedEnemies;

        UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem collected %d enemies"), CachedEnemies.Num());
        return;
    }

    // ★★★ Fallback: 既存のロジック（EnemyAISubsystemがない場合） ★★★
    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] EnemyAISubsystem not available, using fallback"));

        // ★★★ APawnで検索。Includeが不要 ★★★
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);

    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;  // ネイティブタグを使用

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

        // ★★★ 重要: CachedEnemiesを完全にクリアしてから再構築 ★★★
    CachedEnemies.Empty();
    CachedEnemies.Reserve(Found.Num());

    for (AActor* A : Found)
    {
        // ★★★ Nullチェック ★★★
        if (!IsValid(A))
        {
            continue;
        }

        // ★★★ プレイヤーを除外 ★★★
        if (A == CachedPlayerPawn)
        {
            UE_LOG(LogTurnManager, Log, TEXT("[CollectEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            continue;
        }

        const int32 TeamId = GetTeamIdOf(A);

        // ★★★ 修正: TeamId == 2 または 255 を敵として扱う ★★★
        // ログから全ての敵がTeamID=255 であることが判明
        const bool bByTeam = (TeamId == 2 || TeamId == 255);

        const UAbilitySystemComponent* ASC = TryGetASC(A);
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));

        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

        // ★★★ いずれかの条件を満たせば敵として認識 ★★★
        if (bByGTag || bByTeam || bByActorTag)
        {
            CachedEnemies.Add(A);

            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;

            // ★★★ デバッグ: 最初の3体と最後の1体のみ詳細ログ ★★★
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

    // ★★★ エラー検出: 敵が1体も見つからない場合 ★★★
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
    // ★★★ 警告：予想より少ない ★★★
    else if (CachedEnemies.Num() > 0 && CachedEnemies.Num() < 32 && Found.Num() >= 32)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ⚠️ Collected %d enemies, but expected around 32 from %d Pawns"),
            CachedEnemies.Num(), Found.Num());
    }
}




// ========== 修正: Subsystemに委譲 ==========
// GameTurnManagerBase.cpp

void AGameTurnManagerBase::CollectIntents_Implementation()
{
    // ★★★ 修正1: Subsystemを毎回取得（キャッシュ無効化対策） ★★★
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

    // ★★★ 修正2: Observationsの存在確認+ 自動リカバリー ★★★
    if (EnemyTurnDataSys->Observations.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
            CurrentTurnIndex, CachedEnemies.Num());

        // ★★★ 自動リカバリー: Observationsを生成 ★★★
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

    // ★★★ 修正3: CachedEnemiesを直接使用 ★★★
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents input: Observations=%d, Enemies=%d"),
        CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

    // ★★★ 修正4: サイズ一致チェック ★★★
    if (EnemyTurnDataSys->Observations.Num() != CachedEnemies.Num())
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

        // ★★★ リカバリー: Observationsを再生成 ★★★
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

    // ★★★ 修正5: EnemyAISubsystem::CollectIntents実装 ★★★
    TArray<FEnemyIntent> Intents;
    EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemies, Intents);

    // ★★★ 修正6: Subsystemに格納 ★★★
    EnemyTurnDataSys->Intents = Intents;

    // ★★★ Intent数の計測 ★★★
    int32 AttackCount = 0, MoveCount = 0, WaitCount = 0, OtherCount = 0;

    // ★★★ 修正7: GameplayTagをキャッシュしてパフォーマンス改善 ★★★
    static const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイティブタグを使用
    static const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;  // ネイティブタグを使用
    static const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;  // ネイティブタグを使用

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

    // ★★★ 修正8: ログレベルをWarningに変更。重要なイベント ★★★
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents -> %d intents (Attack=%d, Move=%d, Wait=%d, Other=%d)"),
        CurrentTurnIndex, Intents.Num(), AttackCount, MoveCount, WaitCount, OtherCount);

    // ★★★ 修正9: Intent生成失敗の詳細ログ ★★★
    if (Intents.Num() == 0 && EnemyTurnDataSys->Observations.Num() > 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: ❌Failed to generate intents from %d observations!"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() < EnemyTurnDataSys->Observations.Num())
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: ⚠️ Generated %d intents from %d observations (some enemies may have invalid intent)"),
            CurrentTurnIndex, Intents.Num(), EnemyTurnDataSys->Observations.Num());
    }
    else if (Intents.Num() == EnemyTurnDataSys->Observations.Num() && Intents.Num() > 0)
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] CollectIntents: ✅ Successfully generated all intents"),
            CurrentTurnIndex);
    }
}




FEnemyIntent AGameTurnManagerBase::ComputeEnemyIntent_Implementation(AActor* Enemy, const FEnemyObservation& Observation)
{
    FEnemyIntent Intent;
    Intent.Actor = Enemy;

    if (Observation.DistanceInTiles <= 1)
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Attack;  // ネイティブタグを使用
    }
    else
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;  // ネイティブタグを使用
    }

    UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] ComputeEnemyIntent: %s -> %s (Distance=%d)"),
        CurrentTurnIndex, *Enemy->GetName(), *Intent.AbilityTag.ToString(), Observation.DistanceInTiles);

    return Intent;
}

// ========== 修正: Subsystemに委譲 ==========
void AGameTurnManagerBase::ExecuteEnemyMoves_Implementation()
{
    // ★★★ UActionExecutorSubsystem は存在しないためコメントアウト
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
// Ability管理
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

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイティブタグを使用

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
    DOREPLIFETIME(AGameTurnManagerBase, CurrentTurnIndex);  // ★★★ 2025-11-09: 追加（実際に使用されている変数）
    DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);
    DOREPLIFETIME(AGameTurnManagerBase, bPlayerMoveInProgress);
}


void AGameTurnManagerBase::ExecuteEnemyAttacks_Implementation()
{
    // ★★★ UActionExecutorSubsystem は存在しないためコメントアウト
    UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyAttacks: ActionExecutor not available (class not found)"), CurrentTurnIndex);
    return;
    /*
    if (!ActionExecutor || !EnemyTurnData)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteEnemyAttacks: ActionExecutor or EnemyTurnData not found"), CurrentTurnIndex);
        return;
    }

    // UFUNCTION をバインド
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
// 🌟 統合API - Lumina提言B3: AdvanceTurnAndRestart - 最終修正版
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnIndex);

    // ★★★ DEBUG: Log player position before turn advance (2025-11-09) ★★★
    if (APawn* PlayerPawn = GetPlayerPawn())
    {
        if (CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = PlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[AdvanceTurn] ★ PLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y, *PlayerLoc.ToCompactString());
        }
    }

    //==========================================================================
    // ★★★ Phase 4: 二重鍵チェック。AdvanceTurnAndRestartでも実施
    //==========================================================================
    if (!CanAdvanceTurn(CurrentTurnIndex))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[AdvanceTurnAndRestart] ABORT: Cannot advance turn %d (safety check failed)"),
            CurrentTurnIndex);

        // ★★★ デバッグ: Barrierの状態をダンプ
        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
            }
        }

        return;  // ★★★ 進行を中止
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
    // ターンインクリメント
    //==========================================================================
    const int32 PreviousTurn = CurrentTurnIndex;

    // ★★★ Week 1: UTurnFlowCoordinatorに委譲（2025-11-09リファクタリング）
    if (TurnFlowCoordinator)
    {
        // ターンを終了してから進める
        TurnFlowCoordinator->EndTurn();
        TurnFlowCoordinator->AdvanceTurn();
    }

    // ★★★ コアシステム: OnTurnEnded配信（2025-11-09） ★★★
    if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnEnded(PreviousTurn);
    }

    CurrentTurnIndex++;

    // ★★★ Phase 5補完: EndTurnリトライフラグをリセット（2025-11-09） ★★★
    bEndTurnPosted = false;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d → %d (bEndTurnPosted reset)"),
        PreviousTurn, CurrentTurnIndex);

    //==========================================================================
    // ★★★ Phase 4: Barrierに新しいターンを通知
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
    // デバッグObserverの保存
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
    // フラグリセットとイベント発火
    //==========================================================================
    bTurnContinuing = false;

    // ★★★ リファクタリング: EventDispatcher経由でイベント配信（2025-11-09） ★★★
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

    // ★★★ CRITICAL FIX (2025-11-09): 入力フェーズを必ず開始 ★★★
    // Turn 1以降で入力ウィンドウが開かない問題の修正
    OnTurnStartedHandler(CurrentTurnIndex);
}




// ════════════════════════════════════════════════════════════════════
// StartFirstTurn - 初回ターン開始用
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnIndex);

    bTurnStarted = true;

    // ★★★ REMOVED: StartMoveBatch は非推奨。BeginTurn() が AdvanceTurnAndRestart で既に呼ばれている (2025-11-09) ★★★
    // 以前の実装: Barrier->StartMoveBatch(1, CurrentTurnIndex);
    // BeginTurn() がターン開始とバリア初期化を一括処理するため、この呼び出しは冗長。

    // ★★★ リファクタリング: EventDispatcher経由でイベント配信（2025-11-09） ★★★
    if (EventDispatcher)
    {
        EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning, TEXT("UTurnEventDispatcher not available"));
    }

    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnIndex);

    // ★★★ CRITICAL FIX (2025-11-09): 統一された入力フェーズ開始 ★★★
    // OnTurnStartedHandler が BeginPhase(Phase_Player_Wait) を呼ぶ
    OnTurnStartedHandler(CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// ★★★ C++統合：ターンフロー制御 ★★★
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// EndPlayの修正版
//------------------------------------------------------------------------------
void AGameTurnManagerBase::EndPlay(const EEndPlayReason::Type Reason)
{
    // ★★★ Non-Dynamicデリゲート解除 ★★★
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
    // PlayerController から PlayerState 経由で ASC を取得
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
        {
            // LyraPlayerState の場合
            if (ALyraPlayerState* LyraPS = Cast<ALyraPlayerState>(PS))
            {
                return LyraPS->GetAbilitySystemComponent();
            }

            // IAbilitySystemInterface を実装している場合
            if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PS))
            {
                return ASI->GetAbilitySystemComponent();
            }
        }
    }

    // フォールバック: CachedPlayerPawn から直接取得を試みる
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
// OnTurnStartedHandlerのホットフィックス適用版
//------------------------------------------------------------------------------
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnIndex)
{
    if (!HasAuthority()) return;

    CurrentTurnIndex = TurnIndex;

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnIndex);

    // ★★★ CRITICAL FIX (2025-11-11): ターン開始時に残留タグをクリア ★★★
    // 前のターンでアビリティが正しく終了していない場合、ブロッキングタグが残り続ける
    // ターン開始直後に強制クリーンアップして、新しいアビリティが起動できるようにする
    ClearResidualInProgressTags();
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ★ ClearResidualInProgressTags called at turn start"),
        TurnIndex);

    // ★★★ CRITICAL FIX (2025-11-11): ターン開始時に古い予約を即座にパージ ★★★
    // プレイヤー入力がExecuteMovePhaseより先に来る可能性があるため、
    // ターン開始直後にクリーンアップしてPlayerの予約を確実に受け入れる
    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            GridOccupancy->SetCurrentTurnId(TurnIndex);
            GridOccupancy->PurgeOutdatedReservations(TurnIndex);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] ★ PurgeOutdatedReservations called at turn start (before player input)"),
                TurnIndex);

            // ★★★ CRITICAL FIX (2025-11-11): 物理座標ベースの占有マップ再構築（Gemini診断より） ★★★
            // 論理マップ（ActorToCell）ではなく、物理座標から占有マップを完全再構築
            // これにより、コミット失敗等で発生した論理/物理の不整合を強制修正
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
                    TEXT("[Turn %d] ★ RebuildFromWorldPositions called - rebuilding occupancy from physical positions (%d units)"),
                    TurnIndex, AllUnits.Num());
            }
            else
            {
                // フォールバック：ユニットリストが空の場合は論理マップベースのチェック
                GridOccupancy->EnforceUniqueOccupancy();
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[Turn %d] ★ EnforceUniqueOccupancy called (fallback - no units cached yet)"),
                    TurnIndex);
            }
        }
    }

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        CachedPlayerPawn = PC->GetPawn();

        // ★★★ DEBUG: Log player position at turn start (2025-11-09) ★★★
        if (CachedPlayerPawn && CachedPathFinder.IsValid())
        {
            const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
            const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] ★ PLAYER POSITION AT TURN START: Grid(%d,%d) World(%s)"),
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
    // ★★★ CRITICAL FIX (2025-11-11): ゲート開放を早期に実行（最適化） ★★★
    // 理由: DistanceField更新は重い処理だが、プレイヤー入力前には不要
    //       （敵AI判断にのみ必要で、それはプレイヤー行動後に実行される）
    //       ゲートを先に開くことで、入力受付までの遅延を最小化
    //==========================================================================
    BeginPhase(Phase_Turn_Init);
    BeginPhase(Phase_Player_Wait);
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ★ INPUT GATE OPENED EARLY (before DistanceField update)"), TurnIndex);

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

    // ★★★ ホットフィックスC: 動的Margin計算（マンハッタン距離ベース） ★★★
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

            // ★★★ 最適化: GridUtils使用（重複コード削除 2025-11-09）
            // マンハッタン距離計算はGridUtilsを使用
            auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
                {
                    return FGridUtils::ManhattanDistance(A, B);
                };

            // ★★★ 最遠の敵までの距離を計測 ★★★
            int32 MaxD = 0;
            for (const FIntPoint& C : EnemyPositions)
            {
                int32 Dist = Manhattan(C, PlayerGrid);
                if (Dist > MaxD)
                {
                    MaxD = Dist;
                }
            }

            // ★★★ 動的Margin計算（最遠距離 + バッファ4、範囲8-64） ★★★
            const int32 Margin = FMath::Clamp(MaxD + 4, 8, 64);

            UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] DF: Player=(%d,%d) Enemies=%d MaxDist=%d Margin=%d"),
                TurnIndex, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), MaxD, Margin);

            // DistanceField更新
            DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

            // ★★★ 到達確認ループ（診断用） ★★★
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
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ✅All enemies reachable with Margin=%d"),
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
    // ★★★ CRITICAL FIX (2025-11-12): ターン開始時に仮インテントを生成 ★★★
    // 理由: 「インテントが空のまま同時移動フェーズに突入」を防ぐため
    //       ターン開始時に仮インテントを生成し、プレイヤー移動後に再計画で上書き
    //       これにより、ExecuteMovePhaseで確実にインテントが存在する
    //==========================================================================
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Generating preliminary enemy intents at turn start..."),
        TurnIndex);

    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn && CachedEnemies.Num() > 0)
    {
        // 仮のObservationsを生成（現在のプレイヤー位置で）
        TArray<FEnemyObservation> PreliminaryObs;
        EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), PreliminaryObs);
        EnemyTurnDataSys->Observations = PreliminaryObs;

        // 仮のIntentsを収集
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

    // ★★★ 削除: BuildObservations の実行（プレイヤー行動後に移動）
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

    // ★★★ 削除: BeginPhase は早期実行済み（ゲート開放の最適化）
    // BeginPhase(Phase_Turn_Init);
    // BeginPhase(Phase_Player_Wait);
}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    //==========================================================================
    // ★★★ Phase 2: CommandHandler経由でコマンド処理（2025-11-09） ★★★
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[✅ROUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

    //==========================================================================
    // (1) 権威チェック
    //==========================================================================
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Not authority, skipping"));
        return;
    }

    // ★★★ コアシステム: CommandHandler経由で完全処理（2025-11-09） ★★★
    if (CommandHandler)
    {
        // ProcessPlayerCommandで検証と受理を一括処理
        if (!CommandHandler->ProcessPlayerCommand(Command))
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command processing failed by CommandHandler"));
            return;
        }
        // 検証・受理成功 → 既存のロジックで実際の処理を継続
    }
    else
    {
        // Fallback: 既存の検証ロジック
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
            *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId, Command.TargetCell.X, Command.TargetCell.Y);

        //==========================================================================
        // (2) TurnId検証。等価化
        //==========================================================================
        if (Command.TurnId != CurrentTurnIndex && Command.TurnId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
                Command.TurnId, CurrentTurnIndex);
            return;
        }

        //==========================================================================
        // ★★★ (2.5) WindowId検証（2025-11-09 CRITICAL FIX） ★★★
        //==========================================================================
        if (Command.WindowId != InputWindowId && Command.WindowId != INDEX_NONE)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GameTurnManager] Command REJECTED - WindowId mismatch (%d != %d) | Turn=%d"),
                Command.WindowId, InputWindowId, CurrentTurnIndex);
            return;
        }

        //==========================================================================
        // (3) 二重移動チェック（2025-11-10 FIX: クライアント通知追加）
        //==========================================================================
        if (bPlayerMoveInProgress)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Move in progress, ignoring command"));

            // ★★★ クライアントに拒否を通知 (2025-11-10) ★★★
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

            // ★★★ クライアントに拒否を通知 (2025-11-10) ★★★
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
    // ★★★ (4) 早期Gate閉鎖を削除（2025-11-09 FIX） ★★★
    // CloseInputWindowForPlayer() で一括管理
    //==========================================================================

    //==========================================================================
    // (5) World取得
    //==========================================================================
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] World is null"));
        return;
    }

    //==========================================================================
    // (6) PlayerPawn取得
    //==========================================================================
    APawn* PlayerPawn = GetPlayerPawn();
    if (!PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] PlayerPawn not found"));
        return;
    }

    //==========================================================================
    // (7) ASC取得
    //==========================================================================
    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
    if (!ASC)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] Player ASC not found"));
        return;
    }

    //==========================================================================
    // (8) EventData構築。Direction をエンコード
    //==========================================================================
    FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove; // GameplayEvent.Intent.Move
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;
    EventData.OptionalObject = this; // TurnId 取得用

    // ★★★ Direction をエンコード（TurnCommandEncoding 統一） ★★★
    const int32 DirX = FMath::RoundToInt(Command.Direction.X);
    const int32 DirY = FMath::RoundToInt(Command.Direction.Y);
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackDir(DirX, DirY));

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

    //==========================================================================
    // ★★★ Phase 5: Register Player Reservation with Pre-Validation (2025-11-09) ★★★
    // Calculate target cell and VALIDATE BEFORE reservation
    //==========================================================================
    if (CachedPathFinder.IsValid())
    {
        const FIntPoint CurrentCell = CachedPathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
        const FIntPoint TargetCell(
            CurrentCell.X + static_cast<int32>(Command.Direction.X),
            CurrentCell.Y + static_cast<int32>(Command.Direction.Y));

        // ★★★ PRE-VALIDATION: Check if target cell is walkable (2025-11-09 FIX) ★★★
        // ★★★ 2025-11-10: 地形チェック + 占有チェックの二層構造 ★★★

        // 地形ブロックチェック
        const bool bTerrainBlocked = !CachedPathFinder->IsCellWalkableIgnoringActor(TargetCell, PlayerPawn);

        // ★★★ FIX (2025-11-11): 角抜け禁止チェック（斜め移動時）★★★
        // 重要：目的地の地形に関係なく、斜め移動の場合は常にチェックする
        bool bCornerCutting = false;
        const int32 AbsDirX = FMath::Abs(FMath::RoundToInt(Command.Direction.X));
        const int32 AbsDirY = FMath::Abs(FMath::RoundToInt(Command.Direction.Y));
        const bool bIsDiagonalMove = (AbsDirX == 1 && AbsDirY == 1);

        if (bIsDiagonalMove)
        {
            // 斜め移動の場合、両肩が塞がっていないかチェック
            const FIntPoint Side1 = CurrentCell + FIntPoint(static_cast<int32>(Command.Direction.X), 0);  // 横の肩
            const FIntPoint Side2 = CurrentCell + FIntPoint(0, static_cast<int32>(Command.Direction.Y));  // 縦の肩
            const bool bSide1Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side1, PlayerPawn);
            const bool bSide2Walkable = CachedPathFinder->IsCellWalkableIgnoringActor(Side2, PlayerPawn);

            // ★★★ FIX (2025-11-11): 正しいルール - 片方の肩でも壁なら禁止 ★★★
            // 角をすり抜けて移動することを防ぐため、両方の肩が通行可能な場合のみ許可
            if (!bSide1Walkable || !bSide2Walkable)
            {
                bCornerCutting = true;
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] CORNER CUTTING BLOCKED: (%d,%d)→(%d,%d) - at least one shoulder blocked [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
            else
            {
                UE_LOG(LogTurnManager, Verbose,
                    TEXT("[MovePrecheck] Diagonal move OK: (%d,%d)→(%d,%d) - both shoulders clear [Side1=(%d,%d) Walkable=%d, Side2=(%d,%d) Walkable=%d]"),
                    CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                    Side1.X, Side1.Y, bSide1Walkable, Side2.X, Side2.Y, bSide2Walkable);
            }
        }

        // 占有チェック（自分以外のユニットがいるか）
        bool bOccupied = false;
        bool bSwapDetected = false;
        AActor* OccupyingActor = nullptr;
        if (UGridOccupancySubsystem* OccSys = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
        {
            OccupyingActor = OccSys->GetActorAtCell(TargetCell);
            // 自分自身は除外（予約済みの場合）
            if (OccupyingActor && OccupyingActor != PlayerPawn)
            {
                bOccupied = true;

                // ★★★ 2025-11-10: スワップ検出（相手が自分の開始セルに向かっているか） ★★★
                if (const FEnemyIntent* Intent = CachedIntents.Find(OccupyingActor))
                {
                    if (Intent->NextCell == CurrentCell)
                    {
                        // 入れ替わり検出！
                        bSwapDetected = true;
                        UE_LOG(LogTurnManager, Warning,
                            TEXT("[MovePrecheck] ★ SWAP DETECTED: Player (%d,%d)→(%d,%d), Enemy %s (%d,%d)→(%d,%d)"),
                            CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
                            *GetNameSafe(OccupyingActor),
                            TargetCell.X, TargetCell.Y, Intent->NextCell.X, Intent->NextCell.Y);
                    }
                }
            }
        }

        // ★★★ 2025-11-10: ブロック時は回転のみ適用（ターン不消費） ★★★
        // ★★★ 2025-11-11: 角抜けチェックも追加 ★★★
        if (bTerrainBlocked || bOccupied || bCornerCutting)
        {
            const TCHAR* BlockReason = bCornerCutting ? TEXT("corner_cutting") :
                                       bTerrainBlocked ? TEXT("terrain") :
                                       bSwapDetected ? TEXT("swap") : TEXT("occupied");
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] BLOCKED by %s: Cell (%d,%d) | From=(%d,%d) | Applying FACING ONLY (No Turn)"),
                BlockReason, TargetCell.X, TargetCell.Y, CurrentCell.X, CurrentCell.Y);

            // ★★★ DEBUG: 周辺セルの状態を診断 (2025-11-09) ★★★
            const int32 TargetCost = CachedPathFinder->GetGridCost(TargetCell.X, TargetCell.Y);
            UE_LOG(LogTurnManager, Warning,
                TEXT("[MovePrecheck] Target cell (%d,%d) GridCost=%d (expected: 3=Walkable, -1=Blocked)"),
                TargetCell.X, TargetCell.Y, TargetCost);

            // ★★★ 2025-11-10: 占有情報はoccupied/swapの場合のみ出力（terrainの場合は不要） ★★★
            if (!bTerrainBlocked && bOccupied && OccupyingActor)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[MovePrecheck] Occupied by: %s%s"),
                    *GetNameSafe(OccupyingActor),
                    bSwapDetected ? TEXT(" (SWAP detected)") : TEXT(""));
            }

            // 周辺4方向の状態を出力
            const FIntPoint Directions[4] = {
                FIntPoint(1, 0),   // 右
                FIntPoint(-1, 0),  // 左
                FIntPoint(0, 1),   // 上
                FIntPoint(0, -1)   // 下
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

            // ★★★ 2025-11-10: サーバー側でプレイヤーを回転 ★★★
            const float Yaw = FMath::Atan2(Command.Direction.Y, Command.Direction.X) * 180.f / PI;
            FRotator NewRotation = PlayerPawn->GetActorRotation();
            NewRotation.Yaw = Yaw;
            PlayerPawn->SetActorRotation(NewRotation);

            UE_LOG(LogTurnManager, Log,
                TEXT("[MovePrecheck] ★ SERVER: Applied FACING ONLY - Direction=(%.1f,%.1f), Yaw=%.1f"),
                Command.Direction.X, Command.Direction.Y, Yaw);

            // ★★★ 2025-11-11: クライアントに回転専用RPC + ラッチリセット ★★★
            if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
            {
                if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
                {
                    // 回転のみ適用
                    TPCB->Client_ApplyFacingNoTurn(InputWindowId, FVector2D(Command.Direction.X, Command.Direction.Y));
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ★ Sent Client_ApplyFacingNoTurn RPC (WindowId=%d, no turn consumed)"),
                        InputWindowId);

                    // ★★★ CRITICAL FIX (2025-11-11): REJECT送信でラッチをリセット ★★★
                    // ACK は送らない（ターン不消費だから）が、REJECT を送って
                    // クライアント側の bSentThisInputWindow をリセットする。
                    // これにより、プレイヤーは別の方向に移動を試みることができる。
                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ★ Sent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            // ★★★ サーバー側の状態確認ログ (2025-11-10) ★★★
            UE_LOG(LogTurnManager, Verbose, TEXT("[MovePrecheck] Server state after FACING ONLY:"));
            UE_LOG(LogTurnManager, Verbose, TEXT("  - WaitingForPlayerInput: %d (STAYS TRUE - no turn consumed)"), WaitingForPlayerInput);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - bPlayerMoveInProgress: %d (STAYS FALSE)"), bPlayerMoveInProgress);
            UE_LOG(LogTurnManager, Verbose, TEXT("  - InputWindowId: %d (unchanged)"), InputWindowId);

            // ★ ウィンドウを閉じずに継続（プレイヤーが再入力可能、ターン不消費）
            // WaitingForPlayerInput は true のまま、Gate も開いたまま
            // コマンドは"消費"マークしない（MarkCommandAsAccepted()を呼ばない）
            return;
        }

        // ★★★ CRITICAL FIX (2025-11-11): 予約成功/失敗をチェック ★★★
        const bool bReserved = RegisterResolvedMove(PlayerPawn, TargetCell);

        if (!bReserved)
        {
            // 予約失敗 - 他のActorが既に予約済み（通常はPurgeで削除されるはずだが、万一の場合）
            UE_LOG(LogTurnManager, Error,
                TEXT("[MovePrecheck] RESERVATION FAILED: (%d,%d) → (%d,%d) - Cell already reserved by another actor"),
                CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y);

            // 回転のみ適用（ターン不消費）
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

                    // ★★★ CRITICAL FIX (2025-11-11): REJECT送信でラッチをリセット ★★★
                    // ACK は送らない（ターン不消費だから）が、REJECT を送って
                    // クライアント側の bSentThisInputWindow をリセットする。
                    // これにより、プレイヤーは別の方向に移動を試みることができる。
                    TPCB->Client_NotifyMoveRejected();
                    UE_LOG(LogTurnManager, Log,
                        TEXT("[MovePrecheck] ★ Sent Client_NotifyMoveRejected to reset client latch"));
                }
            }

            // ターン不消費で継続
            return;
        }

        UE_LOG(LogTurnManager, Log,
            TEXT("[MovePrecheck] OK: (%d,%d) → (%d,%d) | Walkable=true | Reservation SUCCESS"),
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

        // ★★★ CRITICAL FIX (2025-11-10): Precheck成功後にコマンドを"消費"マーク ★★★
        if (CommandHandler)
        {
            CommandHandler->MarkCommandAsAccepted(Command);
            UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] ★ Command marked as accepted (prevents duplicates)"));
        }

        // ★★★ CRITICAL FIX (2025-11-10): コマンド受理成功 → ACKをクライアントに送信 ★★★
        if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
        {
            if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
            {
                TPCB->Client_ConfirmCommandAccepted(InputWindowId);
                UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] ★ Sent Client_ConfirmCommandAccepted ACK (WindowId=%d)"), InputWindowId);
            }
        }

        // ★ コマンド受理成功 → 入力窓をクローズ（多重入力防止）
        CloseInputWindowForPlayer();
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GameTurnManager] GAS activation failed - No abilities triggered"));
        ApplyWaitInputGate(true);   // 失敗時はゲートを再度開く
    }

    //==========================================================================
    // (10) CachedPlayerCommand保存
    //==========================================================================
    CachedPlayerCommand = Command;

    //==========================================================================
    // ★★★ (11) 攻撃判定による分岐（ヤンガス方式）
    //==========================================================================
    const FGameplayTag InputMove = RogueGameplayTags::InputTag_Move;  // ネイティブタグを使用

    if (Command.CommandTag.MatchesTag(InputMove))
    {
        // ★★★ 攻撃判定: 敵に攻撃インテントがあるか確認
        const bool bHasAttack = HasAnyAttackIntent();

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] bHasAttack=%s (同時移動=%s)"),
            CurrentTurnIndex,
            bHasAttack ? TEXT("TRUE") : TEXT("FALSE"),
            bHasAttack ? TEXT("無効") : TEXT("有効"));

        if (bHasAttack)
        {
            // ★★★ 攻撃あり: 同時移動しない。逐次実行
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected → Sequential mode (no simultaneous move)"),
                CurrentTurnIndex);

            ExecuteSequentialPhase();  // 攻撃→移動を順次に実装
        }
        else
        {
            // ★★★ 攻撃なし: 同時移動する
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] No attacks → Simultaneous move mode"),
                CurrentTurnIndex);

            ExecuteSimultaneousPhase();  // 同時移動
        }
    }
    else if (Command.CommandTag == RogueGameplayTags::InputTag_Turn)  // ネイティブタグを使用
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

    // シーンに既にあるものを使う
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AGridPathfindingLibrary::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        PathFinder = Cast<AGridPathfindingLibrary>(Found[0]);
        CachedPathFinder = PathFinder;
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnPathFinder: Found existing PathFinder: %s"), *GetNameSafe(PathFinder));
        return;
    }

    // 無ければGameModeから取得を試みる
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

    // 最後の手段: スポーン。通常はGameModeが生成するので、ここはフォールバック
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

    // シーンに既にあるものを使う
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AUnitManager::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        UnitMgr = Cast<AUnitManager>(Found[0]);
        UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Found existing UnitManager: %s"), *GetNameSafe(UnitMgr));
        return;
    }

    // 無ければGameModeから取得を試みる（後方互換性のため）
    if (ATBSLyraGameMode* GM = World->GetAuthGameMode<ATBSLyraGameMode>())
    {
        UnitMgr = GM->GetUnitManager();
        if (IsValid(UnitMgr))
        {
            UE_LOG(LogTurnManager, Log, TEXT("ResolveOrSpawnUnitManager: Got UnitManager from GameMode: %s"), *GetNameSafe(UnitMgr));
            return;
        }
    }

    // 最後の手段: スポーン。通常はここで生成される
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

// GameTurnManagerBase.cpp - line 999の既存実装
void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    if (!HasAuthority()) return;

    // ★★★ 修正: bPlayerMoveInProgressのチェックに変更 ★★★
    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnIndex);
        return;
    }

    // ★★★ 修正: bTurnContinuingの削除。PlayerMoveInProgressを使用 ★★★
    bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnIndex);

    //==========================================================================
    // ★★★ CRITICAL FIX (2025-11-11): プレイヤー行動後に敵の意思決定を実行 ★★★
    // 理由: 不思議のダンジョン型の交互ターン制では、プレイヤーが行動してから敵が判断する
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ★ Collecting enemy intents AFTER player move ★"), CurrentTurnIndex);

    // 敵リストを更新
    CollectEnemies();

    //==========================================================================
    // ★★★ FIX (2025-11-12): BuildObservationsを直接実装 ★★★
    // 理由: BuildObservations_Implementation()は空なので、直接EnemyAISubsystemを呼ぶ
    //==========================================================================
    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && CachedPlayerPawn)
    {
        // DistanceFieldを更新（敵AIの判断に必要）
        if (UDistanceFieldSubsystem* DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>())
        {
            FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(CachedPlayerPawn->GetActorLocation());
            DistanceField->UpdateDistanceField(PlayerGrid);
            UE_LOG(LogTurnManager, Log,
                TEXT("[Turn %d] DistanceField updated for PlayerGrid=(%d,%d)"),
                CurrentTurnIndex, PlayerGrid.X, PlayerGrid.Y);
        }

        // プレイヤーの新しい位置を元に観測データを構築
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

    // 敵の意図を収集（この時点の盤面で判断）
    CollectIntents();

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ★ Enemy intent collection complete ★"), CurrentTurnIndex);

    // ★★★ 攻撃判定 ★★★
    const bool bHasAttack = HasAnyAttackIntent();

    if (bHasAttack)
    {
        ExecuteSequentialPhase(); // 内部でExecutePlayerMove()を呼ぶ
    }
    else
    {
        ExecuteSimultaneousPhase(); // 内部でExecutePlayerMove()を呼ぶ
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
    // ★★★ プレイヤーの移動は既に OnPlayerCommandAccepted_Implementation で実行済み
    //==========================================================================
    // ExecutePlayerMove();  // ★★★ 削除: 二重実行を防ぐ

    //==========================================================================
    // (1) 敵の移動フェーズ。同時実行
    //==========================================================================
    ExecuteMovePhase();  // ★★★ 既存の実装を使用。Actor 参照が正しい

    // ★★★ 注意: 移動完了は OnAllMovesFinished() デリゲートで通知される
}

void AGameTurnManagerBase::ExecuteSimultaneousMoves()
{
    // ExecuteSimultaneousPhase() を呼び出す（エイリアス）
    ExecuteSimultaneousPhase();
}




void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    // ★★★ 権限チェック。サーバー限定 ★★★
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] OnSimultaneousPhaseCompleted: Simultaneous phase finished"),
        CurrentTurnIndex);

    // ★★★ 確実に動作する方法：既存の ExecuteAttacks() を呼び出す ★★★
    ExecuteAttacks();
}


//------------------------------------------------------------------------------
// ExecuteSequentialPhaseの修正版
//------------------------------------------------------------------------------
void AGameTurnManagerBase::ExecuteSequentialPhase()
{
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ==== Sequential Phase (Attack → Move) ===="), CurrentTurnIndex);

    //==========================================================================
    // (1) 攻撃フェーズ
    //==========================================================================
    ExecuteAttacks();  // 既存の攻撃実行関数

    // ★★★ 注意: 攻撃完了は OnAttacksFinished() デリゲートで通知される
    // ここでは移動フェーズは開始しない
}





//------------------------------------------------------------------------------
// OnPlayerMoveCompleted - Non-Dynamic - Gameplay.Event.Turn.Ability.Completed 受信時
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    //==========================================================================
    // (1) TurnId検証。EventMagnitudeから取得
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
    // (2) Authority チェック
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

    // ★★★ Phase 5: AP残量確認とGate再オープン
    //==========================================================================

    // ★★★ 将来的な拡張: APシステム統合（2025-11-09） ★★★
    // APシステムが実装されたら、以下のコードを有効化してください
    // - PlayerAttributeSetにAPアトリビュートを追加
    // - AP消費/回復のロジックを実装
    // - Gate再オープンの条件をAP残量に基づいて制御
    /*
    int32 PlayerAP = 0;
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        // APの取得方法（例：AttributeSetから）
        const UPlayerAttributeSet* Attrs = ASC->GetSet<UPlayerAttributeSet>();
        PlayerAP = Attrs ? Attrs->GetActionPoints() : 0;
    }

    if (PlayerAP > 0 && CurrentPhase == Phase_Player_Wait)
    {
        //======================================================================
        // (8a) Gate.Input.Openを付与（連続移動許可）
        //======================================================================
        if (UAbilitySystemComponent* ASC = GetPlayerASC())
        {
            // ★★★ 修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate.Input.Open REOPENED (AP=%d remaining)"),
                CurrentTurnIndex, PlayerAP);
        }

        //======================================================================
        // (8b) Phase.Player.WaitInputを付与
        //======================================================================
        if (UAbilitySystemComponent* ASC = GetPlayerASC())
        {
            // ★★★ 修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Phase.Player.WaitInput RESTORED for continuous move"),
                CurrentTurnIndex);
        }

        //======================================================================
        // (8c) WaitingForPlayerInputを再度trueに設定
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
        // (8d) AP残量なし：フェーズ終了
        //======================================================================
        UE_LOG(LogTurnManager, Log,
            TEXT("Turn %d: No AP remaining or not in WaitInput phase (AP=%d, Phase=%s)"),
            CurrentTurnIndex, PlayerAP, *CurrentPhase.ToString());

        // フェーズ終了。システムの状態に応じて
        // EndPlayerPhase();
    }
    */

    //==========================================================================
    // ★★★ 暫定実装: APシステムがない場合フェーズ終了
    //==========================================================================
    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnIndex);

    // ★★★ 現在はAPシステム未実装のため、フェーズ遷移は別のロジックで制御 ★★★
    // APシステム実装後は、AP残量に基づいてフェーズ遷移を制御する
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

    // ★★★ FIX (2025-11-12): 攻撃フェーズ中は入力ゲートを閉じる ★★★
    // プレイヤー移動と攻撃の競合を防ぐため、攻撃実行中は入力を受け付けない。
    // 入力は OnAttacksFinished で再度開く。
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] ExecuteAttacks: Closing input gate (attack phase starts)"), CurrentTurnIndex);
    ApplyWaitInputGate(false);  // Close input gate

    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* PM = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            // ★★★ Out パラメータで受け取る ★★★
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

    // TurnId検証
    if (TurnId != CurrentTurnIndex)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] OnAttacksFinished: TurnId mismatch (%d != %d)"),
            CurrentTurnIndex, TurnId, CurrentTurnIndex);
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] OnAttacksFinished: All attacks completed"), TurnId);

    // ★★★ FIX (2025-11-12): 攻撃完了後に入力ゲートを再度開く ★★★
    // ExecuteAttacks で閉じたゲートを再度開き、プレイヤーが次の入力をできるようにする。
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] OnAttacksFinished: Re-opening input gate (attack phase complete)"), TurnId);
    ApplyWaitInputGate(true);  // Re-open input gate

    //==========================================================================
    // (2) 移動フェーズ。攻撃後
    //==========================================================================
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Starting Move Phase (after attacks)"), TurnId);

    ExecuteMovePhase();  // 既存の移動実行関数

    // ★★★ 注意: 移動完了は OnAllMovesFinished() デリゲートで通知される
}


void AGameTurnManagerBase::ExecuteMovePhase()
{
    // ★★★ Phase 5: ConflictResolver Integration (2025-11-09) ★★★
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
    // ★★★ CRITICAL FIX (2025-11-12): フェイルセーフ - インテントが空なら再生成 ★★★
    //==========================================================================
    if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteMovePhase: No enemy intents detected, attempting fallback generation..."),
            CurrentTurnIndex);

        // フェイルセーフ: Observations と Intents を再生成
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

            // Observationsを生成
            TArray<FEnemyObservation> Observations;
            EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);
            EnemyData->Observations = Observations;

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Fallback: Generated %d observations"),
                CurrentTurnIndex, Observations.Num());

            // Intentsを収集
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

        // 再チェック: まだ空ならスキップ
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
    // ★★★ ATTACK PRIORITY (2025-11-12): 攻撃インテントがあれば攻撃フェーズへ ★★★
    // 理由: プレイヤー移動後の再計画で攻撃に昇格する可能性があるため、
    //       Execute直前に再チェックして攻撃優先で処理
    //==========================================================================
    const bool bHasAnyAttack = HasAnyAttackIntent();
    if (bHasAnyAttack)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ★ ATTACK INTENT detected (%d intents) - Executing attack phase instead of move phase"),
            CurrentTurnIndex, EnemyData->Intents.Num());

        // 攻撃フェーズを実行（既存の実装を使用）
        ExecuteAttacks();

        // 注意: 攻撃完了後に OnAttacksFinished() が呼ばれ、その後 ExecuteMovePhase() が再度呼ばれる
        //       その時は攻撃インテントが処理済みなので、移動フェーズが実行される
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] No attack intents - Proceeding with move phase"),
        CurrentTurnIndex);

    //==========================================================================
    // ★★★ CRITICAL FIX (2025-11-11): Playerの移動もConflictResolverに追加 ★★★
    // Swap検出を機能させるため、PlayerとEnemyの移動情報を統合する
    //==========================================================================
    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;

    // Playerが移動している場合、Intentリストに追加
    if (CachedPlayerPawn && CachedPathFinder.IsValid())
    {
        const FVector PlayerLoc = CachedPlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = CachedPathFinder->WorldToGrid(PlayerLoc);

        // PendingMoveReservationsからPlayerの移動先を取得
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
                PlayerIntent.BasePriority = 200;  // Playerの優先度を高く設定

                AllIntents.Add(PlayerIntent);

                UE_LOG(LogTurnManager, Log,
                    TEXT("[Turn %d] ★ Added Player intent to ConflictResolver: (%d,%d) → (%d,%d)"),
                    CurrentTurnIndex, PlayerCurrentCell.X, PlayerCurrentCell.Y,
                    PlayerNextCell->X, PlayerNextCell->Y);
            }
        }
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ExecuteMovePhase: Processing %d intents (%d enemies + Player) via ConflictResolver"),
        CurrentTurnIndex, AllIntents.Num(), EnemyData->Intents.Num());

    //==========================================================================
    // (1) Conflict Resolution: Convert Intents → ResolvedActions
    //==========================================================================
    TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);

    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] ConflictResolver produced %d resolved actions"),
        CurrentTurnIndex, ResolvedActions.Num());

    //==========================================================================
    // ★★★ CRITICAL FIX (2025-11-11): Playerの移動がSwap検出でキャンセルされた場合の処理 ★★★
    //==========================================================================
    if (CachedPlayerPawn)
    {
        // ResolvedActions内でPlayerがbIsWait=trueにマークされているかチェック
        for (const FResolvedAction& Action : ResolvedActions)
        {
            if (IsValid(Action.SourceActor.Get()) && Action.SourceActor.Get() == CachedPlayerPawn)
            {
                if (Action.bIsWait)
                {
                    // PlayerのIntentがSwap検出で拒否された
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[Turn %d] ★ Player movement CANCELLED by ConflictResolver (Swap detected or other conflict)"),
                        CurrentTurnIndex);

                    // Playerの予約を解放
                    TWeakObjectPtr<AActor> PlayerKey(CachedPlayerPawn);
                    PendingMoveReservations.Remove(PlayerKey);

                    // Playerのアビリティをキャンセル（実行中なら中止）
                    if (UAbilitySystemComponent* ASC = CachedPlayerPawn->FindComponentByClass<UAbilitySystemComponent>())
                    {
                        // 移動アビリティをキャンセル
                        ASC->CancelAllAbilities();
                        UE_LOG(LogTurnManager, Log,
                            TEXT("[Turn %d] Player abilities cancelled due to Swap conflict"),
                            CurrentTurnIndex);
                    }

                    // ターン終了処理に進む（Enemyの移動は実行しない）
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
// ExecutePlayerMoveの修正版
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ExecutePlayerMoveの完全修正版
//------------------------------------------------------------------------------
// GameTurnManagerBase.cpp - line 1170の既存実装
// ★★★ 変更なし（参照のため全文掲載） ★★★

void AGameTurnManagerBase::ExecutePlayerMove()
{
    //==========================================================================
    // Step 1: 権限チェック
    //==========================================================================
    if (!HasAuthority())
    {
        return;
    }

    //==========================================================================
    // Step 2: PlayerPawn取得
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
    // ★★★ ルーチン: Subsystem経由。推奨・型安全
    // ★★★ UActionExecutorSubsystem は存在しないためコメントアウト
    //==========================================================================
    /*
    if (UWorld* World = GetWorld())
    {
        if (UActionExecutorSubsystem* Exec = World->GetSubsystem<UActionExecutorSubsystem>())
        {
            const bool bSent = Exec->SendPlayerMove(CachedPlayerCommand);
            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: SendPlayerMove: %s"),
                CurrentTurnIndex, bSent ? TEXT("✅OK") : TEXT("❌FAILED"));

            if (bSent)
            {
                return; // 成功したらここで終了
            }
        }
    }
    */

    //==========================================================================
    // ★★★ ルーチン: 直接GAS経由。フォールバック
    // 問題: EventMagnitude に TurnId だけを設定すると、GA_MoveBase で
    // Direction が取得できない。以前の実装では Direction をエンコードしていた
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        //======================================================================
        // ★★★ DIAGNOSTIC (2025-11-12): ASCのアビリティとトリガータグを診断 ★★★
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

                // CanActivateAbility をテスト
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

                // GA_MoveBaseかどうかチェック
                const bool bIsMoveAbility = Spec.Ability->GetClass()->GetName().Contains(TEXT("MoveBase"));
                if (bIsMoveAbility)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("    - ★ This is a MOVE ability! CanActivate=%s"),
                        bCanActivate ? TEXT("YES") : TEXT("NO"));
                    if (bCanActivate)
                    {
                        ++PotentialMoveAbilities;
                    }
                }

                // DynamicAbilityTags
                if (Spec.DynamicAbilityTags.Num() > 0)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("    - DynamicAbilityTags: %s"),
                        *Spec.DynamicAbilityTags.ToStringSimple());
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
        // ★★★ Direction をエンコード（TurnCommandEncoding 統一） ★★★
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
                TEXT("Turn %d: ✅GA_MoveBase activated (count=%d)"),
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
    // ★★★ セーフティ: 実行中フラグ/ゲートを確実に解除
    //==========================================================================
    bPlayerMoveInProgress = false;
 

    ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

    //==========================================================================
    // ★★★ 次ターンへ進行（これが OnTurnStarted をBroadcastする）
    //==========================================================================
    EndEnemyTurn();  // または AdvanceTurnAndRestart(); (実装に応じて)
}








void AGameTurnManagerBase::EndEnemyTurn()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== EndEnemyTurn ===="), CurrentTurnIndex);

    //==========================================================================
    // ★★★ Phase 4: 二重鍵チェック
    //==========================================================================
    if (!CanAdvanceTurn(CurrentTurnIndex))
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[EndEnemyTurn] ABORT: Cannot end turn %d (actions still in progress)"),
            CurrentTurnIndex);

        //======================================================================
        // ★★★ デバッグ: Barrierの状態をダンプ
        //======================================================================
        if (UWorld* World = GetWorld())
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->DumpTurnState(CurrentTurnIndex);
            }
        }

        //======================================================================
        // ★★★ Phase 5補完: リトライ連打防止（2025-11-09） ★★★
        // 最初の1回だけリトライをスケジュール、以降は抑止
        //======================================================================
        if (!bEndTurnPosted)
        {
            bEndTurnPosted = true;  // フラグを立てる

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

        return;  // ★★★ 進行を中止
    }

    //==========================================================================
    // ★★★ Phase 5補完: 残留タグクリーンアップ（2025-11-09） ★★★
    // Barrier完了後、ターン進行前に残留InProgressタグを掃除
    //==========================================================================
    ClearResidualInProgressTags();

    //==========================================================================
    // (1) 次のターンへ進む
    //==========================================================================
    AdvanceTurnAndRestart();
}

//------------------------------------------------------------------------------
// ★★★ Phase 5補完: 残留InProgressタグの強制クリーンアップ（2025-11-09） ★★★
//------------------------------------------------------------------------------
void AGameTurnManagerBase::ClearResidualInProgressTags()
{
    // ★★★ CRITICAL FIX (2025-11-11): 全てのブロッキングタグをクリア ★★★
    // ★★★ EXTENDED FIX (2025-11-11): Movement.Mode.Fallingも追加 ★★★
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

        // ★★★ CRITICAL FIX (2025-11-11): Movement.Mode.Falling をクリア ★★★
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
    // 🌟 3-Tag System: クライアント側でGateを開く（レプリケーション後）
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
    // Standalone + Network両対応：ゲートリセット。既存の処理
    //==========================================================================
    if (UWorld* World = GetWorld())
    {
        if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(World->GetFirstPlayerController()))
        {
            if (WaitingForPlayerInput)
            {
                // 入力ウィンドウが開いたら → ゲートをリセット
                
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
            // ★★★ 修正: 正しいタグ名を使用
            ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
            ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: Gate OPENED (Phase+Gate tags added), WindowId=%d"),
                CurrentTurnIndex, InputWindowId);
        }
        else
        {
            // ★★★ Gate だけを閉じる。Phase は維持
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);
            // ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);  // Phaseは維持

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

    // ★★★ WindowIdをインクリメント
    ++InputWindowId;

    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Opened: Turn=%d WindowId=%d"),
        CurrentTurnIndex, InputWindowId);

    // ★★★ コアシステム: CommandHandler経由でInput Window開始（2025-11-09） ★★★
    // ★★★ Week 1: UPlayerInputProcessorに委譲（2025-11-09リファクタリング）
    if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(TurnFlowCoordinator->GetCurrentTurnId());
    }

    if (CommandHandler)
    {
        CommandHandler->BeginInputWindow(InputWindowId);
    }

    // ★★★ Gate/Phaseタグを付与（既存のApplyWaitInputGateを流用）
    ApplyWaitInputGate(true);

    // WaitingForPlayerInputフラグを立てる
    WaitingForPlayerInput = true;

    // ★★★ Standalone では OnRep が呼ばれないので手動実装
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

    // プレイヤー所持完了フラグを立てる
    bPlayerPossessed = true;

    // すでにターン開始済みで、まだ窓が開いていなければ再オープン
    if (bTurnStarted && !WaitingForPlayerInput)
    {
        OpenInputWindowForPlayer();
        bDeferOpenOnPossess = false; // フラグをクリア
    }
    // または、遅延オープンフラグが立っている場合も開く
    else if (bTurnStarted && bDeferOpenOnPossess)
    {
        OpenInputWindowForPlayer();
        bDeferOpenOnPossess = false; // フラグをクリア
    }
    else if (!bTurnStarted)
    {
        // ターン未開始なら、ゲート機構で開始を試行
        bDeferOpenOnPossess = true;
        UE_LOG(LogTurnManager, Log, TEXT("[Turn] Defer input window open until turn starts"));
        TryStartFirstTurn(); // ★★★ ゲート機構
    }
}

//==========================================================================
// ★★★ TryStartFirstTurn: 全条件が揃ったらStartFirstTurnを実行（2025-11-09 解析サマリ対応）
//==========================================================================
void AGameTurnManagerBase::TryStartFirstTurn()
{
    if (!HasAuthority()) return;

    // 全条件チェック
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

    // ★★★ TurnCommandHandlerに通知（2025-11-09 修正）
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

    // ★★★ フラグとゲートを同時に閉じる（2025-11-09 FIX） ★★★
    WaitingForPlayerInput = false;

    // ★★★ TurnCommandHandlerに通知（2025-11-09 修正）
    if (CommandHandler)
    {
        CommandHandler->EndInputWindow();
    }

    // ★★★ Gate タグを削除（2025-11-09 FIX）
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CachedPlayerPawn))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);
        }
    }

    UE_LOG(LogTurnManager, Warning,
        TEXT("[CloseInputWindow] ✅ SUCCESS: Turn=%d WindowId=%d Reason=AcceptedValidPlayerCmd"),
        CurrentTurnIndex, InputWindowId);
}


void AGameTurnManagerBase::OnRep_InputWindowId()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Client OnRep: WindowId=%d"),
        InputWindowId);

    // ★★★ 重要: クライアント側ではタグを触らない
    // タグはサーバーからレプリケートされる
    // ここではUIの更新のみを行う

    // UI更新デリゲートがあれば発火
    // OnInputWindowChanged.Broadcast(InputWindowId);

    // ★★★ 削除: ResetInputWindowGateは呼ばない
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
            TEXT("[IsInputOpen] ❌ FALSE: WaitingForPlayerInput=FALSE (Turn=%d)"),
            CurrentTurnIndex);
        return false;
    }

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        const bool bGateOpen = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        if (!bGateOpen)
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[IsInputOpen] ❌ FALSE: Gate_Input_Open=FALSE (Turn=%d)"),
                CurrentTurnIndex);
        }
        return bGateOpen;
    }

    UE_LOG(LogTurnManager, Error, TEXT("[IsInputOpen] ❌ FALSE: Player ASC not found"));
    return false;
}



bool AGameTurnManagerBase::CanAdvanceTurn(int32 TurnId) const
{
    //==========================================================================
    // (1) Barrier::IsQuiescent チェック
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
    // (2) State.Action.InProgressタグカウントチェック
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
    // (3) 二重鍵の判定（両方がtrueで進行可能）
    //==========================================================================
    const bool bCanAdvance = bBarrierQuiet && bNoInProgressTags;

    if (bCanAdvance)
    {
            UE_LOG(LogTurnManager, Log,
                TEXT("[CanAdvanceTurn] ✅OK: Turn=%d (Barrier=Quiet, InProgress=0)"),
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
// ダンジョン生成システム統合APIの実装
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

    // ★★★ 将来的な実装: 階段ワープ機能（2025-11-09） ★★★
    // RogueDungeonSubsystemから階段の位置を取得してワープ
    // 実装案：
    // - URogueDungeonSubsystem::GetStairUpLocation() を追加
    // - PlayerPawnをその位置にテレポート
    // - カメラを更新
    UE_LOG(LogTurnManager, Warning, TEXT("[WarpPlayerToStairUp] Not implemented yet - requires dungeon stair tracking"));
}

//------------------------------------------------------------------------------
// グリッドコスト設定。Blueprint用の実装
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
// その他の未定義関数の実装
//------------------------------------------------------------------------------

bool AGameTurnManagerBase::HasAnyMoveIntent() const
{
    // CachedPlayerCommandの方向ベクトルが非ゼロなら移動意図あり
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

    // ★★★ CRITICAL FIX (2025-11-11): 予約成功/失敗をチェック ★★★
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
                return false;  // 予約失敗
            }

            UE_LOG(LogTurnManager, Verbose,
                TEXT("[RegisterResolvedMove] SUCCESS: %s reserved (%d,%d)"),
                *GetNameSafe(Actor), Cell.X, Cell.Y);
            return true;  // 予約成功
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

    // ★★★ CRITICAL FIX (2025-11-10): 敗者の移動を完全ブロック ★★★
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
        UE_LOG(LogTurnManager, Warning,
            TEXT("[ResolvedMove] Move to (%d,%d) not authorized for %s"),
            Action.NextCell.X, Action.NextCell.Y, *GetNameSafe(Unit));
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
        if (TriggerPlayerMoveAbility(Action, Unit))
        {
            return true;
        }

        // ★★★ FIX (2025-11-12): プレイヤー移動はGAS経路のみに統一 ★★★
        // Direct MoveUnit フォールバックを廃止。GA起動失敗時はエラー終了。
        // これにより、移動経路が「コマンド受付 → GAS起動」に一本化され、
        // 攻撃フェーズとの競合や二重実行を防止。
        UE_LOG(LogTurnManager, Error,
            TEXT("[ResolvedMove] ❌ Player GA trigger failed - Move BLOCKED (GAS-only path enforced)"));
        ReleaseMoveReservation(Unit);
        return false;
    }

    // ★★★ 以下のDirect MoveUnitパスは敵専用 ★★★
    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = LocalPathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, bIsPlayerUnit);

    // ★★★ コアシステム: OnActionExecuted配信（2025-11-09） ★★★
    // NOTE: This notification is sent when move STARTS, not when it completes
    // Listeners should not expect the unit to be at the final position yet
    if (EventDispatcher)
    {
        const FGameplayTag MoveActionTag = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.Intent.Move"));
        const int32 UnitID = Unit->GetUniqueID();
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[DispatchResolvedMove] Broadcasting move start notification for Unit %d at (%d,%d) → (%d,%d)"),
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

    // ★★★ CRITICAL FIX (2025-11-11): アビリティ起動前に古いブロックタグを強制削除 ★★★
    // ★★★ EXTENDED FIX (2025-11-11): State.MovingとMovement.Mode.Fallingも追加 ★★★
    // ターン開始時のクリーンアップが間に合わなかった場合の保険
    // これにより、前のターンで残ったタグによるアビリティブロックを回避
    //
    // Gemini分析により判明：OwnedTagsに State.Moving が残留していると
    // ActivationOwnedTags に State.Moving が含まれるGA_MoveBaseは二重起動防止で拒否される
    int32 ClearedCount = 0;
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Ability_Executing))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Ability_Executing);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠️ Cleared blocking State.Ability.Executing tag from %s"),
            *GetNameSafe(Unit));
    }
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Action_InProgress))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠️ Cleared blocking State.Action.InProgress tag from %s"),
            *GetNameSafe(Unit));
    }

    // ★★★ CRITICAL FIX (2025-11-11): State.Moving 残留対策 ★★★
    if (ASC->HasMatchingGameplayTag(RogueGameplayTags::State_Moving))
    {
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Moving);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠️ Cleared residual State.Moving tag from %s (Gemini分析)"),
            *GetNameSafe(Unit));
    }

    // ★★★ CRITICAL FIX (2025-11-11): Movement.Mode.Falling 残留対策 ★★★
    static const FGameplayTag FallingTag = FGameplayTag::RequestGameplayTag(FName("Movement.Mode.Falling"));
    if (FallingTag.IsValid() && ASC->HasMatchingGameplayTag(FallingTag))
    {
        ASC->RemoveLooseGameplayTag(FallingTag);
        ClearedCount++;
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠️ Cleared residual Movement.Mode.Falling tag from %s (Gemini分析)"),
            *GetNameSafe(Unit));
    }

    // ★★★ DIAGNOSTIC: Log ASC ability count (2025-11-11) ★★★
    const int32 AbilityCount = ASC->GetActivatableAbilities().Num();
    if (AbilityCount == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[TriggerPlayerMove] ⚠️ No abilities granted to %s - ASC may not be initialized"),
            *GetNameSafe(Unit));
    }
    else
    {
        UE_LOG(LogTurnManager, Log,
            TEXT("[TriggerPlayerMove] %s has %d abilities in ASC (cleared %d blocking tags)"),
            *GetNameSafe(Unit), AbilityCount, ClearedCount);

        // ★★★ DIAGNOSTIC: List all granted abilities (2025-11-11) ★★★
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

        // ★★★ コアシステム: OnActionExecuted配信（2025-11-09） ★★★
        // NOTE: This notification is sent when ability triggers, not when move completes
        // Listeners should not expect the unit to be at the final position yet
        if (EventDispatcher)
        {
            const int32 UnitID = Unit->GetUniqueID();
            UE_LOG(LogTurnManager, Verbose,
                TEXT("[TriggerPlayerMove] Broadcasting move start notification for Unit %d at (%d,%d) → (%d,%d)"),
                UnitID, Action.CurrentCell.X, Action.CurrentCell.Y, Action.NextCell.X, Action.NextCell.Y);
            EventDispatcher->BroadcastActionExecuted(UnitID, EventData.EventTag, true);
        }

        return true;
    }

    // ★★★ FIX: Better diagnostic when trigger fails (2025-11-11) ★★★
    FGameplayTagContainer CurrentTags;
    ASC->GetOwnedGameplayTags(CurrentTags);
    UE_LOG(LogTurnManager, Error,
        TEXT("[TriggerPlayerMove] ❌ HandleGameplayEvent returned 0 for %s (AbilityCount=%d, OwnedTags=%s)"),
        *GetNameSafe(Unit),
        AbilityCount,
        *CurrentTags.ToStringSimple());

    return false;
}

