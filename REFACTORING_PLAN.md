# 巨大クラス分割計画書
**Roguelike Rogue Subsystem - Class Separation Plan**

**作成日**: 2025-11-09
**対象プロジェクト**: Unreal Engine 5 Turn-Based Strategy Roguelike (Lyraベース)

---

## 📋 エグゼクティブサマリー

本プロジェクトは、4,230行に達する`AGameTurnManagerBase`を筆頭に、複数の巨大クラスを抱えています。これらはGod Objectアンチパターンに該当し、保守性・テスト容易性・拡張性に深刻な問題を抱えています。

### 主要課題クラス（上位5つ）

| クラス名 | 合計行数 | 責務数 | 優先度 |
|---------|---------|-------|-------|
| **AGameTurnManagerBase** | 4,230行 | 15以上 | 🔴 CRITICAL |
| **AGridPathfindingLibrary** | 1,375行 | 10+ | 🔴 CRITICAL |
| **UGA_MoveBase** | 1,291行 | 8+ | 🟡 HIGH |
| **APlayerControllerBase** | 1,073行 | 7+ | 🟡 HIGH |
| **UTurnCorePhaseManager** | 1,010行 | 6+ | 🟡 HIGH |

### 期待される効果

- **保守性向上**: 各クラスを300～500行に分割、単一責任原則に準拠
- **テスト容易性**: 責務ごとの単体テスト実施可能
- **並行開発**: チーム内での機能別並行開発が可能
- **バグ削減**: 責務の明確化による論理エラー削減（推定30～40%減）

---

## 🎯 分割戦略 - 優先度1: AGameTurnManagerBase

### 現状分析

**ファイル**: `/Turn/GameTurnManagerBase.h` (732行) + `.cpp` (3,498行)
**継承元**: `AActor`
**問題**: God Objectアンチパターン - 15以上の責務を単一クラスで管理

#### 現在の責務リスト

1. **ターン進行管理** (BeginTurn, EndTurn, AdvanceTurn)
2. **プレイヤー入力処理** (WindowId検証、コマンド処理)
3. **敵AI調整** (CollectEnemies, BuildObservations, ComputeIntent)
4. **ダンジョン/フロア生成統合**
5. **グリッドシステム管理**
6. **移動フェーズ実行** (逐次・同時移動)
7. **攻撃フェーズ実行**
8. **味方ターン管理**
9. **競合解決** (ResolveConflicts)
10. **バリア/同期管理**
11. **フェーズ管理** (複数フェーズタグ)
12. **レプリケーション処理**
13. **デバッグシステム統合**
14. **システムフック** (Combine, Breeding, Pot, Trap, StatusEffects, Items)
15. **APシステム管理**

### 分割提案 - Strategy Pattern + Subsystem化

#### ✅ 分割後のクラス構成（9クラス）

```
AGameTurnManagerBase (500行程度)
  ├─ UTurnFlowCoordinator (Subsystem, 300行)         [責務: ターン進行制御]
  ├─ UPlayerInputProcessor (Subsystem, 250行)       [責務: 入力検証・処理]
  ├─ UEnemyTurnCoordinator (Subsystem, 400行)       [責務: 敵AI統合]
  ├─ UDungeonIntegrationManager (Component, 200行)  [責務: ダンジョン連携]
  ├─ UPhaseStateMachine (Subsystem, 350行)          [責務: フェーズ遷移]
  ├─ UMovePhaseExecutor (Subsystem, 400行)          [責務: 移動実行]
  ├─ UAttackPhaseExecutor (Subsystem, 300行)        [責務: 攻撃実行]
  ├─ USystemHooksManager (Component, 250行)          [責務: 各種フック]
  └─ UNetworkSyncManager (Component, 200行)          [責務: レプリケーション]
```

#### 詳細設計

##### 1️⃣ **UTurnFlowCoordinator** (WorldSubsystem)

**責務**: ターン全体のライフサイクル管理

```cpp
UCLASS()
class LYRAGAME_API UTurnFlowCoordinator : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ターンID管理
    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const { return CurrentTurnId; }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentInputWindowId() const { return InputWindowId; }

    // ターン進行制御
    UFUNCTION(BlueprintCallable, Category = "Turn")
    void StartTurn();

    UFUNCTION(BlueprintCallable, Category = "Turn")
    void EndTurn();

    UFUNCTION(BlueprintCallable, Category = "Turn")
    void AdvanceTurn();

    // AP管理
    UFUNCTION(BlueprintCallable, Category = "Turn|AP")
    void ConsumePlayerAP(int32 Amount);

    UFUNCTION(BlueprintPure, Category = "Turn|AP")
    bool HasSufficientAP(int32 Required) const;

protected:
    UPROPERTY(Replicated)
    int32 CurrentTurnId = 0;

    UPROPERTY(Replicated)
    int32 InputWindowId = 0;

    UPROPERTY(Replicated)
    int32 PlayerAP = 0;

    UPROPERTY(EditDefaultsOnly, Category = "Turn")
    int32 PlayerAPPerTurn = 1;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, int32);
    FOnTurnStarted OnTurnStarted;
};
```

