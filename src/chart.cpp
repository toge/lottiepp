/**
 * @file    chart.cpp
 * @brief   lottiepp::chart::plot の実装。
 * @note    ENABLE_CHART=ON のときのみコンパイルされる。
 */

#include "chart.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lottiepp::chart {

namespace {

// ---------------------------------------------------------------------------
// 低レベル図形ヘルパ
// ---------------------------------------------------------------------------

/**
 * @brief 2 点間の直線シェイプ（ty="sh"）を生成する
 * @param x1, y1  始点
 * @param x2, y2  終点
 * @return        開いた線分の JSON ノード
 */
json makeLinePath(double x1, double y1, double x2, double y2) {
  json node = parseJson("{\"ty\":\"sh\",\"nm\":\"line\",\"ks\":{\"a\":0,\"k\":{}}}");
  node["ks"]["k"] = parseJson("{\"i\":[],\"o\":[],\"v\":[],\"c\":false}");
  auto push = [&](double x, double y) {
    node["ks"]["k"]["v"].get_array().push_back(parseJson("[" + std::to_string(x) + "," + std::to_string(y) + "]"));
    node["ks"]["k"]["i"].get_array().push_back(parseJson("[0,0]"));
    node["ks"]["k"]["o"].get_array().push_back(parseJson("[0,0]"));
  };
  push(x1, y1);
  push(x2, y2);
  return node;
}

/**
 * @brief 点マーカー用の円シェイプ（ty="el"）を生成する
 * @param cx, cy  中心座標
 * @param size    直径（ピクセル）
 * @return        円の JSON ノード
 */
json makeEllipseShape(double cx, double cy, double size) {
  return parseJson(
      "{\"ty\":\"el\",\"nm\":\"dot\",\"p\":{\"a\":0,\"k\":[" + std::to_string(cx) + "," +
      std::to_string(cy) + "]},\"s\":{\"a\":0,\"k\":[" + std::to_string(size) + "," +
      std::to_string(size) + "]}}");
}

/**
 * @brief 点マーカー（円＋塗り）をグループ化する
 * @details el+fl を 1 つの gr にまとめないと、flat な fl が
 *          グループ内の先行シェイプ（ポリライン）を塗りつぶしてしまう。
 * @return  gr（グループ）の JSON ノード
 */
json makeDotGroup(double cx, double cy, double size, std::string_view colorHex) {
  json g = parseJson("{\"ty\":\"gr\",\"nm\":\"dot\",\"it\":[]}");
  g["it"].get_array().push_back(makeEllipseShape(cx, cy, size));
  g["it"].get_array().push_back(makeFill(colorHex, 100.0));
  g["it"].get_array().push_back(
      parseJson("{\"ty\":\"tr\",\"p\":{\"a\":0,\"k\":[0,0]},\"a\":{\"a\":0,\"k\":[0,0]},"
                "\"s\":{\"a\":0,\"k\":[100,100]},\"r\":{\"a\":0,\"k\":0},"
                "\"o\":{\"a\":0,\"k\":100}}"));
  return g;
}

/**
 * @brief テキストレイヤ（ty=5）を生成する
 * @param text     表示文字列
 * @param x, y     位置
 * @param size     フォントサイズ
 * @param colorHex 色
 * @param op       アウト点（総フレーム数）
 * @param name     レイヤ名
 * @return         ty=5 の Layer
 */
Layer makeTextLayer(std::string_view text, double x, double y, double size,
                    std::string_view colorHex, double op, const std::string& name) {
  Layer l;
  l.ty = 5;
  l.nm = name;
  l.ip = 0.0;
  l.op = op;
  l.st = 0.0;

  Transform ks;
  ks.o = staticProp(100.0);
  ks.r = staticProp(0.0);
  ks.p = staticProp("[" + std::to_string(x) + "," + std::to_string(y) + ",0]");
  ks.a = staticProp("[0,0,0]");
  ks.s = staticProp("[100,100,100]");
  l.ks = ks;

  auto c = parseHexColor(colorHex);
  const std::string col = c ? ("[" + std::to_string(c->r) + "," + std::to_string(c->g) + "," +
                               std::to_string(c->b) + ",1]")
                            : "[1,1,1,1]";

  json doc = parseJson(
      "{\"s\":{\"s\":" + std::to_string(size) +
      ",\"f\":\"sans\",\"t\":\"" + std::string(text) +
      "\",\"fc\":" + col +
      ",\"j\":2,\"tr\":0,\"lh\":" + std::to_string(size * 1.2) +
      ",\"ls\":0,\"fc\":" + col + "}}");
  Text t;
  TextData td;
  td.k = doc;
  t.d = td;
  l.t = t;

  return l;
}

/**
 * @brief エリアチャート用の閉じた塗りパスを生成する
 * @details 系列線の下を塗り潰すため、底辺 y0 → 全データ点 → 底辺で閉じる
 * @param xs, ys  データ点の座標
 * @param y0      グラフ下端 y 座標
 * @return        閉じたパス（c=true）の JSON ノード
 */
json makeAreaPath(const std::vector<double>& xs, const std::vector<double>& ys,
                  double y0) {
  json node = parseJson(
      "{\"ty\":\"sh\",\"nm\":\"area\",\"ks\":{\"a\":0,\"k\":{\"i\":[],\"o\":[],\"v\":[],\"c\":true}}}");
  auto push = [&](double x, double y) {
    node["ks"]["k"]["v"].get_array().push_back(parseJson("[" + std::to_string(x) + "," + std::to_string(y) + "]"));
    node["ks"]["k"]["i"].get_array().push_back(parseJson("[0,0]"));
    node["ks"]["k"]["o"].get_array().push_back(parseJson("[0,0]"));
  };
  push(xs[0], y0);
  for (std::size_t k = 0; k < xs.size(); ++k) push(xs[k], ys[k]);
  push(xs.back(), y0);
  return node;
}

/**
 * @brief 複数頂点から折れ線パス（開いたパス）を生成する
 * @param xs  x 座標配列
 * @param ys  y 座標配列
 * @return    sh（シェイプ）の JSON ノード、c=false（開パス）
 */
json makePolyline(const std::vector<double>& xs, const std::vector<double>& ys) {
  json node = parseJson("{\"ty\":\"sh\",\"nm\":\"line\",\"ks\":{\"a\":0,\"k\":{}}}");
  node["ks"]["k"] = parseJson("{\"i\":[],\"o\":[],\"v\":[],\"c\":false}");
  for (std::size_t i = 0; i < xs.size(); ++i) {
    node["ks"]["k"]["v"].get_array().push_back(
        parseJson("[" + std::to_string(xs[i]) + "," + std::to_string(ys[i]) + "]"));
    node["ks"]["k"]["i"].get_array().push_back(parseJson("[0,0]"));
    node["ks"]["k"]["o"].get_array().push_back(parseJson("[0,0]"));
  }
  return node;
}

/**
 * @brief   grow アニメーション用のトリムパス（ty="tm"）を生成する
 * @details 終端 e を 0%→100% にアニメーションすることで、線が左から右へ
 *          伸びる効果を実現する。開始 s は常に 0%、オフセット o は 0。
 * @param   op  アウト点（総フレーム数）
 * @return      tm（トリム）の JSON ノード
 */
json makeGrowingTrim(double op) {
  const std::string kf =
      "[{\"i\":{\"x\":[0.4],\"y\":[1]},\"o\":{\"x\":[0.6],\"y\":[0]},\"t\":0,\"s\":[0]},"
      "{\"t\":" +
      std::to_string(op) + ",\"s\":[100]}]";
  return parseJson("{\"ty\":\"tm\","
                   "\"s\":{\"a\":0,\"k\":0},"
                   "\"e\":{\"a\":1,\"k\":" +
                   kf +
                   "},"
                   "\"o\":{\"a\":0,\"k\":0},"
                   "\"m\":1}");
}

/**
 * @brief 背景塗りつぶし用の長方形レイヤを生成する
 * @param opt  グラフオプション（幅・高さ・背景色を参照）
 * @param op   アウト点（総フレーム数）
 * @return     ty=4（シェイプレイヤ）の Layer
 */
Layer makeBackground(const ChartOptions& opt, double op) {
  ShapeLayerParams p;
  p.name = "background";
  p.x = opt.width / 2.0;
  p.y = opt.height / 2.0;
  p.from = 0.0;
  p.to = op;
  p.items.push_back(makeRect(opt.width, opt.height));
  p.items.push_back(makeFill(opt.bgColor, 100.0));
  return makeShapeLayer(p);
}

/**
 * @brief 軸＋目盛りを描くレイヤ群を生成する
 * @param opt          グラフオプション（色・目盛り段数を参照）
 * @param x0, y0       原点座標（左下）
 * @param spanX, spanY 軸の長さ
 * @param op           アウト点（総フレーム数）
 * @return             縦軸・横軸それぞれ 1 件ずつの Layer
 */
std::vector<Layer> makeAxes(const ChartOptions& opt, double x0, double y0,
                            double spanX, double spanY, double op) {
  std::vector<Layer> layers;
  const double tick = 6.0;

  // 縦軸（左端）：線 ＋ 目盛り線
  {
    ShapeLayerParams p;
    p.name = "YAxis";
    p.from = 0.0;
    p.to = op;
    p.items.push_back(makeLinePath(x0, y0, x0, y0 - spanY));
    p.items.push_back(makeStroke(opt.axisColor, 2.0));
    for (int i = 0; i <= opt.yTicks; ++i) {
      const double py = y0 - (spanY * i) / opt.yTicks;
      p.items.push_back(makeLinePath(x0, py, x0 - tick, py));
      p.items.push_back(makeStroke(opt.axisColor, 2.0));
    }
    layers.push_back(makeShapeLayer(p));
  }
  // 横軸（下端）：線 ＋ 目盛り線
  {
    ShapeLayerParams p;
    p.name = "XAxis";
    p.from = 0.0;
    p.to = op;
    p.items.push_back(makeLinePath(x0, y0, x0 + spanX, y0));
    p.items.push_back(makeStroke(opt.axisColor, 2.0));
    const int xTicks = 8;
    for (int i = 0; i <= xTicks; ++i) {
      const double px = x0 + (spanX * i) / xTicks;
      p.items.push_back(makeLinePath(px, y0, px, y0 + tick));
      p.items.push_back(makeStroke(opt.axisColor, 2.0));
    }
    layers.push_back(makeShapeLayer(p));
  }

  return layers;
}

/**
 * @brief グリッド線のレイヤ群を生成する
 * @param opt          グラフオプション（色・目盛り段数を参照）
 * @param x0, y0       原点座標（左下）
 * @param spanX, spanY 軸の長さ
 * @param op           アウト点（総フレーム数）
 * @return             横グリッド・縦グリッドの 2 レイヤ
 */
std::vector<Layer> makeGrid(const ChartOptions& opt, double x0, double y0,
                            double spanX, double spanY, double op) {
  std::vector<Layer> layers;
  const std::string col = opt.gridColor.empty() ? opt.axisColor : opt.gridColor;

  {
    ShapeLayerParams p;
    p.name = "HGrid";
    p.from = 0.0;
    p.to = op;
    for (int i = 0; i <= opt.yTicks; ++i) {
      const double py = y0 - (spanY * i) / opt.yTicks;
      p.items.push_back(makeLinePath(x0, py, x0 + spanX, py));
      p.items.push_back(makeStroke(col, 1.0, 30.0));
    }
    layers.push_back(makeShapeLayer(p));
  }
  {
    ShapeLayerParams p;
    p.name = "VGrid";
    p.from = 0.0;
    p.to = op;
    const int xTicks = 8;
    for (int i = 0; i <= xTicks; ++i) {
      const double px = x0 + (spanX * i) / xTicks;
      p.items.push_back(makeLinePath(px, y0, px, y0 - spanY));
      p.items.push_back(makeStroke(col, 1.0, 30.0));
    }
    layers.push_back(makeShapeLayer(p));
  }
  return layers;
}

}  // namespace

