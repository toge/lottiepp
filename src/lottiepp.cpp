#include "lottiepp.hpp"

#include "miniz.h"
#include <fstream>
#include <sstream>

#include <glaze/json/prettify.hpp>

#include <cassert>
#include <cmath>
#include <algorithm>
#include <utility>
#include <memory>

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

/**
 * @brief ファイル内容をバイナリで全読み込みする
 * @param path 読み込むファイルのパス
 * @return ファイル内容を格納した文字列
 */
std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    LOTTIEPP_THROW("failed to open file: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

/**
 * @brief データをバイナリファイルとして書き出す
 * @param path 出力先パス
 * @param data 書き出すデータ
 */
void writeFile(const std::string& path, std::string_view data) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    LOTTIEPP_THROW("failed to write file: " + path);
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    LOTTIEPP_THROW("failed to write file: " + path);
  }
}

/**
 * @brief 文字列を小文字に変換する（破壊的）
 * @param s 変換対象の文字列
 * @return 小文字化された文字列
 */
std::string toLower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

/**
 * @brief パスから拡張子（先頭の '.' 含む、小文字）を取得する
 * @param path 対象のパス
 * @return 拡張子文字列。拡張子がない場合は空文字。
 */
std::string extensionOf(const std::string& path) {
  const auto pos = path.find_last_of('.');
  if (pos == std::string::npos) {
    return {};
  }
  return toLower(path.substr(pos));
}

/**
 * @brief 1 桁の 16 進数字を数値に変換する
 * @param c 対象の文字
 * @return 0～15 の値。無効な文字の場合は -1。
 */
