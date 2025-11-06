// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"
#include "Debug/TurnSystemInterfaces.h"
#include "Turn/TurnSystemTypes.h"
#include "Utility/TurnCommon.h"
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
class AUnitManager;
class URogueDungeonSubsystem;
class UDebugObserverCSV;
class UTurnCorePhaseManager;
struct FGameplayEventData;
class ULyraExperienceDefinition;
class ULyraExperienceManagerComponent;
class ALyraGameState;
class ADungeonFloorGenerator;
class UDungeonConfigAsset;
struct FTurnContext;
class ATBSLyraGameMode;

// 笘・・笘・繝薙Ν繝芽ｭ伜挨逕ｨ螳壽焚 笘・・笘・static constexpr int32 kEnemyPhasePatchID = 20251030;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerInputReceived);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, int32, TurnIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorReady, int32, FloorIndex);

/**
 * AGameTurnManagerBase
 *
 * 繧ｿ繝ｼ繝ｳ繝吶・繧ｹ繧ｲ繝ｼ繝縺ｮ荳ｭ譬ｸ繝槭ロ繝ｼ繧ｸ繝｣繝ｼ・・++邨ｱ蜷育沿・・ */
UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    //==========================================================================
    // 繧ｳ繝ｳ繧ｹ繝医Λ繧ｯ繧ｿ/繧ｪ繝ｼ繝舌・繝ｩ繧､繝蛾未謨ｰ
    //==========================================================================

    AGameTurnManagerBase();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    //==========================================================================
    // 繧ｰ繝ｪ繝・ラ蛻晄悄蛹悶ョ繝舌ャ繧ｰ
    //==========================================================================

    void InitializeGridDebug();

    //==========================================================================
    // 繝ｬ繝励Μ繧ｱ繝ｼ繧ｷ繝ｧ繝ｳ騾夂衍繝上Φ繝峨Λ
    //==========================================================================

    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    UFUNCTION()
    void OnRep_InputWindowId();

    UFUNCTION()
    void OnRep_CurrentTurnId();

    //==========================================================================
    // 繧ｿ繝ｼ繝ｳID/WindowID蜿門ｾ・    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const { return InputWindowId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnIndex() const { return CurrentTurnIndex; }

    //==========================================================================
    // PathFinder繧ｭ繝｣繝・す繝･蜿門ｾ・    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    AGridPathfindingLibrary* GetCachedPathFinder() const;

    //==========================================================================
    // 繝繝ｳ繧ｸ繝ｧ繝ｳ逕滓・繧ｷ繧ｹ繝・Β邨ｱ蜷・PI
    //==========================================================================

    /** 繝繝ｳ繧ｸ繝ｧ繝ｳSubsystem縺ｸ縺ｮ繧｢繧ｯ繧ｻ繧ｵ */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    URogueDungeonSubsystem* GetDungeonSystem() const { return DungeonSystem; }

    /** FloorGenerator縺ｸ縺ｮ繧｢繧ｯ繧ｻ繧ｵ */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    ADungeonFloorGenerator* GetFloorGenerator() const;

    /** 謖・ｮ壹ヵ繝ｭ繧｢縺ｮ逕滓・繧剃ｿ晁ｨｼ・域悴逕滓・縺ｪ繧臥函謌撰ｼ・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool EnsureFloorGenerated(int32 FloorIndex);

    /** 谺｡繝輔Ο繧｢縺ｸ驕ｷ遘ｻ・・loorIndex++縺励※逕滓・・・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool NextFloor();

    /** BP莠呈鋤繧ｰ繝ｪ繝・ラAPI・壹Ρ繝ｼ繝ｫ繝牙ｺｧ讓吶°繧峨げ繝ｪ繝・ラ迥ｶ諷九ｒ蜿門ｾ・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Grid")
    int32 TM_ReturnGridStatus(FVector World) const;

    /** BP莠呈鋤繧ｰ繝ｪ繝・ラAPI・壹Ρ繝ｼ繝ｫ繝牙ｺｧ讓吶・繧ｰ繝ｪ繝・ラ蛟､繧貞､画峩 */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Grid")
    void TM_GridChangeVector(FVector World, int32 Value);

    /** 繝励Ξ繧､繝､繝ｼ繧帝嚴谿ｵ荳翫ｊ・・tairUp・峨・菴咲ｽｮ縺ｫ繝ｯ繝ｼ繝・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    void WarpPlayerToStairUp(AActor* Player);

    //==========================================================================
    // 繧ｰ繝ｪ繝・ラ繧ｳ繧ｹ繝郁ｨｭ螳夲ｼ・lueprint逕ｨ・・    //==========================================================================

    /** Blueprint逕ｨ・壽欠螳壹＠縺溘げ繝ｪ繝・ラ繧ｻ繝ｫ繧貞｣√↓險ｭ螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallCell(int32 X, int32 Y);

    /** Blueprint逕ｨ・壽欠螳壹＠縺溘Ρ繝ｼ繝ｫ繝牙ｺｧ讓吶ｒ螢√↓險ｭ螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallAtWorldPosition(FVector WorldPosition);

    /** Blueprint逕ｨ・夂洸蠖｢鬆伜沺繧貞｣√↓險ｭ螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallRect(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY);

    //==========================================================================
    // Barrier騾｣謳ｺ
    //==========================================================================

    UFUNCTION()
    void OnAllMovesFinished(int32 FinishedTurnId);

    //==========================================================================
    // 蛻晄悄蛹悶す繧ｹ繝・Β
    //==========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|System")
    void InitializeTurnSystem();

    //==========================================================================
    // 繧ｿ繝ｼ繝ｳ騾ｲ陦窟PI
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
    // Yangus・亥酔譎らｧｻ蜍募捉繧奇ｼ・    //==========================================================================


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

    // 陦・21莉倩ｿ代↓霑ｽ蜉・・urrentTurnId縺ｮ荳具ｼ・    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    int32 CurrentTurnIndex = 0;

    //==========================================================================
    // 繝繝ｳ繧ｸ繝ｧ繝ｳ逕滓・繧ｷ繧ｹ繝・Β邨ｱ蜷医・繝ｭ繝代ユ繧｣
    //==========================================================================

    /** 繝繝ｳ繧ｸ繝ｧ繝ｳ逕滓・Subsystem縺ｸ縺ｮ蜿ら・ */
    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSystem = nullptr;

    /** 蛻晄悄繝輔Ο繧｢險ｭ螳夂畑Config */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    TSoftObjectPtr<UDungeonConfigAsset> InitialFloorConfig;

    /** 髢句ｧ九ヵ繝ｭ繧｢繧､繝ｳ繝・ャ繧ｯ繧ｹ */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    int32 StartFloorIndex = 0;

    /** 迴ｾ蝨ｨ縺ｮ繝輔Ο繧｢繧､繝ｳ繝・ャ繧ｯ繧ｹ・亥・驛ｨ邂｡逅・畑・・*/
    UPROPERTY(BlueprintReadOnly, Category="Dungeon|State")
    int32 CurrentFloorIndex = 0;

    /** 繝輔Ο繧｢逕滓・螳御ｺ・う繝吶Φ繝・*/
    UPROPERTY(BlueprintAssignable, Category="Dungeon|Events")
    FOnFloorReady OnFloorReady;

    //==========================================================================
    // 笘・AP System・医い繧ｯ繧ｷ繝ｧ繝ｳ繝昴う繝ｳ繝亥宛・・    //==========================================================================
    
    /** 1繧ｿ繝ｼ繝ｳ縺ゅ◆繧翫・譛螟ｧAP */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAPPerTurn = 1;
    
    /** 迴ｾ蝨ｨ縺ｮ繝励Ξ繧､繝､繝ｼ谿帰P・医Ξ繝励Μ繧ｱ繝ｼ繝域耳螂ｨ・・*/
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAP = 0;

    /** AP縺・縺ｫ縺ｪ縺｣縺溘→縺阪・謨ｵ繝輔ぉ繝ｼ繧ｺ繧ｭ繝･繝ｼ繝輔Λ繧ｰ */
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
    // BP莠呈鋤繝励Ο繝代ユ繧｣
    //==========================================================================

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<APawn> CachedPlayerPawn = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<UDebugObserverCSV> DebugObserverCSV = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TObjectPtr<UEnemyTurnDataSubsystem> EnemyTurnData = nullptr;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    TArray<TObjectPtr<AActor>> CachedEnemies;

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

    //==========================================================================
    // 繝代ヵ繧ｩ繝ｼ繝槭Φ繧ｹ譛驕ｩ蛹・    //==========================================================================

    UPROPERTY(Transient)
    TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;

    UPROPERTY(BlueprintAssignable, Category = "Turn|Player")
    FOnPlayerInputReceived OnPlayerInputReceived;

    UPROPERTY(BlueprintAssignable, Category = "Turn|Flow")
    FOnTurnStarted OnTurnStarted;

    //==========================================================================
    // 繝繝ｳ繧ｸ繝ｧ繝ｳ貅門ｙ螳御ｺ・う繝吶Φ繝医ワ繝ｳ繝峨Λ・育峩蜿ら・豕ｨ蜈･・・    //==========================================================================

    UFUNCTION()
    void HandleDungeonReady(); // 竊・縺薙％縺ｧ縺ｮ縺ｿ InitializeTurnSystem() 繧貞他縺ｶ

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

    /** Open input window for player moves */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void OpenInputWindow();

    /** Apply wait input gate to player ASC */
    void ApplyWaitInputGate(bool bOpen);

    /** 繝悶・繝ｫ・九ち繧ｰ縺ｮ莠碁㍾骰ｵ讀懆ｨｼ・医し繝ｼ繝舌・讓ｩ髯舌・縺ｿ諠ｳ螳夲ｼ・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|State")
    bool IsInputOpen_Server() const;

protected:
    //==========================================================================
    // PathFinder隗｣豎ｺ繝ｻ逕滓・・医ヵ繧ｩ繝ｼ繝ｫ繝舌ャ繧ｯ逕ｨ・・    //==========================================================================
    void ResolveOrSpawnPathFinder();
    
    //==========================================================================
    // UnitManager隗｣豎ｺ繝ｻ逕滓・・医ヵ繧ｩ繝ｼ繝ｫ繝舌ャ繧ｯ逕ｨ・・    //==========================================================================
    void ResolveOrSpawnUnitManager();

protected:
    //==========================================================================
    // Phase 5: Gate蜀阪が繝ｼ繝励Φ
    //==========================================================================

    void OnPlayerMoveCompleted(const FGameplayEventData* Payload);

    //==========================================================================
    // Phase 4: 繧ｿ繝ｼ繝ｳ騾ｲ陦後・莠碁㍾骰ｵ
    //==========================================================================

    bool CanAdvanceTurn(int32 TurnId) const;

    //==========================================================================
    // 繧ｿ繝ｼ繝ｳ騾ｲ謐礼憾諷・    //==========================================================================

    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_CurrentTurnId, Category = "Turn")
    int32 CurrentTurnId = 0;

    /** 蛻晏屓繧ｿ繝ｼ繝ｳ髢句ｧ九・驥崎､・亟豁｢繝輔Λ繧ｰ・医し繝ｼ繝先ｨｩ髯仙・縺ｮ縺ｿ菴ｿ逕ｨ・・*/
    UPROPERTY(Replicated)
    bool bFirstTurnStarted = false;
    /** StartFirstTurn縺ｮ繝ｪ繝医Λ繧､逕ｨ */
    FTimerHandle StartFirstTurnRetryHandle;
    int32 StartFirstTurnRetryCount = 0;

    // 繝励Ξ繧､繝､繝ｼ遘ｻ蜍募ｮ御ｺ・-> 謨ｵ繝輔ぉ繝ｼ繧ｺ遘ｻ陦後・霆ｽ驕・ｻｶ襍ｷ蜍慕畑
    FTimerHandle EnemyPhaseKickoffHandle;

    //==========================================================================
    // 繝・Μ繧ｲ繝ｼ繝医ワ繝ｳ繝峨Ν
    //==========================================================================

    FDelegateHandle AllMovesFinishedHandle;
    FDelegateHandle PlayerMoveCompletedHandle;


    //==========================================================================
    // Helper髢｢謨ｰ
    //==========================================================================

    AGridPathfindingLibrary* FindPathFinder(UWorld* World) const;
    UAbilitySystemComponent* GetPlayerASC() const;

    /** ASC貅門ｙ繧貞ｾ・▲縺ｦ蛻晏屓繧ｿ繝ｼ繝ｳ繧帝幕蟋・*/
    UFUNCTION()
    void TryStartFirstTurnAfterASCReady();

    //==========================================================================
    // Phase螳溯｡・    //==========================================================================

    void ExecuteSequentialPhase();
    void ExecuteSimultaneousPhase();
    void ExecuteMovePhase();
    void ExecuteAttacks();
    void EndEnemyTurn();
    void ExecutePlayerMove();
    void OnPlayerMoveAccepted();
    
    // 笘・・笘・邨ｱ蜷茨ｼ哂ctionExecutorSubsystem縺ｮSendPlayerMove讖溯・繧堤ｧｻ邂｡ 笘・・笘・    bool SendPlayerMove(const FPlayerCommand& Command);
    
    // 笘・・笘・邨ｱ蜷茨ｼ哺oveConflictRuleSet縺ｮ讖溯・繧堤ｧｻ邂｡ 笘・・笘・    /** 繧｢繧ｯ繧ｿ繝ｼ縺ｮ蜆ｪ蜈亥ｺｦ繧貞叙蠕・*/
    int32 GetMovePriority(const FGameplayTagContainer& ActorTags) const;
    
    /** 蜈･繧梧崛繧上ｊ蜿ｯ閭ｽ縺句愛螳・*/
    bool CanSwapActors(const FGameplayTagContainer& ActorA, const FGameplayTagContainer& ActorB) const;
    
    /** 謚ｼ縺怜・縺怜庄閭ｽ縺句愛螳・*/
    bool CanPushActor(const FGameplayTagContainer& Pusher, const FGameplayTagContainer& Pushed) const;

    //==========================================================================
    // Dynamic 繝・Μ繧ｲ繝ｼ繝医ワ繝ｳ繝峨Λ
    //==========================================================================

    UFUNCTION()
    void OnTurnStartedHandler(int32 TurnIndex);

    UFUNCTION()
    void OnAttacksFinished(int32 TurnId);

    UFUNCTION()
    void OnSimultaneousPhaseCompleted();

    UFUNCTION()
    void OnEnemyAttacksCompleted();

    UFUNCTION()
    void OnExperienceLoaded(const class ULyraExperienceDefinition* Experience);

