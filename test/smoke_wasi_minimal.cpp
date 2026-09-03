/**
 * @file test/smoke_wasi_minimal.cpp
 * @brief LOTTIEPP_WASI_MINIMAL モードの検証。
 *
 * -fno-exceptions 付きでビルドされる。LOTTIEPP_THROW を使う全モジュール
 * が例外なしでコンパイル・実行できることを確認する。
 * wasip1 では iostream/fstream は WASI 経由で利用可能なため制限しない。
 */
#include "lottiepp.hpp"

using namespace lottiepp;

int main() {
  // Document parse/dump/parseJson/dumpJson の基本経路
  Document doc = parse(R"({"v":"5.7.4","fr":60,"ip":0,"op":60,"w":100,"h":50,"layers":[],"foo":1})");
  if (!doc.w || *doc.w != 100) return 1;
  if (!doc.extra.contains("foo")) return 2;
  const std::string out = dump(doc);
  if (out.find("\"w\":100") == std::string::npos) return 3;

  // 数値ビルダー
  const json rect = makeRect(320.0, 240.0, 0.0);
  if (dumpJson(rect).find("320") == std::string::npos) return 4;

  // makeDocument + makeTrimPath
  Document made = makeDocument();
  if (!made.layers.empty()) return 5;
  const json trim = makeTrimPath(0.0, 50.0, 0.0, true);
  if (dumpJson(trim).find("\"e\"") == std::string::npos) return 6;

  // recolor / replaceText / setSpeed 経路が例外なしで動作すること
  Document d2 = parse(R"({"v":"5.7.4","fr":60,"ip":0,"op":60,"w":512,"h":512,"layers":[{"ty":4,"nm":"Box","ip":0,"op":60,"shapes":[{"ty":"gr","it":[{"ty":"rc","d":1,"s":{"a":0,"k":[100,100]},"p":{"a":0,"k":[0,0]},"r":{"a":0,"k":0}},{"ty":"fl","c":{"a":0,"k":[1,0,0,1]},"o":{"a":0,"k":100}},{"ty":"tr","p":{"a":0,"k":[0,0]},"a":{"a":0,"k":[0,0]},"s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}]}]}]})");
  recolor(d2, "", "#00ff00");
  setSpeed(d2, 2.0);
  if (!d2.op || *d2.op != 120) return 7;

  return 0;
}
