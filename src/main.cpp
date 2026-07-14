#include "lottie_edit.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

/**
 * @brief コマンドライン利用方法を標準エラー出力へ表示する
 * @param argv0 プログラム名（argv[0]）
 */
void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " <input> -o <output> [--recolor <hex>] [--from <hex>] [--text <layer> <str>]\n"
      << "                            [--speed <factor>] [--variations <n>]\n"
      << "                            [--add-shape <type> <x> <y> <w> <h> <color> <from> <to> [name]]\n"
       << "                            [--add-effect <layer> <type> <value>]\n"
       << "                            [--remove-layer <name>]\n"
      << "\n"
      << "  input/output : .json or .lottie\n"
      << "  --recolor    : replace colors with this hex (#rrggbb). With --from, only matching colors.\n"
      << "  --from       : source color filter for --recolor\n"
      << "  --text       : replace text on named text layer (ty=5)\n"
      << "  --speed      : scale timeline ( >1 slower / longer )\n"
      << "  --variations : emit N variants as <stem>_1<ext> .. <stem>_N<ext>\n"
      << "  --add-shape  : add a shape layer (type: rect|ellipse) at (x,y) size (w,h),\n"
      << "                 filled with hex color, visible [from,to] frames (optional name)\n"
       << "  --add-effect : add an effect to named layer (type: blur <radius>)\n"
       << "  --remove-layer: remove the named layer (top-level and precomps)\n";
}

/**
 * @brief 出力パスにインデックス番号を付与したパスを生成する
 * @param output 元の出力パス（<stem><ext> の形式）
 * @param index 付与する 1 始まりの連番
 * @return <stem>_<index><ext> の形式のパス文字列
 */
std::string stemWithIndex(const std::string& output, int index) {
  const fs::path parent = fs::path(output).parent_path();
  const std::string stem = fs::path(output).stem().string();
  const std::string ext  = fs::path(output).extension().string();
  fs::path out = parent / (stem + "_" + std::to_string(index) + ext);
  return out.string();
}

/**
 * @brief ベースパラメータを元に n 個のバリエーションパラメータを生成する
 * @details 色は既定パレットから、速度はベース値からの揺らぎで決定する。未指定の項目のみ補完する。
 * @param n 生成するバリエーション数
 * @param base すべてのバリエーションに適用するベースパラメータ
 * @return 生成された VariationParams の配列
 */
std::vector<lottie_edit::VariationParams> makeDefaultVariations(int n, const lottie_edit::VariationParams& base) {
  // バリエーション用の既定カラーパレット（指定色がない場合に順繰りで使用）
  static const char* kPalette[] = {
      "#ff0000", "#00aa00", "#0066ff", "#ff9900", "#9900ff", "#00cccc", "#ff3399", "#333333",
  };
  // パレットの要素数（配列サイズから算出）
  constexpr int kPaletteN = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

  std::vector<lottie_edit::VariationParams> sets;
  sets.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    lottie_edit::VariationParams p = base;
    // 色が未指定ならパレットから循環的に選択する
    if (!p.recolor_to) {
      p.recolor_to = kPalette[i % kPaletteN];
    }
    // 速度が未指定なら、基数 1.0 に対して ±の揺らぎを与える
    if (!p.speed) {
      const double wobble = 1.0 + 0.1 * ((i % 2 == 0) ? (i / 2) : -(i / 2 + 1));
      // 揺らぎが小さすぎないよう 0.25 を下限として設定
      p.speed             = std::max(0.25, wobble);
    } else if (n > 1) {
      // 指定速度がある場合は、インデックスに応じてわずかに倍率をずらす
      p.speed = *p.speed * (1.0 + 0.05 * i);
    }
    // 複数バリエーションでテキスト指定がある場合、末尾に連番を付けて差別化する
    if (p.text_layer && p.text_value && n > 1) {
      p.text_value = *p.text_value + " " + std::to_string(i + 1);
    }
    sets.push_back(std::move(p));
  }
  return sets;
}

