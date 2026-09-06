#pragma once

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>

#include <cstdlib>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * @file lottiepp.hpp
 * @brief lottiepp 公開ヘッダ。
 *
 * 本ライブラリは既定で例外を送出しない。I/O・parse 系の失敗は
 * `std::expected<T, std::string>` で返し、プログラマーエラー（ビルダー関数へ
 * 非有限値の指定等）は `std::abort()` とする。
 *
 * 本ライブラリの WASI 対応は wasi-sdk sysroot を用いた wasm32-wasip1 でのビルドを
 * 想定（wasm3 等で実行可能）。
 */
#ifdef LOTTIEPP_WASI_MINIMAL
#error "LOTTIEPP_WASI_MINIMAL was removed: lottiepp is now exception-free by default."
#endif

namespace lottiepp {

// Lottie の多様な（型が不定な）部分木（シェイプ、キーフレーム等）を保持する動的 JSON ノード
using json = glz::generic;
// 未知のキーを保持するための連想配列（未知キーを読み飛ばさずに保存するために使う）
using ExtraMap = std::unordered_map<std::string, json>;

[[noreturn]] inline void fail(std::string_view msg) noexcept {
  (void)msg;
  std::abort();
}

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



// --- 新規要素・エフェクト追加用ヘルパ ---

/**
 * @brief 静的プロパティ値（アニメーションなし）を表す json ノードを生成する
 * @param v 静的に設定する数値
 * @return {"a":0,"k":<v>} の形の json ノード
 */
json staticProp(double v) noexcept;

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
 * @return 読み込まれた Document。失敗時は std::unexpected。
 */
std::expected<Document, std::string> load(const std::string& path);

/**
 * @brief Lottie ドキュメントを保存する（.json または .lottie）
 * @param doc 保存対象の Document
 * @param path 出力ファイルのパス（拡張子で形式を判定）
 * @return 成功時は true、失敗時は false。エラー内容は err に格納される。
 */
bool save(const Document& doc, const std::string& path, std::string& err);

/**
 * @brief Document を JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return シリアライズされた JSON 文字列。失敗時は std::unexpected。
 */
std::expected<std::string, std::string> dump(const Document& doc);

/**
 * @brief Document を整形済み JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return インデント整形された JSON 文字列。失敗時は std::unexpected。
 */
std::expected<std::string, std::string> dumpPretty(const Document& doc);

/**
 * @brief JSON テキストから Document を解析する
 * @param text JSON 形式のテキスト
 * @return 解析された Document。失敗時は std::unexpected。
 */
std::expected<Document, std::string> parse(std::string_view text);

/**
 * @brief 入れ子の動的 JSON ノードをシリアライズする（テスト等で利用）
 * @param node 対象の json ノード
 * @return シリアライズされた JSON 文字列。失敗時は std::unexpected。
 */
std::expected<std::string, std::string> dumpJson(const json& node);

/**
 * @brief 入れ子の動的 JSON ノードを整形済み JSON 文字列にシリアライズする
 * @param node 対象の json ノード
 * @return インデント整形された JSON 文字列。失敗時は std::unexpected。
 */
std::expected<std::string, std::string> dumpJsonPretty(const json& node);

/**
 * @brief JSON テキストから動的 JSON ノードを解析する
 * @param text JSON 形式のテキスト
 * @return 解析された json ノード。失敗時は std::unexpected。
 */
std::expected<json, std::string> parseJson(std::string_view text);

/**
 * @brief 塗りつぶし/ストローク/グラデーションの色を置換する
 * @param doc 対象の Document（破壊的に変更される）
 * @param fromHex 置換対象の元の色（hex）。空の場合はすべての単色を置換する。
 * @param toHex 置換後の色（hex）
 * @return 置換された色の個数。無効な hex の場合は std::unexpected。
 */
std::expected<std::size_t, std::string> recolor(Document& doc, std::string_view fromHex, std::string_view toHex);

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

// ============================================================================
// Inline implementations
// ============================================================================

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <expected>
#include <utility>
#include <memory>

// miniz header
#include <miniz/miniz.h>

// glaze prettify
#include <glaze/json/prettify.hpp>

namespace lottiepp {

namespace detail {
using ::std::to_string;
}

namespace {

template<typename Fn>
void forEachLayer(Document& doc, Fn&& fn) {
  for (auto& l : doc.layers) fn(l);
  if (doc.assets) {
    for (auto& a : *doc.assets) {
      if (a.layers) {
        for (auto& l : *a.layers) fn(l);
      }
    }
  }
}

// 読み込み時のオプション：未知キーをエラーにせず、null メンバは読み飛ばす
constexpr glz::opts kReadOpts{.error_on_unknown_keys = false, .skip_null_members = true};
// 書き出し時のオプション：プリティ化は行わず、上記と同様の未知キー扱いとする
constexpr glz::opts kWriteOpts{.error_on_unknown_keys = false, .skip_null_members = true, .prettify = false};

inline std::expected<std::string, std::string> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected("failed to open file: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

inline bool writeFile(const std::string& path, std::string_view data, std::string& err) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    err = "failed to write file: " + path;
    return false;
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    err = "failed to write file: " + path;
    return false;
  }
  return true;
}

inline std::string toLower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

inline std::string extensionOf(const std::string& path) {
  const auto pos = path.find_last_of('.');
  if (pos == std::string::npos) {
    return {};
  }
  return toLower(path.substr(pos));
}

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

inline bool colorsNearlyEqual(const Rgb& a, const Rgb& b, float eps = 1.0f / 255.0f) {
  return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps && std::fabs(a.b - b.b) <= eps;
}

inline bool isColorArray(const json& arr) {
  if (!arr.is_array() || arr.size() < 3 || arr.size() > 4) {
    return false;
  }
  for (const auto& v : arr.get_array()) {
    if (!v.is_number()) {
      return false;
    }
  }
  return true;
}

inline Rgb colorFromArray(const json& arr) {
  Rgb c;
  c.r = arr[0].as<float>();
  c.g = arr[1].as<float>();
  c.b = arr[2].as<float>();
  if (arr.size() >= 4) {
    c.a = arr[3].as<float>();
  }
  return c;
}

inline json colorToArray(const Rgb& c, std::size_t n) {
  json arr = json::array_t{c.r, c.g, c.b};
  if (n >= 4) {
    arr.get_array().push_back(c.a);
  }
  return arr;
}

inline bool looksLikeColorProp(const json& node) {
  if (!node.is_object() || !node.contains("k")) {
    return false;
  }
  if (node.contains("a") && !node["a"].is_number()) {
    return false;
  }
  return true;
}

inline std::size_t recolorColorValue(json& node, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  auto maybeReplaceArray = [&](json& arr) {
    if (!isColorArray(arr)) {
      return;
    }
    const Rgb cur = colorFromArray(arr);
    if (from && !colorsNearlyEqual(cur, *from)) {
      return;
    }
    arr = colorToArray(to, arr.size());
    ++count;
  };
  if (!looksLikeColorProp(node)) {
    return 0;
  }
  json& k = node["k"];
  if (k.is_array() && !k.get_array().empty() && k[0].is_number()) {
    maybeReplaceArray(k);
    return count;
  }
  if (k.is_array()) {
    for (auto& kf : k.get_array()) {
      if (!kf.is_object()) {
        continue;
      }
      if (kf.contains("s") && kf["s"].is_array()) {
        maybeReplaceArray(kf["s"]);
      }
      if (kf.contains("e") && kf["e"].is_array()) {
        maybeReplaceArray(kf["e"]);
      }
    }
  }
  return count;
}

inline std::size_t recolorRecursive(json& node, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  if (node.is_array()) {
    for (auto& child : node.get_array()) {
      count += recolorRecursive(child, from, to);
    }
    return count;
  }
  if (!node.is_object()) {
    return 0;
  }
  if (node.contains("ty") && node["ty"].is_string()) {
    const auto ty = node["ty"].as<std::string>();
    if ((ty == "fl" || ty == "st") && node.contains("c")) {
      count += recolorColorValue(node["c"], from, to);
    }
    if ((ty == "gf" || ty == "gs") && node.contains("g") && node["g"].is_object()) {
      json& g = node["g"];
      const std::size_t pStops = [&]() -> std::size_t {
        if (g.contains("p") && g["p"].is_number()) {
          return static_cast<std::size_t>(g["p"].as<int>());
        }
        return 0;
      }();
      auto recolorStops = [&](json& stops) {
        if (!stops.is_array()) {
          return;
        }
        auto& a = stops.get_array();
        const std::size_t max = pStops > 0 ? std::min(pStops * 4, a.size()) : a.size();
        for (std::size_t i = 0; i + 3 < max; i += 4) {
          if (!a[i].is_number() || !a[i + 1].is_number() || !a[i + 2].is_number() || !a[i + 3].is_number()) {
            return;
          }
          Rgb cur{a[i + 1].as<float>(), a[i + 2].as<float>(), a[i + 3].as<float>(), 1.f};
          if (!from || colorsNearlyEqual(cur, *from)) {
            a[i + 1] = static_cast<double>(to.r);
            a[i + 2] = static_cast<double>(to.g);
            a[i + 3] = static_cast<double>(to.b);
            ++count;
          }
        }
      };
      if (g.contains("k")) {
        if (g["k"].is_array()) {
          recolorStops(g["k"]);
        } else if (g["k"].is_object() && g["k"].contains("k")) {
          json& kk = g["k"]["k"];
          if (kk.is_array() && !kk.get_array().empty() && kk[0].is_number()) {
            recolorStops(kk);
          } else if (kk.is_array()) {
            for (auto& kf : kk.get_array()) {
              if (!kf.is_object()) {
                continue;
              }
              if (kf.contains("s")) {
                recolorStops(kf["s"]);
              }
              if (kf.contains("e")) {
                recolorStops(kf["e"]);
              }
            }
          }
        }
      }
    }
  }
  for (auto& [key, child] : node.get_object()) {
    (void)key;
    if (child.is_object() || child.is_array()) {
      count += recolorRecursive(child, from, to);
    }
  }
  return count;
}

inline std::size_t recolorExtras(ExtraMap& extra, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  for (auto& [k, v] : extra) {
    (void)k;
    count += recolorRecursive(v, from, to);
  }
  return count;
}

inline std::size_t recolorLayer(Layer& layer, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  if (layer.shapes) {
    count += recolorRecursive(*layer.shapes, from, to);
  }
  count += recolorExtras(layer.extra, from, to);
  return count;
}

inline void scaleTimingValue(json& v, double factor, std::size_t& count) {
  if (v.is_number()) {
    v = v.as<double>() * factor;
    ++count;
  }
}

inline void scaleOptional(std::optional<double>& v, double factor, std::size_t& count) {
  if (v) {
    *v *= factor;
    ++count;
  }
}

inline bool looksLikeKeyframe(const json& node) {
  return node.is_object() && node.contains("t") && node["t"].is_number() &&
         (node.contains("s") || node.contains("e") || node.contains("i") || node.contains("o"));
}

inline std::size_t scaleKeyframes(json& node, double factor) {
  std::size_t count = 0;
  if (node.is_array()) {
    for (auto& child : node.get_array()) {
      count += scaleKeyframes(child, factor);
    }
    return count;
  }
  if (!node.is_object()) {
    return 0;
  }
  if (looksLikeKeyframe(node)) {
    scaleTimingValue(node["t"], factor, count);
  }
  for (auto& [key, child] : node.get_object()) {
    if (key == "ip" || key == "op" || key == "st") {
      scaleTimingValue(child, factor, count);
    } else if (child.is_object() || child.is_array()) {
      count += scaleKeyframes(child, factor);
    }
  }
  return count;
}

inline std::size_t scaleExtras(ExtraMap& extra, double factor) {
  std::size_t count = 0;
  for (auto& [k, v] : extra) {
    (void)k;
    count += scaleKeyframes(v, factor);
  }
  return count;
}

inline std::size_t scaleLayer(Layer& layer, double factor) {
  std::size_t count = 0;
  scaleOptional(layer.ip, factor, count);
  scaleOptional(layer.op, factor, count);
  scaleOptional(layer.st, factor, count);
  if (layer.shapes) {
    count += scaleKeyframes(*layer.shapes, factor);
  }
  if (layer.t && layer.t->d && layer.t->d->k) {
    count += scaleKeyframes(*layer.t->d->k, factor);
  }
  if (layer.ks) {
    Transform& ks = *layer.ks;
    for (auto* prop : {&ks.o, &ks.r, &ks.p, &ks.a, &ks.s, &ks.sk, &ks.sa}) {
      if (*prop) {
        count += scaleKeyframes(**prop, factor);
      }
    }
  }
  ExtraMap saved;
  for (const char* k : {"ip", "op", "st"}) {
    auto it = layer.extra.find(k);
    if (it != layer.extra.end() && ( (k==std::string("ip") && layer.ip) || (k==std::string("op") && layer.op) || (k==std::string("st") && layer.st) )) {
      saved[k] = it->second;
      layer.extra.erase(it);
    }
  }
  count += scaleExtras(layer.extra, factor);
  for (auto& [k, v] : saved) {
    layer.extra[k] = v;
  }
  return count;
}

inline bool replaceTextInLayer(Layer& layer, std::string_view layerName, std::string_view newText) {
  if (layer.ty != 5) {
    return false;
  }
  if (!layer.nm || *layer.nm != layerName) {
    return false;
  }
  if (!layer.t || !layer.t->d || !layer.t->d->k) {
    return false;
  }
  json& k = *layer.t->d->k;
  if (!k.is_array()) {
    if (k.is_object() && k.contains("t") && k["t"].is_string()) {
      k["t"] = std::string(newText);
      return true;
    }
    return false;
  }
  bool found = false;
  for (auto& kf : k.get_array()) {
    if (!kf.is_object()) {
      continue;
    }
    if (kf.contains("s") && kf["s"].is_object() && kf["s"].contains("t")) {
      kf["s"]["t"] = std::string(newText);
      found        = true;
    }
  }
  return found;
}

inline std::expected<std::string, std::string> extractAnimationFromZip(const std::string& path) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
    return std::unexpected("failed to open .lottie zip: " + path);
  }
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  std::string bestName;
  int         bestScore = -1;
  for (mz_uint i = 0; i < n; ++i) {
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) {
      continue;
    }
    std::string name  = st.m_filename;
    std::string lower = toLower(name);
    int         score = -1;
    if (lower == "animations/data.json" || lower == "animation.json") {
      score = 100;
    } else if (lower.starts_with("animations/") && lower.ends_with(".json")) {
      score = 80;
    } else if (lower.ends_with(".json") && lower.find("manifest") == std::string::npos) {
      score = 40;
    }
    if (score > bestScore) {
      bestScore = score;
      bestName  = name;
    }
  }
  if (bestScore < 0) {
    mz_zip_reader_end(&zip);
    return std::unexpected("no animation json found in: " + path);
  }
  size_t size = 0;
  void* data = mz_zip_reader_extract_file_to_heap(&zip, bestName.c_str(), &size, 0);
  struct _ZipReaderEnd {
    mz_zip_archive* _z;
    _ZipReaderEnd(mz_zip_archive* z) : _z(z) {}
    ~_ZipReaderEnd() { if (_z) mz_zip_reader_end(_z); }
  } _ender(&zip);
  if (!data) {
    return std::unexpected("failed to extract animation from: " + path);
  }
  std::unique_ptr<void, decltype(&mz_free)> _data_guard(data, &mz_free);
  std::string out(static_cast<const char*>(data), size);
  return out;
}