**移行対象メンバー**:
- `CurrentTurnId`, `InputWindowId`, `CurrentTurnIndex`
- `PlayerAP`, `PlayerAPPerTurn`, `bEnemyPhaseQueued`
- `OnTurnStarted` デリゲート
- `StartTurn()`, `EndTurn()`, `AdvanceTurn()` 関数

---

##### 2️⃣ **UPlayerInputProcessor** (WorldSubsystem)

**責務**: プレイヤー入力の検証・変換・キューイング

```cpp
UCLASS()
class LYRAGAME_API UPlayerInputProcessor : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // 入力ウィンドウ管理
    UFUNCTION(BlueprintCallable, Category = "Input")
    void OpenInputWindow(int32 TurnId);

    UFUNCTION(BlueprintCallable, Category = "Input")
    void CloseInputWindow();

    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsInputWindowOpen() const;

    // コマンド検証
    UFUNCTION(BlueprintCallable, Category = "Input")
    bool ValidateCommand(const FPlayerCommand& Command, int32 ExpectedWindowId);

    // コマンド処理
    UFUNCTION(BlueprintCallable, Category = "Input")
    void ProcessPlayerCommand(const FPlayerCommand& Command);

protected:
    UPROPERTY(Replicated)
    bool bWaitingForPlayerInput = false;

    UPROPERTY()
    FPlayerCommand CachedPlayerCommand;

    // デリゲート
    DECLARE_MULTICAST_DELEGATE(FOnPlayerInputReceived);
    FOnPlayerInputReceived OnPlayerInputReceived;

    // 入力検証ヘルパー
    bool IsValidWindowId(int32 WindowId) const;
    void ApplyWaitInputGate(bool bOpen);
};
```

**移行対象メンバー**:
- `WaitingForPlayerInput`, `CachedPlayerCommand`
- `OnPlayerInputReceived` デリゲート
- `OpenInputWindow()`, `ProcessPlayerCommand()`, `NotifyPlayerInputReceived()`
- `IsInputOpen_Server()`, `ApplyWaitInputGate()`

---

##### 3️⃣ **UEnemyTurnCoordinator** (WorldSubsystem)

**責務**: 敵のターン全体を統合・調整（収集→観察→思考→実行）

```cpp
UCLASS()
class LYRAGAME_API UEnemyTurnCoordinator : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // 敵収集パイプライン
    UFUNCTION(BlueprintCallable, Category = "Enemy")
    void CollectEnemies();

    UFUNCTION(BlueprintCallable, Category = "Enemy")
    void BuildObservations();

    UFUNCTION(BlueprintCallable, Category = "Enemy")
    void CollectIntents();

    // 敵リスト取得
    UFUNCTION(BlueprintPure, Category = "Enemy")
    void GetCachedEnemies(TArray<AActor*>& OutEnemies) const;

    UFUNCTION(BlueprintPure, Category = "Enemy")
    bool TryGetEnemyIntent(AActor* Enemy, FEnemyIntent& OutIntent) const;

protected:
    UPROPERTY()
    TArray<TObjectPtr<AActor>> CachedEnemies;

    UPROPERTY()
    TArray<TObjectPtr<AActor>> CachedEnemiesForTurn;

    UPROPERTY()
    TMap<TWeakObjectPtr<AActor>, FEnemyIntent> CachedIntents;

    UPROPERTY()
    int32 EnemiesRevision = 0;

    // EnemyTurnDataSubsystem/EnemyAISubsystem への橋渡し
    TWeakObjectPtr<UEnemyTurnDataSubsystem> EnemyTurnData;
    TWeakObjectPtr<UEnemyAISubsystem> EnemyAISubsystem;
};
```

**移行対象メンバー**:
- `CachedEnemies`, `CachedEnemiesForTurn`, `CachedEnemiesWeak`
- `CachedIntents`, `EnemiesRevision`
- `EnemyTurnData`, `EnemyAISubsystem`
- `CollectEnemies()`, `BuildObservations()`, `CollectIntents()`, `ComputeEnemyIntent()`

---

##### 4️⃣ **UPhaseStateMachine** (WorldSubsystem)

**責務**: フェーズ遷移の状態管理（Init → PlayerWait → Move → Attack → Cleanup）

```cpp
UCLASS()
class LYRAGAME_API UPhaseStateMachine : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // フェーズ遷移
    UFUNCTION(BlueprintCallable, Category = "Phase")
    void BeginPhase(FGameplayTag PhaseTag);

    UFUNCTION(BlueprintCallable, Category = "Phase")
    void EndPhase(FGameplayTag PhaseTag);

    UFUNCTION(BlueprintPure, Category = "Phase")
    FGameplayTag GetCurrentPhase() const { return CurrentPhase; }

    UFUNCTION(BlueprintPure, Category = "Phase")
    double GetPhaseElapsedTime() const;

protected:
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Phase")
    FGameplayTag CurrentPhase;

    UPROPERTY()
    double PhaseStartTime = 0.0;

    // フェーズタグキャッシュ
    FGameplayTag Phase_Turn_Init;
    FGameplayTag Phase_Player_Wait;
    FGameplayTag Phase_Move;
    FGameplayTag Phase_Attack;
    FGameplayTag Phase_Cleanup;
};
```

