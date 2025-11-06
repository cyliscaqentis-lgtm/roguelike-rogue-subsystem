// Copyright Epic Games, Inc. All Rights Reserved.

#include "Turn/GameTurnManagerBase.h"
#include "TBSLyraGameMode.h"  // ATBSLyraGameMode用
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonFloorGenerator.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Utility/RogueGameplayTags.h"
#include "Debug/TurnSystemInterfaces.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "Character/EnemyUnitBase.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "Debug/DebugObserverCSV.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Player/PlayerControllerBase.h" 
#include "Net/UnrealNetwork.h"           
#include "Turn/TurnActionBarrierSubsystem.h"          // UTurnActionBarrierSubsystem
#include "Player/LyraPlayerState.h"                   // ALyraPlayerState
#include "Player/LyraPlayerController.h"              // ALyraPlayerController
#include "GameFramework/SpectatorPawn.h"  // ★ これを追加
#include "EngineUtils.h"
#include "Data/MoveInputPayloadBase.h"
#include "Kismet/GameplayStatics.h"
// ★★★ 追加：DistanceFieldSubsystemのinclude ★★★
#include "DistanceFieldSubsystem.h" 
#include "GameModes/LyraExperienceManagerComponent.h"
#include "GameModes/LyraGameState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"

// ★★★ ActionExecutorSubsystem と TurnPhaseManagerComponent のヘッダ
// 注意: これらのクラスが存在しない場合は、.cpp内の該当コードをコメントアウトまたは修正してください
// #include "Turn/ActionExecutorSubsystem.h"  // UActionExecutorSubsystem, FOnActionExecutorCompleted
// #include "Turn/TurnPhaseManagerComponent.h"  // UTurnPhaseManagerComponent

// ★★★ FOnActionExecutorCompleted が定義されていない場合の暫定定義
// ActionExecutorSubsystem.h に定義がある場合は、上記のincludeを有効化してください
#if !defined(FOnActionExecutorCompleted_DEFINED)
DECLARE_DELEGATE(FOnActionExecutorCompleted);
#define FOnActionExecutorCompleted_DEFINED 1
#endif

// ★★★ LogTurnManager と LogTurnPhase は ProjectDiagnostics.cpp で既に定義されているため、ここでは定義しない
// DEFINE_LOG_CATEGORY(LogTurnManager);
// DEFINE_LOG_CATEGORY(LogTurnPhase);
using namespace RogueGameplayTags;

// ★★★ 追加：CVar定義 ★★★
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
}

//------------------------------------------------------------------------------
// ★★★ 新規追加：InitializeTurnSystem ★★★
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// InitializeTurnSystem（修正版）
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// InitializeTurnSystem（修正版）
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
    // Step 2: CachedPathFinder（既に注入済みなので探索不要）
    //==========================================================================
    // PathFinderはHandleDungeonReady()で既に注入されている、またはResolveOrSpawnPathFinder()で解決済み
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
    // Step 4: C++ Dynamic デリゲートバインド（重複防止）
    //==========================================================================
    OnTurnStarted.RemoveDynamic(this, &ThisClass::OnTurnStartedHandler);
    OnTurnStarted.AddDynamic(this, &ThisClass::OnTurnStartedHandler);

    if (UWorld* World = GetWorld())
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

        //======================================================================
        // ★★★ 削除：UTurnInputGuard参照（3タグシステムで不要）
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
    // ★★★ 修正3: ASC Gameplay Event（重複防止）
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
    // ★★★ 削除: BindAbilityCompletion()（重複を避けるため）
    //==========================================================================
    // BindAbilityCompletion();

    //==========================================================================
    // 4. 完了
    //==========================================================================
    bHasInitialized = true;
    UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));

    //==========================================================================
    // 5. 最初のターン開始
    //==========================================================================
    StartFirstTurn();
}







void AGameTurnManagerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 将来的な拡張用（現在は空実装）
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

    // 直参照の"注入"（DungeonSysのみ。PathFinder/UnitMgrはTurnManagerが所有）
    // GameModeからは取得しない（TurnManagerが所有するため）
    DungeonSys = GM->GetDungeonSubsystem();

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: DungeonSys injected (Dgn=%p, PFL/UM will be created on HandleDungeonReady)"),
        static_cast<void*>(DungeonSys.Get()));

    // 準備完了イベントにサブスクライブ
    GM->OnDungeonReady.AddDynamic(this, &AGameTurnManagerBase::HandleDungeonReady);

    UE_LOG(LogTurnManager, Log, TEXT("TurnManager: Subscribed to OnDungeonReady"));
}

void AGameTurnManagerBase::HandleDungeonReady()
{
    // DungeonSysは必須（GameModeから注入済み）
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

    // ★ InitializeTurnSystem は"ここだけ"で呼ぶ
    InitializeTurnSystem();
}



// ★★★ 赤ペン修正5：OnExperienceLoaded の実装 ★★★
void AGameTurnManagerBase::OnExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] ========== EXPERIENCE READY =========="));
    UE_LOG(LogTurnManager, Warning, TEXT("[OnExperienceLoaded] Experience: %s"),
        Experience ? *Experience->GetName() : TEXT("NULL"));

    // ★ Experience完了後はInitializeTurnSystemを呼ばない（HandleDungeonReadyからのみ呼ぶ）
    // InitializeTurnSystem()はHandleDungeonReady()からのみ呼ばれる
}

void AGameTurnManagerBase::OnRep_CurrentTurnId()
{
    // ★★★ クライアント: ターンUI更新/ログ（同期） ★★★
    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] Client: TurnId replicated to %d"), CurrentTurnId);
    // Blueprintイベント発火（任意: OnTurnAdvanced.Broadcast(CurrentTurnId);）
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
                EventData.EventTag = RogueGameplayTags::GameplayEvent_Turn_StartMove;  // ネイティブタグを使用
                ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
                UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: Player GAS Turn.StartMove fired"));
            }
        }

        // Move Batch / Subsystem / CollectEnemies
        // ★★★ エラー該当行439: StartEnemyMoveBatch() 未実装 → コメントアウト ★★★
        // if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
        // {
        //     TArray<AActor*> EnemiesLocal;
        //     GetCachedEnemies(EnemiesLocal);
        //     // AI
        //     EnemyAI->StartEnemyMoveBatch(EnemiesLocal, TurnId);
        //     UE_LOG(LogTurnManager, Log, TEXT("GameTurnManager: EnemyAI StartEnemyMoveBatch for %d enemies"), EnemiesLocal.Num());
        // }

        // ★★★ エラー該当行447: PhaseManager->StartPhase(ETurnPhase::Move) 未実装 → コメントアウト ★★★
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
// ★★★ 新規関数（Experience完了時のコールバック） ★★★


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
    Tag_AbilityMove = RogueGameplayTags::Ability_Move;               // "Ability.Move"
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
    // メンバ変数PathFinderを優先（GameModeから注入されたもの）
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

    // フォールバック：シーン内を探索
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        AGridPathfindingLibrary* PF = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        const_cast<AGameTurnManagerBase*>(this)->CachedPathFinder = PF;
        const_cast<AGameTurnManagerBase*>(this)->PathFinder = PF;
        return PF;
    }

    // 最後の手段：GameModeから取得を試みる
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

