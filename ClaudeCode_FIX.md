# GameTurnManagerBase リファクタリング計画書 v2.0

**作成日:** 2025-11-22
**対象ファイル:** `Turn/GameTurnManagerBase.cpp`
**現在の行数:** 2058行
**目標行数:** 1500行以下
**必要削減:** 558行

---

## 1. アーキテクチャ概要

### 1.1 GameTurnManagerBaseの役割

`AGameTurnManagerBase`は**ターンベースゲームのオーケストレーター**として機能：

```
[プレイヤー入力] → [Intent生成] → [コンフリクト解決] → [フェーズ実行] → [ターン終了]
     ↓                ↓                 ↓                  ↓              ↓
PlayerInputProcessor  EnemyAISubsystem  TurnCorePhaseManager  AttackExecutor  TurnAdvanceGuard
```

### 1.2 既存サブシステム依存関係

```
AGameTurnManagerBase
├── UTurnFlowCoordinator          // ターンID・フロー管理
├── UPlayerInputProcessor         // 入力ウィンドウ開閉
├── UTurnInitializationSubsystem  // ターン開始時初期化
├── UEnemyTurnDataSubsystem       // Intent/Observation保持
├── UEnemyAISubsystem             // AI意思決定
├── UTurnCorePhaseManager         // コンフリクト解決
├── UMoveReservationSubsystem     // 移動予約・ディスパッチ
├── UAttackPhaseExecutorSubsystem // 攻撃実行
├── UTurnActionBarrierSubsystem   // 非同期完了待機
├── UTurnAdvanceGuardSubsystem    // ターン進行ガード
├── UGridOccupancySubsystem       // グリッド占有管理
├── UGridPathfindingSubsystem     // パス探索
└── UDistanceFieldSubsystem       // 距離フィールド
```

---

## 2. 関数マップ（行番号付き）

### 2.1 全関数一覧（2058行中）

