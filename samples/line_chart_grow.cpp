/**
 * @file    samples/line_chart_grow.cpp
 * @brief   lottiepp::chart::plot を使った折れ線グラフ／棒グラフ生成サンプル。
 * @note    ENABLE_CHART=ON のときのみビルドされる。
 *
 * 実行例:
 * @code
 *   ./build/line_chart_grow out/chart.json            # 折れ線グラフ
 *   ./build/line_chart_grow out/chart.json bar         # 棒グラフ（下→上）
 *   ./build/line_chart_grow out/chart.json bar left    # 棒グラフ（左→右）
 * @endcode
 */

#include "chart.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <output.json> [line|bar] [left]\n";
    return 1;
  }

  using namespace lottiepp::chart;

  Series s1;
  s1.name = "sales";
  s1.color = "#2dd4bf";
  s1.showPoints = true;
  s1.fillArea = true;
  s1.grow = true;
  s1.data = {
      {0, 12.0}, {1, 28.0}, {2, 19.0}, {3, 41.0},
      {4, 33.0}, {5, 55.0}, {6, 47.0}, {7, 72.0},
  };

  Series s2;
  s2.name = "target";
  s2.color = "#f472b6";
  s2.dashArray = {12, 8};
  s2.showPoints = false;
  s2.grow = false;
  s2.data = {
      {0, 20.0}, {1, 24.0}, {2, 30.0}, {3, 36.0},
      {4, 42.0}, {5, 48.0}, {6, 54.0}, {7, 60.0},
  };

  ChartOptions opt;
  opt.duration = 2.0;
  opt.showGrid = true;
  opt.showLegend = true;
  opt.showXValues = true;

  const std::string mode = (argc > 2) ? argv[2] : "line";
  if (mode == "bar") {
    opt.chartType = ChartType::Bar;
    if (argc > 3 && std::string(argv[3]) == "left") {
      opt.barAnimation = BarAnimation::LeftToRight;
    }
  }

  auto doc_result = plot({s1, s2}, opt);
  if (!doc_result) {
    std::cerr << "error: " << doc_result.error() << "\n";
    return 1;
  }
  std::string save_err;
  if (!lottiepp::save(*doc_result, argv[1], save_err)) {
    std::cerr << "error: " << save_err << "\n";
    return 1;
  }
  std::cout << "wrote " << argv[1] << " (" << ((opt.chartType == ChartType::Bar) ? "bar" : "line") << ")\n";
  return 0;
}
