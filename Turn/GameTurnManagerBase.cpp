// Copyright Epic Games, Inc. All Rights Reserved.

#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"  // ATBSLyraGameMode用
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridOccupancySubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
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
#include "GameFramework/SpectatorPawn.h"  // ☁Eこれを追加
#include "EngineUtils.h"
#include "Data/MoveInputPayloadBase.h"
#include "Kismet/GameplayStatics.h"
// ☁E�E☁E追加�E�DistanceFieldSubsystemのinclude ☁E�E☁E
#include "DistanceFieldSubsystem.h" 
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameModes/LyraGameState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "Utility/TurnCommandEncoding.h"
// ☁E�E☁EActionExecutorSubsystem と TurnPhaseManagerComponent のヘッダ
// 注愁E これら�Eクラスが存在しなぁE��合�E、Ecpp冁E�E該当コードをコメントアウトまた�E修正してください
// #include "Turn/ActionExecutorSubsystem.h"  // UActionExecutorSubsystem, FOnActionExecutorCompleted
// #include "Turn/TurnPhaseManagerComponent.h"  // UTurnPhaseManagerComponent

// ☁E�E☁EFOnActionExecutorCompleted が定義されてぁE��ぁE��合�E暫定定義
// ActionExecutorSubsystem.h に定義がある場合�E、上記�Eincludeを有効化してください
#if !defined(FOnActionExecutorCompleted_DEFINED)
DECLARE_DELEGATE(FOnActionExecutorCompleted);
#define FOnActionExecutorCompleted_DEFINED 1
#endif

// ☁E�E☁ELogTurnManager と LogTurnPhase は ProjectDiagnostics.cpp で既に定義されてぁE��ため、ここでは定義しなぁE
// DEFINE_LOG_CATEGORY(LogTurnManager);
// DEFINE_LOG_CATEGORY(LogTurnPhase);
using namespace RogueGameplayTags;

// ☁E�E☁E追加�E�CVar定義 ☁E�E☁E
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
}

//------------------------------------------------------------------------------
// ☁E�E☁E新規追加�E�InitializeTurnSystem ☁E�E☁E
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// InitializeTurnSystem�E�修正版！E
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// InitializeTurnSystem�E�修正版！E
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
                    UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to find/possess BPPlayerUnit"));
                }
            }
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get PlayerPawn"));
        }
    }

    //==========================================================================
    // Step 2: CachedPathFinder�E�既に注入済みなので探索不要E��E
    //==========================================================================
    // PathFinderはHandleDungeonReady()で既に注入されてぁE��、また�EResolveOrSpawnPathFinder()で解決済み
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
    // Step 4: C++ Dynamic チE��ゲートバインド（重褁E��止�E�E
    //==========================================================================
    OnTurnStarted.RemoveDynamic(this, &ThisClass::OnTurnStartedHandler);
    OnTurnStarted.AddDynamic(this, &ThisClass::OnTurnStartedHandler);

    if (UWorld* World = GetWorld())
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

        //======================================================================
        // ☁E�E☁E削除�E�UTurnInputGuard参�E�E�EタグシスチE��で不要E��E
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
    // ☁E�E☁E修正3: ASC Gameplay Event�E�重褁E��止�E�E
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
    // ☁E�E☁E削除: BindAbilityCompletion()�E�重褁E��避けるため�E�E
    //==========================================================================
    // BindAbilityCompletion();

    //==========================================================================
    // 4. 完亁E
    //==========================================================================
    bHasInitialized = true;
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));

    //==========================================================================
    // 5. 最初�Eターン開姁E
    //==========================================================================
    StartFirstTurn();
}







void AGameTurnManagerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 封E��皁E��拡張用�E�現在は空実裁E��E
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

    // 直参�Eの"注入"�E�EungeonSysのみ。PathFinder/UnitMgrはTurnManagerが所有！E
    // GameModeからは取得しなぁE��EurnManagerが所有するためE��E
    DungeonSys = GM->GetDungeonSubsystem();

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: DungeonSys injected (Dgn=%p, PFL/UM will be created on HandleDungeonReady)"),
        static_cast<void*>(DungeonSys.Get()));

    // 準備完亁E��ベントにサブスクライチE
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
        UE_LOG(LogTurnManager, Error, TEXT("TurnManager: DungeonSys is null, cannot subscribe to OnGridReady"));
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Ready for HandleDungeonReady"));
}

void AGameTurnManagerBase::HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys)
{
    // DungeonSysは忁E��！EameModeから注入済み�E�E
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
    }

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: HandleDungeonReady completed, initializing turn system..."));

    // ☁EInitializeTurnSystem は"ここだぁEで呼ぶ
    InitializeTurnSystem();
}



// ☁E�E☁E赤ペン修正5�E�OnExperienceLoaded の実裁E☁E�E☁E
void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] ========== EXPERIENCE READY =========="));
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] Experience: %s"),
        Experience ? *Experience->GetName() : TEXT("NULL"));

    // ☁EExperience完亁E���EInitializeTurnSystemを呼ばなぁE��EandleDungeonReadyからのみ呼ぶ�E�E
    // InitializeTurnSystem()はHandleDungeonReady()からのみ呼ばれる
}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    // ☁E�E☁EクライアンチE ターンUI更新/ログ�E�同期！E☁E�E☁E
    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Client: TurnId replicated to %d"), CurrentTurnId);
    // Blueprintイベント発火�E�任愁E OnTurnAdvanced.Broadcast(CurrentTurnId);�E�E
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
        int32 TotalUnits = 1; // Player
        TArray<AActor*> Enemies;
        GetCachedEnemies(Enemies); // Subsystem / EnemyTurnDataSubsystem
        TotalUnits += Enemies.Num();

        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->StartMoveBatch(TotalUnits, TurnId);
            UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: Barrier StartMoveBatch with %d units"), TotalUnits);
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("GameTurnManager: Barrier not found"));
        }

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
        // ☁E�E☁Eエラー該当衁E39: StartEnemyMoveBatch() 未実裁EↁEコメントアウチE☁E�E☁E
        // if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        // {
        //     TArray<AActor*> EnemiesLocal;
        //     GetCachedEnemies(EnemiesLocal);
        //     // AI
        //     EnemyAI->StartEnemyMoveBatch(EnemiesLocal, TurnId);
        //     UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: EnemyAI StartEnemyMoveBatch for %d enemies"), EnemiesLocal.Num());
        // }

        // ☁E�E☁Eエラー該当衁E47: PhaseManager->StartPhase(ETurnPhase::Move) 未実裁EↁEコメントアウチE☁E�E☁E
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
// ☁E�E☁E新規関数�E�Experience完亁E��のコールバック�E�E☁E�E☁E


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
    Tag_AbilityMove = RogueGameplayTags::Ability_Move;               // "Ability.Move"
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
    // メンバ変数PathFinderを優先！EameModeから注入されたもの�E�E
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

    // フォールバック�E�シーン冁E��探索
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        AGridPathfindingLibrary* PF = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // 最後�E手段�E�GameModeから取得を試みめE
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

// ========== 修正�E�Subsystemに委譲 ==========
void AGameTurnManagerBase::BuildAllObservations()
{
    SCOPED_NAMED_EVENT(BuildAllObservations, FColor::Orange);

    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: EnemyAISubsystem not found"), CurrentTurnIndex);
        return;
    }

    // 敵リスト�E変換�E�EObjectPtr ↁE生�Eインタ�E�E
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
    OnPlayerInputReceived.Broadcast();
    // 二重進行防止�E�ここで征E��ゲートを閉じてから継綁E
    if (WaitingForPlayerInput)
    {
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);
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
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] StartTurn called"), CurrentTurnIndex);
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

