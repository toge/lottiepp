#include "catch2/catch_all.hpp"
#include "lottiepp.hpp"

TEST_CASE("parseHexColor") {
  auto c = lottiepp::parseHexColor("#ff0000");
  REQUIRE(c);
  REQUIRE(c->r == Catch::Approx(1.0f));
  REQUIRE(c->g == Catch::Approx(0.0f));
  REQUIRE(c->b == Catch::Approx(0.0f));

  auto shortc = lottiepp::parseHexColor("#0f0");
  REQUIRE(shortc);
  REQUIRE(shortc->g == Catch::Approx(1.0f));
}

TEST_CASE("recolor static fill") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 0,
      "shapes": [{
        "ty": "fl",
        "c": {"a": 0, "k": [1, 0, 0, 1]}
      }]
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  auto n = lottiepp::recolor(doc, "#ff0000", "#00ff00");
  REQUIRE(n.has_value());
  REQUIRE(*n == 1);
  REQUIRE(doc.layers.size() == 1);
  REQUIRE(doc.layers[0].shapes);
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][0].as<float>() == Catch::Approx(0.0f));
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][1].as<float>() == Catch::Approx(1.0f));
}

TEST_CASE("replaceText") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 5, "nm": "Title", "ip": 0, "op": 60, "st": 0,
      "t": {"d": {"k": [{"s": {"t": "Hello"}, "t": 0}]}}
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  REQUIRE(lottiepp::replaceText(doc, "Title", "World"));
  REQUIRE(doc.layers[0].t);
  REQUIRE((*doc.layers[0].t->d->k)[0]["s"]["t"].as<std::string>() == "World");
  REQUIRE_FALSE(lottiepp::replaceText(doc, "Missing", "X"));
}

TEST_CASE("setSpeed scales timing") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 10,
      "ks": {"o": {"a": 1, "k": [{"t": 0, "s": [100]}, {"t": 60, "s": [0]}]}}
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  const auto n = lottiepp::setSpeed(doc, 2.0);
  REQUIRE(n >= 3);
  REQUIRE(doc.op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].st.value() == Catch::Approx(20.0));
  REQUIRE((*doc.layers[0].ks->o)["k"][1]["t"].as<double>() == Catch::Approx(120.0));
}

TEST_CASE("unknown fields preserved") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 30, "ip": 0, "op": 30, "w": 10, "h": 10,
    "ddd": 0,
    "meta": {"g": "test"},
    "layers": [{
      "ty": 4, "nm": "S", "ip": 0, "op": 30, "st": 0,
      "ind": 1,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [0, 0, 0, 1]}}]
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  REQUIRE(doc.extra.contains("ddd"));
  REQUIRE(doc.extra.contains("meta"));
  REQUIRE(doc.layers[0].extra.contains("ind"));

  auto text_result = lottiepp::dump(doc);
  REQUIRE(text_result.has_value());
  auto again = lottiepp::parse(*text_result);
  REQUIRE(again.has_value());
  REQUIRE(again->extra.contains("ddd"));
  REQUIRE(again->extra.contains("meta"));
  REQUIRE(again->layers[0].extra.contains("ind"));
}

TEST_CASE("generateVariations") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 30, "ip": 0, "op": 30, "w": 10, "h": 10,
    "layers": [{
      "ty": 4, "nm": "S", "ip": 0, "op": 30, "st": 0,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [0, 0, 0, 1]}}]
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  std::vector<lottiepp::VariationParams> params(2);
  params[0].recolor_to = "#ff0000";
  params[1].recolor_to = "#0000ff";
  params[1].speed     = 2.0;

  auto outs = lottiepp::generateVariations(doc, params);
  REQUIRE(outs.size() == 2);
  REQUIRE((*outs[0].layers[0].shapes)[0]["c"]["k"][0].as<float>() == Catch::Approx(1.0f));
  REQUIRE(outs[1].op.value() == Catch::Approx(60.0));
}

