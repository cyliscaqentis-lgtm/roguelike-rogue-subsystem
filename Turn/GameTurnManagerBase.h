// Copyright Epic Games, Inc. All Rights Reserved.
// CodeRevision: INC-2025-1135-R1 (Fix encoding issues: correct garbled comments and translate Japanese comments to English) (2025-11-19 17:25)

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
#include "../Utility/ProjectDiagnostics.h"
#include "GameTurnManagerBase.generated.h"

class UEnemyTurnDataSubsystem;
class UEnemyAISubsystem;
class UAttackPhaseExecutorSubsystem;
class UTurnActionBarrierSubsystem;
class UAbilitySystemComponent;
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
class UGridPathfindingSubsystem;
class AUnitBase;
class AUnitManager;
class URogueDungeonSubsystem;
class UDebugObserverCSV;
class UTurnCorePhaseManager;
class UTurnCommandHandler;
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

USTRUCT()
struct FManualMoveBarrierInfo
{
    GENERATED_BODY()

    UPROPERTY()
    int32 TurnId = INDEX_NONE;

    UPROPERTY()
    FGuid ActionId;
};

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
    // CodeRevision: INC-2025-1125-R1 (Expose sequential flag for resolver checks) (2025-11-25 12:00)
    bool IsSequentialModeActive() const;

    //==========================================================================
    // Grid Debug Initialization
    //==========================================================================

    void InitializeGridDebug();

    //==========================================================================
    // Replication Notifies
    //==========================================================================

    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    // CodeRevision: INC-2025-00030-R1 (Removed OnRep_InputWindowId - use TurnFlowCoordinator instead) (2025-11-16 00:00)
    // Removed: UFUNCTION() void OnRep_InputWindowId();

    UFUNCTION()
    void OnRep_CurrentTurnId();

    //==========================================================================
    // Turn ID / Window ID Accessors
    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnIndex() const;

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const;

    // CodeRevision: INC-2025-00030-R1 (Remove legacy accessors - use TurnFlowCoordinator instead) (2025-11-16 00:00)
    // Removed: GetCurrentInputWindowId() - use TurnFlowCoordinator->GetCurrentInputWindowId() instead
    // Removed: GetCurrentTurnIndex() - use TurnFlowCoordinator->GetCurrentTurnIndex() instead

    //==========================================================================
    // PathFinder Accessor
    //==========================================================================

    /** Get GridPathfindingSubsystem (new subsystem-based access) */
    // CodeRevision: INC-2025-00032-R1 (Removed GetCachedPathFinder() - use GetGridPathfindingSubsystem() instead) (2025-01-XX XX:XX)
    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    class UGridPathfindingSubsystem* GetGridPathfindingSubsystem() const;


    // CodeRevision: INC-2025-00017-R1 (Remove wrapper functions - Phase 4) (2025-11-16 15:25)
    // Removed: GetDungeonSystem() - use DungeonSystem member directly
    // Removed: GetFloorGenerator() - use DungeonSystem->GetFloorGenerator() directly

    // CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1) (2025-11-16 15:00)
    // Removed: EnsureFloorGenerated() - unused wrapper function
    // Removed: NextFloor() - unused wrapper function
    // Removed: WarpPlayerToStairUp() - unimplemented wrapper function

    //==========================================================================
    // Barrier Related
    //==========================================================================

    // CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
    UFUNCTION()
    void HandleMovePhaseCompleted(int32 FinishedTurnId);

    UFUNCTION()
    void HandleAttackPhaseCompleted(int32 FinishedTurnId);

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
     * ★★★ Phase 5 Completion: Force cleanup of residual InProgress tags (2025-11-09) ★★★
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

    // CodeRevision: INC-2025-00017-R1 (Remove unused wrapper functions - Phase 1 & 3) (2025-11-16 15:00)
    // Removed: BuildAllObservations() - unused wrapper function
    // Removed: SendGameplayEventWithResult() - unused wrapper function
    // Removed: SendGameplayEvent() - unused wrapper function
    // Removed: GetPlayerController_TBS() - unused wrapper function
    // Removed: GetPlayerPawnCachedOrFetch() - unused wrapper function
    // Removed: GetPlayerPawn() - replaced with direct UGameplayStatics call
    // Removed: GetPlayerActor() - unused wrapper function

    UFUNCTION(BlueprintPure, Category = "Turn|State")
    void GetCachedEnemies(TArray<AActor*>& OutEnemies) const;

    // CodeRevision: INC-2025-00017-R1 (Remove wrapper functions - Phase 3) (2025-11-16 15:20)
    // Removed: HasAnyAttackIntent() - replaced with direct EnemyTurnDataSubsystem access

    //==========================================================================
    // Player BlueprintNativeEvent
    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Player")
    void OnPlayerCommandAccepted(const FPlayerCommand& Command);
    virtual void OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command);

    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Turn|Player")
    bool ShouldSimulMove(const FPlayerCommand& Command, const FBoardSnapshot& Snapshot) const;
    virtual bool ShouldSimulMove_Implementation(const FPlayerCommand& Command, const FBoardSnapshot& Snapshot) const;

    //==========================================================================
    // Enemy Pipeline BlueprintNativeEvent
    //==========================================================================

    // CodeRevision: INC-2025-00017-R1 (Remove wrapper functions - Phase 3) (2025-11-16 15:20)
    // Removed: CollectEnemies() / CollectEnemies_Implementation() - replaced with direct EnemyAISubsystem call
    // Removed: CollectIntents() / CollectIntents_Implementation() - replaced with direct EnemyAISubsystem call


    //==========================================================================
    // Yangus Ability Related
    //==========================================================================

    UFUNCTION(BlueprintImplementableEvent, Category = "Turn|Yangus")
    void OnAllAbilitiesCompleted();

    //==========================================================================
    // Ally Turn BlueprintNativeEvent
    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Ally")
    void BuildAllyIntents(UPARAM(ref) FTurnContext& Context);
    virtual void BuildAllyIntents_Implementation(FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Ally")
    void ExecuteAllyActions(UPARAM(ref) FTurnContext& Context);
    virtual void ExecuteAllyActions_Implementation(FTurnContext& Context);

    //==========================================================================
    // Move Pipeline BlueprintNativeEvent
    //==========================================================================

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
    // Interrupt System BlueprintNativeEvent
    //==========================================================================

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Interrupt")
    void CheckInterruptWindow(const FPendingMove& PlayerMove);
    virtual void CheckInterruptWindow_Implementation(const FPendingMove& PlayerMove);

    //==========================================================================
    // Ability Completion BlueprintNativeEvent
    //==========================================================================

    //==========================================================================
    // System Hooks BlueprintNativeEvent
    //==========================================================================

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

    // CodeRevision: INC-2025-00017-R1 (Remove wrapper functions - Phase 3) (2025-11-16 15:20)
    // Removed: GetEnemyIntentsBP() / GetEnemyIntentsBP_Implementation() - replaced with direct EnemyTurnDataSubsystem access

    

    //==========================================================================
    // Hook Collections
    //==========================================================================

    UPROPERTY(EditAnywhere, Instanced, Category = "Turn|Hooks|Debug")
    TArray<TObjectPtr<UObject>> DebugObservers;

    // CodeRevision: INC-2025-00030-R1 (Removed CurrentTurnIndex - use TurnFlowCoordinator->GetCurrentTurnIndex() instead) (2025-11-16 00:00)
    // Removed: int32 CurrentTurnIndex = 0;

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

    // CodeRevision: INC-2025-00029-R1 (Removed CachedPlayerPawn - Use UGameplayStatics::GetPlayerPawn() instead) (2025-11-16 00:00)
    // Removed: TObjectPtr<APawn> CachedPlayerPawn = nullptr;

    // CodeRevision: INC-2025-00030-R1 (Removed legacy subsystem references - use GetWorld()->GetSubsystem<>() instead) (2025-11-16 00:00)
    // Removed: TObjectPtr<UDebugObserverCSV> DebugObserverCSV = nullptr; (use DebugObservers array and cast)
    // Removed: TObjectPtr<UEnemyTurnDataSubsystem> EnemyTurnData = nullptr; (use GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())

    // CodeRevision: INC-2025-00030-R1 (Removed CachedEnemies - use CachedEnemiesForTurn or EnemyAISubsystem instead) (2025-11-16 00:00)
    // Removed: TArray<TObjectPtr<AActor>> CachedEnemies; (use CachedEnemiesForTurn or EnemyAISubsystem->CollectAllEnemies())

    /** Cached and sorted list of enemies for the current turn */
    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    TArray<TObjectPtr<AActor>> CachedEnemiesForTurn;

    /** Cached enemy intents for the current turn (used for sequential checks) */
    UPROPERTY(Transient)
    TArray<FEnemyIntent> CachedEnemyIntents;

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

    // CodeRevision: INC-2025-00030-R1 (Removed InputWindowId - use TurnFlowCoordinator->GetCurrentInputWindowId() instead) (2025-11-16 00:00)
    // Removed: int32 InputWindowId = 0;

    // CodeRevision: INC-2025-00030-R1 (Removed unused legacy members) (2025-11-16 00:00)
    // Removed: int32 PendingAbilityCount = 0; (unused)
    // Removed: FVector CachedInputDirection = FVector::ZeroVector; (unused)

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


    // CodeRevision: INC-2025-00032-R1 (Removed CachedPathFinder member - use PathFinder only) (2025-01-XX XX:XX)

    UFUNCTION()
    void HandleDungeonReady(URogueDungeonSubsystem* InDungeonSys); // Calls InitializeTurnSystem()

    UPROPERTY()
    TObjectPtr<UGridPathfindingSubsystem> PathFinder = nullptr;

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

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    UGridPathfindingSubsystem* FindPathFinder(UWorld* World) const;
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
    UFUNCTION()
    void HandleManualMoveFinished(AUnitBase* Unit);
    void RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback);
    void FinalizePlayerMove(AActor* CompletedActor);
    TSet<TWeakObjectPtr<AUnitBase>> ActiveMoveDelegates;
    TSet<TWeakObjectPtr<AUnitBase>> PendingPlayerFallbackMoves;
    // CodeRevision: INC-2025-1117H-R1 (Generalize attack execution) (2025-11-17 19:10)
    void ExecuteAttacks(const TArray<FResolvedAction>& PreResolvedAttacks = TArray<FResolvedAction>());
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

    // CodeRevision: INC-2025-00030-R5 (Split attack/move phase completion handlers) (2025-11-17 02:00)
    UPROPERTY(Transient)
    TObjectPtr<UAttackPhaseExecutorSubsystem> AttackExecutor = nullptr;

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
    FTimerHandle EndEnemyTurnDelayHandle;

