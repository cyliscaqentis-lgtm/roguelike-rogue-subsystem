## Rogue CodeRevision Log

このフォルダのコード変更には、以下のような `CodeRevision` コメントを足してバージョンを明示します。Claude Code を含む他のエージェントにもこの方針を共有するため、該当ファイルを編集するたびにここを確認してください。

- `CodeRevision: INC-2025-00001-R1` — `AGameTurnManagerBase::EndEnemyTurn()` に残留タグクリアの直前で追加（タッグ処理の安定化）。
- `CodeRevision: INC-2025-00001-R2` — `APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation` の `WindowId` ACK処理強化。
- `CodeRevision: INC-2025-00002-R1` — `DistanceFieldSubsystem::GetNextStepTowardsPlayer()` に詳細な近傍選定ロジック。

**運用ルール**
1. 追加改修では新しい `CodeRevision` を作り、ファイルの関連箇所にコメントとして残してください。
2. このログにも最新の `CodeRevision` と変更対象のファイル・目的を随時追記して、チャットが切り替わっても履歴がわかるようにしてください。