// ========== 修正：Subsystemに委譲 ==========
void AGameTurnManagerBase::BuildAllObservations()
{
    SCOPED_NAMED_EVENT(BuildAllObservations, FColor::Orange);

    if (!EnemyAISubsystem)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] BuildAllObservations: EnemyAISubsystem not found"), CurrentTurnIndex);
        return;
    }

    // 敵リストの変換（TObjectPtr → 生ポインタ）
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
    OnPlayerInputReceived.Broadcast();
    // 二重進行防止：ここで待ちゲートを閉じてから継続
    if (WaitingForPlayerInput)
    {
        WaitingForPlayerInput = false;
        ApplyWaitInputGate(false);
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

// ========== 修正：PhaseManagerに委譲 ==========
void AGameTurnManagerBase::BeginPhase(FGameplayTag PhaseTag)
{
    CurrentPhase = PhaseTag;
    PhaseStartTime = FPlatformTime::Seconds();
    UE_LOG(LogTurnPhase, Log, TEXT("PhaseStart:%s"), *PhaseTag.ToString());

    if (PhaseTag == Phase_Player_Wait)
    {
        // 入力待ち開始：フラグとゲートを**揃えて**開く
        WaitingForPlayerInput = true;
        ApplyWaitInputGate(true);      // ← 追加（重要）
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

    // DebugObserver への通知（既存処理）
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
        // 入力待ち終了：フラグとゲートを**揃えて**閉じる
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

    // ★★★ APawnで検索（includeが不要） ★★★
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), Found);

    UE_LOG(LogTurnManager, Warning, TEXT("[CollectEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = RogueGameplayTags::Faction_Enemy;  // ネイティブタグを使用

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

    // ★★★ 重要：CachedEnemiesを完全にクリアしてから再構築 ★★★
    CachedEnemies.Empty();

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

        // ★★★ 修正：TeamId == 2 または 255 を敵として扱う ★★★
        // ログから全ての敵が TeamID=255 であることが判明
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

            // ★★★ デバッグ：最初の3体と最後の1体のみ詳細ログ ★★★
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

    // ★★★ エラー検出：敵が1体も見つからない場合 ★★★
    if (CachedEnemies.Num() == 0 && Found.Num() > 1)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[CollectEnemies] ❌ Failed to collect any enemies from %d Pawns!"),
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
    // ★★★ 警告：予想より少ない敵 ★★★
    else if (CachedEnemies.Num() > 0 && CachedEnemies.Num() < 32 && Found.Num() >= 32)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CollectEnemies] ⚠️ Collected %d enemies, but expected around 32 from %d Pawns"),
            CachedEnemies.Num(), Found.Num());
    }
}




// ========== 修正：Subsystemに委譲 ==========
// GameTurnManagerBase.cpp

