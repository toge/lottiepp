// src/chart.cpp
//
// lottiepp::chart::plot の実装。
// ENABLE_CHART=ON のときのみコンパイルされる。

#include "chart.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lottiepp::chart {

namespace {

// --- 低レベル図形ヘルパ ---

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

// 中心点 (cx,cy) を基準とする円シェイプ（ty="el"）
json makeEllipseShape(double cx, double cy, double size) {
  return parseJson(
      "{\"ty\":\"el\",\"nm\":\"dot\",\"p\":{\"a\":0,\"k\":[" + std::to_string(cx) + "," +
      std::to_string(cy) + "]},\"s\":{\"a\":0,\"k\":[" + std::to_string(size) + "," +
      std::to_string(size) + "]}}");
}

// 点マーカー：el+fl を 1 つのグループにまとめる（flat な fl だと直前の線まで塗りつぶすため）
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

// 頂点配列から折れ線パス（開いたパス）を作る
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

// トリムパス：終端 e を 0%→100% でアニメーション（線が伸びる）
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

// 背景長方形レイヤ
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

// 軸＋目盛り＋ラベルを描くレイヤ群
std::vector<Layer> makeAxes(const ChartOptions& opt, double x0, double y0,
                            double spanX, double spanY, double op) {
  std::vector<Layer> layers;
  const double tick = 6.0;

  // 縦軸
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
  // 横軸
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

}  // namespace

Document plot(const std::vector<Series>& series, const ChartOptions& opt) {
  if (series.empty()) {
    throw std::invalid_argument("plot: at least one series required");
  }

  // y 範囲を全系列から決定
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

  const double W = static_cast<double>(opt.width);
  const double H = static_cast<double>(opt.height);
  const double x0 = opt.margin;
  const double y0 = H - opt.margin;
  const double spanX = W - opt.margin * 2.0;
  const double spanY = H - opt.margin * 2.0;

  // y 値 → ピクセル（下向き y なので引く）
  auto mapY = [&](double v) {
    const double norm = (v - minY) / (maxY - minY);
    return y0 - norm * spanY;
  };

  const double op = opt.duration * opt.frameRate;

  DocumentParams dp;
  dp.fr = opt.frameRate;
  dp.op = op;
  dp.w = opt.width;
  dp.h = opt.height;
  dp.name = "LineChart";
  auto doc = makeDocument(dp);

  // 軸
  for (auto& layer : makeAxes(opt, x0, y0, spanX, spanY, op)) {
    addLayer(doc, std::move(layer));
  }

  // 各系列
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
    p.items.push_back(makePolyline(xs, ys));
    p.items.push_back(makeStroke(s.color, 4.0, 100.0));
    if (s.grow) {
      p.items.push_back(makeGrowingTrim(op));
    }

    // 点マーカー
    if (s.showPoints) {
      for (std::size_t i = 0; i < n; ++i) {
        p.items.push_back(makeDotGroup(xs[i], ys[i], 8.0, s.color));
      }
    }

    addLayer(doc, makeShapeLayer(p));
  }

  // 背景は最後に追加（Lottie は配列の先頭が最前面なので、背景は最背面へ）
  addLayer(doc, makeBackground(opt, op));

  return doc;
}

}  // namespace lottiepp::chart