**移行対象メンバー**:
- `CurrentPhase`, `PhaseStartTime`
- `Phase_Turn_Init`, `Phase_Player_Wait` など
- `BeginPhase()`, `EndPhase()`

---

##### 5️⃣ **UMovePhaseExecutor** (WorldSubsystem)

**責務**: 移動フェーズの実行（逐次移動・同時移動・競合解決）

```cpp
UCLASS()
class LYRAGAME_API UMovePhaseExecutor : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // 移動実行
    UFUNCTION(BlueprintCallable, Category = "Move")
    void ExecuteSequentialMoves();

    UFUNCTION(BlueprintCallable, Category = "Move")
    void ExecuteSimultaneousMoves();

    // 競合解決
    UFUNCTION(BlueprintCallable, Category = "Move")
    void ResolveConflicts(TArray<FPendingMove>& Moves);

    // 移動予約管理
    UFUNCTION(BlueprintCallable, Category = "Move")
    void RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell);

    UFUNCTION(BlueprintPure, Category = "Move")
    bool IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const;

protected:
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, FIntPoint> PendingMoveReservations;

    UPROPERTY()
    FSimulBatch CurrentSimulBatch;

    // 優先度・スワップ判定（旧MoveConflictRuleSetから移行）
    int32 GetMovePriority(const FGameplayTagContainer& ActorTags) const;
    bool CanSwapActors(const FGameplayTagContainer& A, const FGameplayTagContainer& B) const;
    bool CanPushActor(const FGameplayTagContainer& Pusher, const FGameplayTagContainer& Pushed) const;
};
```

**移行対象メンバー**:
- `PendingMoveReservations`, `CurrentSimulBatch`
- `ExecuteSequentialPhase()`, `ExecuteSimultaneousPhase()`, `ExecuteMovePhase()`
- `ResolveConflicts()`, `GetMovePriority()`, `CanSwapActors()`, `CanPushActor()`

---

##### 6️⃣ **UAttackPhaseExecutor** (WorldSubsystem)

**責務**: 攻撃フェーズの実行

```cpp
UCLASS()
class LYRAGAME_API UAttackPhaseExecutor : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Attack")
    void ExecutePlayerAttack();

    UFUNCTION(BlueprintCallable, Category = "Attack")
    void ExecuteEnemyAttacks();

    UFUNCTION(BlueprintCallable, Category = "Attack")
    void ExecuteAllyAttacks();

protected:
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnAttacksCompleted, int32);
    FOnAttacksCompleted OnAttacksCompleted;
};
```

**移行対象メンバー**:
- `ExecuteEnemyAttacks()`, `ExecuteAllyActions()`, `ExecuteAttacks()`
- `OnAttacksFinished`, `OnEnemyAttacksCompleted`

---

##### 7️⃣ **UDungeonIntegrationManager** (ActorComponent)

**責務**: ダンジョンサブシステムとの連携

```cpp
UCLASS()
class LYRAGAME_API UDungeonIntegrationManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    bool EnsureFloorGenerated(int32 FloorIndex);

    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    bool NextFloor();

    UFUNCTION(BlueprintPure, Category = "Dungeon")
    URogueDungeonSubsystem* GetDungeonSystem() const;

    UFUNCTION(BlueprintPure, Category = "Dungeon")
    ADungeonFloorGenerator* GetFloorGenerator() const;

protected:
    UPROPERTY()
    TObjectPtr<URogueDungeonSubsystem> DungeonSystem;

    UPROPERTY(EditAnywhere, Category = "Dungeon")
    TSoftObjectPtr<UDungeonConfigAsset> InitialFloorConfig;

    UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
    int32 CurrentFloorIndex = 0;
};
```

**移行対象メンバー**:
- `DungeonSystem`, `DungeonSys`, `InitialFloorConfig`
- `CurrentFloorIndex`, `StartFloorIndex`
- `EnsureFloorGenerated()`, `NextFloor()`, `HandleDungeonReady()`

---

##### 8️⃣ **USystemHooksManager** (ActorComponent)

**責務**: ゲームシステムフックの呼び出し

```cpp
UCLASS()
class LYRAGAME_API USystemHooksManager : public UActorComponent
{
    GENERATED_BODY()

public:
    // BlueprintNativeEvent フック呼び出し
    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnCombineSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnBreedingSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnPotSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnTrapSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnStatusEffectSystemUpdate(const FTurnContext& Context);

    UFUNCTION(BlueprintNativeEvent, Category = "Hooks")
    void OnItemSystemUpdate(const FTurnContext& Context);
};
```

**移行対象メンバー**:
- `OnCombineSystemUpdate()`, `OnBreedingSystemUpdate()`, など6つのフック関数

---

##### 9️⃣ **UNetworkSyncManager** (ActorComponent)

**責務**: レプリケーション・ネットワーク同期