void AGameTurnManagerBase::CollectIntents_Implementation()
{
    // ★★★ 修正1：Subsystemを毎回取得（キャッシュ無効化対策） ★★★
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

    // ★★★ 修正2：Observationsの存在確認 + 自動リカバリー ★★★
    if (EnemyTurnDataSys->Observations.Num() == 0)
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] CollectIntents: No observations available (Enemies=%d) - Auto-generating..."),
            CurrentTurnIndex, CachedEnemies.Num());

        // ★★★ 自動リカバリー：Observationsを生成 ★★★
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

    // ★★★ 修正3：CachedEnemiesを直接使用 ★★★
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents input: Observations=%d, Enemies=%d"),
        CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

    // ★★★ 修正4：サイズ一致チェック ★★★
    if (EnemyTurnDataSys->Observations.Num() != CachedEnemies.Num())
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: Size mismatch! Observations=%d != Enemies=%d"),
            CurrentTurnIndex, EnemyTurnDataSys->Observations.Num(), CachedEnemies.Num());

        // ★★★ リカバリー：Observationsを再生成 ★★★
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

    // ★★★ 修正5：EnemyAISubsystem::CollectIntents実行 ★★★
    TArray<FEnemyIntent> Intents;
    EnemyAISys->CollectIntents(EnemyTurnDataSys->Observations, CachedEnemies, Intents);

    // ★★★ 修正6：Subsystemに格納 ★★★
    EnemyTurnDataSys->Intents = Intents;

    // ★★★ Intent内訳の計算 ★★★
    int32 AttackCount = 0, MoveCount = 0, WaitCount = 0, OtherCount = 0;

    // ★★★ 修正7：GameplayTagをキャッシュ（パフォーマンス改善） ★★★
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

    // ★★★ 修正8：ログレベルをWarningに変更（重要なイベント） ★★★
    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] CollectIntents -> %d intents (Attack=%d, Move=%d, Wait=%d, Other=%d)"),
        CurrentTurnIndex, Intents.Num(), AttackCount, MoveCount, WaitCount, OtherCount);

    // ★★★ 修正9：Intent生成失敗の詳細ログ ★★★
    if (Intents.Num() == 0 && EnemyTurnDataSys->Observations.Num() > 0)
    {
        UE_LOG(LogTurnManager, Error,
            TEXT("[Turn %d] CollectIntents: ❌ Failed to generate intents from %d observations!"),
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

// ========== 修正：Subsystemに委譲 ==========
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

// ========== 修正：Subsystemに委譲 ==========


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
    DOREPLIFETIME(AGameTurnManagerBase, InputWindowId);  // ★★★ 新規追加
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
// 🌟 統合API（Lumina提言B3: AdvanceTurnAndRestart - 最終修正版）
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::AdvanceTurnAndRestart()
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Current Turn: %d"), CurrentTurnIndex);

    //==========================================================================
    // ★★★ Phase 4: 二重鍵チェック（AdvanceTurnAndRestartでも実施）
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
    CurrentTurnIndex++;

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] Turn advanced: %d → %d"),
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

    OnTurnStarted.Broadcast(CurrentTurnIndex);

    UE_LOG(LogTurnManager, Log,
        TEXT("[AdvanceTurnAndRestart] OnTurnStarted broadcasted for turn %d"),
        CurrentTurnIndex);
}




// ════════════════════════════════════════════════════════════════════
// StartFirstTurn（初回ターン開始用）
// ════════════════════════════════════════════════════════════════════

void AGameTurnManagerBase::StartFirstTurn()
{
    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: Starting first turn (Turn %d)"), CurrentTurnIndex);

    // ★★★ ホットフィックス: Turn 0でのBarrier初期化 ★★★
    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            // プレイヤー移動のみなので Count=1
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

    // 既存のOnTurnStarted通知
    OnTurnStarted.Broadcast(CurrentTurnIndex);

    UE_LOG(LogTurnManager, Log, TEXT("StartFirstTurn: OnTurnStarted broadcasted for turn %d"), CurrentTurnIndex);
}