protected:
    void RunTurn();

    // UI譖ｴ譁ｰ蟆ら畑繝ｭ繝ｼ繧ｫ繝ｫ繝上Φ繝峨Λ・医ち繧ｰ謫堺ｽ懊・遖∵ｭ｢・・    void OnWaitingForPlayerInputChanged_Local(bool bNow);
    // 讓ｩ螽∫嶌蠖難ｼ医し繝ｼ繝・or Standalone・峨〒縺ｮ縺ｿ WaitingForPlayerInput 繧貞､画峩
    void SetWaitingForPlayerInput_ServerLike(bool bNew);

    //==========================================================================
    // Subsystem・医く繝｣繝・す繝･・・    //==========================================================================

    UPROPERTY()
    TObjectPtr<UEnemyAISubsystem> EnemyAISubsystem = nullptr;

    // 笘・・笘・ActionExecutorSubsystem 縺ｨ TurnPhaseManagerComponent 縺ｯ蟄伜惠縺励↑縺・◆繧√さ繝｡繝ｳ繝医い繧ｦ繝・    // 蠢・ｦ√↓蠢懊§縺ｦ縲・cpp蛛ｴ縺ｧ驕ｩ蛻・↑繧ｯ繝ｩ繧ｹ縺ｫ鄂ｮ縺肴鋤縺医※縺上□縺輔＞
    // UPROPERTY(Transient)
    // TObjectPtr<UActionExecutorSubsystem> ActionExecutor = nullptr;

    // UPROPERTY(Transient)
    // TObjectPtr<UTurnPhaseManagerComponent> PhaseManager = nullptr;

    UPROPERTY(Transient)
    bool bTurnContinuing = false;

    //==========================================================================
    // 繧ｿ繧ｰ繝｡繝ｳ繝舌・螟画焚
    //==========================================================================

    FGameplayTag Tag_AbilityMove;
    FGameplayTag Tag_TurnAbilityCompleted;
    FGameplayTag Phase_Turn_Init;
    FGameplayTag Phase_Player_Wait;

    //==========================================================================
    // 迥ｶ諷狗ｮ｡逅・    //==========================================================================

    bool bHasInitialized = false;
    
    FTimerHandle SubsystemRetryHandle;
    int32 SubsystemRetryCount = 0;

