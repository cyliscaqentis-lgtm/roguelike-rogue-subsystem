# フリーズ&アニメーション修正プラン

## 問題1: 移動完了時のラグ

### 原因分析
`HandleManualMoveFinished`や`FinishMovement`で以下の処理が行われており、これが遅延を引き起こしている可能性：

1. **GridOccupancy更新のリトライロジック** (最大5回、100msタイマー)
   - `FinishMovement()`内で`UpdateActorCell`が失敗すると、タイマーで再試行
   - これが最悪500msの遅延を生む可能性

2. **TurnActionBarrier完了通知**
   - `CompleteAction`がNextTickでブロードキャストをスケジュール
   - 次のターンの開始がフレーム遅延する

### 修正方針

#### A. GridOccupancy更新の即時化
現在のリトライロジックを見直し、失敗時の処理を改善：

```cpp
// UnitMovementComponent.cpp の FinishMovement()

// 現在のコード (問題):
if (!bSuccess)
{
    // タイマーで100ms後に再試行 → これが遅延を生む
    World->GetTimerManager().SetTimer(..., 0.1f, false);
}

// 修正案:
if (!bSuccess)
{
    // リトライを即座に複数回試行
    for (int32 RetryIdx = 0; RetryIdx < 3 && !bSuccess; ++RetryIdx)
    {
        bSuccess = GridOccupancy->UpdateActorCell(OwnerUnit, FinalCell);
    }
    
    if (!bSuccess)
    {
        // それでも失敗なら強制更新
        GridOccupancy->ForceUpdateActorCell(OwnerUnit, FinalCell);
        UE_LOG(LogUnitMovement, Warning, 
            TEXT("Force updated GridOccupancy for %s"), *GetNameSafe(OwnerUnit));
    }
}
```

#### B. Barrier通知の即時化
`TurnActionBarrierSubsystem::CompleteAction`のNextTickスケジュールを削除して即座にブロードキャスト：

```cpp
// TurnActionBarrierSubsystem.cpp の CompleteAction()

// 現在のコード (問題):
GetWorld()->GetTimerManager().SetTimerForNextTick([this, TurnId]()
{
    OnAllMovesFinished.Broadcast(TurnId);
});

// 修正案:
OnAllMovesFinished.Broadcast(TurnId);  // 即座にブロードキャスト
```

---

## 問題2: 歩行アニメーションが再生されない

### 原因分析
`UnitMovementComponent`は独自の移動システムを使用しており、`CharacterMovementComponent`の`Velocity`を更新していない。Lyraの`ABP_Mannequin_Base`は`Velocity`を参照して歩行アニメーションを判定するため、常に`Velocity = 0`となり、Idleアニメーションのままになっている。

### 修正方針

#### オプション1: CharacterMovementComponent Velocityの同期 (推奨)

`UnitMovementComponent`の移動中に`CharacterMovementComponent`の`Velocity`を更新：

```cpp
// UnitMovementComponent.cpp の UpdateMovement()

void UUnitMovementComponent::UpdateMovement(float DeltaTime)
{
    // ... 既存の移動ロジック ...
    
    // 移動方向と速度を計算
    const FVector Direction = (TargetLocation - CurrentLocation).GetSafeNormal();
    
    // CharacterMovementComponentにVelocityを設定してアニメーションを駆動
    if (ACharacter* Character = Cast<ACharacter>(Owner))
    {
        if (UCharacterMovementComponent* CharMoveComp = Character->GetCharacterMovement())
        {
            // 歩行アニメーションのためにVelocityを設定
            CharMoveComp->Velocity = Direction * PixelsPerSec;
        }
    }
    
    // Ownerの実際の位置を更新 (既存の処理)
    Owner->SetActorLocation(NewLocation);
}
```

**FinishMovement()でのVelocityリセット:**

```cpp
void UUnitMovementComponent::FinishMovement()
{
    bIsMoving = false;
    SetComponentTickEnabled(false);
    
    // CharacterMovementComponentのVelocityをリセット
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CharMoveComp = Character->GetCharacterMovement())
        {
            CharMoveComp->Velocity = FVector::ZeroVector;
        }
    }
    
    // ... 既存の処理 ...
}
```

#### オプション2: AnimBPでUnitMovementComponentを直接参照

Lyraの`ABP_Mannequin_Base`を継承したカスタムABPを作成し、`UnitMovementComponent`の状態を直接参照：

```
// AnimBP内のEvent Graph:
1. Get Owner -> Cast to UnitBase
2. Get Movement Component (UnitMovementComponent)
3. Get IsMoving -> Boolean
4. Set to IsMoving変数
5. Locomotionステートマシンで使用
```

**長所**: 既存のMovementロジックを変更不要
**短所**: AnimBPの変更が必要、Blueprint作業が増える

---

## 実装優先順位

### Phase 1: 移動完了ラグの修正 (高優先度)
1. `UnitMovementComponent::FinishMovement()`のGridOccupancy更新を即時リトライに変更
2. `TurnActionBarrierSubsystem::CompleteAction()`のNextTickスケジュールを削除

### Phase 2: 歩行アニメーションの修正 (中優先度)
オプション1を推奨:
1. `UnitMovementComponent::UpdateMovement()`で`CharacterMovementComponent::Velocity`を設定
2. `UnitMovementComponent::FinishMovement()`で`Velocity`をリセット
3. `UnitMovementComponent::CancelMovement()`でも`Velocity`をリセット

---

## テスト項目

### Phase 1テスト:
- [ ] 移動完了から次の移動開始までのラグが無くなったか
- [ ] GridOccupancy更新が正常に動作するか
- [ ] ターン進行が正常に動作するか

### Phase 2テスト:
- [ ] プレイヤーの歩行アニメーションが再生されるか
- [ ] 敵の歩行アニメーションが再生されるか (敵もUnitBaseを継承している場合)
- [ ] 移動停止時にIdleアニメーションに戻るか
- [ ] 移動速度とアニメーション速度が一致するか

---

## コードリビジョンタグ

### Phase 1:
- `INC-2025-1122-PERF-R6`: Remove GridOccupancy retry timer, use immediate retry loop
- `INC-2025-1122-PERF-R7`: Remove NextTick delay from TurnActionBarrier broadcast

### Phase 2:
- `INC-2025-1122-ANIM-R1`: Sync CharacterMovementComponent Velocity with UnitMovementComponent

