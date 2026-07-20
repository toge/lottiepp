// samples/line_chart_grow.cpp
//
// lottiepp::chart::plot を使った折れ線グラフ生成サンプル。
// ENABLE_CHART=ON のときのみビルドされる。
//
// 実行:
//   ./out/line_chart_grow out/line_chart.json

#include "chart.hpp"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <output.json>\n";
    return 1;
  }

  using namespace lottiepp::chart;

  // 系列 1: サンプルデータ（右へ伸びるアニメーション + 点マーカー）
  Series s1;
  s1.name = "sales";
  s1.color = "#2dd4bf";
  s1.showPoints = true;
  s1.grow = true;
  s1.data = {
      {0, 12.0}, {1, 28.0}, {2, 19.0}, {3, 41.0},
      {4, 33.0}, {5, 55.0}, {6, 47.0}, {7, 72.0},
  };

  // 系列 2: 比較用（別色、アニメーションなし）
  Series s2;
  s2.name = "target";
  s2.color = "#f472b6";
  s2.showPoints = false;
  s2.grow = false;
  s2.data = {
      {0, 20.0}, {1, 24.0}, {2, 30.0}, {3, 36.0},
      {4, 42.0}, {5, 48.0}, {6, 54.0}, {7, 60.0},
  };

  ChartOptions opt;
  opt.duration = 2.0;  // grow アニメーション 2 秒

  auto doc = plot({s1, s2}, opt);
  lottiepp::save(doc, argv[1]);
  std::cout << "wrote " << argv[1] << "\n";
  return 0;
}