int hexDigit(char c) {
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

/**
 * @brief 2 つの色がほぼ等しいかを判定する
 * @param a 比較対象の色
 * @param b 比較対象の色
 * @param eps 許容誤差（デフォルトは 1/255、すなわち 8bit の最小ステップ）
 * @return 各成分の差が eps 以下なら true
 */
bool colorsNearlyEqual(const Rgb& a, const Rgb& b, float eps = 1.0f / 255.0f) {
  return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps && std::fabs(a.b - b.b) <= eps;
}

/**
 * @brief ノードが色配列（長さ 3 または 4 の数値配列）かを判定する
 * @param arr 判定対象の json ノード
 * @return 色配列として妥当なら true
 */
bool isColorArray(const json& arr) {
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

/**
 * @brief 色配列から Rgb を構築する
 * @param arr 長さ 3 または 4 の数値配列
 * @return 構築された Rgb（4 要素目があればアルファとして採用）
 */
Rgb colorFromArray(const json& arr) {
  Rgb c;
  c.r = arr[0].as<float>();
  c.g = arr[1].as<float>();
  c.b = arr[2].as<float>();
  if (arr.size() >= 4) {
    c.a = arr[3].as<float>();
  }
  return c;
}

/**
 * @brief Rgb から長さ n の色配列を構築する
 * @param c 変換元の色
 * @param n 配列長（3 または 4。4 の場合はアルファを含める）
 * @return 構築された json 配列
 */
json colorToArray(const Rgb& c, std::size_t n) {
  json arr = json::array_t{c.r, c.g, c.b};
  if (n >= 4) {
    arr.get_array().push_back(c.a);
  }
  return arr;
}

/**
 * @brief ノードが色プロパティ（"k" を持つオブジェクト）っぽいかを判定する
 * @details "a"（アニメーションか否か）があれば数値であることを要求する。
 * @param node 判定対象の json ノード
 * @return 色プロパティとして妥当なら true
 */
bool looksLikeColorProp(const json& node) {
  if (!node.is_object() || !node.contains("k")) {
    return false;
  }
  if (node.contains("a") && !node["a"].is_number()) {
    return false;
  }
  return true;
}

/**
 * @brief 単一の色プロパティ値（k）を対象色へ置換する
 * @details 静的値の場合と、キーフレーム配列の場合の両方に対応する。
 * @param node 色プロパティノード（"k" を保持）
 * @param from 置換元の色。指定がある場合は一致する色のみ置換する。
 * @param to 置換後の色
 * @return このノード内で置換された個数
 */
std::size_t recolorColorValue(json& node, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;

  // 配列を色配列とみなして必要に応じ置換するラムダ
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
  // 静的な色値（数値のみの配列）の場合
  if (k.is_array() && !k.get_array().empty() && k[0].is_number()) {
    maybeReplaceArray(k);
    return count;
  }

  // キーフレーム配列の場合：開始値(s)と終了値(e)をそれぞれ置換
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

/**
 * @brief ノードツリーを再帰的に探索し、色を置換する
 * @details シェイプ種別 ty が fl/st（単色）および gf/gs（グラデーション）の場合に色を置換する。
 * @param node 探索対象の json ノード
 * @param from 置換元の色（指定なしならすべて置換）
 * @param to 置換後の色
 * @return 置換された色の個数
 */
std::size_t recolorRecursive(json& node, const std::optional<Rgb>& from, const Rgb& to) {
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

  // シェイプ種別に応じて色プロパティを特定し置換する
  if (node.contains("ty") && node["ty"].is_string()) {
    const auto ty = node["ty"].as<std::string>();
    // 単色の塗りつぶし/ストローク
    if ((ty == "fl" || ty == "st") && node.contains("c")) {
      count += recolorColorValue(node["c"], from, to);
    }
    // グラデーション（線形/放射）の場合：g 内のストップカラーを置換
    if ((ty == "gf" || ty == "gs") && node.contains("g") && node["g"].is_object()) {
      json& g = node["g"];
      // p が存在すればその値で停止数を制限（アルファ停止の読み越し防止）
      const std::size_t pStops = [&]() -> std::size_t {
        if (g.contains("p") && g["p"].is_number()) {
          return static_cast<std::size_t>(g["p"].as<int>());
        }
        return 0;
      }();
      // グラデーションのストップ列を置換するラムダ（4 要素ごとに [位置, r, g, b] が並ぶ）
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
          // アニメーションされたグラデーション：内側の k を取り出す
          json& kk = g["k"]["k"];
          if (kk.is_array() && !kk.get_array().empty() && kk[0].is_number()) {
            recolorStops(kk);
          } else if (kk.is_array()) {
            // キーフレーム配列の場合：各キーフレームの s/e を置換
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

  // 子要素を再帰的に探索（オブジェクト/配列のみ）
  for (auto& [key, child] : node.get_object()) {
    (void)key;
    if (child.is_object() || child.is_array()) {
      count += recolorRecursive(child, from, to);
    }
  }
  return count;
}

/**
 * @brief ExtraMap 内の各値に対して再帰的に色置換を行う
 * @param extra 対象の ExtraMap
 * @param from 置換元の色
 * @param to 置換後の色
 * @return 置換された色の個数
 */
std::size_t recolorExtras(ExtraMap& extra, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  for (auto& [k, v] : extra) {
    (void)k;
    count += recolorRecursive(v, from, to);
  }
  return count;
}

/**
 * @brief 単一レイヤに対して色置換を行う
 * @param layer 対象のレイヤ（破壊的に変更）
 * @param from 置換元の色
 * @param to 置換後の色
 * @return 置換された色の個数
 */
std::size_t recolorLayer(Layer& layer, const std::optional<Rgb>& from, const Rgb& to) {
  std::size_t count = 0;
  if (layer.shapes) {
    count += recolorRecursive(*layer.shapes, from, to);
  }
  // トランスフォーム / テキストレイヤは色データを持たないため、shapes のみを対象とする
  count += recolorExtras(layer.extra, from, to);
  return count;
}

/**
 * @brief 単一の数値ノードを倍率でスケールする
 * @param v 対象の json ノード（数値の場合のみスケール）
 * @param factor 倍率
 * @param count スケールした個数を加算する参照
 */
void scaleTimingValue(json& v, double factor, std::size_t& count) {
  if (v.is_number()) {
    v = v.as<double>() * factor;
    ++count;
  }
}

/**
 * @brief optional の数値を倍率でスケールする
 * @param v 対象の optional<double>
 * @param factor 倍率
 * @param count スケールした個数を加算する参照
 */
void scaleOptional(std::optional<double>& v, double factor, std::size_t& count) {
  if (v) {
    *v *= factor;
    ++count;
  }
}

/**
 * @brief ノードがキーフレーム（t を含み、s/e/i/o のいずれかを伴う）かを判定する
 * @param node 判定対象の json ノード
 * @return キーフレームとして妥当なら true
 */
bool looksLikeKeyframe(const json& node) {
  return node.is_object() && node.contains("t") && node["t"].is_number() &&
         (node.contains("s") || node.contains("e") || node.contains("i") || node.contains("o"));
}

/**
 * @brief ノードツリーを再帰的に探索し、時間関連の値をスケールする
 * @details キーフレームの t、および ip/op/st を対象とする。
 * @param node 探索対象の json ノード
 * @param factor 時間軸の倍率
 * @return スケールされた個数
 */
std::size_t scaleKeyframes(json& node, double factor) {
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

  // キーフレームの時間 t をスケール
  if (looksLikeKeyframe(node)) {
    scaleTimingValue(node["t"], factor, count);
  }

  // 特定キー（ip/op/st）および子要素を再帰的に探索
  for (auto& [key, child] : node.get_object()) {
    if (key == "ip" || key == "op" || key == "st") {
      scaleTimingValue(child, factor, count);
    } else if (child.is_object() || child.is_array()) {
      count += scaleKeyframes(child, factor);
    }
  }
  return count;
}

/**
 * @brief ExtraMap 内の各値に対して再帰的に時間スケールを行う
 * @param extra 対象の ExtraMap
 * @param factor 時間軸の倍率
 * @return スケールされた個数
 */
std::size_t scaleExtras(ExtraMap& extra, double factor) {
  std::size_t count = 0;
  for (auto& [k, v] : extra) {
    (void)k;
    count += scaleKeyframes(v, factor);
  }
  return count;
}

/**
 * @brief 単一レイヤに対して時間スケールを行う
 * @details レイヤの ip/op/st、shapes、テキスト、トランスフォーム各プロパティを対象とする。
 * @param layer 対象のレイヤ（破壊的に変更）
 * @param factor 時間軸の倍率
 * @return スケールされた個数
 */
std::size_t scaleLayer(Layer& layer, double factor) {
  std::size_t count = 0;
  // Scale canonical members first
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
    // トランスフォームの全プロパティ（o/r/p/a/s/sk/sa）を順にスケール
    for (auto* prop : {&ks.o, &ks.r, &ks.p, &ks.a, &ks.s, &ks.sk, &ks.sa}) {
      if (*prop) {
        count += scaleKeyframes(**prop, factor);
      }
    }
  }

  // Avoid double-scaling ip/op/st when they appear both in structured members and in extra map.
  // If the structured member exists, temporarily remove corresponding extra entries so that
  // scaleExtras won't touch them; restore the original extras unchanged afterwards.
  ExtraMap saved;
  for (const char* k : {"ip", "op", "st"}) {
    auto it = layer.extra.find(k);
    if (it != layer.extra.end() && ( (k==std::string("ip") && layer.ip) || (k==std::string("op") && layer.op) || (k==std::string("st") && layer.st) )) {
      saved[k] = it->second;
      layer.extra.erase(it);
    }
  }

  count += scaleExtras(layer.extra, factor);

  // restore saved extras (unchanged)
  for (auto& [k, v] : saved) {
    layer.extra[k] = v;
  }

  return count;
}

/**
 * @brief 単一レイヤのテキストを置換する
 * @details ty==5 かつレイヤ名が一致する場合、キーフレームの s.t を書き換える。
 * @param layer 対象のレイヤ（破壊的に変更）
 * @param layerName 置換対象のレイヤ名
 * @param newText 置換後のテキスト
 * @return 置換された場合は true、該当しない場合は false
 */
bool replaceTextInLayer(Layer& layer, std::string_view layerName, std::string_view newText) {
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

/**
 * @brief .lottie(zip) 内から最適なアニメーション JSON を抽出する
 * @details ファイル名のスコアリングにより、最も妥当な JSON を 1 つ選択する。
 * @param path .lottie / .zip ファイルのパス
 * @return 抽出された JSON テキスト
 */
std::string extractAnimationFromZip(const std::string& path) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
    LOTTIEPP_THROW("failed to open .lottie zip: " + path);
  }

  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  std::string bestName;    // 最もスコアの高いファイル名
  int         bestScore = -1;  // 選択されたファイルのスコア（高いほど優先）
  for (mz_uint i = 0; i < n; ++i) {
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) {
      continue;
    }
    std::string name  = st.m_filename;
    std::string lower = toLower(name);
    int         score = -1;
    // 規約通りのパス（最優先）
    if (lower == "animations/data.json" || lower == "animation.json") {
      score = 100;
    } else if (lower.starts_with("animations/") && lower.ends_with(".json")) {
      score = 80;
    } else if (lower.ends_with(".json") && lower.find("manifest") == std::string::npos) {
      // それ以外の JSON（マニフェスト以外）は低優先
      score = 40;
    }
    if (score > bestScore) {
      bestScore = score;
      bestName  = name;
    }
  }

  if (bestScore < 0) {
    mz_zip_reader_end(&zip);
    LOTTIEPP_THROW("no animation json found in: " + path);
  }

  size_t size = 0;
  void* data = mz_zip_reader_extract_file_to_heap(&zip, bestName.c_str(), &size, 0);
  // Ensure zip reader is ended in all cases
  struct _ZipReaderEnd {
    mz_zip_archive* _z;
    _ZipReaderEnd(mz_zip_archive* z) : _z(z) {}
    ~_ZipReaderEnd() { if (_z) mz_zip_reader_end(_z); }
  } _ender(&zip);
  if (!data) {
    LOTTIEPP_THROW("failed to extract animation from: " + path);
  }
  // RAII for mz_free
  std::unique_ptr<void, decltype(&mz_free)> _data_guard(data, &mz_free);
  std::string out(static_cast<const char*>(data), size);
  return out;
}

/**
 * @brief Document を .lottie(zip) 形式で書き出す
 * @details manifest.json と animations/data.json の 2 エントリを格納する。
 * @param doc 保存対象の Document
 * @param path 出力先の .lottie / .zip パス
 */
void writeLottieZip(const Document& doc, const std::string& path) {
  const std::string animation = dumpPretty(doc);

  std::string manifestStr =
      "{\"version\":\"1\",\"generator\":\"lottieproc\",\"animations\":[{\"id\":\"data\"}]}";
  manifestStr = glz::prettify_json(manifestStr);

  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
    LOTTIEPP_THROW("failed to create .lottie: " + path);
  }
  // RAII for writer end
  struct _ZipWriterEnd {
    mz_zip_archive* _z;
    bool _finalized = false;
    _ZipWriterEnd(mz_zip_archive* z) : _z(z) {}
    ~_ZipWriterEnd()
    {
      if (_z) {
        // best-effort finalize if not finalized; ignore errors in dtor
        mz_zip_writer_end(_z);
      }
    }
  } _wender(&zip);

  if (!mz_zip_writer_add_mem(&zip, "manifest.json", manifestStr.data(), manifestStr.size(), MZ_DEFAULT_COMPRESSION)) {
    LOTTIEPP_THROW("failed to add manifest.json to: " + path);
  }
  if (!mz_zip_writer_add_mem(&zip, "animations/data.json", animation.data(), animation.size(), MZ_DEFAULT_COMPRESSION)) {
    LOTTIEPP_THROW("failed to add animations/data.json to: " + path);
  }
  if (!mz_zip_writer_finalize_archive(&zip)) {
    LOTTIEPP_THROW("failed to finalize .lottie: " + path);
  }
  // explicit end (RAII will also end on scope exit)
  mz_zip_writer_end(&zip);
}

}  // namespace

/**
 * @brief 静的プロパティ値（数値）の json ノードを生成する
 * @details "a":0 は「アニメーションなし（静的）」を意味する。キー "k" に実値を保持する。
 * @param v 静的に設定する数値
 * @return {"a":0,"k":<v>} の形の json ノード
 */
json staticProp(double v) {
  if (!std::isfinite(v)) {
    LOTTIEPP_THROW_ARG("staticProp: value must be finite (not NaN/Inf)");
  }
  // 静的プロパティ（a=0）の定型を組み立てる
  return parseJson("{\"a\":0,\"k\":" + detail::to_string(v) + "}");
}

/**
 * @brief 静的プロパティ値（配列リテラル）の json ノードを生成する
 * @details "a":0 は「アニメーションなし（静的）」を意味する。キー "k" に実値を保持する。
 * @param arr JSON 配列リテラル文字列（例: "[100,100]"）
 * @return {"a":0,"k":<arr>} の形の json ノード
 */
json staticProp(std::string_view arr) {
  // 静的プロパティ（a=0）の定型を組み立てる
  return parseJson("{\"a\":0,\"k\":" + std::string(arr) + "}");
}

/**
 * @brief シェイプグループ用のトランスフォーム（ty="tr"）を生成する
 * @details シェイプグループ末尾に必須のトランスフォーム。原点・等倍・不透明な既定値とする。
 * @return トランスフォームノード（ty="tr"）を表す json
 */
json makeShapeTransform() {
  // p=位置, a=アンカー, s=スケール(%), r=回転(度), o=不透明度(%)
  static const json kTransform = parseJson(
      "{\"ty\":\"tr\","
      "\"p\":{\"a\":0,\"k\":[0,0]},"
      "\"a\":{\"a\":0,\"k\":[0,0]},"
      "\"s\":{\"a\":0,\"k\":[100,100]},"
      "\"r\":{\"a\":0,\"k\":0},"
      "\"o\":{\"a\":0,\"k\":100}}");
  return kTransform;
}

/**
 * @brief 長方形シェイプ（ty="rc"）を生成する
 * @param w 幅（レイヤ原点を中心とする）
 * @param h 高さ（レイヤ原点を中心とする）
 * @param round 角丸め半径（0=角丸めなし）
 * @return シェイプアイテムを表す json ノード
 */
// トリムパス（ty="tm"）を生成するユーティリティ
json makeTrimPath(double startPct, double endPct, double offsetDeg, bool simultaneous)
{
  if (!std::isfinite(startPct) || !std::isfinite(endPct) || !std::isfinite(offsetDeg)) {
    LOTTIEPP_THROW_ARG("makeTrimPath: parameters must be finite");
  }
  const int m = simultaneous ? 1 : 2;
  const std::string node = "{\"ty\":\"tm\","
      "\"s\":{\"a\":0,\"k\":" + detail::to_string(startPct) + "},"
      "\"e\":{\"a\":0,\"k\":" + detail::to_string(endPct) + "},"
      "\"o\":{\"a\":0,\"k\":" + detail::to_string(offsetDeg) + "},"
      "\"m\":" + detail::to_string(m) + "}";
  return parseJson(node);
}

json makeRect(double w, double h, double round) {
  if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(round)) {
    LOTTIEPP_THROW_ARG("makeRect: w/h/round must be finite");
  }
  // d=1 は描画方向（順方向）を示す既定値
  static const json kBase = parseJson(
      "{\"ty\":\"rc\",\"d\":1,"
      "\"s\":{\"a\":0,\"k\":[0,0]},\"p\":{\"a\":0,\"k\":[0,0]},\"r\":{\"a\":0,\"k\":0}}");
  json n = kBase;
  // サイズと角丸め半径を指定値で上書きする
  n["s"] = staticProp("[" + detail::to_string(w) + "," + detail::to_string(h) + "]");
  n["r"] = staticProp(round);
  return n;
}

