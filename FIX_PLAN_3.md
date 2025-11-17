# 修正計画書: INC-2025-1117E (根本原因の最終解決)

## 1. 概要

- **インシデントID:** INC-2025-1117E
- **問題:** ユニットの移動完了時に `Movement.Mode.Falling` （落下中）タグが残存する。これは、移動処理の責務が `GA_MoveBase` と `UnitMovementComponent` に不適切に分割されており、`GA_MoveBase` 側での強制テレポートが `CharacterMovementComponent` の状態を破壊していることが原因。

## 2. 原因仮説と根拠

- **仮説:** `GA_MoveBase::OnMoveFinished` 内で実行されるグリッド中心への「スナップ」処理 (`SetActorLocation`) が、`CharacterMovementComponent` の状態と競合し、ユニットを一時的に「落下中」状態にしている。
- **根拠:**
    - ログ分析により、ターン開始時に `Falling` タグがクリーンアップされていることを確認。これは、前ターンの移動処理の副作用としてタグが残存していることを示す。
    - `UnitMovementComponent.cpp` の `FinishMovement` にはスナップ処理がなく、`GA_MoveBase.cpp` の `OnMoveFinished` にスナップ処理が存在することを確認。責務の分割が問題の直接原因であると断定。

## 3. 修正方針案

移動完了時の最終的な位置決定と状態管理の責務を `UnitMovementComponent` に完全に移管する。

1.  **`UnitMovementComponent::FinishMovement` の機能拡充:**
    -   `FinishMovement` 関数の内部で、移動完了時にユニットを最終目的地のグリッドセル中心へ正確にスナップさせる処理を追加する。
    -   パスの最後のウェイポイントが最終目的地であるため、その座標を基準にスナップ処理を行う。
    -   `OnMoveFinished` デリゲートをブロードキャストするのは、このスナップ処理が完了した**後**にする。

2.  **`GA_MoveBase::OnMoveFinished` の責務縮小:**
    -   `OnMoveFinished` 関数から、ユニットの `SetActorLocation` や `SetActorRotation` を呼び出しているスナップ/回転処理のコードを完全に削除する。
    -   この関数の責務を、`TurnActionBarrier` の完了とアビリティの終了処理のみに限定する。

- **修正イメージ: `UnitMovementComponent.cpp`**
  ```cpp
  // UnitMovementComponent.cpp -> FinishMovement()

  void UUnitMovementComponent::FinishMovement()
  {
      bIsMoving = false;
      SetComponentTickEnabled(false); // Tickを先に無効化

      AUnitBase* OwnerUnit = GetOwnerUnit();
      if (OwnerUnit && CurrentPath.Num() > 0)
      {
          // ★★★ START: 新しいスナップ処理 ★★★
          const FVector FinalDestination = CurrentPath.Last();

          // 向きを最終目的地に合わせる
          FVector MoveDirection = FinalDestination - OwnerUnit->GetActorLocation();
          MoveDirection.Z = 0.0f;
          if (!MoveDirection.IsNearlyZero())
          {
              const float TargetYaw = MoveDirection.Rotation().Yaw;
              FRotator NewRotation = OwnerUnit->GetActorRotation();
              NewRotation.Yaw = TargetYaw;
              OwnerUnit->SetActorRotation(NewRotation);
          }

          // 最終目的地にスナップする
          OwnerUnit->SetActorLocation(FinalDestination, false, nullptr, ETeleportType::None);
          // ★★★ END: 新しいスナップ処理 ★★★

          // GridOccupancyの更新
          if (UWorld* World = GetWorld())
          {
              // (既存のGridOccupancy更新処理...)
          }
      }

      CurrentPath.Empty();
      CurrentPathIndex = 0;

      // イベント配信
      OnMoveFinished.Broadcast(OwnerUnit);
      UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement finished and snapped."));
  }
  ```

- **修正イメージ: `GA_MoveBase.cpp`**
  ```cpp
  // GA_MoveBase.cpp -> OnMoveFinished()

  void UGA_MoveBase::OnMoveFinished(AUnitBase* Unit)
  {
      // ▼▼▼ このブロックをすべて削除 ▼▼▼
      /*
      UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
      if (Unit && Pathfinding)
      {
          // (スナップと回転のロジック)
      }
      */
      // ▲▲▲ ここまで削除 ▲▲▲

      UE_LOG(LogTurnManager, Log,
          TEXT("[MoveComplete] Unit %s reached destination, GA_MoveBase ending."),
          *GetNameSafe(Unit));

      // バリア完了とアビリティ終了のロジックは残す
      if (bBarrierRegistered && MoveActionId.IsValid())
      {
          // ...
      }

      EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, false);
  }
  ```

## 4. 影響範囲

- **移動の安定性:** `CharacterMovementComponent` との競合がなくなり、`Falling` タグが残存する問題が解決される。これにより、移動後のユニットの状態が常に安定的（`MOVE_Walking`）になる。
- **コードの責務分担:** 移動に関するロジックが `UnitMovementComponent` に集約され、アビリティ（`GA_MoveBase`）は移動の「きっかけ」を与えるだけの、よりクリーンな設計になる。
- **副作用のリスク:** 低い。責務の重複を解消し、単一責任の原則に近づける修正であるため、副作用よりも安定性の向上が期待される。

## 5. テスト方針

- **手動テスト:**
    - プレイヤーを1タイル移動させ、正常に移動が完了することを確認する。
    - ターンをまたいで、コンソールコマンド `showdebug abilitysystem` を使用し、プレイヤーユニットに `Movement.Mode.Falling` タグが付与されていないことを確認する。
    - ログを確認し、ターン開始時に `[TagCleanup]` が `Falling=1` を報告しなくなったことを確認する。