| 行 | 関数名 | 行数 | カテゴリ | 委譲先候補 |
|----|--------|------|----------|------------|
| 94 | `InitializeTurnSystem` | 35 | 初期化 | TurnSystemInitializer |
| 129 | `Tick` | 6 | コア | - |
| 135 | `BeginPlay` | 66 | 初期化 | TurnSystemInitializer |
| 201 | `HandleDungeonReady` | 102 | 初期化 | **抽出候補** |
| 303 | `OnExperienceLoaded` | 8 | 初期化 | - |
| 311 | `OnRep_CurrentTurnId` | 7 | ネットワーク | - |
| 318 | `InitGameplayTags` | 30 | 初期化 | TagUtils |
| 348 | `GetCurrentTurnIndex` | 5 | アクセサ | - |
| 353 | `GetCurrentInputWindowId` | 8 | アクセサ | - |
| 371 | `StartTurn` | 13 | フロー | - |
| 384 | `BeginPhase` | 40 | フロー | - |
| 424 | `EndPhase` | 35 | フロー | - |
| 459-569 | `*_Implementation` (10個) | 110 | BPスタブ | - |
| 571 | `GetCachedEnemies` | 5 | アクセサ | - |
| 576 | `GetLifetimeReplicatedProps` | 9 | ネットワーク | - |
| 585 | `OnEnemyAttacksCompleted` | 6 | コールバック | - |
| 591 | `AdvanceTurnAndRestart` | 27 | フロー | TurnAdvanceGuard |
| 618 | `StartFirstTurn` | 10 | フロー | - |
| 628 | `EndPlay` | 63 | ライフサイクル | - |
| 691 | `CopyEnemyActors` | 13 | ユーティリティ | - |
| 704 | `RefreshEnemyRoster` | 32 | 敵管理 | UnitTurnStateSubsystem |
| 736 | `OnTurnStartedHandler` | 60 | **重要** | TurnInitializationSubsystem |
| 796 | `OnPlayerCommandAccepted_Impl` | 28 | 入力 | - |
| 824 | `ResolveOrSpawnPathFinder` | 27 | 初期化 | **削除候補** |
| 851 | `ResolveOrSpawnUnitManager` | 47 | 初期化 | **削除候補** |
| 909 | `ContinueTurnAfterInput` | 4 | 非推奨 | 削除検討 |
| 915 | `ResolveMovePhaseDependencies` | 39 | ヘルパー | - |
| 954 | `AppendPlayerIntentIfPending` | 45 | Intent | EnemyTurnDataSubsystem |
| 999 | `ExecuteSimultaneousPhase` | 49 | **フェーズ** | **抽出候補** |
| 1048 | `OnSimultaneousPhaseCompleted` | 15 | コールバック | - |
| 1063 | `ExecuteSequentialPhase` | 19 | **フェーズ** | **抽出候補** |
| 1082 | `OnPlayerMoveCompleted` | 33 | コールバック | PlayerMoveHandler |
| 1115 | `ExecuteEnemyPhase` | 48 | **フェーズ** | **抽出候補** |
| 1163 | `ExecuteEnemyMoves_Sequential` | 62 | **フェーズ** | **抽出候補** |
| 1225 | `DispatchMoveActions` | 14 | 移動 | MoveReservationSubsystem |
| 1239 | `IsSequentialModeActive` | 6 | アクセサ | - |
| 1245 | `DoesAnyIntentHaveAttack` | 27 | Intent | EnemyTurnDataSubsystem |
| 1272 | `ExecuteAttacks` | 64 | **フェーズ** | **抽出候補** |
| 1336 | `EnsureEnemyIntents` | 24 | Intent | ✅完了 |
| 1360 | `ExecuteMovePhase` | 128 | **最大** | **抽出候補** |
| 1488 | `HandleMovePhaseCompleted` | 46 | コールバック | - |
| 1534 | `HandleAttackPhaseCompleted` | 34 | コールバック | - |
| 1568 | `EndEnemyTurn` | 21 | フロー | TurnAdvanceGuard |
| 1589 | `ClearResidualInProgressTags` | 9 | タグ | ✅完了 |
| 1598 | `RemoveGameplayTagLoose` | 5 | タグ | ✅完了 |
| 1603 | `CleanseBlockingTags` | 6 | タグ | ✅完了 |
| 1609 | `LogPlayerPositionBeforeAdvance` | 22 | ログ | **削除候補** |
| 1631 | `PerformTurnAdvanceAndBeginNextTurn` | 52 | フロー | TurnAdvanceGuard |
| 1683 | `OnRep_WaitingForPlayerInput` | 33 | ネットワーク | - |
| 1716 | `ApplyWaitInputGate` | 32 | 入力 | **抽出候補** |
| 1748 | `OpenInputWindow` | 46 | 入力 | **抽出候補** |
| 1794 | `NotifyPlayerPossessed` | 29 | 入力 | - |
| 1823 | `TryStartFirstTurn` | 20 | フロー | - |
| 1843 | `IsInputOpen_Server` | 27 | 入力 | - |
| 1870 | `CanAdvanceTurn` | 26 | フロー | TurnAdvanceGuard |
| 1896 | `MarkMoveInProgress` | 15 | 状態 | - |
| 1911 | `TryStartFirstTurnAfterASCReady` | 11 | フロー | - |
| 1922 | `FinalizePlayerMove` | 22 | 移動 | MoveReservationSubsystem |
| 1944 | `RegisterManualMoveDelegate` | 11 | 移動 | MoveReservationSubsystem |
| 1955 | `HandleManualMoveFinished` | 14 | 移動 | MoveReservationSubsystem |
| 1969 | `ClearResolvedMoves` | 11 | 移動 | MoveReservationSubsystem |
| 1980 | `RegisterResolvedMove` | 12 | 移動 | MoveReservationSubsystem |
| 1992 | `IsMoveAuthorized` | 12 | 移動 | MoveReservationSubsystem |
| 2004 | `HasReservationFor` | 12 | 移動 | MoveReservationSubsystem |
| 2016 | `ReleaseMoveReservation` | 12 | 移動 | MoveReservationSubsystem |
| 2028 | `DispatchResolvedMove` | 13 | 移動 | MoveReservationSubsystem |
| 2041 | `TriggerPlayerMoveAbility` | 12 | 移動 | ✅完了 |
| 2053 | `SetPlayerMoveState` | 5 | 状態 | - |

### 2.2 抽出優先度（行数順）