/**
 * @brief 楕円シェイプ（ty="el"）を生成する
 * @param w 幅（レイヤ原点を中心とする）
 * @param h 高さ（レイヤ原点を中心とする）
 * @return シェイプアイテムを表す json ノード
 */
json makeEllipse(double w, double h) {
  if (!std::isfinite(w) || !std::isfinite(h)) {
    LOTTIEPP_THROW_ARG("makeEllipse: w/h must be finite");
  }
  // d=1 は描画方向（順方向）を示す既定値
  static const json kBase = parseJson(
      "{\"ty\":\"el\",\"d\":1,"
      "\"s\":{\"a\":0,\"k\":[0,0]},\"p\":{\"a\":0,\"k\":[0,0]}}");
  json n = kBase;
  // サイズを指定値で上書きする
  n["s"] = staticProp("[" + detail::to_string(w) + "," + detail::to_string(h) + "]");
  return n;
}

/**
 * @brief 単色塗りつぶし（ty="fl"）を生成する
 * @param hex 色（#rrggbb 等）
 * @param opacity 不透明度（0～100、単位はパーセント）
 * @return シェイプアイテムを表す json ノード
 */
json makeFill(std::string_view hex, double opacity) {
  if (!std::isfinite(opacity)) {
    LOTTIEPP_THROW_ARG("makeFill: opacity must be finite");
  }
  const auto c = parseHexColor(hex);
  if (!c) {
    LOTTIEPP_THROW_ARG("invalid color: " + std::string(hex));
  }
  // r=1 は塗りつぶしルール（nonzero）、bm=0 はブレンドモード（通常）の既定値
  static const json kBase = parseJson(
      "{\"ty\":\"fl\",\"r\":1,\"bm\":0,"
      "\"o\":{\"a\":0,\"k\":0},\"c\":{\"a\":0,\"k\":[0,0,0,1]}}");
  json n = kBase;
  // 不透明度と色（RGBA、各成分 0.0～1.0）を指定値で上書きする
  n["o"] = staticProp(opacity);
  n["c"] = staticProp("[" + detail::to_string(c->r) + "," + detail::to_string(c->g) + "," +
                      detail::to_string(c->b) + "," + detail::to_string(c->a) + "]");
  return n;
}

