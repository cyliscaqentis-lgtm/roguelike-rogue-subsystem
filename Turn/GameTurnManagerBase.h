// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"
#include "Debug/TurnSystemInterfaces.h"
#include "Turn/TurnSystemTypes.h"
#include "Utility/TurnAuthorityUtils.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/DungeonConfigAsset.h"
#include "../ProjectDiagnostics.h"
#include "GameTurnManagerBase.generated.h"

class UEnemyTurnDataSubsystem;
class UEnemyAISubsystem;
class UTurnActionBarrierSubsystem;
class UAbilitySystemComponent;
class AGridPathfindingLibrary;
class AUnitBase;
class AUnitManager;
class URogueDungeonSubsystem;
class UDebugObserverCSV;
class UTurnCorePhaseManager;
class UTurnCommandHandler;
class UTurnEventDispatcher;
class UTurnDebugSubsystem;
class UTurnFlowCoordinator;
class UPlayerInputProcessor;
struct FGameplayEventData;
class ULyraExperienceDefinition;
class ULyraExperienceManagerComponent;
class ALyraGameState;
class ADungeonFloorGenerator;
class UDungeonConfigAsset;
struct FTurnContext;
class ATBSLyraGameMode;

// NOTE: Enemy phase patch ID
static constexpr int32 kEnemyPhasePatchID = 20251030;

/**
 * AGameTurnManagerBase
 *
 * Main turn manager class for the roguelike dungeon game.
 * Manages turn flow, player input, enemy AI, and phase execution.
 */
UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    //==========================================================================
    // Turn Lifecycle / Replication
    //==========================================================================

    AGameTurnManagerBase();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    //==========================================================================
    // Grid Debug Initialization
    //==========================================================================

    void InitializeGridDebug();

    //==========================================================================
    // Replication Notifies
    //==========================================================================

    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    UFUNCTION()
    void OnRep_InputWindowId();

    UFUNCTION()
    void OnRep_CurrentTurnId();

    //==========================================================================
    // Turn ID / Window ID Accessors
    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const { return InputWindowId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnIndex() const { return CurrentTurnIndex; }

    //==========================================================================
    // PathFinder Accessor
    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    AGridPathfindingLibrary* GetCachedPathFinder() const;

    //==========================================================================
    // 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�E繧�E�繧�E�繝�EΒ邨�E�蜷・PI
    //==========================================================================

    /** 繝繝ｳ繧�E�繝ｧ繝ｳSubsystem縺�E�縺�E�繧�E�繧�E�繧�E�繧�E� */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    URogueDungeonSubsystem* GetDungeonSystem() const { return DungeonSystem; }

    /** Access the FloorGenerator */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    ADungeonFloorGenerator* GetFloorGenerator() const;

    /** Ensure floor is generated (e.g., for a specific floor index) */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool EnsureFloorGenerated(int32 FloorIndex);

    /** Advance to the next floor (FloorIndex++) */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool NextFloor();

    /** Warp player to the stair-up location */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    void WarpPlayerToStairUp(AActor* Player);

    //==========================================================================
    // Barrier Related
    //==========================================================================

    UFUNCTION()
    void OnAllMovesFinished(int32 FinishedTurnId);

    //==========================================================================
    // Turn System Initialization
    //==========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|System")
    void InitializeTurnSystem();

    //==========================================================================
    // Turn Flow API
    //==========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void ContinueTurnAfterInput();

    UFUNCTION(BlueprintCallable, Category = "Turn|Player")
    void NotifyPlayerInputReceived();

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void AdvanceTurnAndRestart();

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void StartFirstTurn();

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void StartTurn();

    UFUNCTION(BlueprintCallable, Category = "Turn", meta = (BlueprintAuthorityOnly))
    void BeginPhase(FGameplayTag PhaseTag);

    UFUNCTION(BlueprintCallable, Category = "Turn", meta = (BlueprintAuthorityOnly))
    void EndPhase(FGameplayTag PhaseTag);

    //==========================================================================
    // Utilities
    //==========================================================================

    /**
     * ★★★ Phase 5補完: 残留InProgressタグの強制クリーンアップ（2025-11-09） ★★★
     *
     * 全ユニット（プレイヤー + 敵）のState.Action.InProgressタグを強制除去
     *
     * 用途:
     * - Barrier完了後のターン終了時に呼び出し
     * - タグがスタックしてターン進行が止まるのを防ぐ
     * - 通常は不要だが、予期しないエラーで残留した場合の保険
     *
     * ログ出力:
     * - Before: クリーンアップ前のInProgressタグ数
     * - After: クリーンアップ後（通常は0）
     */
    void ClearResidualInProgressTags();

    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void BuildAllObservations();

    UFUNCTION(BlueprintCallable, Category = "Turn|Utility")
    bool SendGameplayEventWithResult(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload);

    UFUNCTION(BlueprintCallable, Category = "Turn|Utility")
    void SendGameplayEvent(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload);

    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    FEnemyObservation BP_BuildObservationForEnemy(AActor* Enemy);

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APlayerController* GetPlayerController_TBS() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APawn* GetPlayerPawnCachedOrFetch();

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APawn* GetPlayerPawn() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    AActor* GetPlayerActor() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    FBoardSnapshot CaptureSnapshot() const;

    UFUNCTION(BlueprintPure, Category = "Turn|State")
    void GetCachedEnemies(TArray<AActor*>& OutEnemies) const;

    UFUNCTION(BlueprintPure, Category = "Turn|State")
    bool TryGetEnemyIntent(AActor* Enemy, FEnemyIntent& OutIntent) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Yangus")
    bool HasAnyAttackIntent() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Yangus")
    bool HasAnyMoveIntent() const;

    //==========================================================================
    // Player・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Player")
    void OnPlayerCommandAccepted(const FPlayerCommand& Command);
    virtual void OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command);

    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Turn|Player")
    bool ShouldSimulMove(const FPlayerCommand& Command, const FBoardSnapshot& Snapshot) const;
    virtual bool ShouldSimulMove_Implementation(const FPlayerCommand& Command, const FBoardSnapshot& Snapshot) const;

    //==========================================================================
    // Enemy Pipeline・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Turn|Enemy")
    void CollectEnemies();
    virtual void CollectEnemies_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void BuildObservations();
    virtual void BuildObservations_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void CollectIntents();
    virtual void CollectIntents_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    FEnemyIntent ComputeEnemyIntent(AActor* Enemy, const FEnemyObservation& Observation);
    virtual FEnemyIntent ComputeEnemyIntent_Implementation(AActor* Enemy, const FEnemyObservation& Observation);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void ExecuteEnemyMoves();
    virtual void ExecuteEnemyMoves_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void ExecuteEnemyAttacks();
    virtual void ExecuteEnemyAttacks_Implementation();

    //==========================================================================
    // Yangus Ability Related
    //==========================================================================


    UFUNCTION(BlueprintCallable, Category = "Turn|Yangus")
    void NotifyAbilityStarted();

    UFUNCTION(BlueprintCallable, Category = "Turn|Yangus")
    void OnAbilityCompleted();

    UFUNCTION(BlueprintImplementableEvent, Category = "Turn|Yangus")
    void OnAllAbilitiesCompleted();

    void ExecuteSimultaneousMoves();

    //==========================================================================
    // Ally Turn・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Ally")
    void BuildAllyIntents(UPARAM(ref) FTurnContext& Context);
    virtual void BuildAllyIntents_Implementation(FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Ally")
    void ExecuteAllyActions(UPARAM(ref) FTurnContext& Context);
    virtual void ExecuteAllyActions_Implementation(FTurnContext& Context);

    //==========================================================================
    // Move Pipeline・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Move")
    void BuildSimulBatch();
    virtual void BuildSimulBatch_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Move")
    void CommitSimulStep(const FSimulBatch& Batch);
    virtual void CommitSimulStep_Implementation(const FSimulBatch& Batch);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Move")
    void ResolveConflicts(UPARAM(ref) TArray<FPendingMove>& Moves);
    virtual void ResolveConflicts_Implementation(TArray<FPendingMove>& Moves);

    //==========================================================================
    // Interrupt System・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Interrupt")
    void CheckInterruptWindow(const FPendingMove& PlayerMove);
    virtual void CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove);

    //==========================================================================
    // Ability Completion・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Utility")
    void WaitForAbilityCompletion();
    virtual void WaitForAbilityCompletion_Implementation();

    //==========================================================================
    // System Hooks・・lueprintNativeEvent・・    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnCombineSystemUpdate(const FTurnContext& Context);
    virtual void OnCombineSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnBreedingSystemUpdate(const FTurnContext& Context);
    virtual void OnBreedingSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnPotSystemUpdate(const FTurnContext& Context);
    virtual void OnPotSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnTrapSystemUpdate(const FTurnContext& Context);
    virtual void OnTrapSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnStatusEffectSystemUpdate(const FTurnContext& Context);
    virtual void OnStatusEffectSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnItemSystemUpdate(const FTurnContext& Context);
    virtual void OnItemSystemUpdate_Implementation(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void GetEnemyIntentsBP(TArray<FEnemyIntent>& OutIntents) const;
    virtual void GetEnemyIntentsBP_Implementation(TArray<FEnemyIntent>& OutIntents) const;

    

    //==========================================================================
    // Hook Collections
    //==========================================================================

    UPROPERTY(EditAnywhere, Instanced, Category = "Turn|Hooks|Debug")
    TArray<TObjectPtr<UObject>> DebugObservers;

    // Current turn index (used for legacy compatibility)
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    int32 CurrentTurnIndex = 0;

    //==========================================================================
    // 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�E繧�E�繧�E�繝�EΒ邨�E�蜷医・繝ｭ繝代ユ繧�E�
    //==========================================================================

    /** 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�ESubsystem縺�E�縺�E�蜿ら�E */
    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSystem = nullptr;

    /** Initial floor configuration asset */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    TSoftObjectPtr<UDungeonConfigAsset> InitialFloorConfig;

    /** Starting floor index */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    int32 StartFloorIndex = 0;

    /** Current floor index */
    UPROPERTY(BlueprintReadOnly, Category="Dungeon|State")
    int32 CurrentFloorIndex = 0;

    //==========================================================================
    // 笘�EAP System・医ぁE���E�繧�E�繝ｧ繝ｳ繝昴ぁE��ｳ繝亥宛�E・    //==========================================================================
    
    /** 1繧�E�繝ｼ繝ｳ縺めE��繧翫・譛螟ｧAP */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAPPerTurn = 1;
    
    /** Current player AP */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAP = 0;

    /** Flag to queue enemy phase (for AP consumption) */
    UPROPERTY()
    bool bEnemyPhaseQueued = false;


    //==========================================================================
    // Turn State
    //==========================================================================

    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    FGameplayTag CurrentPhase;

    UPROPERTY(BlueprintReadWrite, Category = "Turn|State")
    FPlayerCommand CachedPlayerCommand;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    FSimulBatch CurrentSimulBatch;

    //==========================================================================
    // BP Exposed Members
    //==========================================================================

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<APawn> CachedPlayerPawn = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<UDebugObserverCSV> DebugObserverCSV = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<UEnemyTurnDataSubsystem> EnemyTurnData = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TArray<TObjectPtr<AActor>> CachedEnemies;

    /** ターン冁E��使用する、収雁E�Eソート済みの安定した敵リスチE*/
    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    TArray<TObjectPtr<AActor>> CachedEnemiesForTurn;

    /** プレイヤーの移動意図�E�次のResolveまで保持�E�E*/
    UPROPERTY()
    FEnemyIntent PendingPlayerIntent;

    UPROPERTY()
    bool bPendingPlayerIntent = false;

    /** 今ターン中に解決済みの移動�E予紁E��Ector→Cell�E�E*/
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, FIntPoint> PendingMoveReservations;

    /** 敵リスト�Eリビジョン番号、EollectEnemiesが完亁E��るたびにインクリメンチE*/
    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    int32 EnemiesRevision = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    int32 InputWindowId = 0;

 

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    int32 PendingAbilityCount = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    FVector CachedInputDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_WaitingForPlayerInput, Category = "Turn|State")
    bool WaitingForPlayerInput = false;

    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Turn|State")
    bool bPlayerMoveInProgress = false;

    /** ターン開始済みフラグ */
    UPROPERTY()
    bool bTurnStarted = false;

    /** Pawn未確定時の遅延オープンフラグ */
    UPROPERTY()
    bool bDeferOpenOnPossess = false;

    /** PathFinder初期化完了フラグ */
    UPROPERTY()
    bool bPathReady = false;

    /** ユニットスポーン完了フラグ */
    UPROPERTY()
    bool bUnitsSpawned = false;

    /** プレイヤー所持完了フラグ */
    UPROPERTY()
    bool bPlayerPossessed = false;

    //==========================================================================
    // 繝代ヵ繧�E�繝ｼ繝槭Φ繧�E�譛驕ｩ蛹・    //==========================================================================

    UPROPERTY(Transient)
    TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;

    //==========================================================================
    // 繝繝ｳ繧�E�繝ｧ繝ｳ貁E���E�螳御�E�・ぁE��吶Φ繝医ワ繝ｳ繝峨Λ�E育峩蜿ら�E豕ｨ蜈･・・    //==========================================================================

    UFUNCTION()
    void HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys); // 竊�E縺薙！E���E�縺�E�縺�E� InitializeTurnSystem() 繧貞他縺�E�

    UPROPERTY()
    TObjectPtr<AGridPathfindingLibrary> PathFinder = nullptr;

    UPROPERTY()
    TObjectPtr<AUnitManager> UnitMgr = nullptr;

    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSys = nullptr;

    /** Phase start time for duration tracking */
    UPROPERTY(BlueprintReadOnly, Category = "Turn|Phase")
    double PhaseStartTime = 0.0;

    /** Toggle State.Action.InProgress tag on the player ASC */
    UFUNCTION(BlueprintCallable, Category="Turn|State")
    void MarkMoveInProgress(bool bInProgress);

    /** 解決済み移動�E予紁E��クリア */
    void ClearResolvedMoves();

    /** 解決済み移動を登録 */
    /**
     * ★★★ CRITICAL FIX (2025-11-11): bool型に変更（予約成功/失敗を返す） ★★★
     * Register a resolved move for an actor.
     * @return true if reservation succeeded, false if failed
     */
    bool RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell);

    /** 持E��セルへの移動が予紁E��れてぁE��ぁE*/
    bool IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const;
    bool HasReservationFor(AActor* Actor, const FIntPoint& Cell) const;
    void ReleaseMoveReservation(AActor* Actor);

    bool TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit);

    /** Open input window for player moves */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void OpenInputWindow();

    /** Possess通知 & 入力窓を再オープン */
    UFUNCTION(BlueprintCallable, Category="Turn|Flow")
    void NotifyPlayerPossessed(APawn* NewPawn);

    /** 入力窓を開く（プレイヤー用） */
    UFUNCTION(BlueprintCallable, Category="Turn|Flow")
    void OpenInputWindowForPlayer();

    /** 入力窓を閉じる（プレイヤー用） */
    UFUNCTION(BlueprintCallable, Category="Turn|Flow")
    void CloseInputWindowForPlayer();

    /** Apply wait input gate to player ASC */
    void ApplyWaitInputGate(bool bOpen);

    /** Clear all resolved move reservations */
    UFUNCTION(BlueprintCallable, Category = "Turn|State")
    bool IsInputOpen_Server() const;

