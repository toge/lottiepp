// chart ヘルパ（src/chart.cpp、ENABLE_CHART=ON のときのみリンクされる）のテスト
#include "catch2/catch_all.hpp"
#include "lottiepp.hpp"

#ifdef ENABLE_CHART

#include "chart.hpp"

#include <cmath>

namespace {

// 生成ドキュメント内の全 "tm"（トリムパス）ノードを収集する
std::vector<lottiepp::json> collectTrimNodes(const lottiepp::Document& doc) {
  std::vector<lottiepp::json> out;
  for (const auto& layer : doc.layers) {
    if (!layer.shapes) {
      continue;
    }
    const auto& groups = (*layer.shapes).get_array();
    for (const auto& group : groups) {
      if (!group.contains("it")) {
        continue;
      }
      for (const auto& item : group["it"].get_array()) {
        if (item.contains("ty") && item["ty"].as<std::string>() == "tm") {
          out.push_back(item);
        }
      }
    }
  }
  return out;
}

}  // namespace

TEST_CASE("plot (grow) embeds a flat valid trim path") {
  // 過去バグ（makeTrimPath の生成 JSON の閉じブレース不足）と同種の
  // 文字列組み立て → parseJson 経路のリグレッション。
  // grow アニメーション付き plot が生成する tm ノードが
  // フラットかつ妥当な構造であることを確認する。
  using namespace lottiepp;

  chart::Series s;
  s.name = "line";
  s.data = {{0, 1.0}, {1, 2.0}, {2, 3.0}};
  s.grow = true;

  chart::ChartOptions opt;
  opt.frameRate = 60.0;
  opt.duration  = 2.0;  // op = 120 フレーム

  Document doc;
  REQUIRE_NOTHROW(doc = chart::plot({s}, opt));

  // grow 有効時は系列ごとに 1 つの tm ノードが埋め込まれる
  const auto trims = collectTrimNodes(doc);
  REQUIRE(trims.size() == 1);
  const json& trim = trims[0];

  // s / o は静的（a=0）でフラットに並ぶ（旧バグのように "e" が "s" 内に
  // ネストしていないこと）
  REQUIRE(trim["ty"].as<std::string>() == "tm");
  REQUIRE(trim["s"]["a"].as<int>() == 0);
  REQUIRE(trim["s"]["k"].as<double>() == Catch::Approx(0.0));
  REQUIRE(trim["o"]["a"].as<int>() == 0);
  REQUIRE(trim["o"]["k"].as<double>() == Catch::Approx(0.0));
  REQUIRE_FALSE(trim["s"].contains("e"));
  REQUIRE_FALSE(trim["e"].contains("o"));
  REQUIRE(trim["m"].as<int>() == 1);

  // e は 0%→100% の 2 キーフレームでアニメーションする
  REQUIRE(trim["e"]["a"].as<int>() == 1);
  const auto& kf = trim["e"]["k"].get_array();
  REQUIRE(kf.size() == 2);
  REQUIRE(kf[0]["t"].as<double>() == Catch::Approx(0.0));
  REQUIRE(kf[0]["s"][0].as<double>() == Catch::Approx(0.0));
  REQUIRE(kf[1]["t"].as<double>() == Catch::Approx(120.0));
  REQUIRE(kf[1]["s"][0].as<double>() == Catch::Approx(100.0));

  // 生成ノードはシリアライズ → 再解析のラウンドトリップを通ること
  REQUIRE_NOTHROW(lottiepp::parseJson(dumpJson(trim)));

  // ドキュメント全体もラウンドトリップを通ること
  REQUIRE_NOTHROW(parse(dump(doc)));
}

TEST_CASE("plot (no grow) has no trim path") {
  using namespace lottiepp;

  chart::Series s;
  s.name = "line";
  s.data = {{0, 1.0}, {1, 2.0}};
  s.grow = false;

  Document doc;
  REQUIRE_NOTHROW(doc = chart::plot({s}, chart::ChartOptions{}));
  REQUIRE(collectTrimNodes(doc).empty());
}

#endif  // ENABLE_CHART
