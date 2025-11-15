# ビルド手順 (Build Instructions)

## 推奨ビルドコマンド

このプロジェクトをビルドする際は、以下のコマンドを使用してください：

```cmd
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -NoHotReload -Verbose -NoUBA -Log="C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log"
```

### パラメータ説明

- **LyraEditor**: ターゲット名（エディタビルド）
- **Win64**: プラットフォーム
- **Development**: ビルド構成
- **-Project**: プロジェクトファイルのパス
- **-WaitMutex**: 他のビルドが完了するまで待機
- **-NoHotReload**: ホットリロード無効（安定性向上）
- **-Verbose**: 詳細ログ出力
- **-NoUBA**: Unreal Build Accelerator を無効化（エラー9666回避）
- **-Log**: ログファイル出力先

### ログファイル

ビルドログは以下に出力されます：

```
C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log
```

ビルドエラーが発生した場合は、このログファイルを確認してください。

---

## 標準ビルドコマンド（代替）

標準的な Build.bat を使用する場合：

```cmd
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -FromMsBuild
```

**注意:** このコマンドは UBA を使用するため、エラー9666が発生する場合があります。推奨コマンドの使用を検討してください。

---

## ビルド前の確認事項

1. **Visual Studio 2022** がインストールされていること
2. **Unreal Engine 5.6** がインストールされていること
3. **Git** で最新の変更が取得されていること

---

## トラブルシューティング

### エラー9666が発生する場合

Unreal Build Accelerator (UBA) との互換性問題の可能性があります。
推奨ビルドコマンド（`-NoUBA` オプション付き）を使用してください。

### リンクエラー (LNK2019, LNK1120)

1. プロジェクトをクリーンビルドしてください
2. `Intermediate` フォルダと `Binaries` フォルダを削除
3. 再度ビルドを実行

---

## 最終更新

- **日付**: 2025-11-15
- **更新者**: Claude Code
- **インシデント**: INC-2025-00004 修正対応