protected:
    //==========================================================================
    // PathFinder Resolution / Spawning (for editor)
    //==========================================================================
    void ResolveOrSpawnPathFinder();
    
    //==========================================================================
    // UnitManager Resolution / Spawning (for editor)
    //==========================================================================
    void ResolveOrSpawnUnitManager();

public:
    // Public access for TurnCorePhaseManager
    bool DispatchResolvedMove(const FResolvedAction& Action);

protected:
    //==========================================================================
    // Phase 5: Gate Synchronization
    //==========================================================================

    void OnPlayerMoveCompleted(const FGameplayEventData* Payload);

    //==========================================================================
    // Phase 4: Turn Advance Condition
    //==========================================================================

    bool CanAdvanceTurn(int32 TurnId) const;

    //==========================================================================
    // Turn State Variables
    //==========================================================================

    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_CurrentTurnId, Category = "Turn")
    int32 CurrentTurnId = 0;

    /** Flag indicating if the first turn has started (for retry logic) */
    UPROPERTY(Replicated)
    bool bFirstTurnStarted = false;
    /** Timer handle for StartFirstTurn retry */
    FTimerHandle StartFirstTurnRetryHandle;
    int32 StartFirstTurnRetryCount = 0;

    // Player move processing -> Kick off enemy phase after player move
    FTimerHandle EnemyPhaseKickoffHandle;

    //==========================================================================
    // Delegate Handlers
    //==========================================================================

    FDelegateHandle AllMovesFinishedHandle;
    FDelegateHandle PlayerMoveCompletedHandle;


    //==========================================================================
    // Helper Functions
    //==========================================================================

    AGridPathfindingLibrary* FindPathFinder(UWorld* World) const;
    UAbilitySystemComponent* GetPlayerASC() const;

    /** Try to start the first turn after ASC is ready */
    UFUNCTION()
    void TryStartFirstTurnAfterASCReady();

    /** Try to start the first turn when all conditions are met */
    void TryStartFirstTurn();

    //==========================================================================
    // Phase Execution
    //==========================================================================

    void ExecuteSequentialPhase();
    void ExecuteSimultaneousPhase();
    void ExecuteMovePhase(bool bSkipAttackCheck = false);
    void HandleManualMoveFinished(AUnitBase* Unit);
    void RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback);
    void FinalizePlayerMove(AActor* CompletedActor);
    TMap<AUnitBase*, FDelegateHandle> ActiveMoveDelegates;
    TSet<TWeakObjectPtr<AUnitBase>> PendingPlayerFallbackMoves;
    void ExecuteAttacks();
    void EndEnemyTurn();
    void ExecutePlayerMove();
    void OnPlayerMoveAccepted();
    
    // NOTE: ActionExecutorSubsystem's SendPlayerMove is deprecated
    bool SendPlayerMove(const FPlayerCommand& Command);
    
    // NOTE: MoveConflictRuleSet's functions are deprecated
    /** Determine move priority for an actor based on its tags */
    int32 GetMovePriority(const FGameplayTagContainer& ActorTags) const;
    
    /** Check if two actors can swap positions */
    bool CanSwapActors(const FGameplayTagContainer& ActorA, const FGameplayTagContainer& ActorB) const;
    
    /** Check if a pusher actor can push a pushed actor */
    bool CanPushActor(const FGameplayTagContainer& Pusher, const FGameplayTagContainer& Pushed) const;

    //==========================================================================
    // Dynamic Delegate Handlers
    //==========================================================================

    UFUNCTION()
    void OnTurnStartedHandler(int32 TurnIndex);

    // ★★★ BUGFIX [INC-2025-TIMING]: Function disabled ★★★
    // UFUNCTION()
    // void OnAttacksFinished(int32 TurnId);

    UFUNCTION()
    void OnSimultaneousPhaseCompleted();

    UFUNCTION()
    void OnEnemyAttacksCompleted();

    UFUNCTION()
    void OnExperienceLoaded(const class ULyraExperienceDefinition* Experience);

