#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Turn/TurnSystemTypes.h"
#include "Debug/DebugObserverInterface.h"
#include "TurnSystemInterfaces.generated.h"

// ========================================
// 敵収集
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UEnemyCollector : public UInterface
{
    GENERATED_BODY()
};

class IEnemyCollector
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Enemy")
    void Collect(TArray<AActor*>& OutEnemies);
};

// ========================================
// 敵ソート
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UEnemySorter : public UInterface
{
    GENERATED_BODY()
};

class IEnemySorter
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Enemy")
    void Sort(UPARAM(ref) TArray<AActor*>& InOutEnemies);
};

// ========================================
// 観測ステージ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UObservationStage : public UInterface
{
    GENERATED_BODY()
};

class IObservationStage
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Observation")
    void Process(AActor* OwnerActor, UPARAM(ref) FEnemyObservation& Observation);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Observation")
    int32 GetExecutionOrder() const;
};

// ========================================
// 意図ソース
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UIntentSource : public UInterface
{
    GENERATED_BODY()
};

class IIntentSource
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    void BuildCandidates(AActor* OwnerActor, const FEnemyObservation& Observation, UPARAM(ref) TArray<FEnemyIntentCandidate>& OutCandidates);
};

// ========================================
// 意図スコアリング
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UIntentScorer : public UInterface
{
    GENERATED_BODY()
};

class IIntentScorer
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    float Score(const FEnemyIntent& Intent, const FEnemyObservation& Observation);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    float GetWeight() const;
};

// ========================================
// 意図フィルタ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UIntentFilter : public UInterface
{
    GENERATED_BODY()
};

class IIntentFilter
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    bool ShouldKeep(const FEnemyIntentCandidate& Candidate);
};

// ========================================
// 意図リデューサ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UIntentReducer : public UInterface
{
    GENERATED_BODY()
};

class IIntentReducer
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    void Reduce(UPARAM(ref) TArray<FEnemyIntentCandidate>& InOutCandidates);
};

// ========================================
// 意図選択マネージャ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UIntentSelectionManager : public UInterface
{
    GENERATED_BODY()
};

class IIntentSelectionManager
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    FEnemyIntent SelectBestIntent(const TArray<FEnemyIntentCandidate>& Candidates);
};

// ========================================
// 前処理フック
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPreSelectionHook : public UInterface
{
    GENERATED_BODY()
};

class IPreSelectionHook
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    void OnBeforeSelection(const FEnemyObservation& Observation, UPARAM(ref) TArray<FEnemyIntentCandidate>& Candidates);
};

// ========================================
// 後処理フック
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPostSelectionHook : public UInterface
{
    GENERATED_BODY()
};

class IPostSelectionHook
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Intent")
    void OnAfterSelection(const FEnemyIntent& ChosenIntent);
};

// ========================================
// 行動計画フェーズハンドラ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPlanningPhaseHandler : public UInterface
{
    GENERATED_BODY()
};

class IPlanningPhaseHandler
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Planning")
    void HandlePlanningPhase(AActor* OwnerActor, const FEnemyObservation& Observation, UPARAM(ref) FEnemyIntent& Intent);
};

// ========================================
// 移動生成
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UMoveGenerator : public UInterface
{
    GENERATED_BODY()
};

class IMoveGenerator
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Move")
    bool GenerateMove(AActor* OwnerActor, const FEnemyIntent& Intent, UPARAM(ref) FPendingMove& OutMove);
};

// ========================================
// 移動フィルタ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UMoveFilter : public UInterface
{
    GENERATED_BODY()
};

class IMoveFilter
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Move")
    bool ShouldKeepMove(const FPendingMove& CandidateMove);
};

// ========================================
// 移動検証
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UMoveValidator : public UInterface
{
    GENERATED_BODY()
};

class IMoveValidator
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Move")
    bool CanApplyMove(const FPendingMove& Move) const;
};

// ========================================
// 移動実行
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UMoveExecutor : public UInterface
{
    GENERATED_BODY()
};

class IMoveExecutor
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Move")
    void ExecuteMove(AActor* OwnerActor, const FPendingMove& Move);
};

// ========================================
// 移動完了ハンドラ
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UMoveCompletionHandler : public UInterface
{
    GENERATED_BODY()
};

class IMoveCompletionHandler
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Move")
    void OnMoveCompleted(AActor* OwnerActor, const FPendingMove& Move);
};

// ========================================
// 前攻撃検証
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPreAttackValidator : public UInterface
{
    GENERATED_BODY()
};

class IPreAttackValidator
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Attack")
    bool CanExecuteAttack(AActor* Attacker, AActor* Target, FText& OutReason);
};

// ========================================
// 攻撃実行
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UAttackExecutor : public UInterface
{
    GENERATED_BODY()
};

class IAttackExecutor
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Attack")
    void ExecuteAttack(AActor* Attacker, AActor* Target, FGameplayTag AbilityTag);
};

// ========================================
// 攻撃後処理
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPostAttackHandler : public UInterface
{
    GENERATED_BODY()
};

class IPostAttackHandler
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Attack")
    void OnAttackResolved(AActor* Attacker, AActor* Target);
};

// ========================================
// ターンフローポリシー
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UTurnFlowPolicy : public UInterface
{
    GENERATED_BODY()
};

class ITurnFlowPolicy
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Flow")
    bool ShouldSplitTurn(const FGameplayTag& PlayerActionTag, const TArray<FIntPoint>& PlannedPath);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Flow")
    FGameplayTag NextPhase(FGameplayTag CurrentPhase, const FTurnContext& Context);
};

// ========================================
// フェーズ遷移条件
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UPhaseTransitionCondition : public UInterface
{
    GENERATED_BODY()
};

class IPhaseTransitionCondition
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Flow")
    bool IsSatisfied(FGameplayTag Phase, const FTurnContext& Context);
};

// ========================================
// 割り込み機会
// ========================================
UINTERFACE(BlueprintType, MinimalAPI)
class UInterruptOpportunity : public UInterface
{
    GENERATED_BODY()
};

class IInterruptOpportunity
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Interrupt")
    bool ShouldTrigger(AActor* Reactor, const FPendingMove& TriggeringMove);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Interrupt")
    FEnemyIntent BuildInterruptIntent(AActor* Reactor, const FPendingMove& TriggeringMove);
};