// ---------------------------------------------------------------------------
// 公開 API
// ---------------------------------------------------------------------------

/**
 * @brief 系列群から折れ線グラフの Lottie ドキュメントを生成する
 * @param series  1 つ以上の系列（各系列は 2 点以上）
 * @param opt     描画オプション
 * @return        生成された Document
 */
Document plot(const std::vector<Series>& series, const ChartOptions& opt) {
  if (series.empty()) {
    throw std::invalid_argument("plot: at least one series required");
  }

  // 全系列から Y の最小値・最大値を決定し、正規化の範囲とする
  double minY = std::get<1>(series[0].data[0]);
  double maxY = minY;
  for (const auto& s : series) {
    if (s.data.size() < 2) {
      throw std::invalid_argument("plot: each series needs >= 2 points");
    }
    for (const auto& d : s.data) {
      minY = std::min(minY, std::get<1>(d));
      maxY = std::max(maxY, std::get<1>(d));
    }
  }
  if (minY == maxY) {
    maxY = minY + 1.0;
  }
  if (!std::isnan(opt.minY)) minY = opt.minY;
  if (!std::isnan(opt.maxY)) maxY = opt.maxY;

  // レイアウト座標計算
  const double W = static_cast<double>(opt.width);
  const double H = static_cast<double>(opt.height);
  const double x0 = opt.margin;
  const double y0 = H - opt.margin;
  const double spanX = W - opt.margin * 2.0;
  const double spanY = H - opt.margin * 2.0;

  // Y 値 → ピクセル変換（canvas は Y 下向きなので上端からの距離で計算）
  auto mapY = [&](double v) {
    const double norm = (v - minY) / (maxY - minY);
    return y0 - norm * spanY;
  };

  const double op = opt.duration * opt.frameRate;

  // 空のドキュメントを生成
  DocumentParams dp;
  dp.fr = opt.frameRate;
  dp.op = op;
  dp.w = opt.width;
  dp.h = opt.height;
  dp.name = "LineChart";
  auto doc = makeDocument(dp);

  // --- 各レイヤをあらかじめ計算しておく ---
  auto axesLayers = makeAxes(opt, x0, y0, spanX, spanY, op);

  struct SerLayer { std::vector<double> xs; };
  std::vector<Layer> serLayers;
  for (const auto& s : series) {
    const std::size_t n = s.data.size();
    std::vector<double> xs(n), ys(n);
    for (std::size_t i = 0; i < n; ++i) {
      xs[i] = (n == 1) ? x0 : x0 + (spanX * static_cast<double>(i)) / (n - 1);
      ys[i] = mapY(std::get<1>(s.data[i]));
    }

    ShapeLayerParams p;
    p.name = s.name;
    p.from = 0.0;
    p.to = op;
    if (s.fillArea) {
      p.items.push_back(makeAreaPath(xs, ys, y0));
      p.items.push_back(makeFill(s.color, 20.0));
    }
    p.items.push_back(makePolyline(xs, ys));
    {
      auto stroke = makeStroke(s.color, 4.0, 100.0);
      if (s.dashArray.size() >= 2) {
        json arr = parseJson("[]");
        arr.get_array().push_back(parseJson(
            "{\"n\":\"d 0\",\"ty\":\"d\",\"v\":{\"a\":0,\"k\":" +
            std::to_string(s.dashArray[0]) + "}}"));
        arr.get_array().push_back(parseJson(
            "{\"n\":\"g 0\",\"ty\":\"g\",\"v\":{\"a\":0,\"k\":" +
            std::to_string(s.dashArray[1]) + "}}"));
        if (s.dashArray.size() >= 3) {
          arr.get_array().push_back(parseJson(
              "{\"n\":\"o 0\",\"ty\":\"o\",\"v\":{\"a\":0,\"k\":" +
              std::to_string(s.dashArray[2]) + "}}"));
        }
        stroke["d"] = arr;
      }
      p.items.push_back(std::move(stroke));
    }
    if (s.grow) {
      p.items.push_back(makeGrowingTrim(op));
    }
    if (s.showPoints) {
      for (std::size_t i = 0; i < n; ++i) {
        p.items.push_back(makeDotGroup(xs[i], ys[i], 8.0, s.color));
      }
    }
    serLayers.push_back(makeShapeLayer(p));
  }

  // 凡例テキスト＋色見本
  std::vector<Layer> legendLayers;
  if (opt.showLegend) {
    double lx = W - opt.margin * 1.5 - 160.0;
    double ly = opt.margin + 10.0;
    for (const auto& s : series) {
      ShapeLayerParams lp;
      lp.name = "swatch_" + s.name;
      lp.from = 0.0;
      lp.to = op;
      lp.x = lx + 7.0;
      lp.y = ly + 7.0;
      lp.items.push_back(makeRect(14.0, 14.0));
      lp.items.push_back(makeFill(s.color, 100.0));
      legendLayers.push_back(makeShapeLayer(lp));
      legendLayers.push_back(makeTextLayer(s.name, lx + 24.0, ly + 2.0, 13.0,
                                           "#e2e8f0", op, "legend_" + s.name));
      ly += 20.0;
    }
  }

  // X 軸ラベル
  std::vector<Layer> xlabelLayers;
  if (opt.showXValues && !series.empty()) {
    const auto& first = series[0];
    const auto n = first.data.size();
    for (std::size_t i = 0; i < n; ++i) {
      const double px = (n == 1) ? x0 : x0 + (spanX * static_cast<double>(i)) / (n - 1);
      xlabelLayers.push_back(makeTextLayer(
          std::to_string(std::get<0>(first.data[i])),
          px, y0 + 20.0, 12.0, opt.axisColor, op, "XLabel" + std::to_string(i)));
    }
  }

  // --- addLayer: 先に追加したものほど前面（先頭） ---
  // 1. 凡例（最前面）
  for (auto& layer : legendLayers) addLayer(doc, std::move(layer));
  // 2. X ラベル
  for (auto& layer : xlabelLayers) addLayer(doc, std::move(layer));
  // 3. 軸
  for (auto& layer : axesLayers) addLayer(doc, std::move(layer));
  // 4. 系列（折れ線）
  for (auto& layer : serLayers) addLayer(doc, std::move(layer));
  // 5. グリッド（系列の後ろ）
  if (opt.showGrid) {
    for (auto& layer : makeGrid(opt, x0, y0, spanX, spanY, op)) {
      addLayer(doc, std::move(layer));
    }
  }
  // 6. 背景（最背面）
  addLayer(doc, makeBackground(opt, op));

  return doc;
}

}  // namespace lottiepp::chart