protected:
    // UI update for WaitingForPlayerInput (for local/standalone)
    void OnWaitingForPlayerInputChanged_Local(bool bNow);
    // Set WaitingForPlayerInput on server (for local/standalone)
    void SetWaitingForPlayerInput_ServerLike(bool bNew);

    //==========================================================================
    // Subsystem Accessors
    //==========================================================================

    UPROPERTY()
    TObjectPtr<UEnemyAISubsystem> EnemyAISubsystem = nullptr;

    // NOTE: ActionExecutorSubsystem and TurnPhaseManagerComponent are commented out
    // If these classes do not exist, please comment out or modify the corresponding code in the .cpp file.
    // UPROPERTY(Transient)
    // TObjectPtr<UActionExecutorSubsystem> ActionExecutor = nullptr;

    // UPROPERTY(Transient)
    // TObjectPtr<UTurnPhaseManagerComponent> PhaseManager = nullptr;

    UPROPERTY(Transient)
    bool bTurnContinuing = false;

    //==========================================================================
    // Gameplay Tag Cache
    //==========================================================================

    FGameplayTag Tag_MoveCommandEvent;
    FGameplayTag Tag_AbilityMove;
    FGameplayTag Tag_TurnAbilityCompleted;
    FGameplayTag Phase_Turn_Init;
    FGameplayTag Phase_Player_Wait;

    //==========================================================================
    // 迥�E�諷狗ｮ�E�送E�E    //==========================================================================

    bool bHasInitialized = false;
    
    FTimerHandle SubsystemRetryHandle;
    int32 SubsystemRetryCount = 0;

