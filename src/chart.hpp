// src/chart.hpp
//
// 簡易折れ線グラフ（plot）生成ヘルパ。
// std::vector<std::tuple<int,double>> を系列として受け取り、
// 軸・目盛り・ラベル・点マーカー・複数系列・伸びるアニメーション付きの
// Lottie ドキュメントを生成する。
//
// ENABLE_CHART=ON のときのみコンパイルされる。

#pragma once

#include <lottiepp.hpp>

#include <string>
#include <tuple>
#include <vector>

namespace lottiepp::chart {

using Point = std::tuple<int, double>;  // (x: 任意ラベル値, y: 値)

/// 1 つの系列を表す
struct Series {
  std::string name = "series";        // 系列名（凡例・レイヤ名に使用）
  std::vector<Point> data;            // データ点
  std::string color = "#2dd4bf";     // 線の色 (#rrggbb)
  bool showPoints = false;            // 各点にマーカーを描くか
  bool grow = true;                   // 線を左→右へ伸ばすアニメーション
};

/// グラフ全体の描画オプション
struct ChartOptions {
  int    width      = 512;
  int    height     = 512;
  double margin     = 56.0;          // 軸周りの余白
  double frameRate  = 60.0;
  double duration   = 2.0;           // 秒（grow アニメーションの長さ）
  std::string bgColor = "#0f172a";   // 背景色 (#rrggbb)
  std::string axisColor = "#64748b"; // 軸・目盛りの色
  int yTicks = 4;                    // 縦軸の目盛り段数
};

/// @brief 系列群から折れ線グラフの Lottie ドキュメントを生成する
/// @param series 1 つ以上の系列
/// @param opt 描画オプション
/// @return 生成された Document（save / dump で出力可能）
Document plot(const std::vector<Series>& series, const ChartOptions& opt = {});

}  // namespace lottiepp::chart
