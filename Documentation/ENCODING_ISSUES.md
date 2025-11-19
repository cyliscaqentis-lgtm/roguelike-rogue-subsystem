# エンコーディング問題の根本原因と対策

## 問題の概要

`GameTurnManagerBase`などのC++ソースファイルが、UTF-8で正しく保存されているにもかかわらず、一部のAIツール（特にGeminiCLI）によってSHIFT-JISとして誤解釈され、コードが壊れる問題が発生しています。

## 根本原因

1. **BOM（Byte Order Mark）の欠如**
   - ファイルがUTF-8 without BOMで保存されている
   - BOMがないため、エンコーディングの自動検出が困難
   - 一部のツールがデフォルトでSHIFT-JISと仮定している

2. **日本語文字の存在**
   - コメント内に日本語文字（例：「補完」「削除」など）が含まれている
   - UTF-8とSHIFT-JISで日本語のバイト表現が異なる
   - 誤解釈により文字化けが発生

3. **ツールのエンコーディング検出ロジック**
   - GeminiCLIなどのツールがファイルを読み込む際、エンコーディングを自動検出
   - Windows環境ではSHIFT-JISがデフォルトエンコーディングとして扱われることがある
   - BOMがない場合、UTF-8とSHIFT-JISの区別が困難

## 確認された問題箇所

以下のコメントが文字化けしている：
- `// Player・・lueprintNativeEvent・・` (214行目)
- `// Enemy Pipeline・・lueprintNativeEvent・・` (225行目)
- その他複数箇所

元々は「BlueprintNativeEvent」の「B」が「・・」（全角中点）に置き換えられている。

## 対策

### 1. ファイルのエンコーディングを明示する

#### オプションA: UTF-8 BOM付きで保存（推奨しない）
- Unreal Engineの標準ではない
- 一部のツールで問題が発生する可能性

#### オプションB: .editorconfigでエンコーディングを指定（推奨）
```ini
[*.{cpp,h}]
charset = utf-8
```

#### オプションC: ファイル先頭にコメントで明示
```cpp
// -*- coding: utf-8 -*-
```

### 2. 日本語コメントを英語に統一（長期的対策）

現在の日本語コメントを英語に変更することで、エンコーディング問題の影響を最小限に抑える。

### 3. Git設定でエンコーディングを指定

`.gitattributes`ファイルを作成：
```
*.cpp text eol=crlf working-tree-encoding=UTF-8
*.h text eol=crlf working-tree-encoding=UTF-8
```

### 4. AIツールへの指示

`AGENTS.md`にエンコーディングに関する明確な指示を追加：
- すべてのC++ファイルはUTF-8（BOMなし）で保存されている
- SHIFT-JISとして解釈しないこと
- ファイルを編集する際は、UTF-8として読み書きすること

## 緊急対応

問題が発生した場合：
1. Gitからファイルを復元：`git checkout -- <file>`
2. ファイルのエンコーディングを確認：PowerShellで `[System.IO.File]::ReadAllText("<file>", [System.Text.Encoding]::UTF8)`
3. 必要に応じて、UTF-8として再保存

## 参考情報

- UTF-8 BOM: `EF BB BF`（ファイル先頭の3バイト）
- SHIFT-JISの日本語「あ」: `82 A0`
- UTF-8の日本語「あ」: `E3 81 82`