private:
    //==========================================================================
    // ★★★ 新規Subsystem参照（2025-11-09リファクタリング） ★★★
    //==========================================================================

    /** コマンド処理Subsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnCommandHandler> CommandHandler = nullptr;

    /** イベント配信Subsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnEventDispatcher> EventDispatcher = nullptr;

    /** デバッグSubsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnDebugSubsystem> DebugSubsystem = nullptr;

    /** Week 1: Turn flow and AP management subsystem (2025-11-09 Refactoring) */
    UPROPERTY(Transient)
    TObjectPtr<UTurnFlowCoordinator> TurnFlowCoordinator = nullptr;

    /** Week 1: Input processing subsystem (2025-11-09 Refactoring) */
    UPROPERTY(Transient)
    TObjectPtr<UPlayerInputProcessor> PlayerInputProcessor = nullptr;

    //==========================================================================
    // Internal State Flags
    //==========================================================================

    // ★★★ Phase 4: Unused variable removal (2025-11-09) ★★★
    // Removed: CachedEnemiesWeak (unused)
    // Removed: RecollectEnemiesTimerHandle (unused)
    // Removed: PendingTeamCountLast (unused)
    // Removed: CollectEnemiesRetryCount (unused)

    // ★★★ Phase 5補完: EndTurn retry prevention (2025-11-09) ★★★
    /**
     * Flag indicating if an EndEnemyTurn retry is already scheduled
     * - true: Retry timer already set -> suppress additional retries
     * - false: Retry not set -> allow new retry
     * Reset to false in AdvanceTurnAndRestart()
     */
    bool bEndTurnPosted = false;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FEnemyIntent> CachedIntents;

    mutable TWeakObjectPtr<AActor> CachedPlayerActor;

    FTimerHandle AbilityWaitTimerHandle;

    //==========================================================================
    // Internal Initialization
    //==========================================================================

    void InitGameplayTags();

    static float EncodeDir8ToMagnitude(const FVector2D& Dir);
    FVector2D CalculateDirectionFromTargetCell(const FIntPoint& TargetCell);

    // Helper for packing/unpacking directions
    static int32 PackDirection(const FIntPoint& Dir);
    static FIntPoint UnpackDirection(int32 Magnitude);

    // ★★★ Phase 4: Unused variable removal (2025-11-09) ★★★
    // Removed: bEnemyTurnEnding (unused)
};