```cpp
UCLASS()
class LYRAGAME_API UNetworkSyncManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnRep_WaitingForPlayerInput();

    UFUNCTION()
    void OnRep_InputWindowId();

    UFUNCTION()
    void OnRep_CurrentTurnId();

protected:
    void BroadcastTurnStateToClients();
    void SyncInputWindowState();
};
```

**移行対象メンバー**:
- `OnRep_*` 関数群
- `SetWaitingForPlayerInput_ServerLike()`

---

### 実装フェーズ計画

#### Phase 1: Subsystem抽出（2～3週間）

1. **Week 1**: `UTurnFlowCoordinator` + `UPlayerInputProcessor`
   - 既存の`AGameTurnManagerBase`からターン進行とインプット処理を分離
   - 既存コードとの互換性レイヤー作成

2. **Week 2**: `UEnemyTurnCoordinator` + `UPhaseStateMachine`
   - 敵AIパイプラインを独立
   - フェーズ遷移を状態機械化

3. **Week 3**: `UMovePhaseExecutor` + `UAttackPhaseExecutor`
   - 移動・攻撃実行ロジックを分離
   - 競合解決ルールを整理

#### Phase 2: Component化（1～2週間）

4. **Week 4**: `UDungeonIntegrationManager` + `USystemHooksManager` + `UNetworkSyncManager`
   - 周辺システムとの連携をコンポーネント化
   - レプリケーション処理を整理

#### Phase 3: 統合テスト・リファクタリング（1週間）

5. **Week 5**: 統合テスト、既存BP互換性確認、パフォーマンス検証

### マイグレーション戦略

#### 後方互換性の維持

```cpp
// AGameTurnManagerBase.h (リファクタリング後)

UCLASS(Blueprintable, BlueprintType)
class LYRAGAME_API AGameTurnManagerBase : public AActor
{
    GENERATED_BODY()

public:
    // ★★★ 互換性レイヤー: 旧APIを新Subsystemに転送 ★★★

    UFUNCTION(BlueprintCallable, Category = "Turn|Flow")
    void StartTurn()
    {
        if (UTurnFlowCoordinator* TFC = GetWorld()->GetSubsystem<UTurnFlowCoordinator>())
        {
            TFC->StartTurn();
        }
    }

    UFUNCTION(BlueprintPure, Category = "Turn")
    int32 GetCurrentTurnId() const
    {
        if (const UTurnFlowCoordinator* TFC = GetWorld()->GetSubsystem<UTurnFlowCoordinator>())
        {
            return TFC->GetCurrentTurnId();
        }
        return 0;
    }

    // ... 同様に全旧API関数を転送関数として実装 ...

protected:
    // Subsystem参照（BeginPlayで初期化）
    UPROPERTY()
    TObjectPtr<UTurnFlowCoordinator> TurnFlowCoordinator;

    UPROPERTY()
    TObjectPtr<UPlayerInputProcessor> PlayerInputProcessor;

    UPROPERTY()
    TObjectPtr<UEnemyTurnCoordinator> EnemyTurnCoordinator;

    // ... 他のSubsystemも同様 ...
};
```

#### 段階的移行パス

1. **ステップ1**: 新Subsystemを追加（既存コード無修正）
2. **ステップ2**: 既存関数を「転送関数」に変更
3. **ステップ3**: Blueprint参照を新Subsystemに順次移行
4. **ステップ4**: 転送関数に`DEPRECATED`マクロ追加
5. **ステップ5**: 完全移行後、旧コード削除

---

## 🎯 分割戦略 - 優先度2: AGridPathfindingLibrary

### 現状分析

**ファイル**: `/Grid/GridPathfindingLibrary.h` (309行) + `.cpp` (1,066行)
**継承元**: `AActor`
**問題**: 複数の独立した責務（パスファインディング、視野検知、座標変換、占有管理）

#### 現在の責務リスト

1. **グリッド初期化・設定**
2. **A*パスファインディング**
3. **視野検知 (FOV)** - ExpandingVision, Radius検知
4. **視線チェック (LOS)**
5. **周囲タイル検索**
6. **歩行可能性判定** (地形 + 占有)
7. **ワールド⇔グリッド座標変換**
8. **距離計算** (Manhattan, Euclidean, Chebyshev)
9. **グリッドコスト管理**
10. **位置のActor検出**

### 分割提案 - Utility Class化 + Subsystem化

#### ✅ 分割後のクラス構成（4クラス + 1 Utility）

```
AGridPathfindingLibrary (300行 - グリッド初期化のみ)
  ├─ UPathfindingAlgorithms (Static Utility, 250行)    [責務: A*アルゴリズム]
  ├─ UVisionSystem (Subsystem, 300行)                   [責務: FOV/LOS検知]
  ├─ UGridCoordinateConverter (Static Utility, 150行)  [責務: 座標変換]
  └─ UWalkabilityEvaluator (Component, 200行)           [責務: 歩行可能性判定]
```

#### 詳細設計

##### 1️⃣ **AGridPathfindingLibrary** (簡素化版)

**残す責務**: グリッド初期化・グリッドコスト配列の管理のみ

