#include "lottie_edit.hpp"

#include "miniz.h"

#include <glaze/exceptions/json_exceptions.hpp>
#include <glaze/json/prettify.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lottie_edit {
namespace {

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
    throw std::runtime_error("failed to open file: " + path);
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
    throw std::runtime_error("failed to write file: " + path);
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    throw std::runtime_error("failed to write file: " + path);
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
      // グラデーションのストップ列を置換するラムダ（4 要素ごとに [位置, r, g, b] が並ぶ）
      auto recolorStops = [&](json& stops) {
        if (!stops.is_array()) {
          return;
        }
        auto& a = stops.get_array();
        for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
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
      json& g = node["g"];
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
  count += scaleExtras(layer.extra, factor);
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
  if (!layer.t || !layer.t->d || !layer.t->d->k || !layer.t->d->k->is_array()) {
    return false;
  }
  json& k = *layer.t->d->k;
  bool found = false;
  for (auto& kf : k.get_array()) {
    if (!kf.is_object()) {
      continue;
    }
    // キーフレーム内のテキスト s.t を置換
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
    throw std::runtime_error("failed to open .lottie zip: " + path);
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
    throw std::runtime_error("no animation json found in: " + path);
  }

  size_t     size = 0;
  void* const data = mz_zip_reader_extract_file_to_heap(&zip, bestName.c_str(), &size, 0);
  mz_zip_reader_end(&zip);
  if (!data) {
    throw std::runtime_error("failed to extract animation from: " + path);
  }
  std::string out(static_cast<const char*>(data), size);
  mz_free(data);
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

  // マニフェストを組み立ててシリアライズ（整形）する
  Manifest manifest;
  manifest.animations.push_back(AnimationEntry{.id = "data"});
  std::string manifestStr;
  {
    const auto ec = glz::write<kWriteOpts>(manifest, manifestStr);
    if (ec) {
      throw std::runtime_error("failed to serialize manifest");
    }
    manifestStr = glz::prettify_json(manifestStr);
  }

  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
    throw std::runtime_error("failed to create .lottie: " + path);
  }
  if (!mz_zip_writer_add_mem(&zip, "manifest.json", manifestStr.data(), manifestStr.size(), MZ_DEFAULT_COMPRESSION)) {
    mz_zip_writer_end(&zip);
    throw std::runtime_error("failed to add manifest.json to: " + path);
  }
  if (!mz_zip_writer_add_mem(&zip, "animations/data.json", animation.data(), animation.size(), MZ_DEFAULT_COMPRESSION)) {
    mz_zip_writer_end(&zip);
    throw std::runtime_error("failed to add animations/data.json to: " + path);
  }
  if (!mz_zip_writer_finalize_archive(&zip)) {
    mz_zip_writer_end(&zip);
    throw std::runtime_error("failed to finalize .lottie: " + path);
  }
  mz_zip_writer_end(&zip);
}

}  // namespace

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
  return glz::ex::read_json<json>(text);
}

/**
 * @brief 動的 json ノードを JSON 文字列にシリアライズする
 * @param node 対象の json ノード
 * @return シリアライズされた JSON 文字列
 */
std::string dumpJson(const json& node) {
  auto out = node.dump();
  if (!out) {
    throw std::runtime_error("failed to serialize json");
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
    throw std::runtime_error("read_json error: " + glz::format_error(ec, text));
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
    throw std::runtime_error("failed to serialize document");
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
 * @brief ドキュメント内の色を再着色する
 * @param doc 対象の Document（破壊的に変更）
 * @param fromHex 置換元の色（hex）。空の場合はすべての単色を対象とする。
 * @param toHex 置換後の色（hex）
 * @return 置換された色の個数
 */
std::size_t recolor(Document& doc, std::string_view fromHex, std::string_view toHex) {
  const auto to = parseHexColor(toHex);
  if (!to) {
    throw std::invalid_argument("invalid toHex color: " + std::string(toHex));
  }
  std::optional<Rgb> from;
  if (!fromHex.empty()) {
    from = parseHexColor(fromHex);
    if (!from) {
      throw std::invalid_argument("invalid fromHex color: " + std::string(fromHex));
    }
  }

  std::size_t count = 0;
  // トップレベルのレイヤ群を走査
  for (auto& layer : doc.layers) {
    count += recolorLayer(layer, from, *to);
  }
  // アセット内のプリコンポジションも走査
  if (doc.assets) {
    for (auto& asset : *doc.assets) {
      if (asset.layers) {
        for (auto& layer : *asset.layers) {
          count += recolorLayer(layer, from, *to);
        }
      }
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
  for (auto& layer : doc.layers) {
    found = replaceTextInLayer(layer, layerName, newText) || found;
  }
  if (doc.assets) {
    for (auto& asset : *doc.assets) {
      if (!asset.layers) {
        continue;
      }
      for (auto& layer : *asset.layers) {
        found = replaceTextInLayer(layer, layerName, newText) || found;
      }
    }
  }
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
    throw std::invalid_argument("speed factor must be finite and > 0");
  }
  std::size_t count = 0;
  scaleOptional(doc.ip, factor, count);
  scaleOptional(doc.op, factor, count);
  for (auto& layer : doc.layers) {
    count += scaleLayer(layer, factor);
  }
  if (doc.assets) {
    for (auto& asset : *doc.assets) {
      if (asset.layers) {
        for (auto& layer : *asset.layers) {
          count += scaleLayer(layer, factor);
        }
      }
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
    // ディープコピーはラウンドトリップ（dump→parse）で作成する
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

}  // namespace lottie_edit