| 優先度 | 関数 | 行数 | 理由 |
|--------|------|------|------|
| 1 | `ExecuteMovePhase` | 128 | 最大関数、ロジック複雑 |
| 2 | `HandleDungeonReady` | 102 | 初期化、独立性高い |
| 3 | `*_Implementation`群 | 110 | BPスタブ、圧縮可能 |
| 4 | `ExecuteAttacks` | 64 | フェーズ実行 |
| 5 | `ExecuteEnemyMoves_Sequential` | 62 | フェーズ実行 |
| 6 | `BeginPlay` | 66 | 初期化 |
| 7 | `OnTurnStartedHandler` | 60 | ターン開始 |

---

## 3. 具体的リファクタリング手順

### Phase 7: コメント・ログ圧縮（-150行目標）

#### 7.1 CodeRevisionコメント圧縮

**検索パターン:**
```regex
// CodeRevision: INC-\d{4}-\d{4}-R\d.*\n.*\n
```

**変換ルール:**
```cpp
// Before
// CodeRevision: INC-2025-1208-R1 (Split DispatchResolvedMove into focused helpers
// for wait actions, player moves, and enemy moves) (2025-11-22 01:10)

// After
// INC-2025-1208-R1: Split DispatchResolvedMove
```

#### 7.2 ログマクロ導入

**新規ファイル作成:** `Utility/TurnLogMacros.h`

```cpp
#pragma once

#include "Logging/LogMacros.h"

// Turn-aware logging macros
#define LOG_TURN_MANAGER(Verbosity, TurnId, Format, ...) \
    UE_LOG(LogTurnManager, Verbosity, TEXT("[Turn %d] " Format), TurnId, ##__VA_ARGS__)

#define LOG_TURN_PHASE(Verbosity, TurnId, Format, ...) \
    UE_LOG(LogTurnPhase, Verbosity, TEXT("[Turn %d] " Format), TurnId, ##__VA_ARGS__)

// Conditional verbose logging (controlled by CVar)
#define LOG_TURN_VERBOSE(TurnId, Format, ...) \
    if (CVarTurnLog.GetValueOnGameThread() >= 2) { \
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] " Format), TurnId, ##__VA_ARGS__); \
    }
```

**使用例:**
```cpp
// Before (2行)
UE_LOG(LogTurnManager, Warning,
    TEXT("[Turn %d] ExecuteEnemyPhase: Starting sequential phase"), CurrentTurnId);

// After (1行)
LOG_TURN_MANAGER(Warning, CurrentTurnId, TEXT("ExecuteEnemyPhase: Starting sequential phase"));
```

---

### Phase 8: ExecuteMovePhase抽出（-100行目標）

#### 8.1 現在の構造（1360-1487行）

```cpp
void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{
    // 1. PlayerPawn取得 (5行)
    // 2. サブシステム取得・検証 (20行)
    // 3. EnsureEnemyIntents呼び出し (5行)
    // 4. Intent空チェック (10行)
    // 5. 攻撃Intent判定 (20行)
    // 6. Intent配列準備 (10行)
    // 7. CoreResolvePhase呼び出し (5行)
    // 8. プレイヤーSwap検出 (25行)
    // 9. DispatchMoveActions (5行)
    // 10. ログ出力 (23行)
}
```

#### 8.2 抽出先: UTurnCorePhaseManager

**追加メソッド:**
```cpp
// Turn/TurnCorePhaseManager.h に追加

/**
 * Execute the move phase with conflict resolution
 * @param TurnId Current turn ID
 * @param PlayerPawn Player's pawn
 * @param bSkipAttackCheck Skip attack intent check
 * @param OutCancelledPlayer True if player move was cancelled
 * @return Resolved actions to dispatch
 */
TArray<FResolvedAction> ExecuteMovePhaseWithResolution(
    int32 TurnId,
    APawn* PlayerPawn,
    bool bSkipAttackCheck,
    bool& OutCancelledPlayer);
```

#### 8.3 GameTurnManagerBase側の薄いラッパー

```cpp
void AGameTurnManagerBase::ExecuteMovePhase(bool bSkipAttackCheck)
{
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

    if (UTurnCorePhaseManager* PhaseManager = GetWorld()->GetSubsystem<UTurnCorePhaseManager>())
    {
        bool bCancelledPlayer = false;
        TArray<FResolvedAction> Actions = PhaseManager->ExecuteMovePhaseWithResolution(
            CurrentTurnId, PlayerPawn, bSkipAttackCheck, bCancelledPlayer);

        if (bCancelledPlayer)
        {
            EndEnemyTurn();
            return;
        }

        DispatchMoveActions(Actions);
    }
}
```

