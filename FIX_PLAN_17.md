# 修正計画書: 3つの主要問題（ダメージ、敵移動、プレイヤー回転）の修正

## 1. 概要

現在確認されている以下の3つの問題を修正する。
1.  **ダメージ適用エラー:** 敵の攻撃が `Handle valid=NO` で失敗し、ダメージが入らない。
2.  **敵の移動不全:** 複数の敵が移動を指示されても1体しか動かない。
3.  **プレイヤーの向き:** 移動完了まで進行方向を向かず、挙動が不自然。

## 2. 原因仮説と修正方針

### Task 1: ダメージ適用エラー

*   **原因:**
    1.  `GA_MeleeAttack::ApplyDamageToTarget` がサーバー権限をチェックせずにクライアントで実行されている。
    2.  `SetByCaller` のタグがLyra標準 (`SetByCaller.Damage`) と不一致、またはGE側に固定値Modifierが存在し、ダメージ値(28 vs 35)が矛盾している。
    3.  `BaseDamage` と `Damage` プロパティの不一致。
*   **修正方針:**
    1.  `ApplyDamageToTarget` の冒頭に `HasAuthority()` ガードを追加する。
    2.  `SetByCallerMagnitude` で `RogueGameplayTags::SetByCaller_Damage` を使用するよう統一する。
    3.  `BaseDamage` の使用箇所を `Damage` プロパティに修正する。
    4.  `GE_Damage_Basic_Instant_C` の固定値Modifierを削除する。
    5.  `Instant` エフェクト適用時のログを修正する。

### Task 2: 敵の移動不全

*   **原因:** `TurnManager` は複数の敵の移動イベントを同時に発火させているが、`GA_MoveBase` の起動シーケンス内で移動先のセルが通行不可と判定され、アビリティが即時終了している。その結果、`TurnActionBarrierSubsystem` にアクションが登録されず、1体の成功した移動が完了した時点でバリアが全アクション完了と判断し、フェーズを終了させている。
*   **修正方針:** これはAIの移動先決定ロジックの問題である可能性が高い。根本解決にはAI側の修正が必要だが、まずは問題の可視化を優先する。`GA_MoveBase::ActivateAbility` 内で、移動先セルが原因で早期終了する箇所に、どのActorがどのセルで失敗したかを記録する警告ログを追加する。

### Task 3: プレイヤーの向き

*   **原因:** `UnitMovementComponent` が、`FinishMovement` 関数内でのみキャラクターの最終的な向きを設定している。移動中の向きを更新するロジックが `UpdateMovement` (Tick) 関数に存在しない。
*   **修正方針:** `UnitMovementComponent::UpdateMovement` 関数内に、現在の移動方向ベクトルから目標のYawを計算し、`FMath::RInterpTo` を使用して現在の向きから滑らかに補間する処理を追加する。

## 3. 実装詳細 (コード差分)

