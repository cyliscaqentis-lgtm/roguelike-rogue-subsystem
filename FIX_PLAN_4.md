# 最終修正計画書: AIサブシステムへのステート伝達バグの修正 (FIX_PLAN_4)

## 1. 問題の概要

**現象:**
これまでの修正により、`AGameTurnManagerBase` はターン開始時にプレイヤーの正しいグリッド座標を認識するようになった。しかし、その正しい座標が `UEnemyAISubsystem` に伝達されず、AIは依然として不正な座標 `(-1,-1)` を基に行動決定を行うため、ゲームがソフトロックする。

**根本原因:**
`AGameTurnManagerBase::OnTurnStartedHandler` は、プレイヤーの正しい座標をローカル変数に保持しているが、`UEnemyAISubsystem::BuildObservations` を呼び出す際にその変数を渡していない。`BuildObservations` は渡されなかった座標を内部で再計算しようとし、その際にバグの原因である物理座標 (`GetActorLocation`) を参照してしまうため、不正な座標が使われてしまう。

## 2. 修正方針

`UEnemyAISubsystem::BuildObservations` の関数シグネチャを変更し、プレイヤーのグリッド座標を直接引数として受け取るようにする。これにより、`TurnManager` が持つ信頼できる座標情報を直接サブシステムに注入（Dependency Injection）し、サブシステム内での不要かつ危険な再計算を排除する。

## 3. 実装計画

### 手順 1: `UEnemyAISubsystem` の関数シグネチャ変更

#### **対象ファイル**
*   `Source/LyraGame/Rogue/AI/Enemy/EnemyAISubsystem.h`
*   `Source/LyraGame/Rogue/AI/Enemy/EnemyAISubsystem.cpp`

#### **変更内容**
`BuildObservations` 関数のシグネチャを以下のように変更します。`AActor* Player` の引数を `const FIntPoint& PlayerGrid` に置き換えます。

##### **ヘッダファイル (.h) の変更**
```cpp
// EnemyAISubsystem.h

// 変更前 (Before)
void BuildObservations(
    const TArray<AActor*>& Enemies,
    AActor* Player,
    UGridPathfindingSubsystem* PathFinder,
    TArray<FEnemyObservation>& OutObs);

// 変更後 (After)
void BuildObservations(
    const TArray<AActor*>& Enemies,
    const FIntPoint& PlayerGrid, // AActor* Player を FIntPoint に変更
    UGridPathfindingSubsystem* PathFinder,
    TArray<FEnemyObservation>& OutObs);
```

##### **ソースファイル (.cpp) の変更**
```cpp
// EnemyAISubsystem.cpp

// 変更前 (Before)
void UEnemyAISubsystem::BuildObservations(
    const TArray<AActor*>& Enemies,
    AActor* Player,
    UGridPathfindingSubsystem* PathFinder,
    TArray<FEnemyObservation>& OutObs)
{
    // ...
    const FIntPoint PlayerGrid = PathFinder->WorldToGrid(Player->GetActorLocation()); // この行が問題
    // ...
}

// 変更後 (After)
void UEnemyAISubsystem::BuildObservations(
    const TArray<AActor*>& Enemies,
    const FIntPoint& PlayerGrid, // 引数を変更
    UGridPathfindingSubsystem* PathFinder,
    TArray<FEnemyObservation>& OutObs)
{
    // ...
    // const FIntPoint PlayerGrid = PathFinder->WorldToGrid(Player->GetActorLocation()); // この行を完全に削除
    UE_LOG(LogEnemyAI, Log, TEXT("[BuildObservations] PlayerGrid=(%d, %d)"), PlayerGrid.X, PlayerGrid.Y); // ログは引数の値を表示
    // ...
}
```
**注:** `BuildObservations` 内で `Player` アクターへの参照が他にないことを確認してください。もし他の目的で `Player` が必要であれば、引数を `(..., const FIntPoint& PlayerGrid, AActor* Player, ...)` のように両方渡す形にします。ログを見る限り、`PlayerGrid` の計算にしか使われていないため、置き換えが適切と判断します。

---

### 手順 2: `AGameTurnManagerBase` の呼び出し箇所を修正

#### **対象ファイル**
*   `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`

#### **変更内容**
`OnTurnStartedHandler` 内で `EnemyAISys->BuildObservations` を呼び出している箇所を、新しい関数シグネチャに合わせて修正します。事前に取得した正しい `PlayerGrid` 変数を渡します。

##### **修正箇所**
```cpp
// GameTurnManagerBase.cpp -> OnTurnStartedHandler()

// ...
// この時点で PlayerGrid は正しい座標 (例: 15,16) を保持している
const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn); 
// ...

// ...
// さらに下の行で...
if (EnemyAISys && EnemyTurnDataSys && CachedPathFinder.IsValid() && PlayerPawn && CachedEnemiesForTurn.Num() > 0)
{
    TArray<FEnemyObservation> PreliminaryObs;
    
    // 変更前 (Before)
    // EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerPawn, CachedPathFinder.Get(), PreliminaryObs);

    // 変更後 (After)
    EnemyAISys->BuildObservations(CachedEnemiesForTurn, PlayerGrid, CachedPathFinder.Get(), PreliminaryObs);

    EnemyTurnDataSys->Observations = PreliminaryObs;
    // ...
}
// ...
```
**注:** `OnPlayerCommandAccepted_Implementation` 内にも `BuildObservations` の呼び出しがあります。こちらも同様に修正が必要です。

---

### 手順 3: （念のため）`OnPlayerCommandAccepted` 内の呼び出しも修正

#### **対象ファイル**
*   `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`

#### **変更内容**
プレイヤーの入力後に敵の意図を再計算する `OnPlayerCommandAccepted_Implementation` 内の `BuildObservations` 呼び出しも、同様に修正します。

##### **修正箇所**
```cpp
// GameTurnManagerBase.cpp -> OnPlayerCommandAccepted_Implementation()

// ...
// この関数内では、プレイヤーの移動先を PlayerDestination として計算している
FIntPoint PlayerDestination = CurrentCell + Direction;
// ...

// ...
// TArray<FEnemyObservation> Observations;
// ...

// 変更前 (Before)
// EnemyAISys->BuildObservations(Enemies, PlayerPawn, CachedPathFinder.Get(), Observations);

// 変更後 (After)
EnemyAISys->BuildObservations(Enemies, PlayerDestination, CachedPathFinder.Get(), Observations);
// ...
```

以上の修正により、`TurnManager` が一元的に管理する正しい座標が、常にAIサブシステムに伝達されるようになり、一連のバグが完全に解決されるはずです。