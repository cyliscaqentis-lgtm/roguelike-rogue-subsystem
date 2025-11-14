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

### Recent Fixes

- `CodeRevision: INC-2025-00001-R1` - `AGameTurnManagerBase::ExecuteAttacks()` now notifies `Barrier->BeginTurn()` before invoking attacks so GA_AttackBase receives a valid turn ID.
- `CodeRevision: INC-2025-00001-R2` - `AGameTurnManagerBase::OnAttacksFinished()` drops stale turn notifications instead of processing them, preventing outdated attack states from corrupting the turn flow.
- `CodeRevision: INC-2025-00002-R1` - `AUnitBase::StartNextLeg()` now retries `UGridOccupancySubsystem::UpdateActorCell()` on failure (0.1s timer) instead of logging a critical error, and only broadcasts `OnMoveFinished` after the occupancy commit succeeds.
- `CodeRevision: INC-2025-00002-R2` - `UGA_MoveBase::OnMoveFinished()` mirrors the occupancy retry behavior so ability completion waits for a successful grid commit instead of rolling back.

## Comment Policy

- Document any new C++ comments in English only and log the change here (2025-11-15).
