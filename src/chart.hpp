/**
 * @file    chart.hpp
 * @brief   簡易折れ線グラフ（plot）生成ヘルパ。
 *          std::vector<std::tuple<int,double>> を系列として受け取り、
 *          軸・目盛り・点マーカー・複数系列・伸びるアニメーション付きの
 *          Lottie ドキュメントを生成する。
 * @note    ENABLE_CHART=ON のときのみコンパイルされる。
 */

#pragma once

#include <lottiepp.hpp>

#include <cmath>
#include <string>
#include <tuple>
#include <vector>

namespace lottiepp::chart {

/** @brief グラフ上の 1 点を表す型 */
using Point = std::tuple<int, double>;

/**
 * @brief 1 つの系列を表す構造体
 * @details 系列ごとに名前・色・点マーカーの有無・grow アニメーションの
 *          有効/無効を設定できる。
 */
struct Series {
  std::string name = "series";        ///< 系列名（凡例・レイヤ名に使用）
  std::vector<Point> data;            ///< データ点の配列
  std::string color = "#2dd4bf";      ///< 線の色 (#rrggbb)
  std::vector<double> dashArray = {}; ///< 破線パターン {dash, gap, ...}。空なら実線
  bool showPoints = false;            ///< 各点にマーカーを描くか
  bool fillArea = false;              ///< 線の下を半透明で塗り潰す（エリアチャート）
  bool grow = true;                   ///< 線を左→右へ伸ばすアニメーション
};

/**
 * @brief グラフ全体の描画オプション
 * @details キャンバスサイズ、余白、フレームレート、配色などを指定する。
 *          grow アニメーションは duration（秒）と frameRate から総フレーム数を計算する。
 */
struct ChartOptions {
  int    width      = 512;            ///< キャンバス幅（ピクセル）
  int    height     = 512;            ///< キャンバス高さ（ピクセル）
  double margin     = 56.0;           ///< 軸周りの余白（ピクセル）
  double frameRate  = 60.0;           ///< フレームレート（fps）
  double duration   = 2.0;            ///< grow アニメーションの長さ（秒）
  std::string bgColor = "#0f172a";    ///< 背景色 (#rrggbb)
  std::string axisColor = "#64748b";  ///< 軸・目盛りの色
  int yTicks = 4;                     ///< 縦軸の目盛り段数
  // 軸範囲（NaN=自動）
  double minY = NAN;                  ///< Y 最小値（NaN の場合はデータから自動計算）
  double maxY = NAN;                  ///< Y 最大値（NaN の場合はデータから自動計算）
  // グリッド線
  bool showGrid = false;              ///< グリッド線を表示
  std::string gridColor = "";         ///< グリッド線の色（空文字=axisColor の半透明）
  // 凡例・ラベル
  bool showLegend = false;            ///< 凡例を表示
  bool showXValues = false;           ///< 横軸の数値ラベルを表示
};

/**
 * @brief 系列群から折れ線グラフの Lottie ドキュメントを生成する
 * @param series  1 つ以上の系列
 * @param opt     描画オプション（デフォルト値あり）
 * @return        生成された Document（save / dump で出力可能）
 */
Document plot(const std::vector<Series>& series, const ChartOptions& opt = {});

}  // namespace lottiepp::chart