// 保留中の新規追加要求（ロード後にまとめて適用する）
struct PendingShape {
  std::string type;   // シェイプ種別（"rect" / "ellipse"）
  std::string name;   // レイヤ名（空なら type を使用）
  double     x = 0, y = 0;  // レイヤ位置（中心座標、単位はピクセル）
  double     w = 0, h = 0;  // シェイプの幅・高さ（単位はピクセル）
  double     from = 0, to = 0;  // 表示フレーム範囲 [from, to]
  std::string color;  // 塗りつぶし色（#rrggbb 等）
};
struct PendingEffect {
  std::string layer;  // 対象レイヤ名
  std::string type;   // エフェクト種別（"blur" 等）
  double     value = 0;  // エフェクトのパラメータ値（例: ブラー半径）
};

}  // namespace

/**
 * @brief コマンドラインエントリポイント
 * @param argc 引数の数
 * @param argv 引数配列
 * @return 正常終了時は 0、引数エラー等は 1 を返す
 */
int main(int argc, char** argv) {
  // 引数がない場合は利用方法を表示して終了
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  std::string input;
  std::string output;
  std::string recolorTo;
  std::string recolorFrom;
  std::string textLayer;
  std::string textValue;
  double      speed      = 0.0;  // 速度倍率の一時保持用
  bool        hasSpeed   = false; // --speed が指定されたかどうか
  int         variations = 0;     // 生成するバリエーション数（0=なし）
  std::vector<PendingShape>  pendingShapes;   // 追加するシェイプレイヤ群
  std::vector<PendingEffect> pendingEffects;  // 追加するエフェクト群
  std::vector<std::string>   removeLayers;    // 削除するレイヤ名群

  try {
    // 引数を先頭から順に解析する
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      // 残り引数が n 個未満なら例外を投げるヘルパ
      auto need = [&](int n) {
        if (i + n >= argc) {
          throw std::runtime_error("missing argument after " + std::string(arg));
        }
      };
      if (arg == "-h" || arg == "--help") {
        usage(argv[0]);
        return 0;
      }
      if (arg == "-o" || arg == "--output") {
        need(1);
        output = argv[++i];
      } else if (arg == "--recolor") {
        need(1);
        recolorTo = argv[++i];
      } else if (arg == "--from") {
        need(1);
        recolorFrom = argv[++i];
      } else if (arg == "--text") {
        need(2);
        textLayer = argv[++i];
        textValue = argv[++i];
      } else if (arg == "--speed") {
        need(1);
        speed    = std::stod(argv[++i]);
        hasSpeed = true;
      } else if (arg == "--variations") {
        need(1);
        variations = std::stoi(argv[++i]);
        if (variations < 1) {
          throw std::runtime_error("--variations must be >= 1");
        }
      } else if (arg == "--add-shape") {
        // --add-shape <type> <x> <y> <w> <h> <color> <from> <to> [name]
        // 必須 8 引数を順に読み取り、続く引数がオプション（'-' 始まり）でなければ名前とする
        need(8);
        PendingShape s;
        s.type  = argv[++i];
        s.x     = std::stod(argv[++i]);
        s.y     = std::stod(argv[++i]);
        s.w     = std::stod(argv[++i]);
        s.h     = std::stod(argv[++i]);
        s.color = argv[++i];
        s.from  = std::stod(argv[++i]);
        s.to    = std::stod(argv[++i]);
        // 名前は省略可能：次引数がオプションでなければ採用する
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          s.name = argv[++i];
        }
        pendingShapes.push_back(std::move(s));
      } else if (arg == "--add-effect") {
        // --add-effect <layer> <type> <value>
        need(3);
        PendingEffect e;
        e.layer = argv[++i];
        e.type  = argv[++i];
        e.value = std::stod(argv[++i]);
        pendingEffects.push_back(std::move(e));
      } else if (arg == "--remove-layer") {
        // --remove-layer <name>
        need(1);
        removeLayers.emplace_back(argv[++i]);
      } else if (!arg.empty() && arg[0] == '-') {
        // 未知のオプション
        std::cerr << "unknown option: " << arg << "\n";
        usage(argv[0]);
        return 1;
      } else if (input.empty()) {
        // 最初の非オプション引数を入力パスとする
        input = std::string(arg);
      } else {
        std::cerr << "unexpected argument: " << arg << "\n";
        usage(argv[0]);
        return 1;
      }
    }

    // 入力・出力がいずれも指定されていなければ利用方法を表示
    if (input.empty() || output.empty()) {
      usage(argv[0]);
      return 1;
    }

    // 入力ファイルを読み込み Document を構築
    auto doc = lottie_edit::load(input);

    // 新規シェイプレイヤの追加（エフェクトより先に適用して名前解決する）
    for (auto& s : pendingShapes) {
      lottie_edit::ShapeLayerParams p;
      // 名前が未指定の場合はシェイプ種別をそのままレイヤ名とする
      p.name  = s.name.empty() ? s.type : s.name;
      p.x     = s.x;
      p.y     = s.y;
      p.from  = s.from;
      p.to    = s.to;
      // 種別に応じたシェイプを生成し、塗りつぶしを追加する
      if (s.type == "rect") {
        p.items.push_back(lottie_edit::makeRect(s.w, s.h));
      } else if (s.type == "ellipse") {
        p.items.push_back(lottie_edit::makeEllipse(s.w, s.h));
      } else {
        throw std::runtime_error("unknown shape type: " + s.type);
      }
      p.items.push_back(lottie_edit::makeFill(s.color));
      lottie_edit::addLayer(doc, lottie_edit::makeShapeLayer(p));
      std::cout << "add-shape: " << p.name << "\n";
    }

    // レイヤへのエフェクト追加（名前で対象レイヤを解決する）
    for (auto& e : pendingEffects) {
      auto* layer = lottie_edit::findLayer(doc, e.layer);
      if (!layer) {
        throw std::runtime_error("layer not found for effect: " + e.layer);
      }
      if (e.type == "blur") {
        lottie_edit::addEffect(*layer, lottie_edit::makeGaussianBlur(e.value));
      } else {
        throw std::runtime_error("unknown effect type: " + e.type);
      }
      std::cout << "add-effect: " << e.type << " -> " << e.layer << "\n";
    }

    // コマンドライン引数からベースパラメータを組み立てる
    lottie_edit::VariationParams base;
    if (!recolorTo.empty()) {
      base.recolor_to = recolorTo;
    }
    if (!recolorFrom.empty()) {
      base.recolor_from = recolorFrom;
    }
    if (!textLayer.empty()) {
      base.text_layer = textLayer;
      base.text_value = textValue;
    }
    if (hasSpeed) {
      base.speed = speed;
    }

    // --variations が指定された場合は、複数のバリエーションを生成して出力する
    if (variations > 0) {
      const auto sets = makeDefaultVariations(variations, base);
      const auto docs = lottie_edit::generateVariations(doc, sets);
      for (std::size_t i = 0; i < docs.size(); ++i) {
        // ラウンドトリップでシリアライズ/パースが通ることを確認（戻り値は利用しない）
        (void)lottie_edit::parse(lottie_edit::dump(docs[i]));
        const std::string path = stemWithIndex(output, static_cast<int>(i + 1));
        lottie_edit::save(docs[i], path);
        std::cout << "wrote " << path << "\n";
      }
      return 0;
    }

    // 単一出力の場合：各処理を順に適用する
    if (base.recolor_to) {
      const auto n = lottie_edit::recolor(doc, base.recolor_from.value_or(""), *base.recolor_to);
      std::cout << "recolor: " << n << " values\n";
    }
    if (base.text_layer && base.text_value) {
      const bool ok = lottie_edit::replaceText(doc, *base.text_layer, *base.text_value);
      std::cout << "replaceText: " << (ok ? "ok" : "layer not found") << "\n";
    }
    if (base.speed) {
      const auto n = lottie_edit::setSpeed(doc, *base.speed);
      std::cout << "setSpeed: " << n << " fields\n";
    }
    // 指定されたレイヤの削除（名前一致）
    for (auto& name : removeLayers) {
      const bool ok = lottie_edit::removeLayer(doc, name);
      std::cout << "removeLayer " << name << ": " << (ok ? "ok" : "not found") << "\n";
    }

    // ラウンドトリップ検証
    (void)lottie_edit::parse(lottie_edit::dump(doc));

    lottie_edit::save(doc, output);
    std::cout << "wrote " << output << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
}