private:
    //==========================================================================
    // New Subsystem References (2025-11-09 Refactoring)
    //==========================================================================

    /** Command processing subsystem */
    UPROPERTY(Transient)
    TObjectPtr<UTurnCommandHandler> CommandHandler = nullptr;

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

    // ★★★ Phase 5 Completion: EndTurn retry prevention (2025-11-09) ★★★
    /**
     * Flag indicating if an EndEnemyTurn retry is already scheduled
     * - true: Retry timer already set -> suppress additional retries
     * - false: Retry not set -> allow new retry
     * Reset to false in AdvanceTurnAndRestart()
     */
    bool bEndTurnPosted = false;

    mutable TWeakObjectPtr<AActor> CachedPlayerActor;

    FTimerHandle AbilityWaitTimerHandle;

    /** Sequential phase tracking */
    // CodeRevision: INC-2025-00023-R1 (Handle sequential attack -> move transitions) (2025-11-17 19:15)
    bool bSequentialModeActive = false;
    bool bSequentialMovePhaseStarted = false;
    UPROPERTY()
    bool bIsInMoveOnlyPhase = false;

    // CodeRevision: INC-2025-1117G-R1 (Cache resolved actions for sequential enemy processing) (2025-11-17 19:30)
    TArray<FResolvedAction> CachedResolvedActions;

    /** Pending barrier registrations for manual enemy moves */
    UPROPERTY()
    TMap<TWeakObjectPtr<AUnitBase>, FManualMoveBarrierInfo> PendingMoveActionRegistrations;

    //==========================================================================
    // Internal Initialization
    //==========================================================================

    void InitGameplayTags();

    static float EncodeDir8ToMagnitude(const FVector2D& Dir);
    FVector2D CalculateDirectionFromTargetCell(const FIntPoint& TargetCell);

    // Helper for packing/unpacking directions
    static int32 PackDirection(const FIntPoint& Dir);
    static FIntPoint UnpackDirection(int32 Magnitude);


    void ExecuteEnemyMoves_Sequential();
    // CodeRevision: INC-2025-1117H-R1 (Centralized move dispatch) (2025-11-17 19:10)
    void DispatchMoveActions(const TArray<FResolvedAction>& ActionsToDispatch);
    // ★★★ Phase 4: Unused variable removal (2025-11-09) ★★★
    // Removed: bEnemyTurnEnding (unused)

    bool DoesAnyIntentHaveAttack() const;
};