```cpp
UCLASS(BlueprintType, Blueprintable)
class LYRAGAME_API AGridPathfindingLibrary : public AActor
{
    GENERATED_BODY()

public:
    // グリッド初期化
    UFUNCTION(BlueprintCallable, Category = "Grid|Setup")
    void InitializeGrid(const TArray<int32>& InGridCost, const FVector& InMapSize, int32 InTileSizeCM = 100);

    UFUNCTION(BlueprintCallable, Category = "Grid|Setup")
    void InitializeFromParams(const FGridInitParams& Params);

    // グリッドコスト管理（地形専用）
    UFUNCTION(BlueprintCallable, Category = "Grid|Terrain")
    void SetGridCost(int32 X, int32 Y, int32 NewCost);

    UFUNCTION(BlueprintPure, Category = "Grid|Terrain")
    int32 GetGridCost(int32 X, int32 Y) const;

    // 基本情報取得
    UFUNCTION(BlueprintPure, Category = "Grid|Info")
    void GetGridInfo(int32& OutWidth, int32& OutHeight, int32& OutTileSize) const;

    UFUNCTION(BlueprintPure, Category = "Grid|Info")
    FVector GetGridOrigin() const { return Origin; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
    int32 GridWidth = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
    int32 GridHeight = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
    int32 TileSize = 100;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
    FVector Origin = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Grid")
    TArray<int32> mGrid;
};
```

---

##### 2️⃣ **UPathfindingAlgorithms** (Static Utility Library)

**責務**: A*アルゴリズムの実装（静的関数のみ）

```cpp
UCLASS()
class LYRAGAME_API UPathfindingAlgorithms : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * A*パスファインディング
     * @param GridCostArray - 地形コスト配列
     * @param OccupancyArray - 占有配列（別Subsystemから取得）
     * @param StartCell - 開始セル
     * @param EndCell - 終了セル
     * @param GridWidth/Height - グリッドサイズ
     * @param bAllowDiagonal - 斜め移動許可
     * @param Heuristic - ヒューリスティック関数タイプ
     * @param SearchLimit - 探索上限
     * @param OutPath - 出力パス（セル配列）
     * @return パス発見成功か
     */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding")
    static bool FindPath(
        const TArray<int32>& GridCostArray,
        const TArray<int32>& OccupancyArray,
        const FIntPoint& StartCell,
        const FIntPoint& EndCell,
        int32 GridWidth,
        int32 GridHeight,
        bool bAllowDiagonal,
        EGridHeuristic Heuristic,
        int32 SearchLimit,
        bool bHeavyDiagonal,
        TArray<FIntPoint>& OutPath
    );

protected:
    static int32 CalculateHeuristic(int32 x0, int32 y0, int32 x1, int32 y1, EGridHeuristic Mode);
    static bool InBounds(int32 X, int32 Y, int32 W, int32 H);
};
```

---

##### 3️⃣ **UVisionSystem** (WorldSubsystem)

**責務**: 視野検知（FOV）・視線チェック（LOS）

```cpp
UCLASS()
class LYRAGAME_API UVisionSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /**
     * 拡張視野検知（Expanding Vision）
     */
    UFUNCTION(BlueprintCallable, Category = "Vision")
    FGridVisionResult DetectInExpandingVision(
        const FIntPoint& CenterCell,
        const FVector& ForwardDirection,
        int32 MaxDepth = 3,
        TSubclassOf<AActor> ActorClassFilter = nullptr
    ) const;

    /**
     * 半径視野検知
     */
    UFUNCTION(BlueprintCallable, Category = "Vision")
    FGridVisionResult DetectInRadius(
        const FIntPoint& CenterCell,
        int32 Radius = 5,
        bool bCheckLineOfSight = true,
        TSubclassOf<AActor> ActorClassFilter = nullptr
    ) const;

    /**
     * 視線チェック（LOS）
     */
    UFUNCTION(BlueprintPure, Category = "Vision")
    bool HasLineOfSight(
        const FIntPoint& StartCell,
        const FIntPoint& EndCell
    ) const;

protected:
    bool IsVisibleFromPoint(const FIntPoint& From, const FIntPoint& To) const;
    void GetActorsAtGridPosition(const FIntPoint& GridPos, TSubclassOf<AActor> ClassFilter, TArray<AActor*>& OutActors) const;
};
```

---

##### 4️⃣ **UGridCoordinateConverter** (Static Utility Library)

**責務**: ワールド座標⇔グリッド座標変換

```cpp
UCLASS()
class LYRAGAME_API UGridCoordinateConverter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * ワールド座標 → グリッド座標
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Utility")
    static FIntPoint WorldToGrid(
        const FVector& WorldPos,
        const FVector& GridOrigin,
        int32 TileSize
    );

    /**
     * グリッド座標 → ワールド座標
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Utility")
    static FVector GridToWorld(
        const FIntPoint& GridPos,
        const FVector& GridOrigin,
        int32 TileSize,
        float Z = 0.0f
    );

    /**
     * セル中心のワールド座標取得
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Utility")
    static FVector GridToWorldCenter(
        const FIntPoint& Cell,
        const FVector& GridOrigin,
        int32 TileSize,
        float Z = 0.0f
    );

    /**
     * マンハッタン距離（ワールド座標版）
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance")
    static int32 GetManhattanDistance(
        const FVector& PosA,
        const FVector& PosB,
        const FVector& GridOrigin,
        int32 TileSize
    );

    /**
     * チェビシェフ距離（グリッド座標版）
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance")
    static int32 GetChebyshevDistance(FIntPoint A, FIntPoint B);

    /**
     * マンハッタン距離（グリッド座標版）
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance")
    static int32 GetManhattanDistanceGrid(FIntPoint A, FIntPoint B);

    /**
     * ユークリッド距離（グリッド座標版）
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance")
    static int32 GetEuclideanDistanceGrid(FIntPoint A, FIntPoint B);
};
```