// ========== 修正�E�PhaseManagerに委譲 ==========
void AGameTurnManagerBase::BeginPhase(FGameplayTag PhaseTag)
{
    CurrentPhase = PhaseTag;
    PhaseStartTime = FPlatformTime::Seconds();
    UE_LOG(LogTurnPhase, Log, TEXT("PhaseStart:%s"), *PhaseTag.ToString());

    if (PhaseTag == Phase_Player_Wait)
    {
        // 入力征E��開始：フラグとゲートを**揁E��て**開く
        WaitingForPlayerInput = true;
        ApplyWaitInputGate(true);      // ↁE追加�E�重要E��E
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

    // DebugObserver への通知�E�既存�E琁E��E
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
        // 入力征E��終亁E��フラグとゲートを**揁E��て**閉じめE
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

    // ☁E�E☁EAPawnで検索�E�Encludeが不要E��E☁E�E☁E
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);

    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;  // ネイチE��ブタグを使用

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

    // ☁E�E☁E重要E��CachedEnemiesを完�Eにクリアしてから再構篁E☁E�E☁E
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

        // ☁E�E☁E修正�E�TeamId == 2 また�E 255 を敵として扱ぁE☁E�E☁E
        // ログから全ての敵ぁETeamID=255 であることが判昁E
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

            // ☁E�E☁EチE��チE���E�最初�E3体と最後�E1体�Eみ詳細ログ ☁E�E☁E
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

    // ☁E�E☁Eエラー検�E�E�敵ぁE体も見つからなぁE��吁E☁E�E☁E
    if (CachedEnemies.Num() == 0 && Found.Num() > 1)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[CollectEnemies] ❁EFailed to collect any enemies from %d Pawns!"),
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
    // ☁E�E☁E警告：予想より少なぁE�� ☁E�E☁E
    else if (CachedEnemies.Num() > 0 && CachedEnemies.Num() < 32 && Found.Num() >= 32)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ⚠�E�ECollected %d enemies, but expected around 32 from %d Pawns"),
            CachedEnemies.Num(), Found.Num());
    }
}




// ========== 修正�E�Subsystemに委譲 ==========
// GameTurnManagerBase.cpp

void AGameTurnManagerBase::CollectIntents_Implementation()
{
    // ☁E�E☁E修正1�E�Subsystemを毎回取得（キャチE��ュ無効化対策！E☁E�E☁E
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

    // ☁E�E☁E修正2�E�Observationsの存在確誁E+ 自動リカバリー ☁E�E☁E
    if (EnemyTurnDataSys->Observations.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
            CurrentTurnIndex, CachedEnemies.Num());

        // ☁E�E☁E自動リカバリー�E�Observationsを生戁E☁E�E☁E
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

    // ☁E�E☁E修正3�E�CachedEnemiesを直接使用 ☁E�E☁E
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents input: Observations=%d, Enemies=%d"),
        CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

    // ☁E�E☁E修正4�E�サイズ一致チェチE�� ☁E�E☁E
    if (EnemyTurnDataSys->Observations.Num() != CachedEnemies.Num())
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

        // ☁E�E☁Eリカバリー�E�Observationsを�E生�E ☁E�E☁E
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

    // ☁E�E☁E修正5�E�EnemyAISubsystem::CollectIntents実衁E☁E�E☁E
    TArray<FEnemyIntent> Intents;
    EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemies, Intents);

    // ☁E�E☁E修正6�E�Subsystemに格紁E☁E�E☁E
    EnemyTurnDataSys->Intents = Intents;

    // ☁E�E☁EIntent冁E��の計箁E☁E�E☁E
    int32 AttackCount = 0, MoveCount = 0, WaitCount = 0, OtherCount = 0;

    // ☁E�E☁E修正7�E�GameplayTagをキャチE��ュ�E�パフォーマンス改喁E��E☁E�E☁E
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

    // ☁E�E☁E修正8�E�ログレベルをWarningに変更�E�重要なイベント！E☁E�E☁E
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents -> %d intents (Attack=%d, Move=%d, Wait=%d, Other=%d)"),
        CurrentTurnIndex, Intents.Num(), AttackCount, MoveCount, WaitCount, OtherCount);

    // ☁E�E☁E修正9�E�Intent生�E失敗�E詳細ログ ☁E�E☁E
    if (Intents.Num() == 0 && EnemyTurnDataSys->Observations.Num() > 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: ❁EFailed to generate intents from %d observations!"),
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

// ========== 修正�E�Subsystemに委譲 ==========
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

// ========== 修正�E�Subsystemに委譲 ==========


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
    DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);  // ☁E�E☁E新規追加
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
// 🌟 統吁EPI�E�Eumina提言B3: AdvanceTurnAndRestart - 最終修正版！E
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnIndex);

    //==========================================================================
    // ☁E�E☁EPhase 4: 二重鍵チェチE���E�EdvanceTurnAndRestartでも実施�E�E
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
    CurrentTurnIndex++;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d ↁE%d"),
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

    OnTurnStarted.Broadcast(CurrentTurnIndex);

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnIndex);
}