TEST_CASE("addShapeLayer builds a valid layer") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 200, "h": 200, "layers": []
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  lottiepp::ShapeLayerParams p;
  p.name = "Box";
  p.x    = 50;
  p.y    = 40;
  p.from = 0;
  p.to   = 60;
  p.items.push_back(lottiepp::makeRect(80, 40));
  p.items.push_back(lottiepp::makeFill("#00ff00"));

  lottiepp::addLayer(doc, lottiepp::makeShapeLayer(p));
  REQUIRE(doc.layers.size() == 1);

  const auto& layer = doc.layers[0];
  REQUIRE(layer.ty == 4);
  REQUIRE((layer.nm && *layer.nm == "Box"));
  REQUIRE(layer.extra.contains("ind"));
  REQUIRE(layer.extra.at("ind").as<int>() == 1);

  // ラウンドトリップ後にシェイプが保持されること
  auto dump_result = lottiepp::dump(doc);
  REQUIRE(dump_result.has_value());
  auto again = lottiepp::parse(*dump_result);
  REQUIRE(again.has_value());
  REQUIRE(again->layers.size() == 1);
  const auto& shapes = *again->layers[0].shapes;
  REQUIRE(shapes[0]["ty"].as<std::string>() == "gr");
  REQUIRE(shapes[0]["it"][0]["ty"].as<std::string>() == "rc");
  REQUIRE(shapes[0]["it"][1]["ty"].as<std::string>() == "fl");
  REQUIRE(shapes[0]["it"][1]["c"]["k"][1].as<float>() == Catch::Approx(1.0f));
  // レイヤ位置の反映
  REQUIRE((*again->layers[0].ks->p)["k"][0].as<double>() == Catch::Approx(50.0));
}

TEST_CASE("addEffect appends to ef") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 200, "h": 200,
    "layers": [{"ty": 4, "nm": "Target", "ip": 0, "op": 60, "st": 0, "ind": 1, "shapes": []}]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  auto* layer = lottiepp::findLayer(doc, "Target");
  REQUIRE(layer != nullptr);
  lottiepp::addEffect(*layer, lottiepp::makeGaussianBlur(12.0));

  auto dump_result = lottiepp::dump(doc);
  REQUIRE(dump_result.has_value());
  auto again = lottiepp::parse(*dump_result);
  REQUIRE(again.has_value());
  auto* l2   = lottiepp::findLayer(*again, "Target");
  REQUIRE(l2 != nullptr);
  REQUIRE(l2->extra.contains("ef"));
  REQUIRE(l2->extra["ef"].is_array());
  REQUIRE(l2->extra["ef"].get_array().size() == 1);
  REQUIRE(l2->extra["ef"][0]["nm"].as<std::string>() == "Gaussian Blur");
  REQUIRE(l2->extra["ef"][0]["ef"][1]["v"]["k"].as<double>() == Catch::Approx(12.0));
}

TEST_CASE("removeLayer removes by name") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [
      {"ty": 4, "nm": "Keep", "ip": 0, "op": 60, "st": 0, "shapes": []},
      {"ty": 4, "nm": "Drop", "ip": 0, "op": 60, "st": 0, "shapes": []}
    ],
    "assets": [{
      "id": "pre",
      "layers": [{"ty": 4, "nm": "Drop", "ip": 0, "op": 60, "st": 0, "shapes": []}]
    }]
  })");
  REQUIRE(doc_result.has_value());
  auto& doc = *doc_result;

  REQUIRE(lottiepp::removeLayer(doc, "Drop"));
  REQUIRE(doc.layers.size() == 1);
  REQUIRE((doc.layers[0].nm && *doc.layers[0].nm == "Keep"));
  REQUIRE((doc.assets && doc.assets->size() == 1));
  REQUIRE(doc.assets->at(0).layers);
  REQUIRE(doc.assets->at(0).layers->empty());

  // ラウンドトリップ後も削除が維持される
  auto dump_result = lottiepp::dump(doc);
  REQUIRE(dump_result.has_value());
  auto again = lottiepp::parse(*dump_result);
  REQUIRE(again.has_value());
  REQUIRE(again->layers.size() == 1);
  REQUIRE_FALSE(lottiepp::removeLayer(*again, "Missing"));
}