---

##### 5️⃣ **UWalkabilityEvaluator** (Component)

**責務**: 歩行可能性判定（地形 + 占有の統合）

```cpp
UCLASS()
class LYRAGAME_API UWalkabilityEvaluator : public UActorComponent
{
    GENERATED_BODY()

public:
    /**
     * 統合された歩行可能性判定（地形 + 占有）
     */
    UFUNCTION(BlueprintCallable, Category = "Walkability")
    bool IsCellWalkable(const FIntPoint& Cell) const;

    /**
     * 特定Actorを無視した歩行可能性判定
     */
    UFUNCTION(BlueprintCallable, Category = "Walkability")
    bool IsCellWalkableIgnoringActor(const FIntPoint& Cell, AActor* IgnoreActor) const;

    /**
     * 周囲タイル検索
     */
    UFUNCTION(BlueprintCallable, Category = "Walkability")
    FGridSurroundResult SearchAdjacentTiles(
        const FIntPoint& CenterCell,
        bool bIncludeDiagonal = true,
        TSubclassOf<AActor> ActorClassFilter = nullptr
    ) const;

protected:
    // GridPathfindingLibrary参照（地形コスト取得用）
    UPROPERTY()
    TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;

    // GridOccupancySubsystem参照（占有情報取得用）
    TWeakObjectPtr<UGridOccupancySubsystem> CachedOccupancy;
};
```

---

### 実装フェーズ計画

#### Phase 1: Utility Class抽出（1週間）

1. **Day 1-2**: `UGridCoordinateConverter` 作成
   - 座標変換関数を静的関数化
   - 既存コードから呼び出しパス変更

2. **Day 3-4**: `UPathfindingAlgorithms` 作成
   - A*アルゴリズムを静的関数化
   - テストケース作成

#### Phase 2: Subsystem化（1週間）

3. **Day 5-7**: `UVisionSystem` + `UWalkabilityEvaluator`
   - FOV/LOS検知をSubsystem化
   - 歩行可能性判定をComponent化

#### Phase 3: 統合テスト（2日間）

4. **Day 8-9**: 統合テスト、既存BP互換性確認

---

## 🎯 分割戦略 - 優先度3: UGA_MoveBase

### 現状分析

**ファイル**: `/Abilities/GA_MoveBase.h` (273行) + `.cpp` (1,018行)
**継承元**: `UGA_TurnActionBase`
**問題**: 移動アビリティに複数の責務（検証・実行・アニメーション・バリア・座標スナップ）

#### 現在の責務リスト

1. **移動アビリティ起動・終了**
2. **方向量子化** (8方向グリッド移動)
3. **タイル歩行可能性検証**
4. **グリッド占有更新**
5. **移動アニメーション制御**
6. **バリア登録・完了**
7. **ターン同期** (TurnId追跡)
8. **状態タグ管理** (State.Moving, State.Action.InProgress)
9. **タイムアウト処理**
10. **デリゲート管理** (OnMoveFinished)
11. **Z座標スナップ・地面配置**

### 分割提案 - Strategy Pattern + Utility Helper

#### ✅ 分割後のクラス構成（5クラス）

```
UGA_MoveBase (400行程度)
  ├─ UMovementValidator (Static Utility, 200行)        [責務: 移動検証]
  ├─ UMovementAnimationController (Component, 250行)   [責務: アニメーション]
  ├─ UGridMovementHandler (Static Utility, 200行)      [責務: グリッド操作]
  ├─ UBarrierIntegrationHelper (Static Utility, 150行) [責務: バリア連携]
  └─ UCoordinateSnapUtility (Static Utility, 150行)    [責務: 座標補正]
```

#### 詳細設計

##### 1️⃣ **UGA_MoveBase** (簡素化版)

**残す責務**: アビリティライフサイクル・高レベル制御のみ

```cpp
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    virtual void ActivateAbility(...) override;
    virtual void EndAbility(...) override;

protected:
    // 移動実行（高レベル）
    void ExecuteMove(const FVector& Direction);

    // 移動完了ハンドラ
    UFUNCTION()
    void OnMoveFinished(AUnitBase* Unit);

    // Utility/Helper呼び出しのみ
    // 実装詳細は各Utilityクラスに委譲
};
```

---

##### 2️⃣ **UMovementValidator** (Static Utility)

**責務**: 移動の検証（歩行可能性、方向量子化）

