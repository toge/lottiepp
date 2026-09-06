/**
 * @file test/smoke.cpp
 * @brief 例外なしモードの検証。
 *
 * ライブラリが例外を送出せず、std::expected でエラーを返すことを確認する。
 * hosted / wasip1 の両方でビルドされる。
 */
#include "lottiepp.hpp"

using namespace lottiepp;

int main() {
  // Document parse/dump/parseJson/dumpJson の基本経路
  auto doc_result = parse(R"({"v":"5.7.4","fr":60,"ip":0,"op":60,"w":100,"h":50,"layers":[],"foo":1})");
  if (!doc_result) return 1;
  Document& doc = *doc_result;
  if (!doc.w || *doc.w != 100) return 2;
  if (!doc.extra.contains("foo")) return 3;
  auto out_result = dump(doc);
  if (!out_result) return 4;
  const std::string& out = *out_result;
  if (out.find("\"w\":100") == std::string::npos) return 5;

  // 数値ビルダー
  const json rect = makeRect(320.0, 240.0, 0.0);
  auto rect_out = dumpJson(rect);
  if (!rect_out) return 6;
  if (rect_out->find("320") == std::string::npos) return 7;

  // makeDocument + makeTrimPath
  Document made = makeDocument();
  if (!made.layers.empty()) return 8;
  const json trim = makeTrimPath(0.0, 50.0, 0.0, true);
  auto trim_out = dumpJson(trim);
  if (!trim_out) return 9;
  if (trim_out->find("\"e\"") == std::string::npos) return 10;

  // recolor / replaceText / setSpeed 経路が例外なしで動作すること
  auto d2_result = parse(R"({"v":"5.7.4","fr":60,"ip":0,"op":60,"w":512,"h":512,"layers":[{"ty":4,"nm":"Box","ip":0,"op":60,"shapes":[{"ty":"gr","it":[{"ty":"rc","d":1,"s":{"a":0,"k":[100,100]},"p":{"a":0,"k":[0,0]},"r":{"a":0,"k":0}},{"ty":"fl","c":{"a":0,"k":[1,0,0,1]},"o":{"a":0,"k":100}},{"ty":"tr","p":{"a":0,"k":[0,0]},"a":{"a":0,"k":[0,0]},"s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}]}]}]})");
  if (!d2_result) return 11;
  Document& d2 = *d2_result;
  auto recolor_result = recolor(d2, "", "#00ff00");
  if (!recolor_result) return 12;
  setSpeed(d2, 2.0);
  if (!d2.op || *d2.op != 120) return 13;

  // エラーハンドリングの検証：無効な JSON は std::unexpected を返す
  auto bad_result = parse("not valid json");
  if (bad_result) return 14;  // 失敗するべき

  // エラーハンドリングの検証：無効な色は std::unexpected を返す
  Document d3 = makeDocument();
  auto bad_recolor = recolor(d3, "", "not-a-color");
  if (bad_recolor) return 15;  // 失敗するべき

  return 0;
}