/**
 * @brief 単色ストローク（ty="st"）を生成する
 * @param hex 色（#rrggbb 等）
 * @param width 線幅（単位は composition のピクセル）
 * @param opacity 不透明度（0～100、単位はパーセント）
 * @return シェイプアイテムを表す json ノード
 */
json makeStroke(std::string_view hex, double width, double opacity) {
  if (!std::isfinite(width) || !std::isfinite(opacity)) {
    LOTTIEPP_THROW_ARG("makeStroke: width/opacity must be finite");
  }
  const auto c = parseHexColor(hex);
  if (!c) {
    LOTTIEPP_THROW_ARG("invalid color: " + std::string(hex));
  }
  // r=1: 塗りつぶしルール, bm=0: ブレンドモード(通常)
  // lc=2: 線端をラウンド, lj=2: 線結合をラウンド, ml=4: マイター限界値(既定)
  static const json kBase = parseJson(
      "{\"ty\":\"st\",\"r\":1,\"bm\":0,\"lc\":2,\"lj\":2,\"ml\":4,"
      "\"o\":{\"a\":0,\"k\":0},\"w\":{\"a\":0,\"k\":0},\"c\":{\"a\":0,\"k\":[0,0,0,1]}}");
  json n = kBase;
  // 不透明度・線幅・色を指定値で上書きする
  n["o"] = staticProp(opacity);
  n["w"] = staticProp(width);
  n["c"] = staticProp("[" + detail::to_string(c->r) + "," + detail::to_string(c->g) + "," +
                      detail::to_string(c->b) + "," + detail::to_string(c->a) + "]");
  return n;
}