```cpp
UCLASS()
class LYRAGAME_API UMovementValidator : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 8方向量子化
     */
    UFUNCTION(BlueprintPure, Category = "Movement|Validation")
    static FVector2D QuantizeToGridDirection(const FVector& InDirection);

    /**
     * 次のタイル位置計算
     */
    UFUNCTION(BlueprintPure, Category = "Movement|Validation")
    static FVector CalculateNextTilePosition(
        const FVector& CurrentPosition,
        const FVector2D& Dir,
        int32 TileSize
    );

    /**
     * タイル歩行可能性判定
     */
    UFUNCTION(BlueprintCallable, Category = "Movement|Validation")
    static bool IsTileWalkable(
        const FIntPoint& Cell,
        UWorld* World,
        AActor* IgnoreActor = nullptr
    );

    /**
     * イベントデータから方向抽出
     */
    UFUNCTION(BlueprintCallable, Category = "Movement|Validation")
    static bool ExtractDirectionFromEventData(
        const FGameplayEventData* EventData,
        FVector& OutDirection
    );
};
```

---

##### 3️⃣ **UMovementAnimationController** (Component)

**責務**: 移動アニメーション制御

```cpp
UCLASS()
class LYRAGAME_API UMovementAnimationController : public UActorComponent
{
    GENERATED_BODY()

public:
    /**
     * アニメーションスキップ判定
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Animation")
    bool ShouldSkipAnimation(float Distance, float Threshold);

    /**
     * 移動アニメーション実行
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Animation")
    void ExecuteMoveAnimation(const TArray<FVector>& Path);

    /**
     * Yaw角度を45度単位に丸める
     */
    UFUNCTION(BlueprintPure, Category = "Animation")
    static float RoundYawTo45Degrees(float Yaw);

protected:
    UPROPERTY(EditAnywhere, Category = "Animation")
    float SkipAnimIfUnderDistance = 0.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    float Speed = 600.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    float SpeedBuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    float SpeedDebuff = 1.0f;
};
```

---

##### 4️⃣ **UGridMovementHandler** (Static Utility)

**責務**: グリッド占有更新

```cpp
UCLASS()
class LYRAGAME_API UGridMovementHandler : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 占有更新（旧UpdateOccupancyの代替）
     */
    UFUNCTION(BlueprintCallable, Category = "Grid|Movement")
    static void UpdateOccupancy(
        UWorld* World,
        AActor* Unit,
        const FIntPoint& NewCell
    );

    /**
     * グリッド状態更新（地形用 - 非推奨）
     */
    UFUNCTION(BlueprintCallable, Category = "Grid|Movement", meta = (DeprecatedFunction))
    static void UpdateGridState(
        AGridPathfindingLibrary* PathFinder,
        const FVector& Position,
        int32 Value
    );
};
```

---

##### 5️⃣ **UBarrierIntegrationHelper** (Static Utility)

**責務**: バリアサブシステム連携

```cpp
UCLASS()
class LYRAGAME_API UBarrierIntegrationHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * バリア登録（Token方式）
     */
    UFUNCTION(BlueprintCallable, Category = "Barrier")
    static FGuid RegisterBarrier(
        UWorld* World,
        AActor* Avatar,
        int32 TurnId
    );

    /**
     * バリア完了（Token方式・冪等）
     */
    UFUNCTION(BlueprintCallable, Category = "Barrier")
    static void CompleteBarrier(
        UWorld* World,
        const FGuid& Token,
        bool bTimedOut = false
    );

    /**
     * バリアタイムアウトチェック
     */
    UFUNCTION(BlueprintCallable, Category = "Barrier")
    static bool CheckBarrierTimeout(
        double StartTime,
        double TimeoutSeconds = 5.0
    );
};
```

---

##### 6️⃣ **UCoordinateSnapUtility** (Static Utility)

**責務**: 座標スナップ・Z座標補正

```cpp
UCLASS()
class LYRAGAME_API UCoordinateSnapUtility : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * セル中心にスナップ
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Snap")
    static FVector SnapToCellCenter(
        const FVector& WorldPos,
        const FVector& GridOrigin,
        int32 TileSize
    );

    /**
     * セル中心にスナップ（Z固定）
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Snap")
    static FVector SnapToCellCenterFixedZ(
        const FVector& WorldPos,
        const FVector& GridOrigin,
        int32 TileSize,
        float FixedZ
    );

    /**
     * Z座標を地面に配置
     */
    UFUNCTION(BlueprintCallable, Category = "Grid|Snap")
    static FVector AlignZToGround(
        UWorld* World,
        const FVector& WorldPos,
        float TraceUp = 200.0f,
        float TraceDown = 2000.0f
    );

    /**
     * ユニット用固定Z計算
     */
    UFUNCTION(BlueprintPure, Category = "Grid|Snap")
    static float ComputeFixedZ(
        const AUnitBase* Unit,
        const AGridPathfindingLibrary* PathFinder
    );
};
```

---

### 実装フェーズ計画

#### Phase 1: Utility抽出（1週間）

1. **Day 1-2**: `UMovementValidator` + `UGridMovementHandler`
2. **Day 3-4**: `UBarrierIntegrationHelper` + `UCoordinateSnapUtility`
3. **Day 5**: `UMovementAnimationController` Component化