inline bool writeLottieZip(const Document& doc, const std::string& path, std::string& err) {
  auto animation = dumpPretty(doc);
  if (!animation) {
    err = std::move(animation.error());
    return false;
  }
  std::string manifestStr =
      "{\"version\":\"1\",\"generator\":\"lottieproc\",\"animations\":[{\"id\":\"data\"}]}";
  manifestStr = glz::prettify_json(manifestStr);
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
    err = "failed to create .lottie: " + path;
    return false;
  }
  struct _ZipWriterEnd {
    mz_zip_archive* _z;
    bool _finalized = false;
    _ZipWriterEnd(mz_zip_archive* z) : _z(z) {}
    ~_ZipWriterEnd() {
      if (_z) {
        mz_zip_writer_end(_z);
      }
    }
  } _wender(&zip);
  if (!mz_zip_writer_add_mem(&zip, "manifest.json", manifestStr.data(), manifestStr.size(), MZ_DEFAULT_COMPRESSION)) {
    err = "failed to add manifest.json to: " + path;
    return false;
  }
  if (!mz_zip_writer_add_mem(&zip, "animations/data.json", animation->data(), animation->size(), MZ_DEFAULT_COMPRESSION)) {
    err = "failed to add animations/data.json to: " + path;
    return false;
  }
  if (!mz_zip_writer_finalize_archive(&zip)) {
    err = "failed to finalize .lottie: " + path;
    return false;
  }
  mz_zip_writer_end(&zip);
  return true;
}

}  // namespace