// ════════════════════════════════════════════════════════════════════
// StartFirstTurn�E��E回ターン開始用�E�E
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnIndex);

    // ☁E�E☁EホットフィチE��ス: Turn 0でのBarrier初期匁E☁E�E☁E
    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            // プレイヤー移動�Eみなので Count=1
            Barrier->StartMoveBatch(1, CurrentTurnIndex);
            UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier: StartMoveBatch with 1 move (player only)"),
                CurrentTurnIndex);
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] TurnActionBarrierSubsystem not found!"),
                CurrentTurnIndex);
        }
    }

    // 既存�EOnTurnStarted通知
    OnTurnStarted.Broadcast(CurrentTurnIndex);

    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// ☁E�E☁EC++統合：ターンフロー制御 ☁E�E☁E
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// EndPlay�E�修正版！E
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// EndPlay�E�修正版！E
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
// OnTurnStartedHandler�E��EチE��フィチE��ス適用版！E
//------------------------------------------------------------------------------
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnIndex)
{
    if (!HasAuthority()) return;

    CurrentTurnIndex = TurnIndex;

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnIndex);

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        CachedPlayerPawn = PC->GetPawn();
        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] CachedPlayerPawn re-cached: %s"),
            TurnIndex, CachedPlayerPawn ? *CachedPlayerPawn->GetName() : TEXT("NULL"));
    }
    else
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] PlayerController not found!"), TurnIndex);
    }

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

            // ★★★ 最適化: GridUtils使用（重複コード削除 2025-11-09）
            // マンハッタン距離計算はGridUtilsを使用
            auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
                {
                    return FGridUtils::ManhattanDistance(A, B);
                };

            // ☁E�E☁E最遠の敵までの距離を計箁E☁E�E☁E
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
                UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ❁EDistanceField has unreachable enemies with Margin=%d"),
                    TurnIndex, Margin);
            }
            else
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ✁EAll enemies reachable with Margin=%d"),
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

    // ☁E�E☁E既存�EObservation・Intent処琁E��変更なし！E☁E�E☁E
    UEnemyAISubsystem* EnemyAISys = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyTurnDataSys = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();

    if (EnemyAISys && CachedPathFinder.IsValid() && CachedPlayerPawn)
    {
        TArray<FEnemyObservation> Observations;
        EnemyAISys->BuildObservations(CachedEnemies, CachedPlayerPawn, CachedPathFinder.Get(), Observations);

        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] BuildObservations completed: %d observations (input enemies=%d)"),
            TurnIndex, Observations.Num(), CachedEnemies.Num());

        if (EnemyTurnDataSys)
        {
            EnemyTurnDataSys->Observations = Observations;
            UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Observations assigned to EnemyTurnDataSubsystem"), TurnIndex);

            CollectIntents();

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] CollectIntents completed: %d intents generated"),
                TurnIndex, EnemyTurnDataSys->Intents.Num());

            int32 AttackCount = 0, MoveCount = 0, OtherCount = 0;
            static const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイチE��ブタグを使用
            static const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;  // ネイチE��ブタグを使用

            for (const FEnemyIntent& Intent : EnemyTurnDataSys->Intents)
            {
                if (Intent.AbilityTag.MatchesTag(AttackTag))
                    ++AttackCount;
                else if (Intent.AbilityTag.MatchesTag(MoveTag))
                    ++MoveCount;
                else
                    ++OtherCount;
            }

            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Intent breakdown: Attack=%d, Move=%d, Other=%d"),
                TurnIndex, AttackCount, MoveCount, OtherCount);
        }
        else
        {
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] EnemyTurnDataSubsystem is NULL!"), TurnIndex);
        }
    }
    else
    {
        if (!EnemyAISys)
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] EnemyAISubsystem is NULL!"), TurnIndex);
        if (!CachedPathFinder.IsValid())
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] CachedPathFinder is invalid!"), TurnIndex);
        if (!CachedPlayerPawn)
            UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] CachedPlayerPawn is NULL!"), TurnIndex);
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnIndex);

    BeginPhase(Phase_Turn_Init);
    BeginPhase(Phase_Player_Wait);
}

