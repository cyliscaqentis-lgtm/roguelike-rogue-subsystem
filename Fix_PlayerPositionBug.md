# 修正計画書: ターン進行時のプレイヤー座標消失バグ

## 1. 問題の概要

**現象:**
Turn 0 から Turn 1 へ進行する際、`AGameTurnManagerBase` がプレイヤーの最新のグリッド座標を取得できず、無効な座標 `(-1, -1)` として処理してしまう。これにより、`DistanceField` の構築が失敗し、敵AIがすべて「待機」を選択するため、ゲームが進行不能（ソフトロック）になる。

**根本原因:**
`AGameTurnManagerBase::AdvanceTurnAndRestart()` 関数内で、プレイヤーの座標を取得する際に、信頼できる唯一の情報源である `UGridOccupancySubsystem` ではなく、アクターの物理的なワールド座標 (`PlayerPawn->GetActorLocation()`) を参照している。

物理座標の更新は、`GridOccupancySubsystem` へのコミットよりも遅れる可能性があるため、ターン進行のクリティカルなタイミングで不正確な（または無効な）値を取得してしまっている。

---

## 2. 修正方針

`AGameTurnManagerBase` がプレイヤーの座標を取得するロジックを、`UGridOccupancySubsystem::GetCellOfActor()` を使用するように変更する。これにより、常に公式で最新のグリッド座標を参照することを保証する。

---

## 3. 実装計画

#### **対象ファイル**
*   `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`

#### **修正箇所**
`AGameTurnManagerBase::AdvanceTurnAndRestart()` 関数内の、プレイヤー座標を取得している部分。

#### **修正前のコード (Before)**
```cpp
// Around line 850 in GameTurnManagerBase.cpp

if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
{
    if (CachedPathFinder.IsValid())
    {
        const FVector PlayerLoc = PlayerPawn->GetActorLocation();
        const FIntPoint PlayerGrid = CachedPathFinder->WorldToGrid(PlayerLoc);
        UE_LOG(LogTurnManager, Warning,
            TEXT("[AdvanceTurn] PLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
            CurrentTurnId, PlayerGrid.X, PlayerGrid.Y, *PlayerLoc.ToCompactString());
    }
}
```

#### **修正後のコード (After)**
```cpp
// Around line 850 in GameTurnManagerBase.cpp

if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
{
    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
            // WorldLocation is not critical here, but can be retrieved from the grid for logging if needed.
            const FVector PlayerWorldLoc = CachedPathFinder.IsValid() ? CachedPathFinder->GridToWorld(PlayerGrid) : PlayerPawn->GetActorLocation();

            UE_LOG(LogTurnManager, Warning,
                TEXT("[AdvanceTurn] PLAYER POSITION BEFORE ADVANCE: Turn=%d Grid(%d,%d) World(%s)"),
                CurrentTurnId, PlayerGrid.X, PlayerGrid.Y, *PlayerWorldLoc.ToCompactString());
        }
    }
}
```

#### **修正のポイント**
- `GetWorld()->GetSubsystem<UGridOccupancySubsystem>()` を使って、`GridOccupancySubsystem` へのポインタを取得します。
- `Occupancy->GetCellOfActor(PlayerPawn)` を呼び出して、信頼できるグリッド座標を取得します。
- 物理的なワールド座標 (`PlayerPawn->GetActorLocation()`) の直接使用を完全にやめます。ログ出力用のワールド座標は、取得したグリッド座標から逆算するか、参考値としてのみ使用します。