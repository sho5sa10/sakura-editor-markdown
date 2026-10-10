# Markdown プレビュー

エディタの右側に Markdown のプレビューを表示する機能です。Win32/GDI で描画します(WebView2 は使いません)。

## 構成

| ファイル | 役割 | 依存 |
|---|---|---|
| `MarkdownModel.h` | 中間データモデル(ブロックとインライン要素) | 標準ライブラリのみ |
| `IMarkdownParser.h` | パーサーのインターフェース | モデル |
| `Md4cParser.{h,cpp}` | md4c を使ったパーサー。md4c を取り込むのはこのファイルだけ | md4c |
| `MarkdownText.h` | UTF-16 の文字判定(東アジアの文字、禁則処理) | 標準ライブラリのみ |
| `MarkdownLayout.{h,cpp}` | 折り返しと配置。文字の幅は `ITextMeasurer` から取る | モデル |
| `CMarkdownPreviewWnd.{h,cpp}` | 子ウィンドウ。レイアウトの結果を GDI で描き、スクロールを処理する | Win32 |

エディタ本体との接点は `CEditWnd`(`ToggleMarkdownPreview` など)だけです。
パーサーは `IMarkdownParser` を介して使うので、別のパーサーに交換するときは、
`MarkdownModel.h` のモデルを作る実装を用意すれば、描画側は変更不要です。

## md4c

- https://github.com/mity/md4c (MIT License, Copyright (c) 2016-2026 Martin Mitáš)
- vcpkg のローカルレジストリ(`tools/vcpkg-local-registry/ports/md4c`)で、UTF-16 版(`MD4C_USE_UTF16`)をビルドします。
  この定義は `md4c.h` の `MD_CHAR` の型を変えるので、`md4c.h` を取り込む側も同じ定義が要ります(`Md4cParser.cpp` が定義します)。
- 生 HTML は解釈せず、文字列として表示します(`MD_FLAG_NOHTMLBLOCKS` と `MD_FLAG_NOHTMLSPANS`)。

## 由来

このディレクトリのコードは、この作業のために新しく書いたものです。
Sakura Editor NEXT など、ほかのプロジェクトのソースはコピーしていません。

## 既知の制限

- 取り消し線(`~~`)は、md4c の規則により、前後が空白や記号でないと認識されません。
  日本語の文章のように前後に空白がない場合(`漢字~~取り消し~~漢字`)は、そのまま `~~` が表示されます。
  空白や句読点で区切るか、パーサーの差し替え・修正が必要です。同じ理由で `_斜体_` も語中では認識されません(`*斜体*` は認識されます)。
- 画像は代替テキストを `[代替テキスト]` の形で表示します。画像の読み込みは未実装です。
- リンクは表示するだけで、クリックしても開きません。
- 文書の編集には追従しません。表示を開いたときと、保存したときに更新します。
- 解析する上限は 2Mi 文字(UTF-16 のコードユニット)、ブロック数は 10 万です(`ParseOptions`)。