void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
{
    //==========================================================================
    // ☁E�E☁EチE��チE��: 経路確誁E
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[✁EROUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

    //==========================================================================
    // (1) 権威チェチE��
    //==========================================================================
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Not authority, skipping"));
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
        *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId, Command.TargetCell.X, Command.TargetCell.Y);

    //==========================================================================
    // (2) TurnId検証�E��E等化�E�E
    //==========================================================================
    if (Command.TurnId != CurrentTurnIndex && Command.TurnId != INDEX_NONE)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
            Command.TurnId, CurrentTurnIndex);
        return;
    }

    //==========================================================================
    // (3) 二重移動チェチE��
    //==========================================================================
    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Move in progress, ignoring command"));
        return;
    }

    if (!WaitingForPlayerInput)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Not waiting for input"));
        return;
    }

    //==========================================================================
    // (4) 3-Tag System: Gateを閉じる
    //==========================================================================
    ApplyWaitInputGate(false);

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
    // (8) EventData構築！Eirection をエンコード！E
    //==========================================================================
    FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove; // Ability.Move
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;

    // Direction をエンコーチE 1000 + (X * 100) + Y
    EventData.EventMagnitude = 1000.0f
        + (Command.Direction.X * 100.0f)
        + (Command.Direction.Y * 1.0f);

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

    //==========================================================================
    // (9) GAS起動！Ebility.Moveを発動！E
    //==========================================================================
    const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

    if (TriggeredCount > 0)
    {
        UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] GAS activated for Move (count=%d)"), TriggeredCount);
        bPlayerMoveInProgress = true;
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
    // ☁E�E☁E(11) 攻撁E��定による刁E��（ヤンガス方式！E
    //==========================================================================
    const FGameplayTag InputMove = RogueGameplayTags::InputTag_Move;  // ネイチE��ブタグを使用

    if (Command.CommandTag.MatchesTag(InputMove))
    {
        // ☁E�E☁E攻撁E��宁E 敵に攻撁E��ンチE��トがあるか確誁E
        const bool bHasAttack = HasAnyAttackIntent();

        UE_LOG(LogTurnManager, Log,
            TEXT("[Turn %d] bHasAttack=%s (同時移勁E%s)"),
            CurrentTurnIndex,
            bHasAttack ? TEXT("TRUE") : TEXT("FALSE"),
            bHasAttack ? TEXT("無効") : TEXT("有効"));

        if (bHasAttack)
        {
            // ☁E�E☁E攻撁E��めE 同時移動しなぁE��逐次実行！E
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected ↁESequential mode (no simultaneous move)"),
                CurrentTurnIndex);

            ExecuteSequentialPhase();  // 攻撁E�E移動を頁E��に実衁E
        }
        else
        {
            // ☁E�E☁E攻撁E��ぁE 同時移動すめE
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] No attacks ↁESimultaneous move mode"),
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
// Helper: ResolveOrSpawnPathFinder�E�フォールバック用�E�E
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnPathFinder()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: World is null"));
        return;
    }

    // 既にセチE��済みならそのまま
    if (IsValid(PathFinder))
    {
        return;
    }

    // シーンに既にあるめE��を使ぁE
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

    // 最後�E手段�E�スポ�Eン�E�通常はGameModeが生成する�Eで、ここ�Eフォールバック�E�E
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
// Helper: ResolveOrSpawnUnitManager�E�フォールバック用�E�E
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnUnitManager()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnUnitManager: World is null"));
        return;
    }

    // 既にセチE��済みならそのまま
    if (IsValid(UnitMgr))
    {
        return;
    }

    // シーンに既にあるめE��を使ぁE
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

    // 最後�E手段�E�スポ�Eン�E�通常はここで生�Eされる！E
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