// --- 公開 API 実装 ---

inline json staticProp(double v) noexcept {
  if (!std::isfinite(v)) {
    ::lottiepp::fail("staticProp: value must be finite (not NaN/Inf)");
  }
  auto result = parseJson("{\"a\":0,\"k\":" + detail::to_string(v) + "}");
  if (!result) {
    ::lottiepp::fail(result.error());
  }
  return std::move(*result);
}

inline json staticProp(std::string_view arr) {
  auto result = parseJson("{\"a\":0,\"k\":" + std::string(arr) + "}");
  if (!result) {
    ::lottiepp::fail(result.error());
  }
  return std::move(*result);
}

inline json makeShapeTransform() {
  static const json kTransform = [] {
    auto r = parseJson(
        "{\"ty\":\"tr\","
        "\"p\":{\"a\":0,\"k\":[0,0]},"
        "\"a\":{\"a\":0,\"k\":[0,0]},"
        "\"s\":{\"a\":0,\"k\":[100,100]},"
        "\"r\":{\"a\":0,\"k\":0},"
        "\"o\":{\"a\":0,\"k\":100}}");
    return std::move(*r);
  }();
  return kTransform;
}

inline json makeTrimPath(double startPct, double endPct, double offsetDeg, bool simultaneous) {
  if (!std::isfinite(startPct) || !std::isfinite(endPct) || !std::isfinite(offsetDeg)) {
    ::lottiepp::fail("makeTrimPath: parameters must be finite");
  }
  const int m = simultaneous ? 1 : 2;
  const std::string node = "{\"ty\":\"tm\","
      "\"s\":{\"a\":0,\"k\":" + detail::to_string(startPct) + "},"
      "\"e\":{\"a\":0,\"k\":" + detail::to_string(endPct) + "},"
      "\"o\":{\"a\":0,\"k\":" + detail::to_string(offsetDeg) + "},"
      "\"m\":" + detail::to_string(m) + "}";
  auto result = parseJson(node);
  if (!result) {
    ::lottiepp::fail(result.error());
  }
  return std::move(*result);
}