#### Phase 2: UGA_MoveBase簡素化（3日間）

4. **Day 6-8**: 既存`UGA_MoveBase`を各Utilityへの呼び出しに書き換え

#### Phase 3: テスト（2日間）

5. **Day 9-10**: 統合テスト

---

## 🎯 分割戦略 - 優先度4: APlayerControllerBase

### 現状分析

**ファイル**: `/Player/PlayerControllerBase.h` (271行) + `.cpp` (802行)
**問題**: 入力処理 + カメラ計算 + グリッド計算 + ネットワークRPCが混在

### 分割提案

```
APlayerControllerBase (400行)
  ├─ UInputQuantizer (Static Utility, 150行)      [責務: 入力量子化]
  ├─ UCameraUtility (Static Utility, 150行)       [責務: カメラ相対方向計算]
  ├─ UInputWindowManager (Component, 200行)        [責務: WindowId同期]
  └─ UGridInputHelper (Static Utility, 150行)     [責務: グリッド入力変換]
```

---

## 🎯 分割戦略 - 優先度5: UTurnCorePhaseManager

### 現状分析

**ファイル**: `/Turn/TurnCorePhaseManager.h` (205行) + `.cpp` (805行)
**問題**: フェーズパイプライン + TimeSlot管理 + ASC解決が混在

### 分割提案

```
UTurnCorePhaseManager (400行)
  ├─ UTimeSlotExecutor (Static Utility, 200行)    [責務: TimeSlot実行]
  ├─ UIntentProcessor (Component, 250行)           [責務: Intent管理]
  ├─ UASCResolver (Static Utility, 150行)         [責務: ASC解決]
  └─ UPhaseExecutor (Component, 250行)             [責務: フェーズ実行]
```

---

## 📊 実装スケジュール概要

### 全体タイムライン（10週間）

| 週 | 対象クラス | タスク | 工数 |
|----|-----------|-------|------|
| **Week 1-3** | `AGameTurnManagerBase` | Subsystem抽出 (Phase 1) | 3週間 |
| **Week 4** | `AGameTurnManagerBase` | Component化 (Phase 2) | 1週間 |
| **Week 5** | `AGameTurnManagerBase` | 統合テスト (Phase 3) | 1週間 |
| **Week 6** | `AGridPathfindingLibrary` | Utility + Subsystem化 | 1週間 |
| **Week 7** | `UGA_MoveBase` | Utility抽出・簡素化 | 1週間 |
| **Week 8** | `APlayerControllerBase` | 分割 | 1週間 |
| **Week 9** | `UTurnCorePhaseManager` | 分割 | 1週間 |
| **Week 10** | 全体 | 最終統合テスト・ドキュメント | 1週間 |

---

## ⚠️ リスク管理

### 主要リスク

| リスク | 影響度 | 対策 |
|-------|-------|------|
| **Blueprint参照の破損** | 高 | 段階的移行 + 互換性レイヤー維持 |
| **ネットワーク同期の不整合** | 高 | レプリケーションテストの徹底 |
| **パフォーマンス低下** | 中 | プロファイリング + 最適化 |
| **既存バグの再現** | 中 | リグレッションテスト自動化 |

### 対策詳細

#### Blueprint参照破損対策

- **対策1**: 旧APIを`DEPRECATED`マクロ付きで残す
- **対策2**: 移行ガイド作成（旧API → 新API対応表）
- **対策3**: Blueprint Validation Tool作成

#### ネットワーク同期対策

- **対策1**: 各Subsystemでレプリケーション明示
- **対策2**: クライアント/サーバーでの動作検証
- **対策3**: `WithValidation`/`WithPrediction`の適用

---

## ✅ 成功基準

### 定量的指標

- [ ] 各クラスが500行以下
- [ ] 単体テストカバレッジ80%以上
- [ ] ビルド時間10%削減
- [ ] 既存Blueprint互換性100%維持

### 定性的指標

- [ ] 各クラスが単一責任原則に準拠
- [ ] コードレビューで「理解しやすい」評価
- [ ] 新機能追加時の変更箇所が明確

---

## 📚 参考資料

### 設計原則

- **SOLID原則**: 特に単一責任原則（SRP）を重視
- **依存性逆転の原則**: SubsystemはInterfaceで疎結合
- **Unreal Engine Subsystem Best Practices**: [公式ドキュメント](https://docs.unrealengine.com/5.0/en-US/programming-subsystems-in-unreal-engine/)

### コーディング規約

- Unreal Engine C++ Coding Standard準拠
- コメント：日本語可（英語推奨）
- 命名：`UMySubsystem`, `FMyStruct`, `EMyEnum`

---

## 📝 次のアクション

1. **チームレビュー**: 本計画書の承認
2. **環境準備**: テストフレームワーク・CI/CD整備
3. **Week 1開始**: `UTurnFlowCoordinator` + `UPlayerInputProcessor` 実装開始

---

**作成者**: ClassSeparationAgent
**承認待ち**: プロジェクトリード
**最終更新**: 2025-11-09
