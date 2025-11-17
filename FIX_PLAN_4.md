# 修正計画書: INC-2025-1117E (v2 - 状態の強制リセット)

## 1. 概要

- **インシデントID:** INC-2025-1117E
- **問題:** プレイヤーの移動コマンドが受理されたにもかかわらず、特定のターン（Turn 1, Turn 3）で移動が実行されずドロップされる。
- **原因:** `AGameTurnManagerBase` の状態フラグ `bPlayerMoveInProgress` が、前のターンの完了時に正しくリセットされず `true` のまま残存している。これにより、`DispatchResolvedMove` が新しい移動のトリガーを意図せずスキップしてしまう。

## 2. 原因仮説と根拠

- **仮説:** 複数のユニットが関わる複雑なターン（同時移動や逐次移動）の完了処理において、`bPlayerMoveInProgress` フラグを `false` にリセットするロジックが、何らかのエッジケースによって実行されない、または上書きされている。
- **根拠:**
    - `DispatchResolvedMove` のコードを確認した結果、`bPlayerMoveInProgress` が `true` の場合にプレイヤーの移動をスキップするロジックが明確に存在する。
    - Turn 1 と Turn 3 で移動がドロップするという事実は、この条件が満たされていることを示している。
    - 正常に動作する Turn 0 との唯一の違いは、他のユニットのアクションが同時に存在するかどうかであるため、複数のアクションが絡むことで状態のリセットに失敗している可能性が極めて高い。

## 3. 修正方針案

プレイヤーからの新しい移動コマンドを受け付けた `OnPlayerCommandAccepted_Implementation` 関数の冒頭で、**いかなる場合でも `bPlayerMoveInProgress` フラグを `false` に強制リセットする**。

これにより、前のターンの状態がどうであれ、新しいコマンドが常にクリーンな状態で処理されることを保証する。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`
- **修正対象関数:** `AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation`

- **修正後 (イメージ):**
  ```cpp
  // GameTurnManagerBase.cpp

  void AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation(const FPlayerCommand& Command)
  {
      // ★★★ START: 新しいフェイルセーフ処理 ★★★
      if (bPlayerMoveInProgress)
      {
          UE_LOG(LogTurnManager, Warning, TEXT("[OnPlayerCommandAccepted] Failsafe: bPlayerMoveInProgress was true, forcing to false."));
          bPlayerMoveInProgress = false;
      }
      // ★★★ END: 新しいフェイルセーフ処理 ★★★

      UE_LOG(LogTurnManager, Warning, TEXT("[ROUTE CHECK] OnPlayerCommandAccepted_Implementation called!"));

      if (!HasAuthority())
      {
          // (既存の処理が続く)
      }
      // ...
  }
  ```
  *注: 既存の `if (bPlayerMoveInProgress)` の入力ガードは、このフェイルセーフ処理の後に続くため、意図通り機能しなくなります。より安全な実装は、既存のガードを削除し、この新しいリセットロジックに置き換えることです。ただし、まずは最小限の変更としてリセット処理を追加します。より良い実装は、既存のガードブロックを以下のように変更することです。*

- **修正方針案 (改良版):**
  ```cpp
  // GameTurnManagerBase.cpp -> OnPlayerCommandAccepted_Implementation

  // ... 関数の冒頭 ...
  if (bPlayerMoveInProgress)
  {
      // 本来ここに来るべきではないが、来た場合は警告を出し、状態をリセットして処理を続行する
      UE_LOG(LogTurnManager, Warning, TEXT("[OnPlayerCommandAccepted] Failsafe: bPlayerMoveInProgress was true, forcing to false to prevent move drop."));
      bPlayerMoveInProgress = false;
  }

  // 既存の入力ガードは削除、または上記に統合する
  // if (bPlayerMoveInProgress) { return; } // ← このブロックは不要になる

  // (以降の処理はそのまま)
  ```

## 4. 影響範囲

- **プレイヤー移動の信頼性:** 過去のターンの状態異常によってプレイヤーの移動がドロップされる問題が根本的に解決され、入力した移動が確実に実行されるようになる。
- **副作用のリスク:**
    - 非常に低い。この関数が呼ばれるのは「プレイヤーが新しいコマンドを入力した」時点であり、その時点で `bPlayerMoveInProgress` が `true` であること自体が異常な状態であるため、これを `false` にリセットすることは論理的に正しい。
    - 万が一、本当に移動中に次のコマンドが（ハッキングなどによって）送られてきた場合でも、後続の `TurnBarrier` や `ActivationBlockedTags` によってアビリティの多重起動は防止されるため、システム全体への影響は軽微。

## 5. テスト方針

- **手動テスト:**
    - ログを提供したシナリオを再度実行し、`Turn 1` と `Turn 3` でプレイヤーの移動がドロップされずに正常に実行されることを確認する。
    - ログを確認し、`[OnPlayerCommandAccepted] Failsafe:` の警告が出力されるかどうかを確認する（もし出力されれば、リセット漏れの根本原因が他にあることの証拠になる）。
    - 複数ターンにわたって連続で移動しても、問題が再発しないことを確認する。