inline json makeRect(double w, double h, double round) {
  if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(round)) {
    ::lottiepp::fail("makeRect: w/h/round must be finite");
  }
  static const json kBase = [] {
    auto r = parseJson(
        "{\"ty\":\"rc\",\"d\":1,"
        "\"s\":{\"a\":0,\"k\":[0,0]},\"p\":{\"a\":0,\"k\":[0,0]},\"r\":{\"a\":0,\"k\":0}}");
    return std::move(*r);
  }();
  json n = kBase;
  n["s"] = staticProp("[" + detail::to_string(w) + "," + detail::to_string(h) + "]");
  n["r"] = staticProp(round);
  return n;
}

inline json makeEllipse(double w, double h) {
  if (!std::isfinite(w) || !std::isfinite(h)) {
    ::lottiepp::fail("makeEllipse: w/h must be finite");
  }
  static const json kBase = [] {
    auto r = parseJson(
        "{\"ty\":\"el\",\"d\":1,"
        "\"s\":{\"a\":0,\"k\":[0,0]},\"p\":{\"a\":0,\"k\":[0,0]}}");
    return std::move(*r);
  }();
  json n = kBase;
  n["s"] = staticProp("[" + detail::to_string(w) + "," + detail::to_string(h) + "]");
  return n;
}