//------------------------------------------------------------------------------
// ★★★ C++統合：ターンフロー制御 ★★★
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// EndPlay（修正版）
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// EndPlay（修正版）
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
// OnTurnStartedHandler（ホットフィックス適用版）
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

            // ★★★ マンハッタン距離計算ラムダ ★★★
            auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
                {
                    return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
                };

            // ★★★ 最遠の敵までの距離を計算 ★★★
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
                UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] ❌ DistanceField has unreachable enemies with Margin=%d"),
                    TurnIndex, Margin);
            }
            else
            {
                UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ✅ All enemies reachable with Margin=%d"),
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

    // ★★★ 既存のObservation・Intent処理（変更なし） ★★★
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
            static const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;  // ネイティブタグを使用
            static const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;  // ネイティブタグを使用

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
    // ★★★ デバッグ: 経路確認
    //==========================================================================
    UE_LOG(LogTurnManager, Warning, TEXT("[✅ ROUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

    //==========================================================================
    // (1) 権威チェック
    //==========================================================================
    if (!HasAuthority())
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Not authority, skipping"));
        return;
    }

    UE_LOG(LogTurnManager, Log, TEXT("[GameTurnManager] OnPlayerCommandAccepted: Tag=%s, TurnId=%d, WindowId=%d, TargetCell=(%d,%d)"),
        *Command.CommandTag.ToString(), Command.TurnId, Command.WindowId, Command.TargetCell.X, Command.TargetCell.Y);

    //==========================================================================
    // (2) TurnId検証（冪等化）
    //==========================================================================
    if (Command.TurnId != CurrentTurnIndex && Command.TurnId != INDEX_NONE)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[GameTurnManager] Command rejected - TurnId mismatch or invalid (%d != %d)"),
            Command.TurnId, CurrentTurnIndex);
        return;
    }

    //==========================================================================
    // (3) 二重移動チェック
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
    // (8) EventData構築（Direction をエンコード）
    //==========================================================================
    FGameplayEventData EventData;
    EventData.EventTag = Tag_AbilityMove; // Ability.Move
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;

    // Direction をエンコード: 1000 + (X * 100) + Y
    EventData.EventMagnitude = 1000.0f
        + (Command.Direction.X * 100.0f)
        + (Command.Direction.Y * 1.0f);

    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] EventData prepared - Tag=%s, Magnitude=%.2f, Direction=(%.0f,%.0f)"),
        *EventData.EventTag.ToString(), EventData.EventMagnitude,
        Command.Direction.X, Command.Direction.Y);

    //==========================================================================
    // (9) GAS起動（Ability.Moveを発動）
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
            // ★★★ 攻撃あり: 同時移動しない（逐次実行）
            UE_LOG(LogTurnManager, Warning,
                TEXT("[Turn %d] Attacks detected → Sequential mode (no simultaneous move)"),
                CurrentTurnIndex);

            ExecuteSequentialPhase();  // 攻撃→移動を順番に実行
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
            TEXT("[GameTurnManager] ❌ Command tag does NOT match InputTag.Move or InputTag.Turn (Tag=%s)"),
            *Command.CommandTag.ToString());
    }
}


//------------------------------------------------------------------------------
// Helper: ResolveOrSpawnPathFinder（フォールバック用）
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnPathFinder()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnPathFinder: World is null"));
        return;
    }

    // 既にセット済みならそのまま
    if (IsValid(PathFinder))
    {
        return;
    }

    // シーンに既にあるやつを使う
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

    // 最後の手段：スポーン（通常はGameModeが生成するので、ここはフォールバック）
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
// Helper: ResolveOrSpawnUnitManager（フォールバック用）
//------------------------------------------------------------------------------

void AGameTurnManagerBase::ResolveOrSpawnUnitManager()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnManager, Error, TEXT("ResolveOrSpawnUnitManager: World is null"));
        return;
    }

    // 既にセット済みならそのまま
    if (IsValid(UnitMgr))
    {
        return;
    }

    // シーンに既にあるやつを使う
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

    // 最後の手段：スポーン（通常はここで生成される）
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

// GameTurnManagerBase.cpp - line 999（既存実装）
void AGameTurnManagerBase::ContinueTurnAfterInput()
{
    if (!HasAuthority()) return;

    // ★★★ 修正: bPlayerMoveInProgressのチェックに変更 ★★★
    if (bPlayerMoveInProgress)
    {
        UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ContinueTurnAfterInput: Move in progress"), CurrentTurnIndex);
        return;
    }

    // ★★★ 修正: bTurnContinuingの削除（bPlayerMoveInProgressを使用） ★★★
    bPlayerMoveInProgress = true;

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] ContinueTurnAfterInput: Starting phase"), CurrentTurnIndex);

    // ★★★ 攻撃判定 ★★★
    const bool bHasAttack = HasAnyAttackIntent();

    if (bHasAttack)
    {
        ExecuteSequentialPhase(); // → 内部でExecutePlayerMove()を呼ぶ
    }
    else
    {
        ExecuteSimultaneousPhase(); // → 内部でExecutePlayerMove()を呼ぶ
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
    // (1) 敵の移動フェーズ（同時実行）
    //==========================================================================
    ExecuteMovePhase();  // ★★★ 既存の実装を使用（Actor 参照が正しい）

    // ★★★ 注意: 移動完了は OnAllMovesFinished() デリゲートで通知される
}

