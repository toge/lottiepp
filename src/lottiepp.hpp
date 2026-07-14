#pragma once

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lottiepp {

// Lottie の多様な（型が不定な）部分木（シェイプ、キーフレーム等）を保持する動的 JSON ノード
using json = glz::generic;
// 未知のキーを保持するための連想配列（未知キーを読み飛ばさずに保存するために使う）
using ExtraMap = std::map<std::string, json, std::less<>>;

/**
 * @brief RGBA 色を表す構造体
 * @details 各成分は 0.0 ～ 1.0 の範囲で表現する（Lottie の色指定と同じ表現）。
 */
struct Rgb {
  float r = 0.f;  // 赤成分（0.0～1.0）
  float g = 0.f;  // 緑成分（0.0～1.0）
  float b = 0.f;  // 青成分（0.0～1.0）
  float a = 1.f;  // アルファ成分（0.0=透明, 1.0=不透明）
};

/**
 * @brief バリエーション生成時のパラメータを保持する構造体
 * @details すべてのメンバが optional であり、指定されなかった項目は元の文書の値を維持する。
 */
struct VariationParams {
  std::optional<std::string> recolor_to;     // 再着色先の色（hex）
  std::optional<std::string> recolor_from;    // 再着色の対象となる元の色（hex）。空の場合はすべて置換
  std::optional<std::string> text_layer;      // テキストを置換する対象レイヤ名
  std::optional<std::string> text_value;      // 置換後のテキスト文字列
  std::optional<double>      speed;           // タイムライン倍率（>1 で遅く/長くなる）
};

// --- 型付き Lottie モデル（固定フィールド）＋ ExtraMap（未知キーを保持） ---

/**
 * @brief Lottie のトランスフォーム（ks）を表す構造体
 * @details o/r/p/a/s 等のプロパティ値はキーフレーム配列やアニメーション値のため、動的型（json）で保持する。
 */
struct Transform {
  std::optional<json> o;   // 不透明度（opacity）
  std::optional<json> r;   // 回転（rotation）
  std::optional<json> p;   // 位置（position）
  std::optional<json> a;   // アンカーポイント（anchor）
  std::optional<json> s;   // スケール（scale）
  std::optional<json> sk;  // スキュー（skew）
  std::optional<json> sa;  // スキュー軸（skew axis）
  ExtraMap            extra{};  // 未知のキーを保持するマップ

  struct glaze {
    using T = Transform;
    static constexpr auto value = glz::object(  //
        "o", &T::o,                             //
        "r", &T::r,                             //
        "p", &T::p,                             //
        "a", &T::a,                             //
        "s", &T::s,                             //
        "sk", &T::sk,                           //
        "sa", &T::sa                            //
    );
    static constexpr auto unknown_read  = &T::extra;  // 未知キーを extra へ読み込む
    static constexpr auto unknown_write = &T::extra;  // 未知キーを extra から書き出す
  };
};

/**
 * @brief Lottie のテキストドキュメント（t.d）を表す構造体
 * @details "k" はドキュメントのキーフレーム、もしくは静的なドキュメント本文を保持する。
 */
struct TextData {
  std::optional<json> k;  // ドキュメントのキーフレーム / 静的ドキュメント
  ExtraMap            extra{};  // 未知のキーを保持するマップ

  struct glaze {
    using T = TextData;
    static constexpr auto value = glz::object(  //
        "k", &T::k                             //
    );
    static constexpr auto unknown_read  = &T::extra;
    static constexpr auto unknown_write = &T::extra;
  };
};

/**
 * @brief Lottie のテキストレイヤ（t）を表す構造体
 */
struct Text {
  std::optional<TextData> d;
  ExtraMap                extra{};  // 未知のキーを保持するマップ

  struct glaze {
    using T = Text;
    static constexpr auto value = glz::object(  //
        "d", &T::d                             //
    );
    static constexpr auto unknown_read  = &T::extra;
    static constexpr auto unknown_write = &T::extra;
  };
};

/**
 * @brief Lottie のレイヤを表す構造体
 * @details ty はレイヤ種別（5=テキストレイヤ等）。未知キーは extra に保持される。
 */
struct Layer {
  int                      ty = 0;            // レイヤ種別（5=テキストレイヤ）
  std::optional<std::string> nm;               // レイヤ名
  std::optional<double>    ip;                 // イン点（in point）
  std::optional<double>    op;                 // アウト点（out point）
  std::optional<double>    st;                 // 開始時間（start time）
  std::optional<json>      shapes;             // シェイプレイヤの内容（異種混在のため動的型で保持）
  std::optional<Text>      t;                  // テキストレイヤの内容
  std::optional<Transform> ks;                 // トランスフォーム
  ExtraMap                 extra{};            // 未知のキーを保持するマップ

  struct glaze {
    using T = Layer;
    static constexpr auto value = glz::object(  //
        "ty", &T::ty,                           //
        "nm", &T::nm,                           //
        "ip", &T::ip,                           //
        "op", &T::op,                           //
        "st", &T::st,                           //
        "shapes", &T::shapes,                   //
        "t", &T::t,                             //
        "ks", &T::ks                            //
    );
    static constexpr auto unknown_read  = &T::extra;
    static constexpr auto unknown_write = &T::extra;
  };
};

/**
 * @brief Lottie のアセット（プリコンポジション等）を表す構造体
 */
struct Asset {
  std::optional<std::string> id;               // アセット識別子
  std::optional<std::vector<Layer>> layers;    // プリコンポジションのレイヤ群
  ExtraMap extra{};                            // 未知のキーを保持するマップ

  struct glaze {
    using T = Asset;
    static constexpr auto value = glz::object(  //
        "id", &T::id,                           //
        "layers", &T::layers                    //
    );
    static constexpr auto unknown_read  = &T::extra;
    static constexpr auto unknown_write = &T::extra;
  };
};

/**
 * @brief Lottie ドキュメント全体を表すルート構造体
 */
struct Document {
  std::optional<std::string> v;        // フォーマットバージョン
  std::optional<std::string> nm;       // ドキュメント名
  std::optional<double>      fr;       // フレームレート
  std::optional<double>      ip;       // 全体のイン点
  std::optional<double>      op;       // 全体のアウト点
  std::optional<int>         w;        // 幅
  std::optional<int>         h;        // 高さ
  std::vector<Layer>         layers{}; // トップレベルのレイヤ群
  std::optional<std::vector<Asset>> assets;  // アセット群
  ExtraMap                   extra{};  // 未知のキーを保持するマップ

  struct glaze {
    using T = Document;
    static constexpr auto value = glz::object(  //
        "v", &T::v,                             //
        "nm", &T::nm,                           //
        "fr", &T::fr,                           //
        "ip", &T::ip,                           //
        "op", &T::op,                           //
        "w", &T::w,                             //
        "h", &T::h,                             //
        "layers", &T::layers,                   //
        "assets", &T::assets                    //
    );
    static constexpr auto unknown_read  = &T::extra;
    static constexpr auto unknown_write = &T::extra;
  };
};

/**
 * @brief .lottie パッケージ内のアニメーションエントリを表す構造体
 */
struct AnimationEntry {
  std::string id = "data";  // アニメーション識別子（デフォルトは "data"）
};

/**
 * @brief .lottie パッケージのマニフェストを表す構造体（完全に型付け済み）
 */
struct Manifest {
  std::string                 version   = "1";         // マニフェストバージョン
  std::string                 generator = "lottieproc"; // 生成ツール名
  std::vector<AnimationEntry> animations{};             // アニメーションエントリ群
};

// --- 新規要素・エフェクト追加用ヘルパ ---

/**
 * @brief 静的プロパティ値（アニメーションなし）を表す json ノードを生成する
 * @param v 静的に設定する数値
 * @return {"a":0,"k":<v>} の形の json ノード
 */
json staticProp(double v);

/**
 * @brief 静的プロパティ値（配列）を表す json ノードを生成する
 * @param arr JSON 配列リテラル文字列（例: "[100,100]"）
 * @return {"a":0,"k":<arr>} の形の json ノード
 */
json staticProp(std::string_view arr);

/**
 * @brief 長方形シェイプ（ty="rc"）を生成する
 * @param w 幅（レイヤ原点を中心とする）
 * @param h 高さ（レイヤ原点を中心とする）
 * @param round 角丸め半径（デフォルト 0）
 * @return シェイプアイテムを表す json ノード
 */
json makeRect(double w, double h, double round = 0.0);

/**
 * @brief 楕円シェイプ（ty="el"）を生成する
 * @param w 幅（レイヤ原点を中心とする）
 * @param h 高さ（レイヤ原点を中心とする）
 * @return シェイプアイテムを表す json ノード
 */
json makeEllipse(double w, double h);

/**
 * @brief 単色塗りつぶし（ty="fl"）を生成する
 * @param hex 色（#rrggbb 等）
 * @param opacity 不透明度（0～100）
 * @return シェイプアイテムを表す json ノード
 */
json makeFill(std::string_view hex, double opacity = 100.0);

/**
 * @brief 単色ストローク（ty="st"）を生成する
 * @param hex 色（#rrggbb 等）
 * @param width 線幅
 * @param opacity 不透明度（0～100）
 * @return シェイプアイテムを表す json ノード
 */
json makeStroke(std::string_view hex, double width, double opacity = 100.0);

/**
 * @brief トリムパス修飾（ty="tm"）を生成する
 * @param startPct 開始位置（0～100、パーセント）
 * @param endPct 終了位置（0～100、パーセント）
 * @param offsetDeg オフセット（度）
 * @param simultaneous true=全パスを同時にトリム, false=各パスを個別にトリム
 * @return シェイプ修飾アイテムを表す json ノード
 */
json makeTrimPath(double startPct, double endPct, double offsetDeg = 0.0, bool simultaneous = true);

/**
 * @brief シェイプレイヤ生成時のパラメータ
 */
struct ShapeLayerParams {
  std::string            name    = "Shape";  // レイヤ名
  std::vector<json>      items;              // シェイプ + 塗り/線（makeRect 等で構築）
  double                 x      = 0.0;        // レイヤ位置 X（中心点）
  double                 y      = 0.0;        // レイヤ位置 Y（中心点）
  double                 from   = 0.0;       // イン点（フレーム）
  double                 to     = 0.0;       // アウト点（フレーム）
  double                 opacity = 100.0;    // レイヤ不透明度（0～100）
};

/**
 * @brief シェイプレイヤ（ty=4）を生成する
 * @param p レイヤパラメータ（items にシェイプと塗り/線を含める）
 * @return 生成された Layer（ind は未設定。addLayer で付与される）
 */
Layer makeShapeLayer(const ShapeLayerParams& p);

/**
 * @brief Document のトップレベルへレイヤを追加する
 * @details 既存レイヤの最大 ind に +1 した値を ind として付与する。
 * @param doc 対象の Document（破壊的に変更）
 * @param layer 追加するレイヤ
 */
void addLayer(Document& doc, Layer layer);

/**
 * @brief 新規 Lottie ドキュメント生成時のパラメータ
 * @details すべてのメンバが既定値を持ち、指定しなかった項目は既定の空ドキュメントとなる。
 */
struct DocumentParams {
  std::string version = "5.7.4";  // フォーマットバージョン（Lottie 互換）
  std::string name    = "";       // ドキュメント名（空の場合は設定しない）
  double      fr      = 60.0;     // フレームレート
  double      ip      = 0.0;      // 全体のイン点
  double      op      = 60.0;     // 全体のアウト点（既定 1 秒分）
  int         w       = 512;      // 幅
  int         h       = 512;      // 高さ
};

/**
 * @brief 空の Lottie ドキュメントを生成する
 * @details レイヤが空の妥当なドキュメントを作成する。makeShapeLayer + addLayer で要素を追加し、save で出力できる。
 * @param p ドキュメントパラメータ（既定値では 512x512 / 60fps / 60 フレーム）
 * @return 生成された空の Document
 */
Document makeDocument(const DocumentParams& p = {});

/**
 * @brief 名前でレイヤを検索する（トップレベルおよびアセット内）
 * @param doc 対象の Document
 * @param name 検索するレイヤ名
 * @return 見つかった場合は Layer へのポインタ、なければ nullptr
 */
Layer* findLayer(Document& doc, std::string_view name);

/**
 * @brief 名前でレイヤを削除する（トップレベルおよびアセット内）
 * @param doc 対象の Document（破壊的に変更）
 * @param name 削除するレイヤ名
 * @return 該当レイヤが 1 つ以上見つかり削除された場合は true、なければ false
 */
bool removeLayer(Document& doc, std::string_view name);

/**
 * @brief ガウシアンブラー（AE 互換）エフェクトノードを生成する
 * @param stddev ブラー半径
 * @param repeatEdge エッジピクセルを繰り返すか（デフォルト true）
 * @return レイヤエフェクト（ef）に追加可能な json ノード
 */
json makeGaussianBlur(double stddev, bool repeatEdge = true);

/**
 * @brief レイヤへエフェクトを追加する
 * @details レイヤの未知キー "ef" にエフェクトを追加する（既存があれば追記）。
 * @param layer 対象のレイヤ（破壊的に変更）
 * @param effect 追加するエフェクトノード
 */
void addEffect(Layer& layer, const json& effect);

/**
 * @brief 16進色文字列を解析する
 * @param hex "#rgb" / "#rrggbb" / "#rrggbbaa" 形式の文字列（大文字小文字は区別しない）。先頭の '#' は省略可。
 * @return 解析に成功した場合は Rgb を保持する optional、失敗した場合は nullopt を返す。
 */
std::optional<Rgb> parseHexColor(std::string_view hex);

/**
 * @brief Lottie ドキュメントを読み込む（.json または .lottie）
 * @param path 入力ファイルのパス（拡張子で形式を判定）
 * @return 読み込まれた Document
 */
Document load(const std::string& path);

/**
 * @brief Lottie ドキュメントを保存する（.json または .lottie）
 * @param doc 保存対象の Document
 * @param path 出力ファイルのパス（拡張子で形式を判定）
 */
void save(const Document& doc, const std::string& path);

/**
 * @brief Document を JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return シリアライズされた JSON 文字列
 */
std::string dump(const Document& doc);

/**
 * @brief Document を整形済み JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return インデント整形された JSON 文字列
 */
std::string dumpPretty(const Document& doc);

/**
 * @brief JSON テキストから Document を解析する
 * @param text JSON 形式のテキスト
 * @return 解析された Document
 */
Document parse(std::string_view text);

/**
 * @brief 入れ子の動的 JSON ノードをシリアライズする（テスト等で利用）
 * @param node 対象の json ノード
 * @return シリアライズされた JSON 文字列
 */
std::string dumpJson(const json& node);

/**
 * @brief 入れ子の動的 JSON ノードを整形済み JSON 文字列にシリアライズする
 * @param node 対象の json ノード
 * @return インデント整形された JSON 文字列
 */
std::string dumpJsonPretty(const json& node);

/**
 * @brief JSON テキストから動的 JSON ノードを解析する
 * @param text JSON 形式のテキスト
 * @return 解析された json ノード
 */
json parseJson(std::string_view text);

/**
 * @brief 塗りつぶし/ストローク/グラデーションの色を置換する
 * @param doc 対象の Document（破壊的に変更される）
 * @param fromHex 置換対象の元の色（hex）。空の場合はすべての単色を置換する。
 * @param toHex 置換後の色（hex）
 * @return 置換された色の個数
 */
std::size_t recolor(Document& doc, std::string_view fromHex, std::string_view toHex);

/**
 * @brief 指定した名前のテキストレイヤ（ty==5）のテキストを置換する
 * @param doc 対象の Document（破壊的に変更される）
 * @param layerName 対象レイヤ名
 * @param newText 置換後のテキスト
 * @return 該当レイヤが見つかり置換された場合は true、見つからなかった場合は false
 */
bool replaceText(Document& doc, std::string_view layerName, std::string_view newText);

/**
 * @brief タイムラインをスケールする（>1 で遅く/長くなる）
 * @param doc 対象の Document（破壊的に変更される）
 * @param factor 時間軸の倍率
 * @return スケールされたフィールドの個数
 */
std::size_t setSpeed(Document& doc, double factor);

/**
 * @brief doc のディープコピーに対して各パラメータセットを適用し、バリエーション群を生成する
 * @param doc 元となる Document
 * @param paramSets バリエーションごとのパラメータ群
 * @return 生成された Document の配列
 */
std::vector<Document> generateVariations(const Document& doc, const std::vector<VariationParams>& paramSets);

}  // namespace lottiepp