/**
 * @brief シェイプレイヤ（ty=4）を生成する
 * @details 渡されたシェイプアイテムを 1 つのグループ（ty="gr"）にまとめ、
 *          末尾にグループ用トランスフォーム（ty="tr"）を付与する。
 *          レイヤの位置はトランスフォームの p で指定し、シェイプ自体は原点を基準に描画する。
 * @param p レイヤパラメータ（items にシェイプと塗り/線を含める）
 * @return 生成された Layer（ind は未設定。addLayer で付与される）
 */
Layer makeShapeLayer(const ShapeLayerParams& p) {
  Layer l;
  l.ty = 4;  // ty=4 はレイヤ種別「シェイプレイヤ」
  l.nm = p.name;
  l.ip = p.from;  // イン点（表示開始フレーム）
  l.op = p.to;    // アウト点（表示終了フレーム）
  l.st = p.from;  // 開始時間（親タイムライン上の開始フレーム）

  // レイヤトランスフォーム：位置でシェイプ全体を移動する
  Transform ks;
  ks.o = staticProp(p.opacity);           // 不透明度（%）
  ks.r = staticProp(0.0);                 // 回転（度）
  ks.p = staticProp("[" + detail::to_string(p.x) + "," + detail::to_string(p.y) + ",0]");  // 位置（中心）
  ks.a = staticProp("[0,0,0]");           // アンカーポイント
  ks.s = staticProp("[100,100,100]");      // スケール（%・等倍）
  l.ks = ks;

  // シェイプアイテムを 1 つのグループにまとめる（末尾に tr を付与）
  static const json kGroupBase = parseJson("{\"ty\":\"gr\",\"nm\":\"Group\",\"it\":[]}");
  json group = kGroupBase;
  auto& it   = group["it"];
  for (auto& item : p.items) {
    it.get_array().push_back(item);
  }
  it.get_array().push_back(makeShapeTransform());

  // shapes 配列へグループを 1 件登録する
  static const json kEmptyArray = parseJson("[]");
  json shapes = kEmptyArray;
  shapes.get_array().push_back(group);
  l.shapes = shapes;
  return l;
}