// GameTurnManagerBase.cpp - line 999�E�既存実裁E��E
void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    if (!HasAuthority()) return;

    // ☁E�E☁E修正: bPlayerMoveInProgressのチェチE��に変更 ☁E�E☁E
    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnIndex);
        return;
    }

    // ☁E�E☁E修正: bTurnContinuingの削除�E�EPlayerMoveInProgressを使用�E�E☁E�E☁E
    bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnIndex);

    // ☁E�E☁E攻撁E��宁E☁E�E☁E
    const bool bHasAttack = HasAnyAttackIntent();

    if (bHasAttack)
    {
        ExecuteSequentialPhase(); // ↁE冁E��でExecutePlayerMove()を呼ぶ
    }
    else
    {
        ExecuteSimultaneousPhase(); // ↁE冁E��でExecutePlayerMove()を呼ぶ
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
    // (1) 敵の移動フェーズ�E�同時実行！E
    //==========================================================================
    ExecuteMovePhase();  // ☁E�E☁E既存�E実裁E��使用�E�Ector 参�Eが正しい�E�E

    // ☁E�E☁E注愁E 移動完亁E�E OnAllMovesFinished() チE��ゲートで通知されめE
}

void AGameTurnManagerBase::ExecuteSimultaneousMoves()
{
    // ExecuteSimultaneousPhase() を呼び出す（エイリアス�E�E
    ExecuteSimultaneousPhase();
}




