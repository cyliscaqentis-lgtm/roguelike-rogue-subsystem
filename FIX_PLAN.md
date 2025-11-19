# 修正計画書: プレイヤー攻撃処理の実装（最終版）

## 1. 概要

本計画は、プレイヤーの攻撃ロジックを、ユーザーからのフィードバックに基づき完全に修正するためのものです。これまでの修正で攻撃は発動するようになりましたが、以下の2つの問題が残っています。

1.  **ターゲティングの誤り**: マウスカーソル位置をターゲットとしており、「プレイヤーの前方1マス」という仕様と異なっていました。
2.  **空振り時の回転不足**: 攻撃対象がいない場合、キャラクターが正しい方向を向かずに攻撃モーションを行っていました。

この最終版の計画では、これらの問題を解決し、「プレイヤーが向いている方向に、ターゲットの有無にかかわらず正しく攻撃する」という仕様を実装します。

## 2. 根本原因 (Root Cause)

1.  **`PlayerControllerBase`**: 攻撃コマンドに、誤ってマウスカーソル下のターゲット情報を含めていました。
2.  **`TurnCommandHandler`**: サーバー側でプレイヤーの向きから攻撃方向を計算するロジックがありませんでした。
3.  **`GA_MeleeAttack`**: アビリティ自体に、攻撃対象がいない場合に「攻撃先の座標」を向いて回転する機能がありませんでした。

## 3. 修正ロードマップ

1.  **`PlayerControllerBase.cpp`の修正**: `Input_Attack_Triggered`を、ターゲット情報を含まない純粋な「攻撃コマンド」を送信するように簡略化します。
2.  **`TurnCommandHandler.cpp`の修正**: サーバー側でプレイヤーの向きから攻撃対象タイルを計算し、そこにいる敵、もしくは「攻撃先の座標」そのものをアビリティに伝えるロジックを実装します。
3.  **`GA_MeleeAttack.cpp`の修正**: アビリティの`ActivateAbility`を修正し、ターゲットがいない場合でも「攻撃先の座標」情報を使って正しい方向を向いてからモンタージュを再生するようにします。

## 4. Diffレベルの修正詳細

### 4-1. `PlayerControllerBase.cpp` の修正

`Input_Attack_Triggered`からカーソル位置の取得ロジックを完全に削除し、シンプルなコマンドを送信します。

```diff
--- a/Player/PlayerControllerBase.cpp
+++ b/Player/PlayerControllerBase.cpp
@@ -231,39 +231,34 @@
 
 void APlayerControllerBase::Input_Attack_Triggered(const FInputActionValue& Value)
 {
-    if (!IsValid(CachedTurnManager)) return;
+    if (!IsValid(CachedTurnManager))
+    {
+        UE_LOG(LogTemp, Warning, TEXT("[Client] Attack Input BLOCKED: TurnManager invalid"));
+        return;
+    }
 
     const bool bWaitingReplicated = CachedTurnManager->WaitingForPlayerInput;
     if (!bWaitingReplicated || bSentThisInputWindow)
     {
         UE_LOG(LogTemp, Warning, TEXT("[Client] Attack Input BLOCKED: Waiting=%d, SentLatch=%d"), bWaitingReplicated, bSentThisInputWindow);
         return;
     }
-
-    // Get the target cell from mouse cursor
-    FHitResult HitResult;
-    if (GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
+    
+    // ★★★ 修正: ターゲット情報を削除し、攻撃の意思のみを伝える ★★★
+    FPlayerCommand Command;
+    Command.CommandTag = RogueGameplayTags::Command_Player_Attack;
+
+    if (UWorld* World = GetWorld())
     {
-        if (PathFinder)
+        if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
         {
-            FIntPoint TargetCell = PathFinder->WorldToGrid(HitResult.Location);
-            
-            FPlayerCommand Command;
-            Command.CommandTag = RogueGameplayTags::Command_Player_Attack;
-            Command.TargetCell = TargetCell;
-            Command.TargetActor = HitResult.GetActor(); // Can be null
-
-            if (UWorld* World = GetWorld())
-            {
-                if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
-                {
-                    Command.TurnId = TFC->GetCurrentTurnId();
-                    Command.WindowId = TFC->GetCurrentInputWindowId();
-                }
-            }
-
-            bSentThisInputWindow = true;
-            Server_SubmitCommand(Command);
-
-            UE_LOG(LogTemp, Warning, TEXT("[Client] 📤 Attack Command Sent: Tag=%s, Cell=(%d,%d)"), *Command.CommandTag.ToString(), TargetCell.X, TargetCell.Y);
+            Command.TurnId = TFC->GetCurrentTurnId();
+            Command.WindowId = TFC->GetCurrentInputWindowId();
         }
     }
-    else
-    {
-        UE_LOG(LogTemp, Log, TEXT("[Client] Attack Input: No target under cursor."));
-    }
+
+    bSentThisInputWindow = true;
+    Server_SubmitCommand(Command);
+
+    UE_LOG(LogTemp, Warning, TEXT("[Client] 📤 Attack Command Sent: Tag=%s"), *Command.CommandTag.ToString());
 }
 
 void APlayerControllerBase::Input_Move_Triggered(const FInputActionValue& Value)

```

