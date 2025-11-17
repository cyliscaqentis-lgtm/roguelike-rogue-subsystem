# 修正計画書: プレイヤー入力がブロックされるバグの修正

## 1. 概要

プレイヤーが最初のターンに一度移動した後、次のターンから移動入力がすべてブロックされ、操作不能になる問題を修正する。

## 2. 原因

プレイヤーの入力を識別・管理するための`WindowId`が、ターンが進行してもインクリメントされず、常に`0`のままになっている。
クライアントは一度処理した`WindowId`での入力をブロックするラッチ機構を持っているため、`WindowId`が更新されない限り、2ターン目以降の入力がすべてブロックされてしまう。

## 3. 修正方針

`GameTurnManagerBase`を改修し、プレイヤーの入力受付が開始されるたびに`WindowId`がインクリメントされ、クライアントに通知されるようにする。

- **対象ファイル:**
    - `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.h`
    - `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`

- **修正内容:**

### Task 1: `GameTurnManagerBase.h` にメンバー変数を追加

`WindowId`の状態をサーバー側で保持するため、`protected`セクションに新しいメンバー変数を追加する。

```cpp
// In GameTurnManagerBase.h
protected:
    // ... other variables ...

    // Tracks the current input window ID to prevent duplicate inputs.
    int32 CurrentInputWindowId = 0;
```

### Task 2: `GameTurnManagerBase.cpp` で`WindowId`をインクリメント

プレイヤーの入力受付を開始する処理（`OnTurnStartedHandler`内など、`BeginPhase(EGamePhase::Phase_Player_WaitInput)`を呼び出す箇所）で、`CurrentInputWindowId`をインクリメントし、それを使用する。

```cpp
// In GameTurnManagerBase.cpp, inside the function that starts the player input phase

// ...
// Increment the window ID for the new turn's input window.
CurrentInputWindowId++;

// Open the input gate for the player, using the new WindowId.
BeginPhase(EGamePhase::Phase_Player_WaitInput, CurrentInputWindowId);
// ...
```

また、`BeginPhase`や`OnRep_WaitingForPlayerInput`など、`WindowId`を扱っているすべての箇所で、ハードコードされた`0`ではなく、この`CurrentInputWindowId`（またはレプリケートされた同等の変数）が使用されていることを確認・修正する。