inline json makeFill(std::string_view hex, double opacity) {
  if (!std::isfinite(opacity)) {
    ::lottiepp::fail("makeFill: opacity must be finite");
  }
  const auto c = parseHexColor(hex);
  if (!c) {
    std::string err = "invalid color: " + std::string(hex);
    ::lottiepp::fail(err);
  }
  static const json kBase = [] {
    auto r = parseJson(
        "{\"ty\":\"fl\",\"r\":1,\"bm\":0,"
        "\"o\":{\"a\":0,\"k\":0},\"c\":{\"a\":0,\"k\":[0,0,0,1]}}");
    return std::move(*r);
  }();
  json n = kBase;
  n["o"] = staticProp(opacity);
  n["c"] = staticProp("[" + detail::to_string(c->r) + "," + detail::to_string(c->g) + "," +
                      detail::to_string(c->b) + "," + detail::to_string(c->a) + "]");
  return n;
}

inline json makeStroke(std::string_view hex, double width, double opacity) {
  if (!std::isfinite(width) || !std::isfinite(opacity)) {
    ::lottiepp::fail("makeStroke: width/opacity must be finite");
  }
  const auto c = parseHexColor(hex);
  if (!c) {
    std::string err = "invalid color: " + std::string(hex);
    ::lottiepp::fail(err);
  }
  static const json kBase = [] {
    auto r = parseJson(
        "{\"ty\":\"st\",\"r\":1,\"bm\":0,\"lc\":2,\"lj\":2,\"ml\":4,"
        "\"o\":{\"a\":0,\"k\":0},\"w\":{\"a\":0,\"k\":0},\"c\":{\"a\":0,\"k\":[0,0,0,1]}}");
    return std::move(*r);
  }();
  json n = kBase;
  n["o"] = staticProp(opacity);
  n["w"] = staticProp(width);
  n["c"] = staticProp("[" + detail::to_string(c->r) + "," + detail::to_string(c->g) + "," +
                      detail::to_string(c->b) + "," + detail::to_string(c->a) + "]");
  return n;
}