---

### Phase 9: フェーズ実行関数群の統合（-150行目標）

#### 9.1 対象関数

| 関数 | 行 | 行数 |
|------|-----|------|
| `ExecuteSimultaneousPhase` | 999 | 49 |
| `ExecuteSequentialPhase` | 1063 | 19 |
| `ExecuteEnemyPhase` | 1115 | 48 |
| `ExecuteEnemyMoves_Sequential` | 1163 | 62 |
| `ExecuteAttacks` | 1272 | 64 |
| **合計** | | **242行** |

#### 9.2 新規サブシステム案

**ファイル:** `Turn/TurnPhaseExecutorSubsystem.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnPhaseExecutorSubsystem.generated.h"

class AGameTurnManagerBase;
struct FResolvedAction;

UCLASS()
class LYRAGAME_API UTurnPhaseExecutorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Execute simultaneous phase (no attacks) */
    void ExecuteSimultaneousPhase(AGameTurnManagerBase* TurnManager, int32 TurnId);

    /** Execute sequential phase (attacks first, then moves) */
    void ExecuteSequentialPhase(AGameTurnManagerBase* TurnManager, int32 TurnId);

    /** Execute enemy phase based on intent analysis */
    void ExecuteEnemyPhase(AGameTurnManagerBase* TurnManager, int32 TurnId, APawn* PlayerPawn);

    /** Execute sequential enemy moves */
    void ExecuteEnemyMoves(AGameTurnManagerBase* TurnManager, int32 TurnId, bool bAfterAttacks);

    /** Execute attacks from resolved actions */
    void ExecuteAttacks(AGameTurnManagerBase* TurnManager, int32 TurnId,
                        const TArray<FResolvedAction>& Attacks);

private:
    TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
};
```

#### 9.3 依存関係

```cpp
// TurnPhaseExecutorSubsystem.cpp includes
#include "Turn/TurnPhaseExecutorSubsystem.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "Turn/MoveReservationSubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Kismet/GameplayStatics.h"
```

---

### Phase 10: Blueprint Implementation圧縮（-50行目標）

#### 10.1 対象関数（459-569行）

```cpp
void AGameTurnManagerBase::BuildSimulBatch_Implementation() { /* 7行 */ }
void AGameTurnManagerBase::CommitSimulStep_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::ResolveConflicts_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::CheckInterruptWindow_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnBreedingSystemUpdate_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnPotSystemUpdate_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnTrapSystemUpdate_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnStatusEffectSystemUpdate_Implementation(...) { /* 7行 */ }
void AGameTurnManagerBase::OnItemSystemUpdate_Implementation(...) { /* 7行 */ }
```

#### 10.2 圧縮パターン

```cpp
// Before (7行 x 10 = 70行)
void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(const FTurnContext& Context)
{
    if (CVarTurnLog.GetValueOnGameThread() >= 2)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[Turn %d] OnCombineSystemUpdate called (Blueprint)"), CurrentTurnId);
    }
}

// After (3行 x 10 = 30行, -40行削減)
void AGameTurnManagerBase::OnCombineSystemUpdate_Implementation(const FTurnContext& Context)
{
    LOG_TURN_VERBOSE(CurrentTurnId, TEXT("OnCombineSystemUpdate called (Blueprint)"));
}
```

---

### Phase 11: 入力ゲート関数の委譲（-60行目標）

#### 11.1 対象関数

| 関数 | 行 | 行数 |
|------|-----|------|
| `ApplyWaitInputGate` | 1716 | 32 |
| `OpenInputWindow` | 1748 | 46 |
| **合計** | | **78行** |

#### 11.2 抽出先: TurnTagCleanupUtils拡張

```cpp
// Utility/TurnTagCleanupUtils.h に追加
namespace TurnTagCleanupUtils
{
    /** Apply or remove input gate tag */
    LYRAGAME_API void ApplyInputGate(
        UAbilitySystemComponent* ASC,
        const FGameplayTag& GateTag,
        bool bOpen);

    /** Full input window opening with validation */
    LYRAGAME_API bool OpenInputWindow(
        UWorld* World,
        APawn* PlayerPawn,
        int32 TurnId,
        int32 InputWindowId);
}
```

---

## 4. 実装チェックリスト

