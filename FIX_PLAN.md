# 修正計画書: プレイヤー攻撃処理の実装（改訂版）

## 1. 概要

本計画は、プレイヤーの攻撃コマンド処理機能の実装を**訂正**するものです。

前回の修正でコマンド処理の責務を `TurnCommandHandler` に集約する構造的なリファクタリングは成功しましたが、アビリティの**起動方法を誤った**ため、攻撃が失敗しました。この改訂版では、その起動ロジックを正しく修正します。

## 2. 根本原因 (Root Cause)

前回の修正で、`TurnCommandHandler` は `TryActivateAbilityByTag` を使ってアビリティを直接起動しようとしました。しかし、ログ分析の結果、対象のアビリティ（`GA_AttackBase`）は直接起動される設計ではなく、`GameplayEvent.Intent.Attack` という名前の**Gameplay Event（合図）** を受け取って起動する「トリガー型」のアビリティであることが判明しました。

従って、`TryActivateAbilityByTag` の呼び出しは空振りし、アビリティは起動しませんでした。これが失敗の根本原因です。

## 3. 修正ロードマップ

1.  **`TurnCommandHandler.cpp` の修正**: `ProcessPlayerCommand` 関数内のロジックを、間違っていた `TryActivateAbilityByTag` の呼び出しから、正しい `HandleGameplayEvent` の呼び出しに置き換えます。これにより、アビリティが期待する通りの「合図」を送信し、攻撃が正しく実行されるようになります。

その他のファイル（`RogueGameplayTags`, `PlayerControllerBase`など）への変更は前回の修正で完了しており、今回の修正対象には含まれません。

## 4. Diffレベルの修正詳細

### 4-1. `Turn/TurnCommandHandler.cpp` のヘッダーインクルード追加

ターゲット特定のために `GridOccupancySubsystem` が、イベント送信のためにアビリティ関連の型定義が必要になります。

```diff
--- a/Turn/TurnCommandHandler.cpp
+++ b/Turn/TurnCommandHandler.cpp
@@ -2,6 +2,9 @@
 #include "Turn/TurnCommandHandler.h"
 #include "Player/PlayerControllerBase.h"
 #include "Utility/RogueGameplayTags.h"
+#include "Grid/GridOccupancySubsystem.h"
+#include "Abilities/GameplayAbilityTargetTypes.h"
 #include "Character/UnitBase.h"
 #include "AbilitySystemComponent.h"
 #include "AbilitySystemGlobals.h"

```

### 4-2. `Turn/TurnCommandHandler.cpp` のロジック修正

`ProcessPlayerCommand` 内のロジックを、Gameplay Eventを送信する形に全面的に書き換えます。

```diff
--- a/Turn/TurnCommandHandler.cpp
+++ b/Turn/TurnCommandHandler.cpp
@@ -47,30 +47,49 @@
         return false;
     }
 
-    if (Command.CommandTag.MatchesTag(RogueGameplayTags::Command_Player_Attack))
+    // ★★★ 修正(改訂): `TryActivateAbilityByTag`ではなく`HandleGameplayEvent`を使用してアビリティを起動する ★★★
+    if (Command.CommandTag.MatchesTag(RogueGameplayTags::Command_Player_Attack) || Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Attack)) // 旧Tagとの互換性のため
     {
-        // 将来的にはここで遠距離・近距離を切り替える
-        const FGameplayTag AttackAbilityTag = RogueGameplayTags::Ability_Attack_Melee;
+        AActor* TargetActor = Command.TargetActor.Get();
+        if (!TargetActor)
+        {
+            // コマンドにターゲットが含まれていない場合、セル座標からターゲットを検索する
+            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
+            {
+                TargetActor = Occupancy->GetActorAtCell(Command.TargetCell);
+            }
+        }
 
-        FGameplayAbilityTargetDataHandle TargetDataHandle;
-        // TargetActorがコマンドに含まれていれば、それを使う
-        if(Command.TargetActor)
+        if (!TargetActor)
         {
-             FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
-             TargetData->HitResult.HitObjectHandle = FActorInstanceHandle(Command.TargetActor);
-             TargetDataHandle.Add(TargetData);
+            UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Attack command failed: No valid target at cell (%d,%d)"), Command.TargetCell.X, Command.TargetCell.Y);
+            return false;
         }
 
-        const bool bActivated = ASC->TryActivateAbilityByTag(AttackAbilityTag, FGameplayAbilityTargetDataHandle(TargetDataHandle));
+        FGameplayEventData AttackEventData;
+        AttackEventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack; // アビリティが待っているトリガータグ
+        AttackEventData.Instigator = PlayerPawn;
+        AttackEventData.Target = TargetActor;
 
-        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Attempting to activate ability %s for attack. Success: %s"), *AttackAbilityTag.ToString(), bActivated ? TEXT("true") : TEXT("false"));
-        return bActivated;
+        // アビリティにターゲット情報を渡すためのデータを作成
+        FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
+        FHitResult HitResult;
+        HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
+        HitResult.Location = TargetActor->GetActorLocation();
+        HitResult.ImpactPoint = HitResult.Location;
+        TargetData->HitResult = HitResult;
+        AttackEventData.TargetData.Add(TargetData);
+
+        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Sending GameplayEvent '%s' to trigger attack on '%s'"), *AttackEventData.EventTag.ToString(), *GetNameSafe(TargetActor));
+
+        ASC->HandleGameplayEvent(AttackEventData.EventTag, &AttackEventData);
+
+        return true; // イベントは送信された。ここから先は成功と見なす
     }
     else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Move))
     {
         // 移動ロジックはGameTurnManagerBaseがまだ持っているので、ここでは何もしない
-        // 将来的にはこれもこちらに統合すべき
+        UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Move command received, passing through for now."));
         return true;
     }
 
```
以上が、攻撃を正しく機能させるための修正計画です。