### 4-2. `TurnCommandHandler.cpp` の修正

前方1マスを計算し、ターゲットと攻撃座標の両方をイベントで渡すようにロジックを刷新します。

```diff
--- a/Turn/TurnCommandHandler.cpp
+++ b/Turn/TurnCommandHandler.cpp
@@ -4,6 +4,7 @@
 #include "Utility/RogueGameplayTags.h"
 #include "Grid/GridOccupancySubsystem.h"
 #include "Abilities/GameplayAbilityTargetTypes.h"
+#include "Grid/GridPathfindingSubsystem.h"
 #include "Character/UnitBase.h"
 #include "AbilitySystemComponent.h"
 #include "AbilitySystemGlobals.h"
@@ -53,49 +54,61 @@
     // ★★★ 修正(改訂): `TryActivateAbilityByTag`ではなく`HandleGameplayEvent`を使用してアビリティを起動する ★★★
     if (Command.CommandTag.MatchesTag(RogueGameplayTags::Command_Player_Attack) || Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Attack)) // 旧Tagとの互換性のため
     {
-        AActor* TargetActor = Command.TargetActor.Get();
-        if (!TargetActor)
+        // ★★★ 修正(最終版): プレイヤーの向きから前方1マスのターゲットを計算する ★★★
+        UGridPathfindingSubsystem* PathFinder = GetWorld()->GetSubsystem<UGridPathfindingSubsystem>();
+        UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();
+        if (!PathFinder || !Occupancy)
         {
-            // コマンドにターゲットが含まれていない場合、セル座標からターゲットを検索する
-            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
-            {
-                TargetActor = Occupancy->GetActorAtCell(Command.TargetCell);
-            }
+            UE_LOG(LogTurnManager, Error, TEXT("[TurnCommandHandler] PathFinder or Occupancy subsystem not found."));
+            return false;
         }
 
+        const FVector ForwardVector = PlayerUnit->GetActorForwardVector();
+        const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerUnit->GetActorLocation());
+        const FIntPoint Direction = FIntPoint(FMath::RoundToInt(ForwardVector.X), FMath::RoundToInt(ForwardVector.Y));
+        const FIntPoint TargetCell = CurrentCell + Direction;
+
+        AActor* TargetActor = Occupancy->GetActorAtCell(TargetCell);
+
+        // ターゲットが自分自身、または攻撃不可能なアクターの場合は無視する
+        if (TargetActor == PlayerUnit || (TargetActor && !TargetActor->IsA(AUnitBase::StaticClass())))
+        {
+            TargetActor = nullptr;
+        }
+
+        FGameplayEventData AttackEventData;
+        AttackEventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack; // アビリティが待っているトリガータグ
+        AttackEventData.Instigator = PlayerPawn;
+        AttackEventData.Target = TargetActor; // 敵がいればセット、いなければNULL（空振り）
+
+        // 攻撃先の座標情報をEventMagnitudeにパックしてアビリティに渡す
+        // これにより、空振りの場合でもアビリティはどこを向くべきかを知ることができる
+        AttackEventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(TargetCell.X, TargetCell.Y));
+
+        FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
+        TargetData->HitResult.Location = PathFinder->GridToWorld(TargetCell); // ターゲットの物理的な位置
+        if (TargetActor)
+        {
+             TargetData->HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
+        }
+        AttackEventData.TargetData.Add(TargetData);
+
+        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Sending GameplayEvent '%s' to trigger attack. TargetActor: '%s', TargetCell: (%d,%d)"), 
+            *AttackEventData.EventTag.ToString(), 
+            *GetNameSafe(TargetActor),
+            TargetCell.X, TargetCell.Y);
+
+        ASC->HandleGameplayEvent(AttackEventData.EventTag, &AttackEventData);
+
+        return true; // イベントは送信された。ここから先は成功と見なす
+    }
+    else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Move))
+    {
+        // 移動ロジックはGameTurnManagerBaseがまだ持っているので、ここでは何もしない
+        return true;
+    }
-        if (!TargetActor)
-        {
-            UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Attack command failed: No valid target at cell (%d,%d)"), Command.TargetCell.X, Command.TargetCell.Y);
-            return false;
-        }
-
-        FGameplayEventData AttackEventData;
-        AttackEventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack; // アビリティが待っているトリガータグ
-        AttackEventData.Instigator = PlayerPawn;
-        AttackEventData.Target = TargetActor;
-
-        // アビリティにターゲット情報を渡すためのデータを作成
-        FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
-        FHitResult HitResult;
-        HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
-        HitResult.Location = TargetActor->GetActorLocation();
-        HitResult.ImpactPoint = HitResult.Location;
-        TargetData->HitResult = HitResult;
-        AttackEventData.TargetData.Add(TargetData);
-
-        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Sending GameplayEvent '%s' to trigger attack on '%s'"), *AttackEventData.EventTag.ToString(), *GetNameSafe(TargetActor));
-
-        ASC->HandleGameplayEvent(AttackEventData.EventTag, &AttackEventData);
-
-        return true; // イベントは送信された。ここから先は成功と見なす
-    }
-    else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Move))
-    {
-        // 移動ロジックはGameTurnManagerBaseがまだ持っているので、ここでは何もしない
-        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Move command received, passing through for now."));
-        return true;
-    }
 
     return false;
 }

```
*(注: `TurnCommandEncoding.h`が`TurnCommandHandler.cpp`に含まれている必要がありますが、これは既に`GameTurnManagerBase.cpp`で使われているため、プロジェクト内には存在しているはずです)*

