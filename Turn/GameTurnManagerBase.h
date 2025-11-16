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
     * ★★★ Phase 5補完: Force cleanup of residual InProgress tags (2025-11-09) ★★★
     *
     * Force remove State.Action.InProgress tags from all units (player + enemies)
     *
     * Usage:
     * - Called after Barrier completion at turn end
     * - Prevents tag stacking that stops turn progression
     * - Usually not needed, but insurance for unexpected errors that leave residual tags
     *
     * Log output:
     * - Before: Number of InProgress tags before cleanup
     * - After: After cleanup (usually 0)
     */
    void ClearResidualInProgressTags();

    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void BuildAllObservations();

    UFUNCTION(BlueprintCallable, Category = "Turn|Utility")
    bool SendGameplayEventWithResult(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload);

    UFUNCTION(BlueprintCallable, Category = "Turn|Utility")
    void SendGameplayEvent(AActor* Target, FGameplayTag EventTag, const FGameplayEventData& Payload);

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APlayerController* GetPlayerController_TBS() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APawn* GetPlayerPawnCachedOrFetch();

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    APawn* GetPlayerPawn() const;

    UFUNCTION(BlueprintPure, Category = "Turn|Utility")
    AActor* GetPlayerActor() const;

    UFUNCTION(BlueprintPure, Category = "Turn|State")
    void GetCachedEnemies(TArray<AActor*>& OutEnemies) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Yangus")
    bool HasAnyAttackIntent() const;

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
    void CollectIntents();
    virtual void CollectIntents_Implementation();


    //==========================================================================
    // Yangus Ability Related
    //==========================================================================

    UFUNCTION(BlueprintImplementableEvent, Category = "Turn|Yangus")
    void OnAllAbilitiesCompleted();

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
    // AP System Configuration    //==========================================================================
    
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

    /** Cached and sorted list of enemies for the current turn */
    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    TArray<TObjectPtr<AActor>> CachedEnemiesForTurn;

    /** Player's pending move intent (held until next Resolve) */
    UPROPERTY()
    FEnemyIntent PendingPlayerIntent;

    UPROPERTY()
    bool bPendingPlayerIntent = false;

    /** Resolved move reservations for the current turn (Actor -> Cell) */
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, FIntPoint> PendingMoveReservations;

    /** Enemy list revision number, increments each time CollectEnemies completes */
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

    /** Flag indicating if the turn has started */
    UPROPERTY()
    bool bTurnStarted = false;

    /** Flag to defer input window opening until player is possessed */
    UPROPERTY()
    bool bDeferOpenOnPossess = false;

    /** Flag indicating if pathfinding is ready */
    UPROPERTY()
    bool bPathReady = false;

    /** Flag indicating if units have been spawned */
    UPROPERTY()
    bool bUnitsSpawned = false;

    /** Flag indicating if player has been possessed */
    UPROPERTY()
    bool bPlayerPossessed = false;


    UPROPERTY(Transient)
    TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;


    UFUNCTION()
    void HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys); // Calls InitializeTurnSystem()

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

    /** Clear all resolved move reservations */
    void ClearResolvedMoves();
    /**
     * CRITICAL FIX (2025-11-11): Changed to bool type (returns success/failure)
     * Register a resolved move for an actor.
     * @return true if reservation succeeded, false if failed
     */
    bool RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell);

    bool IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const;
    bool HasReservationFor(AActor* Actor, const FIntPoint& Cell) const;
    void ReleaseMoveReservation(AActor* Actor);

    bool TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit);

    /** Open input window for player moves */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void OpenInputWindow();

    UFUNCTION(BlueprintCallable, Category="Turn|Flow")
    void NotifyPlayerPossessed(APawn* NewPawn);

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

    bool CanAdvanceTurn(int32 TurnId, bool* OutBarrierQuiet = nullptr, int32* OutInProgressCount = nullptr) const;

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
    // Internal State Flags
    //==========================================================================

    bool bHasInitialized = false;
    
    FTimerHandle SubsystemRetryHandle;
    int32 SubsystemRetryCount = 0;

private:
    //==========================================================================
    // New Subsystem References (2025-11-09 Refactoring)
    //==========================================================================

    /** Command processing subsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnCommandHandler> CommandHandler = nullptr;

    /** Event dispatching subsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnEventDispatcher> EventDispatcher = nullptr;

    /** Debug subsystem */
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




