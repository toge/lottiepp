// FREESTANDING ビルド（wasm32-unknown-unknown + -nostdlib）の動作確認スモークテスト。
// 戻り値 0 = 成功、0 以外 = 失敗した検査の識別子。
#include "lottiepp.hpp"

extern "C" int run() {
  using namespace lottiepp;

  // 1) JSON パース（型付き Document モデル + ExtraMap 経路）
  Document doc = parse(R"({"v":"5.7.4","fr":60,"ip":0,"op":60,"w":100,"h":50,"layers":[],"foo":1})");
  if (!doc.w || *doc.w != 100) {
    return 1;
  }
  if (!doc.extra.contains("foo")) {
    return 2;
  }

  // 2) dump（シリアライズ経路）
  const std::string out = dump(doc);
  if (out.find("\"w\":100") == std::string::npos) {
    return 3;
  }

  // 3) 数値ビルダー（detail::to_string の double / int 経路）
  const json        rect    = makeRect(320.0, 240.0, 0.0);
  const std::string rectStr = dumpJson(rect);
  if (rectStr.find("320") == std::string::npos) {
    return 4;
  }

  // 4) 動的 json ノードのパースと値アクセス
  const json j = parseJson(R"({"a":0,"k":2.5})");
  if (!(j["k"].as<double>() == 2.5)) {
    return 5;
  }

  // 5) makeDocument + makeTrimPath（数値文字列組み立て経路）
  Document made = makeDocument();
  if (!made.layers.empty()) {
    return 6;
  }
  const json trim = makeTrimPath(0.0, 50.0, 0.0, true);
  if (dumpJson(trim).find("\"e\"") == std::string::npos) {
    return 7;
  }

  return 0;
}