### 4-3. `GA_MeleeAttack.cpp` の修正

空振りの場合でも、送られてきた「攻撃先の座標」を向くように回転ロジックを修正します。

```diff
--- a/Abilities/GA_MeleeAttack.cpp
+++ b/Abilities/GA_MeleeAttack.cpp
@@ -9,6 +9,7 @@
 #include "Animation/AnimMontage.h"
 #include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
 #include "GameFramework/PlayerController.h"
+#include "Utility/TurnCommandEncoding.h"
 #include "GameFramework/Pawn.h"
 #include "Grid/GridOccupancySubsystem.h"
 // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
@@ -101,6 +102,9 @@
     UE_LOG(LogTemp, Error,
         TEXT("[GA_MeleeAttack] CommitAbility SUCCESS - Proceeding with attack logic"));
 
+    FIntPoint AttackTargetCell = FIntPoint(EForceInit::ForceInit);
+    bool bHasAttackTargetCell = false;
+
     if (TriggerEventData && TriggerEventData->TargetData.IsValid(0))
     {
         const FGameplayAbilityTargetData* TargetData = TriggerEventData->TargetData.Get(0);
@@ -114,6 +118,14 @@
             }
         }
     }
+    
+    // ★★★ 修正(最終版): EventMagnitudeから攻撃先のセル座標をデコードする ★★★
+    if (TriggerEventData && TriggerEventData->EventMagnitude > 0)
+    {
+        int32 UnpackedCell = static_cast<int32>(TriggerEventData->EventMagnitude);
+        AttackTargetCell = TurnCommandEncoding::UnpackCell(UnpackedCell);
+        bHasAttackTargetCell = true;
+    }
 
     if (!TargetUnit)
     {
@@ -144,38 +156,38 @@
     UGridPathfindingSubsystem* GridLib = World ? World->GetSubsystem<UGridPathfindingSubsystem>() : nullptr;
     const FTargetFacingInfo FacingInfo = ComputeTargetFacingInfo(TargetUnit, World, GridLib);
     UpdateCachedTargetLocation(FacingInfo.Location, FacingInfo.ReservedCell, GridLib);
-    FVector TargetFacingLocation = FacingInfo.Location;
-
-    if (TargetUnit && ActorInfo && ActorInfo->AvatarActor.IsValid())
+    
+    // ★★★ 修正(最終版): 回転ロジックの刷新 ★★★
+    AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
+    if (Avatar)
     {
-        AActor* Avatar = ActorInfo->AvatarActor.Get();
-        if (Avatar && IsValid(TargetUnit))
-        {
-            FVector ToTarget = TargetFacingLocation - Avatar->GetActorLocation();
+        FVector TargetLocation = FVector::ZeroVector;
+        bool bShouldRotate = false;
+
+        if (TargetUnit) // ターゲットがいるなら、そちらを向く
+        {
+            TargetLocation = FacingInfo.Location;
+            bShouldRotate = true;
+        }
+        else if (bHasAttackTargetCell && GridLib) // ターゲットがいない（空振り）が、攻撃先の座標があるなら、そちらを向く
+        {
+            TargetLocation = GridLib->GridToWorld(AttackTargetCell, Avatar->GetActorLocation().Z);
+            bShouldRotate = true;
+        }
+
+        if (bShouldRotate)
+        {
+            FVector ToTarget = TargetLocation - Avatar->GetActorLocation();
             ToTarget.Z = 0.0f;  // Ignore vertical axis
 
             if (!ToTarget.IsNearlyZero())
             {
                 const FVector DirectionToTarget = ToTarget.GetSafeNormal();
-                // CodeRevision: INC-2025-00022-R1 (Correct melee attack rotation) (2025-11-17 19:00)
                 const FRotator NewRotation = DirectionToTarget.Rotation();
                 Avatar->SetActorRotation(NewRotation);
 
                 UE_LOG(LogTemp, Log, TEXT("[GA_MeleeAttack] %s: Rotated to face target location. Yaw=%.1f"),
-                    *GetNameSafe(Avatar), *GetNameSafe(TargetUnit), NewRotation.Yaw);
-
-                if (FacingInfo.bUsedReservedCell)
-                {
-                    UE_LOG(LogTemp, Log,
-                        TEXT("[GA_MeleeAttack] %s: Using reserved cell %s for facing rotation"),
-                        *GetNameSafe(Avatar), *FacingInfo.ReservedCell.ToString());
-                }
-            }
-            else
-            {
-                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: Cannot rotate - target is at same location"),
                     *GetNameSafe(Avatar), NewRotation.Yaw);
             }
         }
-    }
-    else if (!TargetUnit)
-    {
-        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: Cannot rotate - no target"),
-            *GetNameSafe(GetAvatarActorFromActorInfo()));
     }
 
     UE_LOG(LogTemp, Error,
```