/**
 * @brief Document のトップレベルへレイヤを追加する
 * @details 既存レイヤの最大 ind に +1 した値を ind として付与する（Lottie のレイヤ順序は ind で参照される）。
 * @param doc 対象の Document（破壊的に変更）
 * @param layer 追加するレイヤ
 */
void addLayer(Document& doc, Layer layer) {
  int maxInd = 0;  // 既存レイヤの最大 ind（未設定時は 0 から開始）
  for (auto& l : doc.layers) {
    const auto f = l.extra.find("ind");
    if (f != l.extra.end() && f->second.is_number()) {
      const int v = f->second.as<int>();
      if (v > maxInd) {
        maxInd = v;
      }
    }
  }
  // 重複しないよう最大値 +1 を割り当てる
  layer.extra["ind"] = parseJson(detail::to_string(maxInd + 1));
  doc.layers.push_back(std::move(layer));
}

/**
 * @brief 新規 Lottie ドキュメント（空）を生成する
 * @details レイヤ空配列と既定のメタデータを持つ妥当なドキュメントを返す。
 *          生成後は makeShapeLayer 等で要素を追加し、save で出力できる。
 * @param p ドキュメントパラメータ
 * @return 生成された空の Document
 */
Document makeDocument(const DocumentParams& p) {
  Document doc;
  doc.v  = p.version;                    // フォーマットバージョン
  doc.fr = p.fr;                         // フレームレート
  doc.ip = p.ip;                         // イン点
  doc.op = p.op;                         // アウト点
  doc.w  = p.w;                          // 幅
  doc.h  = p.h;                          // 高さ
  if (!p.name.empty()) {
    doc.nm = p.name;                     // 名前（指定時のみ）
  }
  // レイヤは空で初期化される（Document::layers の既定値）
  return doc;
}

/**
 * @brief 名前でレイヤを検索する（トップレベルおよびアセット内のプリコンポジション）
 * @param doc 対象の Document
 * @param name 検索するレイヤ名
 * @return 見つかった場合は Layer へのポインタ、なければ nullptr
 */
