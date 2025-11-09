# 巨大クラス分割 - 具体的実装例とマイグレーションガイド

**対象**: `AGameTurnManagerBase` の分割（最優先クラス）

---

## 📖 目次

1. [現状コードの問題点](#現状コードの問題点)
2. [分割後のクラス構成例](#分割後のクラス構成例)
3. [具体的コード例](#具体的コード例)
4. [ステップバイステップ移行ガイド](#ステップバイステップ移行ガイド)
5. [Blueprint移行例](#blueprint移行例)
6. [関数マッピングテーブル](#関数マッピングテーブル)

---

## 🔴 現状コードの問題点

### Before: AGameTurnManagerBase (4,230行)

```cpp
// GameTurnManagerBase.h の一部抜粋（現状）

UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    //==========================================================================
    // ❌ 問題1: ターン進行、入力処理、敵AI、ダンジョン、フェーズ管理が混在
    //==========================================================================

    // ターン進行
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void StartTurn();

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void AdvanceTurnAndRestart();

    UPROPERTY(Replicated)
    int32 CurrentTurnId = 0;

    // プレイヤー入力
    UFUNCTION(BlueprintCallable, Category = "Turn|Player")
    void NotifyPlayerInputReceived();

    UPROPERTY(Replicated)
    bool WaitingForPlayerInput = false;

    // 敵AI
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void CollectEnemies();

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void BuildObservations();

    UPROPERTY()
    TArray<TObjectPtr<AActor>> CachedEnemies;

    // ダンジョン
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Flow")
    bool NextFloor();

    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSystem;

    // フェーズ管理
    UFUNCTION(BlueprintCallable, Category = "Turn")
    void BeginPhase(FGameplayTag PhaseTag);

    UPROPERTY(BlueprintReadOnly, Category = "Turn|State")
    FGameplayTag CurrentPhase;

    // AP管理
    UPROPERTY(Replicated)
    int32 PlayerAP = 0;

    // 移動実行
    void ExecuteSequentialPhase();
    void ExecuteSimultaneousPhase();

    // 攻撃実行
    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Enemy")
    void ExecuteEnemyAttacks();

    // システムフック
    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnCombineSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Turn|Hooks")
    void OnPotSystemUpdate(const FTurnContext& Context);

    // ... 他にも10以上の異なる責務が混在 ...

    //==========================================================================
    // ❌ 問題2: 膨大なメンバー変数（50以上）
    //==========================================================================
protected:
    UPROPERTY()
    TObjectPtr<UEnemyTurnDataSubsystem> EnemyTurnData;

    UPROPERTY()
    TObjectPtr<UEnemyAISubsystem> EnemyAISubsystem;

    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> CachedEnemiesWeak;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FEnemyIntent> CachedIntents;

    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, FIntPoint> PendingMoveReservations;

    UPROPERTY()
    FSimulBatch CurrentSimulBatch;

    FTimerHandle EnemyPhaseKickoffHandle;
    FTimerHandle AbilityWaitTimerHandle;
    FTimerHandle RecollectEnemiesTimerHandle;

    // ... 他にも30以上のメンバー変数 ...
};
```

**問題点**:
- ✗ 単一クラスに15以上の責務
- ✗ 4,230行 → 理解困難、バグの温床
- ✗ テスト不可能（モックが困難）
- ✗ 並行開発不可（コンフリクト多発）
- ✗ 変更の影響範囲が不明

---

## ✅ 分割後のクラス構成例

### After: 責務ごとに分離（9クラス）

```
AGameTurnManagerBase (500行 - 調整役のみ)
  ├─ UTurnFlowCoordinator (300行)          [ターン進行・AP管理]
  ├─ UPlayerInputProcessor (250行)        [入力処理・検証]
  ├─ UEnemyTurnCoordinator (400行)        [敵AI統合]
  ├─ UDungeonIntegrationManager (200行)   [ダンジョン連携]
  ├─ UPhaseStateMachine (350行)           [フェーズ遷移]
  ├─ UMovePhaseExecutor (400行)           [移動実行]
  ├─ UAttackPhaseExecutor (300行)         [攻撃実行]
  ├─ USystemHooksManager (250行)          [システムフック]
  └─ UNetworkSyncManager (200行)          [レプリケーション]
```

---

## 📝 具体的コード例

### 例1: UTurnFlowCoordinator (ターン進行管理)

#### After: 分離後のSubsystem

```cpp
// TurnFlowCoordinator.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TurnFlowCoordinator.generated.h"

/**
 * UTurnFlowCoordinator
 *
 * 責務:
 * - ターンID・InputWindowIDの管理
 * - ターンの開始・終了・進行
 * - AP（アクションポイント）管理
 * - ターンインデックス管理
 */
UCLASS()
class LYRAGAME_API UTurnFlowCoordinator : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //==========================================================================
    // ターンID・WindowID管理
    //==========================================================================

    /** 現在のターンIDを取得 */
    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    /** 現在の入力ウィンドウIDを取得 */
    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const { return InputWindowId; }

    /** 現在のターンインデックスを取得（レガシー互換用） */
    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnIndex() const { return CurrentTurnIndex; }

    //==========================================================================
    // ターン進行制御
    //==========================================================================

    /** 最初のターンを開始（初回のみ呼び出し） */
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
    void StartFirstTurn();

    /** ターンを開始 */
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
    void StartTurn();

    /** ターンを終了 */
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
    void EndTurn();

    /** ターンを進める（TurnId++） */
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
    void AdvanceTurn();

    /** ターン進行可能か判定 */
    UFUNCTION(BlueprintPure, Category = "Turn|Flow")
    bool CanAdvanceTurn(int32 TurnId) const;

    //==========================================================================
    // AP（アクションポイント）管理
    //==========================================================================

    /** プレイヤーAPを消費 */
    UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
    void ConsumePlayerAP(int32 Amount);

    /** プレイヤーAPを回復 */
    UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
    void RestorePlayerAP(int32 Amount);

    /** プレイヤーAPをターン開始時に全回復 */
    UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
    void ResetPlayerAPForTurn();

    /** APが十分か判定 */
    UFUNCTION(BlueprintPure, Category = "Turn|AP")
    bool HasSufficientAP(int32 Required) const;

    /** 現在のプレイヤーAPを取得 */
    UFUNCTION(BlueprintPure, Category = "Turn|AP")
    int32 GetPlayerAP() const { return PlayerAP; }

    /** 1ターンあたりの最大APを取得 */
    UFUNCTION(BlueprintPure, Category = "Turn|AP")
    int32 GetPlayerAPPerTurn() const { return PlayerAPPerTurn; }

    //==========================================================================
    // 敵フェーズキューイング
    //==========================================================================

    /** 敵フェーズをキューに追加 */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void QueueEnemyPhase();

    /** 敵フェーズがキューされているか */
    UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
    bool IsEnemyPhaseQueued() const { return bEnemyPhaseQueued; }

    /** 敵フェーズキューをクリア */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
    void ClearEnemyPhaseQueue();

    //==========================================================================
    // デリゲート
    //==========================================================================

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, int32 /*TurnIndex*/);
    FOnTurnStarted OnTurnStarted;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnTurnEnded, int32 /*TurnId*/);
    FOnTurnEnded OnTurnEnded;

protected:
    //==========================================================================
    // レプリケーション設定
    //==========================================================================

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual bool IsSupportedForNetworking() const override { return true; }

    //==========================================================================
    // レプリケーション通知
    //==========================================================================

    UFUNCTION()
    void OnRep_CurrentTurnId();

    UFUNCTION()
    void OnRep_InputWindowId();

    //==========================================================================
    // メンバー変数
    //==========================================================================

    /** 現在のターンID（Replicated） */
    UPROPERTY(ReplicatedUsing = OnRep_CurrentTurnId, BlueprintReadOnly, Category = "Turn")
    int32 CurrentTurnId = 0;

    /** 現在の入力ウィンドウID（Replicated） */
    UPROPERTY(ReplicatedUsing = OnRep_InputWindowId, BlueprintReadOnly, Category = "Turn")
    int32 InputWindowId = 0;

    /** 現在のターンインデックス（レガシー互換用） */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn")
    int32 CurrentTurnIndex = 0;

    /** 現在のプレイヤーAP */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn|AP")
    int32 PlayerAP = 0;

    /** 1ターンあたりの最大AP */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turn|AP")
    int32 PlayerAPPerTurn = 1;

    /** 敵フェーズがキューされているか */
    UPROPERTY()
    bool bEnemyPhaseQueued = false;

    /** 最初のターンが開始されたか（リトライ防止用） */
    UPROPERTY(Replicated)
    bool bFirstTurnStarted = false;
};
```

#### 実装例 (.cpp)

```cpp
// TurnFlowCoordinator.cpp

#include "TurnFlowCoordinator.h"
#include "Net/UnrealNetwork.h"

void UTurnFlowCoordinator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UTurnFlowCoordinator, CurrentTurnId);
    DOREPLIFETIME(UTurnFlowCoordinator, InputWindowId);
    DOREPLIFETIME(UTurnFlowCoordinator, CurrentTurnIndex);
    DOREPLIFETIME(UTurnFlowCoordinator, PlayerAP);
    DOREPLIFETIME(UTurnFlowCoordinator, bFirstTurnStarted);
}

void UTurnFlowCoordinator::StartFirstTurn()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode())
    {
        return; // サーバーのみ実行
    }

    if (bFirstTurnStarted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TurnFlowCoordinator] StartFirstTurn already called, skipping"));
        return;
    }

    bFirstTurnStarted = true;
    CurrentTurnId = 1;
    CurrentTurnIndex = 0;
    ResetPlayerAPForTurn();

    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] First turn started: TurnId=%d"), CurrentTurnId);

    // デリゲート通知
    OnTurnStarted.Broadcast(CurrentTurnIndex);
}

void UTurnFlowCoordinator::StartTurn()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode())
    {
        return; // サーバーのみ実行
    }

    ResetPlayerAPForTurn();

    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn started: TurnId=%d, TurnIndex=%d"), CurrentTurnId, CurrentTurnIndex);

    // デリゲート通知
    OnTurnStarted.Broadcast(CurrentTurnIndex);
}

void UTurnFlowCoordinator::EndTurn()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode())
    {
        return; // サーバーのみ実行
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn ended: TurnId=%d"), CurrentTurnId);

    // デリゲート通知
    OnTurnEnded.Broadcast(CurrentTurnId);
}

void UTurnFlowCoordinator::AdvanceTurn()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode())
    {
        return; // サーバーのみ実行
    }

    ++CurrentTurnId;
    ++CurrentTurnIndex;

    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn advanced: TurnId=%d, TurnIndex=%d"), CurrentTurnId, CurrentTurnIndex);
}

bool UTurnFlowCoordinator::CanAdvanceTurn(int32 TurnId) const
{
    return TurnId == CurrentTurnId;
}

void UTurnFlowCoordinator::ConsumePlayerAP(int32 Amount)
{
    PlayerAP = FMath::Max(0, PlayerAP - Amount);
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP consumed: %d, Remaining: %d"), Amount, PlayerAP);
}

void UTurnFlowCoordinator::RestorePlayerAP(int32 Amount)
{
    PlayerAP = FMath::Min(PlayerAPPerTurn, PlayerAP + Amount);
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP restored: %d, Current: %d"), Amount, PlayerAP);
}

void UTurnFlowCoordinator::ResetPlayerAPForTurn()
{
    PlayerAP = PlayerAPPerTurn;
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP reset to: %d"), PlayerAP);
}

bool UTurnFlowCoordinator::HasSufficientAP(int32 Required) const
{
    return PlayerAP >= Required;
}

void UTurnFlowCoordinator::QueueEnemyPhase()
{
    bEnemyPhaseQueued = true;
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Enemy phase queued"));
}

void UTurnFlowCoordinator::ClearEnemyPhaseQueue()
{
    bEnemyPhaseQueued = false;
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Enemy phase queue cleared"));
}

void UTurnFlowCoordinator::OnRep_CurrentTurnId()
{
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] OnRep_CurrentTurnId: %d"), CurrentTurnId);
}

void UTurnFlowCoordinator::OnRep_InputWindowId()
{
    UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] OnRep_InputWindowId: %d"), InputWindowId);
}
```

---

### 例2: UPlayerInputProcessor (入力処理)

```cpp
// PlayerInputProcessor.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "PlayerInputProcessor.generated.h"

/**
 * UPlayerInputProcessor
 *
 * 責務:
 * - 入力ウィンドウの開閉
 * - プレイヤーコマンドの検証
 * - WindowIdの整合性チェック
 * - 入力受付状態の管理
 */
UCLASS()
class LYRAGAME_API UPlayerInputProcessor : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //==========================================================================
    // 入力ウィンドウ管理
    //==========================================================================

    /** 入力ウィンドウを開く */
    UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
    void OpenInputWindow(int32 TurnId);

    /** 入力ウィンドウを閉じる */
    UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
    void CloseInputWindow();

    /** 入力ウィンドウが開いているか */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsInputWindowOpen() const { return bWaitingForPlayerInput; }

    /** サーバー側で入力受付中か（Authority専用） */
    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsInputOpen_Server() const;

    //==========================================================================
    // コマンド検証
    //==========================================================================

    /** コマンドのWindowIdが有効か検証 */
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool ValidateCommand(const FPlayerCommand& Command, int32 ExpectedWindowId);

    //==========================================================================
    // コマンド処理
    //==========================================================================

    /** プレイヤーコマンドを処理 */
    UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
    void ProcessPlayerCommand(const FPlayerCommand& Command);

    /** 入力受付通知 */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void NotifyPlayerInputReceived();

    //==========================================================================
    // キャッシュ取得
    //==========================================================================

    /** キャッシュされたプレイヤーコマンドを取得 */
    UFUNCTION(BlueprintPure, Category = "Input")
    FPlayerCommand GetCachedPlayerCommand() const { return CachedPlayerCommand; }

    //==========================================================================
    // デリゲート
    //==========================================================================

    DECLARE_MULTICAST_DELEGATE(FOnPlayerInputReceived);
    FOnPlayerInputReceived OnPlayerInputReceived;

protected:
    //==========================================================================
    // レプリケーション
    //==========================================================================

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual bool IsSupportedForNetworking() const override { return true; }

    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    //==========================================================================
    // 内部ヘルパー
    //==========================================================================

    /** WindowIdが有効か検証 */
    bool IsValidWindowId(int32 WindowId) const;

    /** 入力ゲートを適用（ASCへのタグ追加/削除） */
    void ApplyWaitInputGate(bool bOpen);

    /** サーバー権限で入力受付状態を設定 */
    void SetWaitingForPlayerInput_ServerLike(bool bNew);

    //==========================================================================
    // メンバー変数
    //==========================================================================

    /** 入力待機中か（Replicated） */
    UPROPERTY(ReplicatedUsing = OnRep_WaitingForPlayerInput, BlueprintReadOnly, Category = "Input")
    bool bWaitingForPlayerInput = false;

    /** キャッシュされたプレイヤーコマンド */
    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FPlayerCommand CachedPlayerCommand;
};
```

---

### 例3: AGameTurnManagerBase (リファクタリング後)

#### After: 各Subsystemへの転送役のみ

```cpp
// GameTurnManagerBase.h (リファクタリング後 - 500行程度)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameTurnManagerBase.generated.h"

// 前方宣言
class UTurnFlowCoordinator;
class UPlayerInputProcessor;
class UEnemyTurnCoordinator;
class UPhaseStateMachine;
class UMovePhaseExecutor;
class UAttackPhaseExecutor;
class UDungeonIntegrationManager;
class USystemHooksManager;
class UNetworkSyncManager;

/**
 * AGameTurnManagerBase (リファクタリング後)
 *
 * 責務:
 * - 各Subsystem/Componentの初期化・取得
 * - 旧APIの互換性レイヤー（Deprecated関数）
 * - 高レベルのターン進行調整
 *
 * ★★★ 実際の処理は各Subsystemに委譲 ★★★
 */
UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    AGameTurnManagerBase();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    //==========================================================================
    // ✅ 互換性レイヤー: 旧APIを新Subsystemに転送
    //==========================================================================

    /** 【互換性】ターン開始（UTurnFlowCoordinatorに転送） */
    UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (DeprecatedFunction, DeprecationMessage = "Use UTurnFlowCoordinator::StartTurn instead"))
    void StartTurn();

    /** 【互換性】現在のターンIDを取得（UTurnFlowCoordinatorに転送） */
    UFUNCTION(BlueprintPure, Category = "Turn", meta = (DeprecatedFunction, DeprecationMessage = "Use UTurnFlowCoordinator::GetCurrentTurnId instead"))
    int32 GetCurrentTurnId() const;

    /** 【互換性】入力ウィンドウを開く（UPlayerInputProcessorに転送） */
    UFUNCTION(BlueprintCallable, Category = "Turn|Player", meta = (DeprecatedFunction, DeprecationMessage = "Use UPlayerInputProcessor::OpenInputWindow instead"))
    void OpenInputWindow();

    /** 【互換性】敵を収集（UEnemyTurnCoordinatorに転送） */
    UFUNCTION(BlueprintCallable, Category = "Turn|Enemy", meta = (DeprecatedFunction, DeprecationMessage = "Use UEnemyTurnCoordinator::CollectEnemies instead"))
    void CollectEnemies();

    /** 【互換性】フェーズ開始（UPhaseStateMachineに転送） */
    UFUNCTION(BlueprintCallable, Category = "Turn", meta = (DeprecatedFunction, DeprecationMessage = "Use UPhaseStateMachine::BeginPhase instead"))
    void BeginPhase(FGameplayTag PhaseTag);

    // ... 他の互換性関数も同様 ...

    //==========================================================================
    // ✅ 新規推奨API: 直接Subsystemを取得
    //==========================================================================

    /** TurnFlowCoordinatorを取得（推奨） */
    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    UTurnFlowCoordinator* GetTurnFlowCoordinator() const { return TurnFlowCoordinator; }

    /** PlayerInputProcessorを取得（推奨） */
    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    UPlayerInputProcessor* GetPlayerInputProcessor() const { return PlayerInputProcessor; }

    /** EnemyTurnCoordinatorを取得（推奨） */
    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    UEnemyTurnCoordinator* GetEnemyTurnCoordinator() const { return EnemyTurnCoordinator; }

    /** PhaseStateMachineを取得（推奨） */
    UFUNCTION(BlueprintPure, Category = "Turn|Services")
    UPhaseStateMachine* GetPhaseStateMachine() const { return PhaseStateMachine; }

    // ... 他のSubsystemも同様 ...

protected:
    //==========================================================================
    // Subsystem/Component参照
    //==========================================================================

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UTurnFlowCoordinator> TurnFlowCoordinator;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UPlayerInputProcessor> PlayerInputProcessor;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UEnemyTurnCoordinator> EnemyTurnCoordinator;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UPhaseStateMachine> PhaseStateMachine;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UMovePhaseExecutor> MovePhaseExecutor;

    UPROPERTY(BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UAttackPhaseExecutor> AttackPhaseExecutor;

    UPROPERTY(Instanced, BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UDungeonIntegrationManager> DungeonIntegrationManager;

    UPROPERTY(Instanced, BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<USystemHooksManager> SystemHooksManager;

    UPROPERTY(Instanced, BlueprintReadOnly, Category = "Turn|Services")
    TObjectPtr<UNetworkSyncManager> NetworkSyncManager;

    //==========================================================================
    // 初期化
    //==========================================================================

    /** 全Subsystem/Componentの初期化 */
    void InitializeSubsystems();
};
```

#### 実装例 (.cpp)

```cpp
// GameTurnManagerBase.cpp (リファクタリング後)

#include "GameTurnManagerBase.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/PlayerInputProcessor.h"
#include "Turn/EnemyTurnCoordinator.h"
#include "Turn/PhaseStateMachine.h"
#include "Turn/MovePhaseExecutor.h"
#include "Turn/AttackPhaseExecutor.h"
#include "Turn/DungeonIntegrationManager.h"
#include "Turn/SystemHooksManager.h"
#include "Turn/NetworkSyncManager.h"

AGameTurnManagerBase::AGameTurnManagerBase()
{
    // Componentの作成（Subsystemは自動生成）
    DungeonIntegrationManager = CreateDefaultSubobject<UDungeonIntegrationManager>(TEXT("DungeonIntegrationManager"));
    SystemHooksManager = CreateDefaultSubobject<USystemHooksManager>(TEXT("SystemHooksManager"));
    NetworkSyncManager = CreateDefaultSubobject<UNetworkSyncManager>(TEXT("NetworkSyncManager"));
}

void AGameTurnManagerBase::BeginPlay()
{
    Super::BeginPlay();

    InitializeSubsystems();
}

void AGameTurnManagerBase::InitializeSubsystems()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Subsystemを取得（自動生成される）
    TurnFlowCoordinator = World->GetSubsystem<UTurnFlowCoordinator>();
    PlayerInputProcessor = World->GetSubsystem<UPlayerInputProcessor>();
    EnemyTurnCoordinator = World->GetSubsystem<UEnemyTurnCoordinator>();
    PhaseStateMachine = World->GetSubsystem<UPhaseStateMachine>();
    MovePhaseExecutor = World->GetSubsystem<UMovePhaseExecutor>();
    AttackPhaseExecutor = World->GetSubsystem<UAttackPhaseExecutor>();

    // 初期化確認
    if (!TurnFlowCoordinator || !PlayerInputProcessor || !EnemyTurnCoordinator)
    {
        UE_LOG(LogTemp, Error, TEXT("[GameTurnManagerBase] Failed to initialize critical subsystems!"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[GameTurnManagerBase] All subsystems initialized successfully"));
}

//==========================================================================
// 互換性レイヤー実装
//==========================================================================

void AGameTurnManagerBase::StartTurn()
{
    if (TurnFlowCoordinator)
    {
        TurnFlowCoordinator->StartTurn();
    }
}

int32 AGameTurnManagerBase::GetCurrentTurnId() const
{
    if (TurnFlowCoordinator)
    {
        return TurnFlowCoordinator->GetCurrentTurnId();
    }
    return 0;
}

void AGameTurnManagerBase::OpenInputWindow()
{
    if (PlayerInputProcessor && TurnFlowCoordinator)
    {
        PlayerInputProcessor->OpenInputWindow(TurnFlowCoordinator->GetCurrentTurnId());
    }
}

void AGameTurnManagerBase::CollectEnemies()
{
    if (EnemyTurnCoordinator)
    {
        EnemyTurnCoordinator->CollectEnemies();
    }
}

void AGameTurnManagerBase::BeginPhase(FGameplayTag PhaseTag)
{
    if (PhaseStateMachine)
    {
        PhaseStateMachine->BeginPhase(PhaseTag);
    }
}

// ... 他の互換性関数も同様に転送 ...
```

---

## 📊 関数マッピングテーブル

### AGameTurnManagerBase → 各Subsystem

| 旧関数 (AGameTurnManagerBase) | 新Subsystem | 新関数 |
|-------------------------------|------------|--------|
| `StartTurn()` | UTurnFlowCoordinator | `StartTurn()` |
| `EndTurn()` | UTurnFlowCoordinator | `EndTurn()` |
| `AdvanceTurnAndRestart()` | UTurnFlowCoordinator | `AdvanceTurn()` |
| `GetCurrentTurnId()` | UTurnFlowCoordinator | `GetCurrentTurnId()` |
| `GetCurrentInputWindowId()` | UTurnFlowCoordinator | `GetCurrentInputWindowId()` |
| `OpenInputWindow()` | UPlayerInputProcessor | `OpenInputWindow(TurnId)` |
| `NotifyPlayerInputReceived()` | UPlayerInputProcessor | `NotifyPlayerInputReceived()` |
| `ProcessPlayerCommand()` | UPlayerInputProcessor | `ProcessPlayerCommand()` |
| `CollectEnemies()` | UEnemyTurnCoordinator | `CollectEnemies()` |
| `BuildObservations()` | UEnemyTurnCoordinator | `BuildObservations()` |
| `CollectIntents()` | UEnemyTurnCoordinator | `CollectIntents()` |
| `GetCachedEnemies()` | UEnemyTurnCoordinator | `GetCachedEnemies()` |
| `BeginPhase()` | UPhaseStateMachine | `BeginPhase()` |
| `EndPhase()` | UPhaseStateMachine | `EndPhase()` |
| `GetCurrentPhase()` | UPhaseStateMachine | `GetCurrentPhase()` |
| `ExecuteSequentialPhase()` | UMovePhaseExecutor | `ExecuteSequentialMoves()` |
| `ExecuteSimultaneousPhase()` | UMovePhaseExecutor | `ExecuteSimultaneousMoves()` |
| `ResolveConflicts()` | UMovePhaseExecutor | `ResolveConflicts()` |
| `ExecuteEnemyAttacks()` | UAttackPhaseExecutor | `ExecuteEnemyAttacks()` |
| `ExecuteAllyActions()` | UAttackPhaseExecutor | `ExecuteAllyAttacks()` |
| `NextFloor()` | UDungeonIntegrationManager | `NextFloor()` |
| `EnsureFloorGenerated()` | UDungeonIntegrationManager | `EnsureFloorGenerated()` |
| `OnCombineSystemUpdate()` | USystemHooksManager | `OnCombineSystemUpdate()` |
| `OnPotSystemUpdate()` | USystemHooksManager | `OnPotSystemUpdate()` |

---

## 🔄 ステップバイステップ移行ガイド

### Phase 1: 準備（Week 0）

#### Step 1.1: テストフレームワーク準備

```cpp
// Tests/TurnFlowCoordinatorTest.cpp

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Turn/TurnFlowCoordinator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurnFlowCoordinatorBasicTest, "Project.Turn.TurnFlowCoordinator.Basic", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTurnFlowCoordinatorBasicTest::RunTest(const FString& Parameters)
{
    // テストワールド作成
    UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(TestWorld);

    // Subsystem取得
    UTurnFlowCoordinator* TFC = TestWorld->GetSubsystem<UTurnFlowCoordinator>();
    TestNotNull(TEXT("TurnFlowCoordinator should exist"), TFC);

    // 初期状態確認
    TestEqual(TEXT("Initial TurnId should be 0"), TFC->GetCurrentTurnId(), 0);
    TestEqual(TEXT("Initial PlayerAP should be 0"), TFC->GetPlayerAP(), 0);

    // 最初のターン開始
    TFC->StartFirstTurn();
    TestEqual(TEXT("TurnId should be 1 after StartFirstTurn"), TFC->GetCurrentTurnId(), 1);
    TestEqual(TEXT("PlayerAP should be reset"), TFC->GetPlayerAP(), 1);

    // AP消費
    TFC->ConsumePlayerAP(1);
    TestEqual(TEXT("PlayerAP should be 0 after consumption"), TFC->GetPlayerAP(), 0);
    TestFalse(TEXT("Should not have sufficient AP"), TFC->HasSufficientAP(1));

    // クリーンアップ
    GEngine->DestroyWorldContext(TestWorld);
    TestWorld->DestroyWorld(false);

    return true;
}
```

#### Step 1.2: ブランチ作成

```bash
git checkout -b refactor/turn-manager-subsystems
```

---

### Phase 2: Subsystem実装（Week 1-3）

#### Step 2.1: UTurnFlowCoordinator作成（Day 1-2）

1. `Turn/TurnFlowCoordinator.h` と `Turn/TurnFlowCoordinator.cpp` を作成（上記コード例参照）
2. ビルド確認
3. 単体テスト作成・実行

```bash
# ビルド
cd /path/to/UnrealProject
"C:\Program Files\Epic Games\UE_5.4\Engine\Build\BatchFiles\Build.bat" RoguelikeDungeonEditor Win64 Development

# テスト実行（エディタから）
# Window → Test Automation → "Project.Turn.TurnFlowCoordinator.Basic" を実行
```

#### Step 2.2: UPlayerInputProcessor作成（Day 3-4）

同様に作成・テスト

#### Step 2.3: 他のSubsystemも順次作成（Day 5-15）

---

### Phase 3: 互換性レイヤー実装（Week 4）

#### Step 3.1: AGameTurnManagerBase に転送関数追加

```cpp
// GameTurnManagerBase.cpp に追加

void AGameTurnManagerBase::StartTurn()
{
    // ★★★ 新Subsystemに転送 ★★★
    if (UTurnFlowCoordinator* TFC = GetWorld()->GetSubsystem<UTurnFlowCoordinator>())
    {
        TFC->StartTurn();
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameTurnManagerBase] UTurnFlowCoordinator not found!"));
    }
}
```

#### Step 3.2: 既存の実装コードを削除

```cpp
// GameTurnManagerBase.cpp

// ❌ 削除: 旧実装
/*
void AGameTurnManagerBase::StartTurn()
{
    // 旧実装（500行のロジック）
    CurrentTurnId++;
    // ...
}
*/

// ✅ 置き換え: 転送関数のみ
void AGameTurnManagerBase::StartTurn()
{
    if (UTurnFlowCoordinator* TFC = GetWorld()->GetSubsystem<UTurnFlowCoordinator>())
    {
        TFC->StartTurn();
    }
}
```

---

### Phase 4: Blueprint移行（Week 5）

#### Step 4.1: 既存Blueprintの確認

Blueprintエディタで `AGameTurnManagerBase` を検索し、使用箇所をリスト化

#### Step 4.2: Blueprint修正例

**Before (旧API使用)**:
```
// BP_TurnManager (Blueprint)

Event BeginPlay
  ├─ Get Turn Manager
  └─ Call Function: Start Turn
```

**After (新Subsystem使用 - 推奨)**:
```
// BP_TurnManager (Blueprint)

Event BeginPlay
  ├─ Get World Subsystem: TurnFlowCoordinator
  └─ Call Function: Start Turn
```

**After (互換性レイヤー使用 - 移行中)**:
```
// BP_TurnManager (Blueprint)

Event BeginPlay
  ├─ Get Turn Manager
  └─ Call Function: Start Turn  // 内部でSubsystemに転送される
```

---

## 🧪 テストケース例

### UTurnFlowCoordinator のテストケース

```cpp
// Tests/TurnFlowCoordinatorTest.cpp

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Turn/TurnFlowCoordinator.h"

// Test 1: 初期状態確認
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurnFlowCoordinatorInitTest, "Project.Turn.TurnFlowCoordinator.Init", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTurnFlowCoordinatorInitTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateTestWorld();
    UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>();

    TestEqual(TEXT("Initial TurnId"), TFC->GetCurrentTurnId(), 0);
    TestEqual(TEXT("Initial AP"), TFC->GetPlayerAP(), 0);
    TestFalse(TEXT("Not queued"), TFC->IsEnemyPhaseQueued());

    CleanupTestWorld(World);
    return true;
}

// Test 2: ターン進行
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurnFlowCoordinatorAdvanceTest, "Project.Turn.TurnFlowCoordinator.Advance", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTurnFlowCoordinatorAdvanceTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateTestWorld();
    UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>();

    TFC->StartFirstTurn();
    TestEqual(TEXT("TurnId after first"), TFC->GetCurrentTurnId(), 1);

    TFC->AdvanceTurn();
    TestEqual(TEXT("TurnId after advance"), TFC->GetCurrentTurnId(), 2);

    TFC->AdvanceTurn();
    TestEqual(TEXT("TurnId after 2nd advance"), TFC->GetCurrentTurnId(), 3);

    CleanupTestWorld(World);
    return true;
}

// Test 3: AP管理
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurnFlowCoordinatorAPTest, "Project.Turn.TurnFlowCoordinator.AP", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTurnFlowCoordinatorAPTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateTestWorld();
    UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>();

    TFC->ResetPlayerAPForTurn();
    TestEqual(TEXT("AP after reset"), TFC->GetPlayerAP(), 1);
    TestTrue(TEXT("Has sufficient AP"), TFC->HasSufficientAP(1));

    TFC->ConsumePlayerAP(1);
    TestEqual(TEXT("AP after consume"), TFC->GetPlayerAP(), 0);
    TestFalse(TEXT("No sufficient AP"), TFC->HasSufficientAP(1));

    TFC->RestorePlayerAP(1);
    TestEqual(TEXT("AP after restore"), TFC->GetPlayerAP(), 1);

    CleanupTestWorld(World);
    return true;
}
```

---

## 📝 チェックリスト

### Week 1-3: Subsystem実装

- [ ] `UTurnFlowCoordinator` 実装完了
- [ ] `UPlayerInputProcessor` 実装完了
- [ ] `UEnemyTurnCoordinator` 実装完了
- [ ] `UPhaseStateMachine` 実装完了
- [ ] `UMovePhaseExecutor` 実装完了
- [ ] `UAttackPhaseExecutor` 実装完了
- [ ] 各Subsystemの単体テスト作成・実行

### Week 4: Component実装

- [ ] `UDungeonIntegrationManager` 実装完了
- [ ] `USystemHooksManager` 実装完了
- [ ] `UNetworkSyncManager` 実装完了

### Week 5: 統合・移行

- [ ] `AGameTurnManagerBase` に互換性レイヤー実装
- [ ] 既存Blueprint動作確認
- [ ] レプリケーションテスト（Server/Client）
- [ ] パフォーマンステスト
- [ ] ドキュメント更新

---

## 🚀 次のステップ

1. **チームレビュー**: 本ドキュメントの承認
2. **実装開始**: Week 1 のタスクから着手
3. **定期レビュー**: 週次で進捗確認

---

**作成者**: ClassSeparationAgent
**最終更新**: 2025-11-09