TEST_CASE("makeDocument creates empty valid doc") {
  auto doc = lottiepp::makeDocument();
  REQUIRE(doc.layers.empty());
  REQUIRE((doc.v && *doc.v == "5.7.4"));
  REQUIRE(doc.fr.value() == Catch::Approx(60.0));
  REQUIRE(doc.ip.value() == Catch::Approx(0.0));
  REQUIRE(doc.op.value() == Catch::Approx(60.0));
  REQUIRE(doc.w.value() == 512);
  REQUIRE(doc.h.value() == 512);
  REQUIRE_FALSE(doc.nm);

  // ラウンドトリップで妥当な空ドキュメントとして保存・再解析できる
  auto dump_result = lottiepp::dump(doc);
  REQUIRE(dump_result.has_value());
  auto again = lottiepp::parse(*dump_result);
  REQUIRE(again.has_value());
  REQUIRE(again->layers.empty());
  REQUIRE(again->w.value() == 512);
}

TEST_CASE("makeDocument respects params") {
  lottiepp::DocumentParams p;
  p.name = "New";
  p.fr   = 30.0;
  p.w    = 1920;
  p.h    = 1080;
  p.op   = 90.0;
  auto doc = lottiepp::makeDocument(p);
  REQUIRE((doc.nm && *doc.nm == "New"));
  REQUIRE(doc.fr.value() == Catch::Approx(30.0));
  REQUIRE(doc.w.value() == 1920);
  REQUIRE(doc.h.value() == 1080);
  REQUIRE(doc.op.value() == Catch::Approx(90.0));
}

TEST_CASE("makeTrimPath generates a flat valid node") {
  // 過去バグのリグレッション: 生成 JSON の閉じブレースが不足しており、
  // "e" が "s" の内側に、"o" が "e" の内側にネストし、ルートも閉じられていなかったため
  // 内部の parseJson が失敗していた（"index N: expected_comma"）。
  lottiepp::json trim = lottiepp::makeTrimPath(10.0, 90.0, 45.0, true);

  // s / e / o はそれぞれ独立した {"a":0,"k":<値>} プロパティとしてフラットに並ぶこと
  REQUIRE(trim["ty"].as<std::string>() == "tm");
  REQUIRE(trim["s"]["a"].as<int>() == 0);
  REQUIRE(trim["s"]["k"].as<double>() == Catch::Approx(10.0));
  REQUIRE(trim["e"]["a"].as<int>() == 0);
  REQUIRE(trim["e"]["k"].as<double>() == Catch::Approx(90.0));
  REQUIRE(trim["o"]["a"].as<int>() == 0);
  REQUIRE(trim["o"]["k"].as<double>() == Catch::Approx(45.0));
  // 旧バグでは "s" の内側に "e"、"e" の内側に "o" が入っていた
  REQUIRE_FALSE(trim["s"].contains("e"));
  REQUIRE_FALSE(trim["e"].contains("o"));
  // simultaneous=true は m=1
  REQUIRE(trim["m"].as<int>() == 1);

  // 非同時モード（m=2）でも同様に妥当なノードを生成すること
  lottiepp::json trim2 = lottiepp::makeTrimPath(0.0, 50.0, 0.0, false);
  auto s2 = trim2["s"]["k"].as<double>();
  auto e2 = trim2["e"]["k"].as<double>();
  auto o2 = trim2["o"]["k"].as<double>();
  auto m2 = trim2["m"].as<int>();
  REQUIRE(s2 == Catch::Approx(0.0));
  REQUIRE(e2 == Catch::Approx(50.0));
  REQUIRE(o2 == Catch::Approx(0.0));
  REQUIRE(m2 == 2);

  // 生成ノードはシリアライズ → 再解析のラウンドトリップを通ること
  auto dump_result = lottiepp::dumpJson(trim);
  REQUIRE(dump_result.has_value());
  auto parse_result = lottiepp::parseJson(*dump_result);
  REQUIRE(parse_result.has_value());
}

TEST_CASE("parse returns unexpected on invalid JSON") {
  auto result = lottiepp::parse("not valid json");
  REQUIRE_FALSE(result.has_value());
  REQUIRE_FALSE(result.error().empty());
}

TEST_CASE("recolor returns unexpected on invalid hex") {
  auto doc_result = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 0,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [1, 0, 0, 1]}}]
    }]
  })");
  REQUIRE(doc_result.has_value());

  auto recolor_result = lottiepp::recolor(*doc_result, "", "not-a-color");
  REQUIRE_FALSE(recolor_result.has_value());
  REQUIRE_FALSE(recolor_result.error().empty());
}
