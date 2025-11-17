# 修正計画書: INC-UNKNOWN - GA_MeleeAttack の権限とデータ不整合の修正

## 1. 概要

近接攻撃アビリティ (`GA_MeleeAttack`) がダメージ適用に失敗する問題を修正する。根本原因は、ダメージ処理がサーバー権限で実行されていないことにある。加えて、関連するログの誤りやデータ不整合も同時に解消する。

## 2. 原因仮説

1.  **根本原因 (最優先):** `ApplyDamageToTarget` 関数がサーバー権限をチェックせずに実行されている。モンタージュの完了コールバックがクライアントでもトリガーされ、クライアントがサーバーへの Effect 適用を試みて失敗し、`Handle valid=NO` が記録されている。
    *   Lyraの標準実装では、有効なASC、有効なGameplayEffectクラス、サーバー権限での実行が揃えば `Handle valid=NO` にはならない。
2.  **副次的問題 A:** `Instant` GameplayEffect の適用時、`ApplyGameplayEffectSpecToTarget` は仕様上無効なハンドルを返す。現在のログはこれを考慮しておらず、誤解を招く `EFFECT FAILED` メッセージを出力する。
3.  **副次的問題 B:** `GA_MeleeAttack.cpp` 内で `BaseDamage` 変数が使用されているが、ヘッダーでは `Damage` プロパティが定義されており、一貫性がない。
4.  **副次的問題 C (データ不整合):** `SetByCaller` で設定されたダメージ値 (28) と、実際のダメージログ (35) が異なる。これは、`GA_MeleeAttack` の `SetByCallerMagnitude` で使用しているタグが、`GE_Damage_Basic_Instant_C` の `ULyraDamageExecution` が期待する `SetByCaller.Damage` タグと一致していないか、または `GE_Damage_Basic_Instant_C` 内にハードコードされた Modifier が存在するため。

## 3. 修正方針

複数の問題を段階的に修正する。Lyraの標準パターンに準拠することを重視する。

1.  **権限問題の解決:** `ApplyDamageToTarget` の冒頭でサーバー権限を必須とするガード節を追加する。
2.  **コード品質の向上:** ログ記録を改善し、`Instant` エフェクトを正しく扱う。また、`BaseDamage` と `Damage` の不整合を解消する。
3.  **データ整合性の確保:**
    *   `GA_MeleeAttack` の `SetByCallerMagnitude` で **`RogueGameplayTags::SetByCaller_Damage`** を使用することを保証する。
    *   `GE_Damage_Basic_Instant_C` の設定（特に `Execution Calculation` が `ULyraDamageExecution` であること、および `ULyraDamageExecution` が `SetByCaller.Damage` タグを正しく読み取る設定になっていること）を確認・修正する。
    *   `GE_Damage_Basic_Instant_C` 内にハードコードされたダメージ値の Modifier があれば削除する。

## 4. 実装詳細 (コード差分)

- **対象ファイル:** `Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp`

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
 
     // Team check diagnostic
     if (AUnitBase* AttackerUnit = Cast<AUnitBase>(Avatar))
@@ -360,26 +366,36 @@
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
+                    // For Instant effects, the handle is not expected to be valid. The effect executes immediately.
+                    UE_LOG(LogTemp, Log, TEXT("[GA_MeleeAttack] Instant GameplayEffect spec was sent to target."));
+                }
             }
         }
         else

```

## 5. テスト方針

1.  Unreal Editor でコードをコンパイルする。
2.  PIE (Play In Editor) を開始する。
3.  敵ユニットがプレイヤーに近接攻撃を行う状況を再現する。
4.  **サーバーログ** を確認し、以下の点を確認する:
    *   `[GA_MeleeAttack] Instant GameplayEffect spec was sent to target.` が表示されること。
    *   `EFFECT FAILED` ログが表示されないこと。
    *   `[DMG]` ログのダメージ値が、`GA_MeleeAttack` の `Damage` プロパティ値と一致していること（または、他の計算（バフ等）を考慮して期待値通りであること）。
5.  ゲーム画面上で、プレイヤーのHPが実際に減少することを目視で確認する。
6.  （推奨）マルチプレイ環境でテストし、クライアント側でもHP減少が正しく反映されることを確認する。