Layer* findLayer(Document& doc, std::string_view name) {
  // トップレベルのレイヤを走査
  for (auto& l : doc.layers) {
    if (l.nm && *l.nm == name) {
      return &l;
    }
  }
  // アセット内のプリコンポジションのレイヤも走査
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

/**
 * @brief ガウシアンブラー（AE 互換）エフェクトノードを生成する
 * @details After Effects の「Gaussian Blur」エフェクトと互換な構造を生成する。
 *          各プロパティは AE のマッチネーム（mn）で識別され、レンダラによっては
 *          この mn に依存してマッピングされるため既定値を維持する。
 * @param stddev ブラー半径
 * @param repeatEdge エッジピクセルを繰り返すか（true=繰り返す）
 * @return レイヤエフェクト（ef）に追加可能な json ノード
 */
json makeGaussianBlur(double stddev, bool repeatEdge) {
  // ty=0: AE 標準エフェクト, np=3: プロパティ数(3),
  // ix: プロパティのインデックス, mn: AE マッチネーム, en=1: 有効
  return parseJson(
      "{\"ty\":0,\"nm\":\"Gaussian Blur\",\"np\":3,\"mn\":\"ADBE Gaussian Blur\","
      "\"ix\":1,\"en\":1,\"ef\":["
      "{\"ty\":\"slider\",\"nm\":\"Blur Dimensions\",\"mn\":\"ADBE Gaussian Blur-0001\",\"ix\":1,\"v\":{\"a\":0,\"k\":1}},"
      "{\"ty\":\"slider\",\"nm\":\"Blur Radius\",\"mn\":\"ADBE Gaussian Blur-0002\",\"ix\":2,\"v\":{\"a\":0,\"k\":" +
      detail::to_string(stddev) + "}},"
      "{\"ty\":\"checkbox\",\"nm\":\"Repeat Edge Pixels\",\"mn\":\"ADBE Gaussian Blur-0003\",\"ix\":3,\"v\":{\"a\":0,\"k\":" +
      detail::to_string(repeatEdge ? 1 : 0) + "}}"
      "]}");
}

/**
 * @brief レイヤへエフェクトを追加する
 * @details レイヤの未知キー "ef" にエフェクトを追加する（既存があれば追記、なければ新規配列を作成）。
 * @param layer 対象のレイヤ（破壊的に変更）
 * @param effect 追加するエフェクトノード
 */
void addEffect(Layer& layer, const json& effect) {
  json arr;
  const auto f = layer.extra.find("ef");
  if (f == layer.extra.end() || !f->second.is_array()) {
    // 既存の ef がない場合は空配列から開始する
    arr = parseJson("[]");
  } else {
    arr = f->second;
  }
  // エフェクト配列へ追加し、未知キー ef へ書き戻す
  arr.get_array().push_back(effect);
  layer.extra["ef"] = arr;
}

/**
 * @brief 16 進色文字列を解析して Rgb に変換する
 * @param hex "#rgb" / "#rrggbb" / "#rrggbbaa"（'#' 省略可、大文字小文字区別なし）
 * @return 成功時は Rgb を保持する optional、失敗時は nullopt
 */
std::optional<Rgb> parseHexColor(std::string_view hex) {
  if (hex.empty()) {
    return std::nullopt;
  }
  if (hex.front() == '#') {
    hex.remove_prefix(1);
  }
  // 1 桁の 16 進数字を取得するラムダ
  auto nibble = [](char c) -> int { return hexDigit(c); };

  // 2 桁（1 バイト）を 1 つの数値に合成するラムダ
  auto byteAt = [&](std::size_t i) -> int {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    return (hi << 4) | lo;
  };

  Rgb c;
  // 短縮形（3/4 桁）：各ニブルを複製して 1 バイト分の値とする
  if (hex.size() == 3 || hex.size() == 4) {
    int vals[4] = {0, 0, 0, 255};  // アルファのデフォルトは不透明(255)
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
  // 完全形（6/8 桁）
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

/**
 * @brief JSON テキストから動的 json ノードを解析する
 * @param text JSON 形式のテキスト
 * @return 解析された json ノード
 */
json parseJson(std::string_view text) {
  json        g{};
  const auto  ec = glz::read_json(g, text);
  if (ec) {
    LOTTIEPP_THROW("read_json error: " + glz::format_error(ec, text));
  }
  return g;
}

/**
 * @brief 動的 json ノードを JSON 文字列にシリアライズする
 * @param node 対象の json ノード
 * @return シリアライズされた JSON 文字列
 */
std::string dumpJson(const json& node) {
  auto out = node.dump();
  if (!out) {
    LOTTIEPP_THROW("failed to serialize json");
  }
  return *out;
}

/**
 * @brief 動的 json ノードを整形済み JSON 文字列にシリアライズする
 * @param node 対象の json ノード
 * @return インデント整形された JSON 文字列
 */
std::string dumpJsonPretty(const json& node) {
  return glz::prettify_json(dumpJson(node));
}

/**
 * @brief JSON テキストから Document を解析する
 * @param text JSON 形式のテキスト
 * @return 解析された Document
 */
Document parse(std::string_view text) {
  Document doc;
  const auto ec = glz::read<kReadOpts>(doc, text);
  if (ec) {
    LOTTIEPP_THROW("read_json error: " + glz::format_error(ec, text));
  }
  return doc;
}

/**
 * @brief Document を JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return シリアライズされた JSON 文字列
 */
std::string dump(const Document& doc) {
  std::string out;
  const auto  ec = glz::write<kWriteOpts>(doc, out);
  if (ec) {
    LOTTIEPP_THROW("failed to serialize document");
  }
  return out;
}

/**
 * @brief Document を整形済み JSON 文字列にシリアライズする
 * @param doc 対象の Document
 * @return インデント整形された JSON 文字列
 */
std::string dumpPretty(const Document& doc) {
  return glz::prettify_json(dump(doc));
}

/**
 * @brief パスから Document を読み込む（.json または .lottie/.zip）
 * @param path 入力ファイルのパス
 * @return 読み込まれた Document
 */
Document load(const std::string& path) {
  const std::string ext = extensionOf(path);
  std::string       text;
  if (ext == ".lottie" || ext == ".zip") {
    text = extractAnimationFromZip(path);
  } else {
    text = readFile(path);
  }
  return parse(text);
}

/**
 * @brief Document をパスへ保存する（.json または .lottie/.zip）
 * @param doc 保存対象の Document
 * @param path 出力ファイルのパス
 */
void save(const Document& doc, const std::string& path) {
  const std::string ext = extensionOf(path);
  if (ext == ".lottie" || ext == ".zip") {
    writeLottieZip(doc, path);
    return;
  }
  writeFile(path, dumpPretty(doc));
}

/**
 * @brief 名前でレイヤを削除する（トップレベルおよびアセット内のプリコンポジション）
 * @param doc 対象の Document（破壊的に変更）
 * @param name 削除するレイヤ名
 * @return 該当レイヤが 1 つ以上見つかり削除された場合は true、なければ false
 */
bool removeLayer(Document& doc, std::string_view name) {
  bool removed = false;
  // トップレベルのレイヤ群から名前一致を削除する
  const auto before = doc.layers.size();
  doc.layers.erase(
      std::remove_if(doc.layers.begin(), doc.layers.end(),
                     [name](const Layer& l) { return l.nm && *l.nm == name; }),
      doc.layers.end());
  if (doc.layers.size() != before) {
    removed = true;
  }
  // アセット内のプリコンポジションのレイヤも削除する
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

/**
 * @brief ドキュメント内の色を再着色する
 * @param doc 対象の Document（破壊的に変更）
 * @param fromHex 置換元の色（hex）。空の場合はすべての単色を対象とする。
 * @param toHex 置換後の色（hex）
 * @return 置換された色の個数
 */
std::size_t recolor(Document& doc, std::string_view fromHex, std::string_view toHex) {
  const auto to = parseHexColor(toHex);
  if (!to) {
    LOTTIEPP_THROW_ARG("invalid toHex color: " + std::string(toHex));
  }
  std::optional<Rgb> from;
  if (!fromHex.empty()) {
    from = parseHexColor(fromHex);
    if (!from) {
      LOTTIEPP_THROW_ARG("invalid fromHex color: " + std::string(fromHex));
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

/**
 * @brief 指定レイヤ名のテキストを置換する
 * @param doc 対象の Document（破壊的に変更）
 * @param layerName 置換対象のレイヤ名
 * @param newText 置換後のテキスト
 * @return 該当レイヤが見つかり置換された場合は true
 */
bool replaceText(Document& doc, std::string_view layerName, std::string_view newText) {
  bool found = false;
  forEachLayer(doc, [&](Layer& l) { found = replaceTextInLayer(l, layerName, newText) || found; });
  return found;
}

/**
 * @brief ドキュメントのタイムラインをスケールする
 * @param doc 対象の Document（破壊的に変更）
 * @param factor 時間軸の倍率（有限かつ正であること）
 * @return スケールされたフィールドの個数
 */
std::size_t setSpeed(Document& doc, double factor) {
  if (factor <= 0.0 || !std::isfinite(factor)) {
    LOTTIEPP_THROW_ARG("speed factor must be finite and > 0");
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

/**
 * @brief ベース Document のコピーに各パラメータを適用し、バリエーション群を生成する
 * @param doc 元となる Document
 * @param paramSets バリエーションごとのパラメータ群
 * @return 生成された Document の配列
 */
std::vector<Document> generateVariations(const Document& doc, const std::vector<VariationParams>& paramSets) {
  std::vector<Document> out;
  out.reserve(paramSets.size());
  for (const auto& p : paramSets) {
    // ディープコピーは dump→parse ラウンドトリップで作成する。
    // glz::generic（ExtraMap を含む）には標準的なディープコピー手段がないため、
    // シリアライズ・デシリアライズによる複製を採用している。
    Document copy = parse(dump(doc));
    if (p.recolor_to) {
      recolor(copy, p.recolor_from.value_or(""), *p.recolor_to);
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