void AGameTurnManagerBase::ExecuteSimultaneousMoves()
{
    // ExecuteSimultaneousPhase() を呼び出す（エイリアス）
    ExecuteSimultaneousPhase();
}




void AGameTurnManagerBase::OnSimultaneousPhaseCompleted()
{
    // ★★★ 権限チェック（サーバー限定） ★★★
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
// ExecuteSequentialPhase（修正版）
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
// OnPlayerMoveCompleted（Non-Dynamic） - Gameplay.Event.Turn.Ability.Completed 受信時
//------------------------------------------------------------------------------

void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    //==========================================================================
    // (1) TurnId検証（EventMagnitudeから取得）
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
    // (3) WaitingForPlayerInput = false
    //==========================================================================
    WaitingForPlayerInput = false;
    OnRep_WaitingForPlayerInput();  // Standaloneでは手動実行

    //==========================================================================
    // (4) bPlayerMoveInProgress = false
    //==========================================================================
    bPlayerMoveInProgress = false;

    //==========================================================================
    // ★★★ (5) 削除: TurnManagerからInProgressタグを触らない
    //==========================================================================
    // MarkMoveInProgress(false);  // 削除

    //==========================================================================
    // ★★★ (6) Phase.Player.WaitInputタグを剥奪（フェーズ終了）
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        // ★★★ 修正: 正しいタグ名を使用
        ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);

        UE_LOG(LogTurnManager, Log,
            TEXT("Turn %d: Phase.Player.WaitInput tag removed"), CurrentTurnIndex);
    }

    //==========================================================================
    // ★★★ (7) 削除: Barrier::NotifyMoveFinishedは呼ばない
    // GAのEndAbility()内でCompleteAction()を呼ぶ
    //==========================================================================
    // if (UWorld* World = GetWorld())
    // {
    //     if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
    //     {
    //         Barrier->NotifyMoveFinished(PlayerPawn, CurrentTurnIndex);  // 削除
    //     }
    // }

    //==========================================================================
    // ★★★ Phase 5: AP残量確認とGate再オープン
    //==========================================================================

    // TODO: APシステムが実装されたら、以下のコメントを解除
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
        // (8a) Gate.Input.Openを再付与（連続移動許可）
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
        // (8b) Phase.Player.WaitInputを再付与
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

        // フェーズ終了処理（必要に応じて）
        // EndPlayerPhase();
    }
    */

    //==========================================================================
    // ★★★ 暫定実装: APシステムがない場合はフェーズ終了
    //==========================================================================
    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: Move completed, ending player phase (AP system not implemented)"),
        CurrentTurnIndex);

    // TODO: APシステム実装後は削除
    // 暫定的にフェーズ終了（必要に応じて次のフェーズへ）
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
            // ★★★ Out パラメータで受け取る ★★★
            TArray<FResolvedAction> AttackActions;
            PM->ExecuteAttackPhaseWithSlots(
                EnemyTurnData->Intents,
                AttackActions  // ★ Out パラメータ
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
    // (2) 移動フェーズ（攻撃後）
    //==========================================================================
    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Starting Move Phase (after attacks)"), TurnId);

    ExecuteMovePhase();  // 既存の移動実行関数

    // ★★★ 注意: 移動完了は OnAllMovesFinished() デリゲートで通知される
}


void AGameTurnManagerBase::ExecuteMovePhase()
{
    // ★★★ UActionExecutorSubsystem は存在しないためコメントアウト
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
    // ... 残りのコード ...
    */
}




