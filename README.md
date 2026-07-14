# lottiepp

[Lottie](https://airbnb.io/lottie/) アニメーションファイル（`.json` 単体、または `.lottie` パッケージ）を読み込み・編集・保存するための C++23 ライブラリと CLI ツールです。

未知のキーを削らずに保持するため、ラウンドトリップ（読み込み→保存）しても元のデータを損ないません。

## 機能

- **読み込み / 保存** — `.json` と `.lottie`（ZIP パッケージ）の両方に対応。
- **色の置換（`recolor`）** — 塗り（`fl`）、線（`st`）、グラデーション（`gf`/`gs`）の色を一括置換。ソース色を指定すれば該当色のみ置換。
- **テキスト置換（`replaceText`）** — 指定したテキストレイヤー（`ty=5`）の文字列を書き換え。
- **速度変更（`setSpeed`）** — タイムラインをスケール（係数 `>1` で遅く・長くなる）。キーフレームとレイヤーの `ip`/`op`/`st` を一括調整。
- **バリエーション生成（`generateVariations`）** — 1 つの元データから色・テキスト・速度の異なる複数バリエーションを生成。
- **新規シェイプレイヤの追加（`--add-shape`）** — 長方形/楕円のシェイプレイヤを指定位置・サイズ・色・表示時間で新規挿入。
- **エフェクトの追加（`--add-effect`）** — 指定レイヤへガウシアンブラー等のエフェクトを追加。
- **レイヤの削除（`--remove-layer`）** — 指定名のレイヤをトップレベルおよびプリコンポジションから削除。

## 依存関係

- [glaze](https://github.com/stephenberry/glaze)（JSON シリアライゼーション）
- [Catch2](https://github.com/catchorg/Catch2)（テスト、任意）
- [miniz](https://github.com/richgel999/miniz)（`.lottie` ZIP 処理、同梱）

パッケージ管理には [vcpkg](https://github.com/microsoft/vcpkg) を想定しています。

## ビルド

```sh
./build.sh
```

静的リンク版は次のようにビルドできます。

```sh
./build.sh static
```

テストを実行する場合:

```sh
./test.sh
```

CMake オプション:

| オプション | 既定 | 説明 |
| --- | --- | --- |
| `BUILD_TEST` | `ON` | Catch2 が見つかればテストを構築 |
| `STATIC` | `OFF` | スタティックバイナリを生成 |

## CLI ツール `lottieproc`

```
lottieproc <input> -o <output> [--recolor <hex>] [--from <hex>] [--text <layer> <str>]
                            [--speed <factor>] [--variations <n>]
```

| オプション | 説明 |
| --- | --- |
| `input` / `-o, --output` | 入力 / 出力ファイル（`.json` または `.lottie`） |
| `--recolor <hex>` | 指定色（`#rrggbb` 等）で色を置換。`--from` と併用すると該当色のみ |
| `--from <hex>` | `--recolor` の対象色フィルタ |
| `--text <layer> <str>` | 指定レイヤー名のテキストを置換 |
| `--speed <factor>` | タイムラインをスケール（`>1` で遅くなる） |
| `--variations <n>` | N 個のバリエーションを `<stem>_1<ext>` … として出力 |
| `--add-shape <type> <x> <y> <w> <h> <color> <from> <to> [name]` | シェイプレイヤ（rect/ellipse）を追加。中心 `(x,y)`、サイズ `(w,h)`、色 `color`、`[from,to]` フレーム表示 |
  | `--add-effect <layer> <type> <value>` | 指定レイヤへエフェクトを追加（例: `blur <半径>`） |
  | `--remove-layer <name>` | 指定名のレイヤを削除（トップレベルおよびプリコンポジション） |

### 使用例

```sh
# すべての色を赤に置換して JSON 出力
lottieproc sample.json -o out.json --recolor "#ff0000"

# 青 (#0066ff) の部分だけ緑に置換
lottieproc sample.json -o out.json --recolor "#00aa00" --from "#0066ff"

# テキストレイヤー "Title" の文字を書き換え
lottieproc sample.json -o out.json --text "Title" "こんにちは"

# 2 倍の長さにスロー
lottieproc sample.json -o out.json --speed 2.0

# 8 個のカラーバリエーションを .lottie として出力
lottieproc sample.json -o out.lottie --variations 8

# 赤い長方形レイヤを (100,100) に追加し、同じレイヤへブラーをかける
lottieproc sample.json -o out.json --add-shape rect 100 100 80 40 "#ff0000" 0 60 "Box" --add-effect Box blur 8
```

## ライブラリ利用

`lottie_edit` というスタティックライブラリとしてリンクできます。

```cpp
#include "lottie_edit.hpp"

int main() {
  auto doc = lottie_edit::load("sample.json");

  // すべての色を赤に置換
  lottie_edit::recolor(doc, "", "#ff0000");

  // テキストレイヤー "Title" を書き換え
  lottie_edit::replaceText(doc, "Title", "こんにちは");

  // 2 倍速（遅く）する
  lottie_edit::setSpeed(doc, 2.0);

  // .json または .lottie として保存
  lottie_edit::save(doc, "out.json");
}
```

主要な API:

| 関数 | 説明 |
| --- | --- |
| `load` / `save` | ファイルからの読み込み / 書き込み（拡張子で形式を判定） |
| `parse` / `dump` / `dumpPretty` | 文字列との相互変換 |
| `recolor(doc, fromHex, toHex)` | 色を置換（戻り値は置換件数） |
| `replaceText(doc, layerName, newText)` | テキストを置換（戻り値は成功可否） |
| `setSpeed(doc, factor)` | タイムラインをスケール（戻り値は処理フィールド数） |
| `generateVariations(doc, paramSets)` | 複数バリエーションを生成 |
| `makeRect/makeEllipse(w, h[, round])` | シェイプアイテム（json）を生成 |
| `makeFill/makeStroke(hex, ...)` | 塗りつぶし/ストロークアイテム（json）を生成 |
| `makeTrimPath(start, end[, offset, simultaneous])` | トリムパス修飾（json）を生成 |
| `makeShapeLayer(params)` / `addLayer(doc, layer)` | シェイプレイヤを生成して Document へ追加 |
| `findLayer(doc, name)` / `addEffect(layer, effect)` | レイヤ検索 / レイヤへエフェクト追加 |
| `removeLayer(doc, name)` | 指定名のレイヤを削除（戻り値は削除成否） |
| `makeGaussianBlur(stddev[, repeatEdge])` | ガウシアンブラーエフェクト（json）を生成 |

## ライセンス

[MIT License](./LICENSE)
