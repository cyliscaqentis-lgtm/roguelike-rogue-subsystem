## Rogue CodeRevision Log

このフォルダのコード変更には、以下のような `CodeRevision` コメントを足してバージョンを明示します。Claude Code を含む他のエージェントにもこの方針を共有するため、該当ファイルを編集するたびにここを確認してください。

- `CodeRevision: INC-2025-00001-R1` ? `AGameTurnManagerBase::EndEnemyTurn()` に残留タグクリアの直前で追加（タッグ処理の安定化）。
- `CodeRevision: INC-2025-00001-R2` ? `APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation` の `WindowId` ACK処理強化。
- `CodeRevision: INC-2025-00002-R1` ? `DistanceFieldSubsystem::GetNextStepTowardsPlayer()` に詳細な近傍選定ロジック。
- `CodeRevision: INC-2025-00003-R1` ? `response_note.md` と本ログに「CodeRevision に日時を含める」運用ルールを追記（2025-11-14 22:24）。
- `CodeRevision: INC-2025-00004-R1` ? `OnAttacksFinished` で古いターン通知を破棄し GridOccupancy の破綻を防止（2025-11-14 22:50）。
- `CodeRevision: INC-2025-00005-R1` ? `TurnManager` の初期ログメッセージを ASCII 化し、`DungeonFloorGenerator::BSP_Split` の `stack.Pop()` を最新 API に合わせて更新（2025-11-14 23:00）。

## 記録フォーマット

- 新しい `CodeRevision` は `CodeRevision: <ID> ? <説明> (YYYY-MM-DD HH:MM)` の形式で記録し、コメントにもログにも同じ日時を記述してください。

**運用ルール**
1. 追加改修では新しい `CodeRevision` を作り、ファイルの関連箇所にコメントとして残してください。
2. このログにも最新の `CodeRevision` と変更対象のファイル・目的を随時追記し、チャットが切り替わっても履歴がわかるようにしてください。
3. コメントとログには日時まで含めるフォーマットを守り、差分のバージョンといつの変更かがセットで確認できるようにしてください。