private:
    //==========================================================================
    // Phase 2: WindowId讀懆ｨｼ逕ｨ縺ｮ蜀・Κ繝倥Ν繝代・
    //==========================================================================

    void ProcessPlayerCommand(const FPlayerCommand& Command);

    void StartTurnMoves(int32 TurnId);

    //==========================================================================
    // 蜀・Κ迥ｶ諷・    //==========================================================================

    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> CachedEnemiesWeak;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FEnemyIntent> CachedIntents;

    mutable TWeakObjectPtr<AActor> CachedPlayerActor;

    FTimerHandle AbilityWaitTimerHandle;

    // CollectEnemies 蜀崎ｩｦ陦檎畑
    FTimerHandle RecollectEnemiesTimerHandle;
    int32 PendingTeamCountLast = 0;
    int32 CollectEnemiesRetryCount = 0;
    static constexpr int32 CollectEnemiesMaxRetries = 3;

    //==========================================================================
    // 繧ｿ繧ｰ蛻晄悄蛹夜未謨ｰ
    //==========================================================================

    void InitGameplayTags();

    static float EncodeDir8ToMagnitude(const FVector2D& Dir);
    FVector2D CalculateDirectionFromTargetCell(const FIntPoint& TargetCell);

    // 譁ｹ蜷代・謨ｴ謨ｰ繝代ャ繧ｯ/繧｢繝ｳ繝代ャ繧ｯ・郁ｪ､蟾ｮ繧ｼ繝ｭ驕区成・・    static int32 PackDirection(const FIntPoint& Dir);
    static FIntPoint UnpackDirection(int32 Magnitude);

    // 笘・・笘・莠碁㍾邨らｫｯ髦ｲ豁｢繝輔Λ繧ｰ 笘・・笘・    UPROPERTY(Transient)
    bool bEnemyTurnEnding = false;
};