inline Layer makeShapeLayer(const ShapeLayerParams& p) {
  Layer l;
  l.ty = 4;
  l.nm = p.name;
  l.ip = p.from;
  l.op = p.to;
  l.st = p.from;
  Transform ks;
  ks.o = staticProp(p.opacity);
  ks.r = staticProp(0.0);
  ks.p = staticProp("[" + detail::to_string(p.x) + "," + detail::to_string(p.y) + ",0]");
  ks.a = staticProp("[0,0,0]");
  ks.s = staticProp("[100,100,100]");
  l.ks = ks;
  static const json kGroupBase = [] {
    auto r = parseJson("{\"ty\":\"gr\",\"nm\":\"Group\",\"it\":[]}");
    return std::move(*r);
  }();
  json group = kGroupBase;
  auto& it   = group["it"];
  for (auto& item : p.items) {
    it.get_array().push_back(item);
  }
  it.get_array().push_back(makeShapeTransform());
  static const json kEmptyArray = [] {
    auto r = parseJson("[]");
    return std::move(*r);
  }();
  json shapes = kEmptyArray;
  shapes.get_array().push_back(group);
  l.shapes = shapes;
  return l;
}

inline void addLayer(Document& doc, Layer layer) {
  int maxInd = 0;
  for (auto& l : doc.layers) {
    const auto f = l.extra.find("ind");
    if (f != l.extra.end() && f->second.is_number()) {
      const int v = f->second.as<int>();
      if (v > maxInd) {
        maxInd = v;
      }
    }
  }
  auto indJson = parseJson(detail::to_string(maxInd + 1));
  if (!indJson) {
    ::lottiepp::fail(indJson.error());
  }
  layer.extra["ind"] = std::move(*indJson);
  doc.layers.push_back(std::move(layer));
}

inline Document makeDocument(const DocumentParams& p) {
  Document doc;
  doc.v  = p.version;
  doc.fr = p.fr;
  doc.ip = p.ip;
  doc.op = p.op;
  doc.w  = p.w;
  doc.h  = p.h;
  if (!p.name.empty()) {
    doc.nm = p.name;
  }
  return doc;
}

inline Layer* findLayer(Document& doc, std::string_view name) {
  for (auto& l : doc.layers) {
    if (l.nm && *l.nm == name) {
      return &l;
    }
  }
  if (doc.assets) {
    for (auto& a : *doc.assets) {
      if (!a.layers) {
        continue;
      }
      for (auto& l : *a.layers) {
        if (l.nm && *l.nm == name) {
          return &l;
        }
      }
    }
  }
  return nullptr;
}