void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    // ☁E�E☁E権限チェチE���E�サーバ�E限定！E☁E�E☁E
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
// ExecuteSequentialPhase�E�修正版！E
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
// OnPlayerMoveCompleted�E�Eon-Dynamic�E�E- Gameplay.Event.Turn.Ability.Completed 受信晁E
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    //==========================================================================
    // (1) TurnId検証�E�EventMagnitudeから取得！E
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

    // TODO: APシスチE��が実裁E��れたら、以下�Eコメントを解除
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
        // (8a) Gate.Input.Openを�E付与（連続移動許可�E�E
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
        // (8b) Phase.Player.WaitInputを�E付丁E
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

        // フェーズ終亁E�E琁E��忁E��に応じて�E�E
        // EndPlayerPhase();
    }
    */

    //==========================================================================
    // ☁E�E☁E暫定実裁E APシスチE��がなぁE��合�Eフェーズ終亁E
    //==========================================================================
    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnIndex);

    // TODO: APシスチE��実裁E���E削除
    // 暫定的にフェーズ終亁E��忁E��に応じて次のフェーズへ�E�E
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
        if (UTurnCorePhaseManager* PM = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            // ☁E�E☁EOut パラメータで受け取る ☁E�E☁E
            TArray<FResolvedAction> AttackActions;
            PM->ExecuteAttackPhaseWithSlots(
                EnemyTurnData->Intents,
                AttackActions  // ☁EOut パラメータ
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

    //==========================================================================
    // (2) 移動フェーズ�E�攻撁E��！E
    //==========================================================================
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Starting Move Phase (after attacks)"), TurnId);

    ExecuteMovePhase();  // 既存�E移動実行関数

    // ☁E�E☁E注愁E 移動完亁E�E OnAllMovesFinished() チE��ゲートで通知されめE
}


void AGameTurnManagerBase::ExecuteMovePhase()
{
    // ☁E�E☁EUActionExecutorSubsystem は存在しなぁE��めコメントアウチE
    UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ExecuteMovePhase: ActionExecutor not available (class not found)"), CurrentTurnIndex);
    return;
    /*
    UActionExecutorSubsystem* ActionExec = GetWorld()->GetSubsystem<UActionExecutorSubsystem>();
    UEnemyTurnDataSubsystem* EnemyData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>();
    UTurnActionBarrierSubsystem* Barrier = GetWorld()->GetSubsystem<UTurnActionBarrierSubsystem>();

    if (!ActionExec || !EnemyData)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] ExecuteMovePhase: ActionExecutor=%d or EnemyTurnData=%d not found"),
            CurrentTurnIndex,
            ActionExec != nullptr,
            EnemyData != nullptr);

        EndEnemyTurn();
        return;
    }

    if (EnemyData->Intents.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] ExecuteMovePhase: No enemy intents, skipping move phase"),
            CurrentTurnIndex);

        EndEnemyTurn();
        return;
    }
    // ... 残りのコーチE...
    */
}




//------------------------------------------------------------------------------
// ExecutePlayerMove�E�修正版！E
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ExecutePlayerMove�E�完�E修正版！E
//------------------------------------------------------------------------------
// GameTurnManagerBase.cpp - line 1170�E�既存実裁E��E
// ☁E�E☁E変更なし（参老E�Eため全斁E��載！E☁E�E☁E

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
    // ☁E�E☁EルーチE: Subsystem経由�E�推奨・型安�E�E�E
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
                CurrentTurnIndex, bSent ? TEXT("✁EOK") : TEXT("❁EFAILED"));

            if (bSent)
            {
                return; // 成功したらここで終亁E
            }
        }
    }
    */

    //==========================================================================
    // ☁E�E☁EルーチE: 直接GAS経由�E�フォールバック�E�E
    // 問顁E EventMagnitude に TurnId だけを設定すると、GA_MoveBase で
    // Direction が取得できなぁE��以前�E実裁E��は Direction をエンコードしてぁE��、E
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        FGameplayEventData EventData;
        EventData.EventTag = Tag_AbilityMove;
        EventData.Instigator = PlayerPawnNow;
        EventData.Target = PlayerPawnNow;

        //======================================================================
        // ☁E�E☁E修正: Direction めEEventMagnitude にエンコーチE
        // Format: 1000 + (X * 100) + Y
        // 侁E Direction(0, 1) ↁE1001
        //     Direction(-1, 0) ↁE900
        //     Direction(1, -1) ↁE1099
        //======================================================================
        EventData.EventMagnitude = 1000.0f
            + (CachedPlayerCommand.Direction.X * 100.0f)
            + (CachedPlayerCommand.Direction.Y * 1.0f);

        UE_LOG(LogTurnManager, Log,
            TEXT("Turn %d: Sending GameplayEvent %s (Magnitude=%.2f, Direction=(%.0f,%.0f))"),
            CurrentTurnIndex,
            *EventData.EventTag.ToString(),
            EventData.EventMagnitude,
            CachedPlayerCommand.Direction.X,
            CachedPlayerCommand.Direction.Y);

        const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

        if (TriggeredCount > 0)
        {
            UE_LOG(LogTurnManager, Log,
                TEXT("Turn %d: ✁EGA_MoveBase activated (count=%d)"),
                CurrentTurnIndex, TriggeredCount);
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("Turn %d: ❁ENo abilities triggered for %s"),
                CurrentTurnIndex, *EventData.EventTag.ToString());
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
// OnAllMovesFinished�E�Earrierコールバック�E�E
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

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier complete  Eall moves finished"), FinishedTurnId);

    //==========================================================================
    // ☁E�E☁Eセーフティ�E�実行中フラグ/ゲートを確実に解除
    //==========================================================================
    bPlayerMoveInProgress = false;
 

    ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

    //==========================================================================
    // ☁E�E☁E次ターンへ進行（これが OnTurnStarted をBroadcast�E�E
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
        // ☁E�E☁Eリトライ機構（オプション�E�E
        // 0.5秒後に再チェチE��
        //======================================================================
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

        return;  // ☁E�E☁E進行を中止
    }

    //==========================================================================
    // (1) 次のターンへ進む
    //==========================================================================
    AdvanceTurnAndRestart();
}





