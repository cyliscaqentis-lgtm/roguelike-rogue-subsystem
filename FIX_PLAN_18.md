# 修正計画書: 敵移動ディスパッチ漏れとプレイヤー回転の修正

## 1. 概要

現在確認されている以下の2つの問題を修正する。
1.  **[最優先] 敵の移動不全:** 移動フェーズにおいて、移動が割り当てられたにも関わらず、一部の敵ユニットがディスパッチから漏れて移動しない。
2.  **[低優先] プレイヤーの向き:** 移動完了まで進行方向を向かず、挙動が不自然。

## 2. 原因仮説と修正方針

### Task 1: 敵の移動不全

*   **原因:** `GameTurnManagerBase::ExecuteEnemyMoves_Sequential` 関数が、移動対象のアクションを選別する際に、元のインテントが `AI.Intent.Move` であるものしか含めていない。これにより、`CoreResolvePhase` によって `WAIT` から `MOVE` に変更されたユニットが除外されてしまっている。
*   **修正方針:** フィルタリング条件を「元のインテント」ではなく、「最終的なアクションが移動であるか」で判断するように変更する。具体的には、`FResolvedAction` 構造体の `SourceCell` と `TargetCell` が異なるアクションをすべて移動対象とする。

### Task 2: プレイヤーの向き

*   **原因:** `UnitMovementComponent` が、`FinishMovement` 関数内でのみキャラクターの最終的な向きを設定している。移動中の向きを更新するロジックが `UpdateMovement` (Tick) 関数に存在しない。
*   **修正方針:** `UnitMovementComponent::UpdateMovement` 関数内に、現在の移動方向ベクトルから目標のYawを計算し、`FMath::RInterpTo` を使用して現在の向きから滑らかに補間する処理を追加する。

## 3. 実装詳細 (コード差分)

### `GameTurnManagerBase.cpp` (Task 1)
```diff
--- a/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
+++ b/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
@@ -1838,7 +1838,9 @@
     TArray<FResolvedAction> MoveActions;
     for (const FResolvedAction& Action : CachedResolvedActions)
     {
-        if (Action.AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Move))
+        // 変更点: 元のインテントタグではなく、実際に移動が発生するかどうかで判断する
+        // これにより、WAITからMOVEに解決されたユニットも対象に含まれる
+        if (Action.SourceCell != Action.TargetCell)
         {
             // Skip player moves, they've already happened
             if (Action.SourceActor && Action.SourceActor->IsA(AEnemyUnitBase::StaticClass()))

```

### `UnitMovementComponent.cpp` (Task 2)
```diff
--- a/Source/LyraGame/Rogue/Character/UnitMovementComponent.cpp
+++ b/Source/LyraGame/Rogue/Character/UnitMovementComponent.cpp
@@ -150,6 +150,18 @@
 		return;
 	}
 
+	// Task 2: Add rotation logic during movement
+	if (!Direction.IsNearlyZero())
+	{
+		// Interpolate rotation towards the direction of movement
+		const FRotator TargetRotation = Direction.Rotation();
+		FRotator NewRotation = FMath::RInterpTo(Owner->GetActorRotation(), TargetRotation, DeltaTime, 10.0f);
+		NewRotation.Pitch = 0; // Keep character upright
+		NewRotation.Roll = 0;
+		Owner->SetActorRotation(NewRotation);
+	}
+
 	// 移動計算
 	FVector NewLocation;
 	if (bUseSmoothMovement)
```