//------------------------------------------------------------------------------
// ExecutePlayerMove（修正版）
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ExecutePlayerMove（完全修正版）
//------------------------------------------------------------------------------
// GameTurnManagerBase.cpp - line 1170（既存実装）
// ★★★ 変更なし（参考のため全文掲載） ★★★

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
    // ★★★ ルートA: Subsystem経由（推奨・型安全）
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
                CurrentTurnIndex, bSent ? TEXT("✅ OK") : TEXT("❌ FAILED"));

            if (bSent)
            {
                return; // 成功したらここで終了
            }
        }
    }
    */

    //==========================================================================
    // ★★★ ルートB: 直接GAS経由（フォールバック）
    // 問題: EventMagnitude に TurnId だけを設定すると、GA_MoveBase で
    // Direction が取得できない。以前の実装では Direction をエンコードしていた。
    //==========================================================================
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        FGameplayEventData EventData;
        EventData.EventTag = Tag_AbilityMove;
        EventData.Instigator = PlayerPawnNow;
        EventData.Target = PlayerPawnNow;

        //======================================================================
        // ★★★ 修正: Direction を EventMagnitude にエンコード
        // Format: 1000 + (X * 100) + Y
        // 例: Direction(0, 1) → 1001
        //     Direction(-1, 0) → 900
        //     Direction(1, -1) → 1099
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
                TEXT("Turn %d: ✅ GA_MoveBase activated (count=%d)"),
                CurrentTurnIndex, TriggeredCount);
        }
        else
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("Turn %d: ❌ No abilities triggered for %s"),
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
// OnAllMovesFinished（Barrierコールバック）
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

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Barrier complete — all moves finished"), FinishedTurnId);

    //==========================================================================
    // ★★★ セーフティ：実行中フラグ/ゲートを確実に解除
    //==========================================================================
    bPlayerMoveInProgress = false;
 

    ApplyWaitInputGate(false);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] All flags/gates cleared, advancing turn"), FinishedTurnId);

    //==========================================================================
    // ★★★ 次ターンへ進行（これが OnTurnStarted をBroadcast）
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
        // ★★★ リトライ機構（オプション）
        // 0.5秒後に再チェック
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

        return;  // ★★★ 進行を中止
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
    // 🌟 3-Tag System: クライアント側でもGateを開く（レプリケーション後）
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
    // Standalone + Network両対応：ゲートリセット（既存処理）
    //==========================================================================
    if (UWorld* World = GetWorld())
    {
        if (APlayerControllerBase* PC = Cast<APlayerControllerBase>(World->GetFirstPlayerController()))
        {
            if (WaitingForPlayerInput)
            {
                // 入力ウィンドウが開いた → ゲートをリセット
                
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
            // ★★★ Gate だけを閉じる（Phase は維持）
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
    // サーバー専用
    if (!HasAuthority())
    {
        return;
    }

    // ★★★ WindowIdをインクリメント
    ++InputWindowId;

    UE_LOG(LogTurnManager, Log,
        TEXT("[WindowId] Opened: Turn=%d WindowId=%d"),
        CurrentTurnIndex, InputWindowId);

    // ★★★ Gate/Phaseタグを付与（既存のApplyWaitInputGateを流用）
    ApplyWaitInputGate(true);

    // WaitingForPlayerInputフラグを立てる
    WaitingForPlayerInput = true;

    // ★★★ Standalone では OnRep が呼ばれないので手動実行
    OnRep_WaitingForPlayerInput();
    OnRep_InputWindowId();
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
        return bGateOpen; // Waiting&&Gate のANDは上で担保されている
    }

    UE_LOG(LogTurnManager, Error, TEXT("[IsInputOpen]FALSE: Player ASC not found"));
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
            TEXT("[CanAdvanceTurn] ✅ OK: Turn=%d (Barrier=Quiet, InProgress=0)"),
            TurnId);
    }
    else
    {
        UE_LOG(LogTurnManager, Warning,
            TEXT("[CanAdvanceTurn] ❌ BLOCKED: Turn=%d (Barrier=%s, InProgress=%d)"),
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
    // TODO: 階段上りの位置を取得してワープ
    UE_LOG(LogTurnManager, Warning, TEXT("WarpPlayerToStairUp: Not implemented yet"));
}

//------------------------------------------------------------------------------
// グリッドコスト設定（Blueprint用）の実装
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
    // TODO: 実装が必要
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