//------------------------------------------------------------------------------
// Replication Callbacks
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnRep_WaitingForPlayerInput()
{
    UE_LOG(LogTurnManager, Warning, TEXT("[Client] OnRep_WaitingForPlayerInput: %d"),
        WaitingForPlayerInput);

    //==========================================================================
    // 🌟 3-Tag System: クライアント�EでめEateを開く（レプリケーション後！E
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
    // Standalone + Network両対応：ゲートリセチE���E�既存�E琁E��E
    //==========================================================================
    if (UWorld* World = GetWorld())
    {
        if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(World->GetFirstPlayerController()))
        {
            if (WaitingForPlayerInput)
            {
                // 入力ウィンドウが開ぁE�� ↁEゲートをリセチE��
                
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
            // ☁E�E☁EGate だけを閉じる！Ehase は維持E��E
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

    // ☁E�E☁EGate/Phaseタグを付与（既存�EApplyWaitInputGateを流用�E�E
    ApplyWaitInputGate(true);

    // WaitingForPlayerInputフラグを立てめE
    WaitingForPlayerInput = true;

    // ☁E�E☁EStandalone では OnRep が呼ばれなぁE�Eで手動実衁E
    OnRep_WaitingForPlayerInput();
    OnRep_InputWindowId();
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
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[IsInputOpen]FALSE: WaitingForPlayerInput=FALSE (Turn=%d)"),
            CurrentTurnIndex);
        return false;
    }

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        const bool bGateOpen = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        if (!bGateOpen)
        {
            UE_LOG(LogTurnManager, Verbose,
                TEXT("[IsInputOpen]FALSE: Gate_Input_Open=FALSE (Turn=%d)"),
                CurrentTurnIndex);
        }
        return bGateOpen; // Waiting&&Gate のANDは上で拁E��されてぁE��
    }

    UE_LOG(LogTurnManager, Error, TEXT("[IsInputOpen]FALSE: Player ASC not found"));
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
            TEXT("[CanAdvanceTurn] ✁EOK: Turn=%d (Barrier=Quiet, InProgress=0)"),
            TurnId);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CanAdvanceTurn] ❁EBLOCKED: Turn=%d (Barrier=%s, InProgress=%d)"),
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
    // TODO: 階段上りの位置を取得してワーチE
    UE_LOG(LogTurnManager, Warning, TEXT("WarpPlayerToStairUp: Not implemented yet"));
}

//------------------------------------------------------------------------------
// グリチE��コスト設定！Elueprint用�E��E実裁E
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
    // TODO: 実裁E��忁E��E
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

void AGameTurnManagerBase::RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        return;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    PendingMoveReservations.FindOrAdd(ActorKey) = Cell;

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            Occupancy->ReserveCellForActor(Actor, Cell);
        }
    }
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

        UE_LOG(LogTurnManager, Warning,
            TEXT("[ResolvedMove] Player fallback to direct MoveUnit (GA trigger failed)"));
    }

    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = LocalPathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, bIsPlayerUnit);
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
        UE_LOG(LogTurnManager, Warning, TEXT("[ResolvedMove] Player ASC missing"));
        return false;
    }

    const FIntPoint Delta = Action.NextCell - Action.CurrentCell;
    const int32 DirX = FMath::Clamp(Delta.X, -1, 1);
    const int32 DirY = FMath::Clamp(Delta.Y, -1, 1);

    if (DirX == 0 && DirY == 0)
    {
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
            TEXT("[ResolvedMove] Player move ability triggered toward (%d,%d)"),
            Action.NextCell.X, Action.NextCell.Y);
        return true;
    }

    return false;
}

