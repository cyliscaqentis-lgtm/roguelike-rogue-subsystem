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
struct FGameplayEventData;
class ULyraExperienceDefinition;
class ULyraExperienceManagerComponent;
class ALyraGameState;
class ADungeonFloorGenerator;
class UDungeonConfigAsset;
struct FTurnContext;
class ATBSLyraGameMode;

// 笘�E・笘�E繝薙Ν繝芽�E�伜挨逕ｨ螳壽焁E笘�E・笘�Estatic constexpr int32 kEnemyPhasePatchID = 20251030;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerInputReceived);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, int32, TurnIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorReady, int32, FloorIndex);

/**
 * AGameTurnManagerBase
 *
 * 繧�E�繝ｼ繝ｳ繝吶・繧�E�繧�E�繝ｼ繝縺�E�荳�E�譬�E�繝槭ロ繝ｼ繧�E�繝｣繝ｼ・・++邨�E�蜷育沿・・ */
UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    //==========================================================================
    // 繧�E�繝ｳ繧�E�繝医Λ繧�E�繧�E�/繧�E�繝ｼ繝�E・繝ｩ繧�E�繝蛾未謨�E�
    //==========================================================================

    AGameTurnManagerBase();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    //==========================================================================
    // 繧�E�繝ｪ繝�Eラ蛻晁E��蛹悶ョ繝�Eャ繧�E�
    //==========================================================================

    void InitializeGridDebug();

    //==========================================================================
    // 繝ｬ繝励Μ繧�E�繝ｼ繧�E�繝ｧ繝ｳ騾夂衍繝上Φ繝峨΁E
    //==========================================================================

    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    UFUNCTION()
    void OnRep_InputWindowId();

    UFUNCTION()
    void OnRep_CurrentTurnId();

    //==========================================================================
    // 繧�E�繝ｼ繝ｳID/WindowID蜿門�E�・    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const { return InputWindowId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnIndex() const { return CurrentTurnIndex; }

    //==========================================================================
    // PathFinder繧�E�繝｣繝�Eす繝･蜿門�E�・    //==========================================================================

    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    AGridPathfindingLibrary* GetCachedPathFinder() const;

    //==========================================================================
    // 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�E繧�E�繧�E�繝�EΒ邨�E�蜷・PI
    //==========================================================================

    /** 繝繝ｳ繧�E�繝ｧ繝ｳSubsystem縺�E�縺�E�繧�E�繧�E�繧�E�繧�E� */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    URogueDungeonSubsystem* GetDungeonSystem() const { return DungeonSystem; }

    /** FloorGenerator縺�E�縺�E�繧�E�繧�E�繧�E�繧�E� */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Access")
    ADungeonFloorGenerator* GetFloorGenerator() const;

    /** 謖�E�E�壹ヵ繝ｭ繧�E�縺�E�逕滓�E繧剁E��晁E���E�・域悴逕滓�E縺�E�繧臥函謌撰�E�・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool EnsureFloorGenerated(int32 FloorIndex);

    /** 谺�E�繝輔Ο繧�E�縺�E�驕ｷ遘ｻ・・loorIndex++縺励※逕滓�E・・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    bool NextFloor();

    /** BP莠呈鋤繧�E�繝ｪ繝�EラAPI・壹Ρ繝ｼ繝ｫ繝牙�E��E�讓吶°繧峨げ繝ｪ繝�Eラ迥�E�諷九ｒ蜿門�E�・*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Grid")
    int32 TM_ReturnGridStatus(FVector World) const;

    /** BP莠呈鋤繧�E�繝ｪ繝�EラAPI・壹Ρ繝ｼ繝ｫ繝牙�E��E�讓吶・繧�E�繝ｪ繝�Eラ蛟､繧貞､画峩 */
    UFUNCTION(BlueprintCallable, Category="Dungeon|Grid")
    void TM_GridChangeVector(FVector World, int32 Value);

    /** 繝励Ξ繧�E�繝､繝ｼ繧帝嚴谿�E�荳翫�E��E・tairUp・峨・菴咲�E��E�縺�E�繝ｯ繝ｼ繝�E*/
    UFUNCTION(BlueprintCallable, Category="Dungeon|Flow")
    void WarpPlayerToStairUp(AActor* Player);

    //==========================================================================
    // 繧�E�繝ｪ繝�Eラ繧�E�繧�E�繝郁�E��E�螳夲�E�・lueprint逕ｨ・・    //==========================================================================

    /** Blueprint逕ｨ・壽欠螳壹�E�縺溘げ繝ｪ繝�Eラ繧�E�繝ｫ繧貞｣√�E險�E�螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallCell(int32 X, int32 Y);

    /** Blueprint逕ｨ・壽欠螳壹�E�縺溘Ρ繝ｼ繝ｫ繝牙�E��E�讓吶�E�螢√�E險�E�螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallAtWorldPosition(FVector WorldPosition);

    /** Blueprint逕ｨ・夂洸蠖｢鬁E��沺繧貞｣√�E險�E�螳・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|Grid")
    void SetWallRect(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY);

    //==========================================================================
    // Barrier騾�E�謳�E�
    //==========================================================================

    UFUNCTION()
    void OnAllMovesFinished(int32 FinishedTurnId);

    //==========================================================================
    // 蛻晁E��蛹悶す繧�E�繝�E΁E
    //==========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|System")
    void InitializeTurnSystem();

    //==========================================================================
    // 繧�E�繝ｼ繝ｳ騾�E�陦窟PI
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
    // Yangus・亥酔譎ら�E��E�蜍募捉繧奁E��・    //==========================================================================


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

    // 陦・21莉倩�E�代↓霑�E�蜉・・urrentTurnId縺�E�荳具�E�・    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|Legacy")
    int32 CurrentTurnIndex = 0;

    //==========================================================================
    // 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�E繧�E�繧�E�繝�EΒ邨�E�蜷医・繝ｭ繝代ユ繧�E�
    //==========================================================================

    /** 繝繝ｳ繧�E�繝ｧ繝ｳ逕滓�ESubsystem縺�E�縺�E�蜿ら�E */
    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSystem = nullptr;

    /** 蛻晁E��繝輔Ο繧�E�險�E�螳夂畑Config */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    TSoftObjectPtr<UDungeonConfigAsset> InitialFloorConfig;

    /** 髢句�E�九ヵ繝ｭ繧�E�繧�E�繝ｳ繝�Eャ繧�E�繧�E� */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dungeon|Config")
    int32 StartFloorIndex = 0;

    /** 迴�E�蝨�E�縺�E�繝輔Ο繧�E�繧�E�繝ｳ繝�Eャ繧�E�繧�E�・亥・驛ｨ邂｡送E�E畑�E・*/
    UPROPERTY(BlueprintReadOnly, Category="Dungeon|State")
    int32 CurrentFloorIndex = 0;

    /** 繝輔Ο繧�E�逕滓�E螳御�E�・ぁE��吶Φ繝�E*/
    UPROPERTY(BlueprintAssignable, Category="Dungeon|Events")
    FOnFloorReady OnFloorReady;

    //==========================================================================
    // 笘�EAP System・医ぁE���E�繧�E�繝ｧ繝ｳ繝昴ぁE��ｳ繝亥宛�E・    //==========================================================================
    
    /** 1繧�E�繝ｼ繝ｳ縺めE��繧翫・譛螟ｧAP */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAPPerTurn = 1;
    
    /** 迴�E�蝨�E�縺�E�繝励Ξ繧�E�繝､繝ｼ谿帰P・医Ξ繝励Μ繧�E�繝ｼ繝域耳螂ｨ・・*/
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn System|AP")
    int32 PlayerAP = 0;

    /** AP縺・縺�E�縺�E�縺�E�縺溘�E縺阪・謨�E�繝輔ぉ繝ｼ繧�E�繧�E�繝･繝ｼ繝輔Λ繧�E� */
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
    // BP莠呈鋤繝励Ο繝代ユ繧�E�
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

    //==========================================================================
    // 繝代ヵ繧�E�繝ｼ繝槭Φ繧�E�譛驕ｩ蛹・    //==========================================================================

    UPROPERTY(Transient)
    TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;

    UPROPERTY(BlueprintAssignable, Category = "Turn|Player")
    FOnPlayerInputReceived OnPlayerInputReceived;

    UPROPERTY(BlueprintAssignable, Category = "Turn|Flow")
    FOnTurnStarted OnTurnStarted;

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
    void RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell);

    /** 持E��セルへの移動が予紁E��れてぁE��ぁE*/
    bool IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const;
    bool HasReservationFor(AActor* Actor, const FIntPoint& Cell) const;
    void ReleaseMoveReservation(AActor* Actor);

    bool TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit);

    /** Open input window for player moves */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void OpenInputWindow();

    /** Apply wait input gate to player ASC */
    void ApplyWaitInputGate(bool bOpen);

    /** 繝悶・繝ｫ・九ち繧�E�縺�E�莠碁㍾骰�E�讀懁E���E�・医し繝ｼ繝�E・讓ｩ髯舌�E縺�E�諠�E�螳夲�E�・*/
    UFUNCTION(BlueprintCallable, Category = "Turn|State")
    bool IsInputOpen_Server() const;