### `GA_MeleeAttack.cpp` (Task 1)
```diff
--- a/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp
+++ b/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp
@@ -293,6 +293,12 @@
 
 void UGA_MeleeAttack::ApplyDamageToTarget(AActor* Target)
 {
+    if (!GetAvatarActorFromActorInfo()->HasAuthority())
+    {
+        // Damage application is server-only.
+        return;
+    }
+
     if (!Target || !MeleeAttackEffect)
     {
         UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Invalid Target or Effect"));
@@ -308,8 +314,8 @@
     UE_LOG(LogTemp, Warning,
         TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC START ===="));
     UE_LOG(LogTemp, Warning,
-        TEXT("[GA_MeleeAttack] Attacker=%s, Target=%s, BaseDamage=%.2f"),
-        *Avatar->GetName(), *Target->GetName(), Damage);
+        TEXT("[GA_MeleeAttack] Attacker=%s, Target=%s, Damage=%.2f"),
+        *Avatar->GetName(), *Target->GetName(), Damage);
 
     // ... (rest of function)
 
@@ -354,26 +360,36 @@
         FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(MeleeAttackEffect, 1.0f, ContextHandle);
         if (SpecHandle.IsValid())
         {
-            const float FinalDamage = (Damage > 0.0f) ? Damage : 28.0f;
+            const float FinalDamage = (Damage > 0.0f) ? Damage : 28.0f;
             
-            if (Damage <= 0.0f)
+            if (Damage <= 0.0f)
             {
                 UE_LOG(LogTemp, Error,
-                    TEXT("[GA_MeleeAttack] WARNING: Damage property=%.2f is invalid! Using fallback damage=%.2f"),
-                    Damage, FinalDamage);
+                    TEXT("[GA_MeleeAttack] WARNING: Damage property=%.2f is invalid! Using fallback damage=%.2f"),
+                    Damage, FinalDamage);
             }
             
-            SpecHandle.Data->SetSetByCallerMagnitude(RogueGameplayTags::SetByCaller_Damage, FinalDamage); 
+            // Lyra標準のSetByCallerタグを使用
+            SpecHandle.Data->SetSetByCallerMagnitude(RogueGameplayTags::SetByCaller_Damage, FinalDamage); 
 
             UE_LOG(LogTemp, Warning,
-                TEXT("[GA_MeleeAttack] Applying GameplayEffect: %s with SetByCaller damage=%.2f (From Damage Prop=%.2f)"),
-                *MeleeAttackEffect->GetName(), FinalDamage, Damage);
+                TEXT("[GA_MeleeAttack] Applying GameplayEffect: %s with SetByCaller.Damage=%.2f (From Damage Prop=%.2f)"),
+                *MeleeAttackEffect->GetName(), FinalDamage, Damage);
 
             if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
             {
                 FActiveGameplayEffectHandle EffectHandle = ASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
-                UE_LOG(LogTemp, Warning,
-                    TEXT("[GA_MeleeAttack] GameplayEffect applied, Handle valid=%s"),
-                    EffectHandle.IsValid() ? TEXT("YES") : TEXT("NO - EFFECT FAILED"));
+
+                if (SpecHandle.Data->Def->DurationPolicy != EGameplayEffectDurationType::Instant)
+                {
+                    UE_LOG(LogTemp, Log,
+                        TEXT("[GA_MeleeAttack] Duration/Infinite GameplayEffect applied, Handle valid=%s"),
+                        EffectHandle.IsValid() ? TEXT("YES") : TEXT("NO - EFFECT FAILED"));
+                }
+                else
+                {
+                    UE_LOG(LogTemp, Log, TEXT("[GA_MeleeAttack] Instant GameplayEffect spec was sent to target."));
+                }
             }
         }
         else
```

### `GA_MoveBase.cpp` (Task 2)
```diff
--- a/Source/LyraGame/Rogue/Abilities/GA_MoveBase.cpp
+++ b/Source/LyraGame/Rogue/Abilities/GA_MoveBase.cpp
@@ -409,9 +409,12 @@
 
 	if (!Pathfinding->IsCellWalkableIgnoringActor(TargetCell, Unit))
 	{
-		UE_LOG(LogTurnManager, Warning,
-			TEXT("[GA_MoveBase] Cell (%d,%d) is blocked for %s; aborting move"),
-			TargetCell.X, TargetCell.Y, *GetNameSafe(Unit));
+		// Task 2: Add detailed log for movement failure
+		const int32 TargetCost = Pathfinding->GetGridCost(TargetCell.X, TargetCell.Y);
+		AActor* BlockingActor = GetWorld()->GetSubsystem<UGridOccupancySubsystem>()->GetActorAtCell(TargetCell);
+		UE_LOG(LogTurnManager, Warning, 
+			TEXT("[GA_MoveBase] ABORT: Actor '%s' move to cell (%d,%d) failed. Reason: Cell blocked. GridCost=%d. BlockingActor=%s."),
+			*GetNameSafe(Unit), TargetCell.X, TargetCell.Y, TargetCost, *GetNameSafe(BlockingActor));
 		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
 		return;
 	}
```

### `UnitMovementComponent.cpp` (Task 3)
```diff
--- a/Source/LyraGame/Rogue/Character/UnitMovementComponent.cpp
+++ b/Source/LyraGame/Rogue/Character/UnitMovementComponent.cpp
@@ -150,6 +150,18 @@
 		return;
 	}
 
+	// Task 3: Add rotation logic during movement
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