### Phase 7 (コメント・ログ圧縮)
- [ ] `Utility/TurnLogMacros.h` 作成
- [ ] CodeRevisionコメントを1行に圧縮（手動または正規表現）
- [ ] `LOG_TURN_MANAGER`マクロに置換（約50箇所）
- [ ] `LOG_TURN_VERBOSE`マクロに置換（約20箇所）
- [ ] コンパイル確認

### Phase 8 (ExecuteMovePhase抽出)
- [ ] `UTurnCorePhaseManager::ExecuteMovePhaseWithResolution`追加
- [ ] `AGameTurnManagerBase::ExecuteMovePhase`を薄いラッパーに変更
- [ ] コンパイル確認
- [ ] ターンフロー動作テスト

### Phase 9 (フェーズ実行統合)
- [ ] `Turn/TurnPhaseExecutorSubsystem.h/.cpp`作成
- [ ] 5つのフェーズ関数をサブシステムに移動
- [ ] `AGameTurnManagerBase`から呼び出しに変更
- [ ] コンパイル確認
- [ ] Sequential/Simultaneousフロー動作テスト

### Phase 10 (BPスタブ圧縮)
- [ ] ログマクロ適用（10関数）
- [ ] コンパイル確認

### Phase 11 (入力ゲート委譲)
- [ ] `TurnTagCleanupUtils`拡張
- [ ] `ApplyWaitInputGate`委譲
- [ ] `OpenInputWindow`委譲
- [ ] コンパイル確認

---

## 5. 削減予測サマリー

| Phase | 施策 | 予測削減 | 累計 |
|-------|------|----------|------|
| 7 | コメント・ログ圧縮 | -150 | 1908 |
| 8 | ExecuteMovePhase抽出 | -100 | 1808 |
| 9 | フェーズ実行統合 | -150 | 1658 |
| 10 | BPスタブ圧縮 | -40 | 1618 |
| 11 | 入力ゲート委譲 | -60 | 1558 |
| **合計** | | **-500** | **~1558行** |

**目標1500行達成には追加で58行の最適化が必要**
（空行削除、追加ログ圧縮等で対応可能）

---

## 6. 注意事項

### 6.1 変更禁止項目
- `UFUNCTION`マクロ付き関数のシグネチャ変更
- `UPROPERTY`のReplication設定
- `OnRep_*`関数の削除

### 6.2 テスト必須項目
- ターン0開始
- プレイヤー移動 → 敵フェーズ
- 攻撃Intent時のSequentialフロー
- 移動のみIntent時のSimultaneousフロー
- マルチプレイヤーレプリケーション

### 6.3 関連ファイル（修正時に確認）
- `Turn/GameTurnManagerBase.h` - 関数宣言
- `Turn/TurnCorePhaseManager.h/.cpp` - Phase 8の抽出先
- `Utility/TurnTagCleanupUtils.h/.cpp` - Phase 11の抽出先
- `AI/Enemy/EnemyTurnDataSubsystem.h/.cpp` - Intent関連

---

## 付録A: 完了済みCodeRevision

| ID | 内容 | 日付 |
|----|------|------|
| INC-2025-1122-R1 | TurnTagCleanupUtils作成 | 2025-11-22 |
| INC-2025-1122-R2 | MoveReservationSubsystem拡張 | 2025-11-22 |
| INC-2025-1122-R3 | EnsureIntentsFallback追加 | 2025-11-22 |

---

## 付録B: 関数カテゴリ別分類

### 初期化系（削減候補）
- `InitializeTurnSystem` (94)
- `BeginPlay` (135)
- `HandleDungeonReady` (201)
- `InitGameplayTags` (318)
- `ResolveOrSpawnPathFinder` (824)
- `ResolveOrSpawnUnitManager` (851)

### フェーズ実行系（抽出候補）
- `ExecuteSimultaneousPhase` (999)
- `ExecuteSequentialPhase` (1063)
- `ExecuteEnemyPhase` (1115)
- `ExecuteEnemyMoves_Sequential` (1163)
- `ExecuteMovePhase` (1360)
- `ExecuteAttacks` (1272)

### 入力系（委譲候補）
- `ApplyWaitInputGate` (1716)
- `OpenInputWindow` (1748)
- `MarkMoveInProgress` (1896)

### 移動系（完了済み）
- `RegisterResolvedMove` (1980)
- `DispatchResolvedMove` (2028)
- `TriggerPlayerMoveAbility` (2041)
