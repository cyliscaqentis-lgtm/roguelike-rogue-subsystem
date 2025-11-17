# デバッグ計装計画: INC-2025-1117F (v3)

## 1. 目的

`AGameTurnManagerBase::ExecuteMovePhase` から `UTurnCorePhaseManager::CoreExecutePhase` への呼び出しが、逐次実行モードにおいてスキップされるように見える問題の根本原因を特定する。そのために、コードの关键点に詳細なデバッグログを追加（計装）し、プログラムの実際の実行フローを可視化する。

## 2. 計装（ログ追加）方針

問題の核心である、アクションの解決から実行への移行プロセスに、意図的に目立つレベル（`Error`）でログを挿入する。

### 修正1: `AGameTurnManagerBase.cpp`

- **対象関数:** `AGameTurnManagerBase::ExecuteMovePhase`
- **修正内容:** `PhaseManager->CoreExecutePhase(ResolvedActions);` の呼び出し直前に、以下のログを追加する。

  ```cpp
  // GameTurnManagerBase.cpp -> ExecuteMovePhase の末尾

  UE_LOG(LogTurnManager, Error, TEXT("DEBUG: About to call CoreExecutePhase with %d actions."), ResolvedActions.Num());

  PhaseManager->CoreExecutePhase(ResolvedActions);
  ```
- **目的:** `ExecuteMovePhase` が、ディスパッチ処理の呼び出し元まで到達しているかを確実に確認する。

### 修正2: `TurnCorePhaseManager.cpp`

- **対象関数:** `UTurnCorePhaseManager::CoreExecutePhase`
- **修正内容1（関数冒頭）:** 関数の先頭に、以下のログを追加する。

  ```cpp
  // TurnCorePhaseManager.cpp -> CoreExecutePhase の冒頭

  void UTurnCorePhaseManager::CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions)
  {
      UE_LOG(LogTurnCore, Error, TEXT("DEBUG: CoreExecutePhase STARTING with %d actions."), ResolvedActions.Num());

      for (const FResolvedAction& Action : ResolvedActions)
      {
          // ...
      }
      // ...
  }
  ```
- **目的:** `CoreExecutePhase` が実際に呼び出されているか、また、期待される数のアクションを受け取っているかを確認する。

- **修正内容2（ループ内部）:** `for` ループの内部、`if (!Action.SourceActor)` のチェックの直後に、以下のログを追加する。

  ```cpp
  // TurnCorePhaseManager.cpp -> CoreExecutePhase の for ループ内

  for (const FResolvedAction& Action : ResolvedActions)
  {
      if (!Action.SourceActor)
      {
          // ...
      }

      UE_LOG(LogTurnCore, Error, TEXT("DEBUG: Processing action for actor %s, IsWait=%d"), *GetNameSafe(Action.SourceActor.Get()), Action.bIsWait);

      // (既存の処理が続く)
  }
  ```
- **目的:** `CoreExecutePhase` が、プレイヤーのアクションを含む、すべての解決済みアクションを正しくループ処理しようとしているかを確認する。

## 3. 期待される効果

- この計装により、逐次モードで `CoreExecutePhase` が呼び出されていない場合、その事実がログに明確に記録される。
- もし呼び出されているにも関わらずアクションが実行されない場合、ループ内のログから、どのアクションで問題が起きているかが特定できる。
- これにより、現在の「コードとログの矛盾」というブラックボックス状態を解消し、真の根本原因の特定に繋がる具体的なデータを得ることができる。