protected:
    //==========================================================================
    // PathFinder隗｣豎ｺ繝ｻ逕滓�E・医ヵ繧�E�繝ｼ繝ｫ繝�Eャ繧�E�逕ｨ・・    //==========================================================================
    void ResolveOrSpawnPathFinder();
    
    //==========================================================================
    // UnitManager隗｣豎ｺ繝ｻ逕滓�E・医ヵ繧�E�繝ｼ繝ｫ繝�Eャ繧�E�逕ｨ・・    //==========================================================================
    void ResolveOrSpawnUnitManager();

public:
    // Public access for TurnCorePhaseManager
    bool DispatchResolvedMove(const FResolvedAction& Action);

protected:
    //==========================================================================
    // Phase 5: Gate蜀阪が繝ｼ繝励Φ
    //==========================================================================

    void OnPlayerMoveCompleted(const FGameplayEventData* Payload);

    //==========================================================================
    // Phase 4: 繧�E�繝ｼ繝ｳ騾�E�陦後�E莠碁㍾骰�E�
    //==========================================================================

    bool CanAdvanceTurn(int32 TurnId) const;

    //==========================================================================
    // 繧�E�繝ｼ繝ｳ騾�E�謐礼憾諷・    //==========================================================================

    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_CurrentTurnId, Category = "Turn")
    int32 CurrentTurnId = 0;

    /** 蛻晏屓繧�E�繝ｼ繝ｳ髢句�E�九�E驥崎､・亟豁E��繝輔Λ繧�E�・医し繝ｼ繝�E�E��E�髯仙�E縺�E�縺�E�菴�E�逕ｨ・・*/
    UPROPERTY(Replicated)
    bool bFirstTurnStarted = false;
    /** StartFirstTurn縺�E�繝ｪ繝医Λ繧�E�逕ｨ */
    FTimerHandle StartFirstTurnRetryHandle;
    int32 StartFirstTurnRetryCount = 0;

    // 繝励Ξ繧�E�繝､繝ｼ遘ｻ蜍募�E�御�E�・-> 謨�E�繝輔ぉ繝ｼ繧�E�遘ｻ陦後�E霁E��驕�E�E��E�襍ｷ蜍�E畁E
    FTimerHandle EnemyPhaseKickoffHandle;

    //==========================================================================
    // 繝�EΜ繧�E�繝ｼ繝医ワ繝ｳ繝峨΁E
    //==========================================================================

    FDelegateHandle AllMovesFinishedHandle;
    FDelegateHandle PlayerMoveCompletedHandle;


    //==========================================================================
    // Helper髢�E�謨�E�
    //==========================================================================

    AGridPathfindingLibrary* FindPathFinder(UWorld* World) const;
    UAbilitySystemComponent* GetPlayerASC() const;

    /** ASC貁E���E�繧貞ｾ・▲縺�E�蛻晏屓繧�E�繝ｼ繝ｳ繧帝幕蟋・*/
    UFUNCTION()
    void TryStartFirstTurnAfterASCReady();

    //==========================================================================
    // Phase螳溯�E�・    //==========================================================================

    void ExecuteSequentialPhase();
    void ExecuteSimultaneousPhase();
    void ExecuteMovePhase();
    void HandleManualMoveFinished(AUnitBase* Unit);
    void RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback);
    void FinalizePlayerMove(AActor* CompletedActor);
    TMap<AUnitBase*, FDelegateHandle> ActiveMoveDelegates;
    TSet<TWeakObjectPtr<AUnitBase>> PendingPlayerFallbackMoves;
    void ExecuteAttacks();
    void EndEnemyTurn();
    void ExecutePlayerMove();
    void OnPlayerMoveAccepted();
    
    // 笘�E・笘�E邨�E�蜷茨�E�哂ctionExecutorSubsystem縺�E�SendPlayerMove讖溯・繧堤�E��E�邂｡ 笘�E・笘�E    bool SendPlayerMove(const FPlayerCommand& Command);
    
    // 笘�E・笘�E邨�E�蜷茨�E�哺oveConflictRuleSet縺�E�讖溯・繧堤�E��E�邂｡ 笘�E・笘�E    /** 繧�E�繧�E�繧�E�繝ｼ縺�E�蜁E��蜈亥�E��E�繧貞叙蠕�E*/
    int32 GetMovePriority(const FGameplayTagContainer& ActorTags) const;
    
    /** 蜈･繧梧崛繧上ｊ蜿�E�閭�E�縺句愛螳・*/
    bool CanSwapActors(const FGameplayTagContainer& ActorA, const FGameplayTagContainer& ActorB) const;
    
    /** 謚ｼ縺怜�E縺怜庁E���E�縺句愛螳・*/
    bool CanPushActor(const FGameplayTagContainer& Pusher, const FGameplayTagContainer& Pushed) const;

    /** Barrier完亁E��にInProgressタグが残留してぁE��ぁE��チェチE��し、忁E��なら強制除去する */
    void ClearResidualInProgressTags();

    //==========================================================================
    // Dynamic 繝�EΜ繧�E�繝ｼ繝医ワ繝ｳ繝峨΁E
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

    // UI譖ｴ譁E��蟁E��畑繝ｭ繝ｼ繧�E�繝ｫ繝上Φ繝峨Λ�E医ち繧�E�謫堺�E�懊�E遖∵�E��E�・・    void OnWaitingForPlayerInputChanged_Local(bool bNow);
    // 讓ｩ螽∫嶌蠖難�E�医し繝ｼ繝�Eor Standalone・峨〒縺�E�縺�E� WaitingForPlayerInput 繧貞､画峩
    void SetWaitingForPlayerInput_ServerLike(bool bNew);

    //==========================================================================
    // Subsystem・医く繝｣繝�Eす繝･・・    //==========================================================================

    UPROPERTY()
    TObjectPtr<UEnemyAISubsystem> EnemyAISubsystem = nullptr;

    // 笘�E・笘�EActionExecutorSubsystem 縺�E� TurnPhaseManagerComponent 縺�E�蟁E��惠縺励↑縺・◁E��√さ繝｡繝ｳ繝医ぁE���E�繝�E    // 蠢・�E�√�E蠢懊§縺�E�縲・cpp蛛ｴ縺�E�驕ｩ蛻・↑繧�E�繝ｩ繧�E�縺�E�鄂ｮ縺肴鋤縺医※縺上□縺輔！E
    // UPROPERTY(Transient)
    // TObjectPtr<UActionExecutorSubsystem> ActionExecutor = nullptr;

    // UPROPERTY(Transient)
    // TObjectPtr<UTurnPhaseManagerComponent> PhaseManager = nullptr;

    UPROPERTY(Transient)
    bool bTurnContinuing = false;

    //==========================================================================
    // 繧�E�繧�E�繝｡繝ｳ繝�E・螟画焁E
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

    /** ★★★ Week 1: ターン進行・AP管理Subsystem（2025-11-09リファクタリング） */
    UPROPERTY(Transient)
    TObjectPtr<UTurnFlowCoordinator> TurnFlowCoordinator = nullptr;

    //==========================================================================
    // Phase 2: WindowId讀懁E���E�逕ｨ縺�E�蜀・Κ繝倥Ν繝代・
    //==========================================================================

    void ProcessPlayerCommand(const FPlayerCommand& Command);

    void StartTurnMoves(int32 TurnId);

    //==========================================================================
    // 蜀・Κ迥�E�諷・    //==========================================================================

    // ★★★ Phase 4: 未使用変数削除（2025-11-09） ★★★
    // 削除: CachedEnemiesWeak（使用されていない）
    // 削除: RecollectEnemiesTimerHandle（使用されていない）
    // 削除: PendingTeamCountLast（使用されていない）
    // 削除: CollectEnemiesRetryCount（使用されていない）

    // ★★★ Phase 5補完: EndTurnリトライ連打防止（2025-11-09） ★★★
    /**
     * EndEnemyTurn のリトライが既にスケジュールされているかを示すフラグ
     * - true: リトライタイマーが既に設定済み → 追加のリトライは抑止
     * - false: リトライ未設定 → 新規リトライを許可
     * AdvanceTurnAndRestart() で false にリセット
     */
    bool bEndTurnPosted = false;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FEnemyIntent> CachedIntents;

    mutable TWeakObjectPtr<AActor> CachedPlayerActor;

    FTimerHandle AbilityWaitTimerHandle;

    //==========================================================================
    // 繧�E�繧�E�蛻晁E��蛹夜未謨�E�
    //==========================================================================

    void InitGameplayTags();

    static float EncodeDir8ToMagnitude(const FVector2D& Dir);
    FVector2D CalculateDirectionFromTargetCell(const FIntPoint& TargetCell);

    // 譁E��蜷代・謨�E�謨�E�繝代ャ繧�E�/繧�E�繝ｳ繝代ャ繧�E�・郁E���E�蟾�E�繧�E�繝ｭ驕区成�E・    static int32 PackDirection(const FIntPoint& Dir);
    static FIntPoint UnpackDirection(int32 Magnitude);

    // ★★★ Phase 4: 未使用変数削除（2025-11-09） ★★★
    // 削除: bEnemyTurnEnding（使用されていない）
};