inline json makeGaussianBlur(double stddev, bool repeatEdge) {
  auto result = parseJson(
      "{\"ty\":0,\"nm\":\"Gaussian Blur\",\"np\":3,\"mn\":\"ADBE Gaussian Blur\","
      "\"ix\":1,\"en\":1,\"ef\":["
      "{\"ty\":\"slider\",\"nm\":\"Blur Dimensions\",\"mn\":\"ADBE Gaussian Blur-0001\",\"ix\":1,\"v\":{\"a\":0,\"k\":1}},"
      "{\"ty\":\"slider\",\"nm\":\"Blur Radius\",\"mn\":\"ADBE Gaussian Blur-0002\",\"ix\":2,\"v\":{\"a\":0,\"k\":" +
      detail::to_string(stddev) + "}},"
      "{\"ty\":\"checkbox\",\"nm\":\"Repeat Edge Pixels\",\"mn\":\"ADBE Gaussian Blur-0003\",\"ix\":3,\"v\":{\"a\":0,\"k\":" +
      detail::to_string(repeatEdge ? 1 : 0) + "}}"
      "]}");
  if (!result) {
    ::lottiepp::fail(result.error());
  }
  return std::move(*result);
}

inline void addEffect(Layer& layer, const json& effect) {
  json arr;
  const auto f = layer.extra.find("ef");
  if (f == layer.extra.end() || !f->second.is_array()) {
    auto parsed = parseJson("[]");
    if (!parsed) {
      ::lottiepp::fail(parsed.error());
    }
    arr = std::move(*parsed);
  } else {
    arr = f->second;
  }
  arr.get_array().push_back(effect);
  layer.extra["ef"] = arr;
}

inline std::optional<Rgb> parseHexColor(std::string_view hex) {
  if (hex.empty()) {
    return std::nullopt;
  }
  if (hex.front() == '#') {
    hex.remove_prefix(1);
  }
  auto nibble = [](char c) -> int { return hexDigit(c); };
  auto byteAt = [&](std::size_t i) -> int {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    return (hi << 4) | lo;
  };
  Rgb c;
  if (hex.size() == 3 || hex.size() == 4) {
    int vals[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < hex.size(); ++i) {
      const int d = nibble(hex[i]);
      if (d < 0) {
        return std::nullopt;
      }
      vals[i] = (d << 4) | d;
    }
    c.r = vals[0] / 255.f;
    c.g = vals[1] / 255.f;
    c.b = vals[2] / 255.f;
    c.a = vals[3] / 255.f;
    return c;
  }
  if (hex.size() == 6 || hex.size() == 8) {
    const int r = byteAt(0);
    const int g = byteAt(2);
    const int b = byteAt(4);
    if (r < 0 || g < 0 || b < 0) {
      return std::nullopt;
    }
    c.r = r / 255.f;
    c.g = g / 255.f;
    c.b = b / 255.f;
    if (hex.size() == 8) {
      const int a = byteAt(6);
      if (a < 0) {
        return std::nullopt;
      }
      c.a = a / 255.f;
    }
    return c;
  }
  return std::nullopt;
}

inline std::expected<json, std::string> parseJson(std::string_view text) {
  json        g{};
  const auto  ec = glz::read_json(g, text);
  if (ec) {
    return std::unexpected("read_json error: " + glz::format_error(ec, text));
  }
  return g;
}

inline std::expected<std::string, std::string> dumpJson(const json& node) {
  auto out = node.dump();
  if (!out) {
    return std::unexpected("failed to serialize json");
  }
  return *out;
}

inline std::expected<std::string, std::string> dumpJsonPretty(const json& node) {
  auto out = dumpJson(node);
  if (!out) {
    return std::unexpected(std::move(out.error()));
  }
  return glz::prettify_json(*out);
}

inline std::expected<Document, std::string> parse(std::string_view text) {
  Document doc;
  const auto ec = glz::read<kReadOpts>(doc, text);
  if (ec) {
    return std::unexpected("read_json error: " + glz::format_error(ec, text));
  }
  return doc;
}

inline std::expected<std::string, std::string> dump(const Document& doc) {
  std::string out;
  const auto  ec = glz::write<kWriteOpts>(doc, out);
  if (ec) {
    return std::unexpected("failed to serialize document");
  }
  return out;
}

inline std::expected<std::string, std::string> dumpPretty(const Document& doc) {
  auto out = ::lottiepp::dump(doc);
  if (!out) {
    return std::unexpected(std::move(out.error()));
  }
  return glz::prettify_json(*out);
}

inline std::expected<Document, std::string> load(const std::string& path) {
  const std::string ext = extensionOf(path);
  std::string       text;
  if (ext == ".lottie" || ext == ".zip") {
    auto result = extractAnimationFromZip(path);
    if (!result) {
      return std::unexpected(std::move(result.error()));
    }
    text = std::move(*result);
  } else {
    auto result = readFile(path);
    if (!result) {
      return std::unexpected(std::move(result.error()));
    }
    text = std::move(*result);
  }
  return parse(text);
}

inline bool save(const Document& doc, const std::string& path, std::string& err) {
  const std::string ext = extensionOf(path);
  if (ext == ".lottie" || ext == ".zip") {
    return writeLottieZip(doc, path, err);
  }
  auto pretty = ::lottiepp::dumpPretty(doc);
  if (!pretty) {
    err = std::move(pretty.error());
    return false;
  }
  return writeFile(path, *pretty, err);
}

inline bool removeLayer(Document& doc, std::string_view name) {
  bool removed = false;
  const auto before = doc.layers.size();
  doc.layers.erase(
      std::remove_if(doc.layers.begin(), doc.layers.end(),
                     [name](const Layer& l) { return l.nm && *l.nm == name; }),
      doc.layers.end());
  if (doc.layers.size() != before) {
    removed = true;
  }
  if (doc.assets) {
    for (auto& a : *doc.assets) {
      if (!a.layers) {
        continue;
      }
      const auto ab = a.layers->size();
      a.layers->erase(
          std::remove_if(a.layers->begin(), a.layers->end(),
                         [name](const Layer& l) { return l.nm && *l.nm == name; }),
          a.layers->end());
      if (a.layers->size() != ab) {
        removed = true;
      }
    }
  }
  return removed;
}

inline std::expected<std::size_t, std::string> recolor(Document& doc, std::string_view fromHex, std::string_view toHex) {
  const auto to = parseHexColor(toHex);
  if (!to) {
    return std::unexpected("invalid toHex color: " + std::string(toHex));
  }
  std::optional<Rgb> from;
  if (!fromHex.empty()) {
    from = parseHexColor(fromHex);
    if (!from) {
      return std::unexpected("invalid fromHex color: " + std::string(fromHex));
    }
  }
  std::size_t count = 0;
  forEachLayer(doc, [&](Layer& l) { count += recolorLayer(l, from, *to); });
  if (doc.assets) {
    for (auto& asset : *doc.assets) {
      count += recolorExtras(asset.extra, from, *to);
    }
  }
  count += recolorExtras(doc.extra, from, *to);
  return count;
}

inline bool replaceText(Document& doc, std::string_view layerName, std::string_view newText) {
  bool found = false;
  forEachLayer(doc, [&](Layer& l) { found = replaceTextInLayer(l, layerName, newText) || found; });
  return found;
}

inline std::size_t setSpeed(Document& doc, double factor) {
  if (factor <= 0.0 || !std::isfinite(factor)) {
    ::lottiepp::fail("speed factor must be finite and > 0");
  }
  std::size_t count = 0;
  scaleOptional(doc.ip, factor, count);
  scaleOptional(doc.op, factor, count);
  forEachLayer(doc, [&](Layer& l) { count += scaleLayer(l, factor); });
  if (doc.assets) {
    for (auto& asset : *doc.assets) {
      count += scaleExtras(asset.extra, factor);
    }
  }
  count += scaleExtras(doc.extra, factor);
  return count;
}

inline std::vector<Document> generateVariations(const Document& doc, const std::vector<VariationParams>& paramSets) {
  std::vector<Document> out;
  out.reserve(paramSets.size());
  for (const auto& p : paramSets) {
    auto dumped = ::lottiepp::dump(doc);
    if (!dumped) {
      ::lottiepp::fail(dumped.error());
    }
    auto parsed = parse(*dumped);
    if (!parsed) {
      ::lottiepp::fail(parsed.error());
    }
    Document copy = std::move(*parsed);
    if (p.recolor_to) {
      auto recolor_result = recolor(copy, p.recolor_from.value_or(""), *p.recolor_to);
      if (!recolor_result) {
        ::lottiepp::fail(recolor_result.error());
      }
    }
    if (p.text_layer && p.text_value) {
      replaceText(copy, *p.text_layer, *p.text_value);
    }
    if (p.speed) {
      setSpeed(copy, *p.speed);
    }
    out.push_back(std::move(copy));
  }
  return out;
}

}  // namespace lottiepp
