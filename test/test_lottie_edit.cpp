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
  auto doc = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 0,
      "shapes": [{
        "ty": "fl",
        "c": {"a": 0, "k": [1, 0, 0, 1]}
      }]
    }]
  })");

  const auto n = lottiepp::recolor(doc, "#ff0000", "#00ff00");
  REQUIRE(n == 1);
  REQUIRE(doc.layers.size() == 1);
  REQUIRE(doc.layers[0].shapes);
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][0].as<float>() == Catch::Approx(0.0f));
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][1].as<float>() == Catch::Approx(1.0f));
}

TEST_CASE("replaceText") {
  auto doc = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 5, "nm": "Title", "ip": 0, "op": 60, "st": 0,
      "t": {"d": {"k": [{"s": {"t": "Hello"}, "t": 0}]}}
    }]
  })");

  REQUIRE(lottiepp::replaceText(doc, "Title", "World"));
  REQUIRE(doc.layers[0].t);
  REQUIRE((*doc.layers[0].t->d->k)[0]["s"]["t"].as<std::string>() == "World");
  REQUIRE_FALSE(lottiepp::replaceText(doc, "Missing", "X"));
}

TEST_CASE("setSpeed scales timing") {
  auto doc = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 10,
      "ks": {"o": {"a": 1, "k": [{"t": 0, "s": [100]}, {"t": 60, "s": [0]}]}}
    }]
  })");

  const auto n = lottiepp::setSpeed(doc, 2.0);
  REQUIRE(n >= 3);
  REQUIRE(doc.op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].st.value() == Catch::Approx(20.0));
  REQUIRE((*doc.layers[0].ks->o)["k"][1]["t"].as<double>() == Catch::Approx(120.0));
}

TEST_CASE("unknown fields preserved") {
  auto doc = lottiepp::parse(R"({
    "fr": 30, "ip": 0, "op": 30, "w": 10, "h": 10,
    "ddd": 0,
    "meta": {"g": "test"},
    "layers": [{
      "ty": 4, "nm": "S", "ip": 0, "op": 30, "st": 0,
      "ind": 1,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [0, 0, 0, 1]}}]
    }]
  })");

  REQUIRE(doc.extra.contains("ddd"));
  REQUIRE(doc.extra.contains("meta"));
  REQUIRE(doc.layers[0].extra.contains("ind"));

  const auto text = lottiepp::dump(doc);
  auto again = lottiepp::parse(text);
  REQUIRE(again.extra.contains("ddd"));
  REQUIRE(again.extra.contains("meta"));
  REQUIRE(again.layers[0].extra.contains("ind"));
}

TEST_CASE("generateVariations") {
  auto doc = lottiepp::parse(R"({
    "fr": 30, "ip": 0, "op": 30, "w": 10, "h": 10,
    "layers": [{
      "ty": 4, "nm": "S", "ip": 0, "op": 30, "st": 0,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [0, 0, 0, 1]}}]
    }]
  })");

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
  auto doc = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 200, "h": 200, "layers": []
  })");

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
  auto again = lottiepp::parse(lottiepp::dump(doc));
  REQUIRE(again.layers.size() == 1);
  const auto& shapes = *again.layers[0].shapes;
  REQUIRE(shapes[0]["ty"].as<std::string>() == "gr");
  REQUIRE(shapes[0]["it"][0]["ty"].as<std::string>() == "rc");
  REQUIRE(shapes[0]["it"][1]["ty"].as<std::string>() == "fl");
  REQUIRE(shapes[0]["it"][1]["c"]["k"][1].as<float>() == Catch::Approx(1.0f));
  // レイヤ位置の反映
  REQUIRE((*again.layers[0].ks->p)["k"][0].as<double>() == Catch::Approx(50.0));
}

TEST_CASE("addEffect appends to ef") {
  auto doc = lottiepp::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 200, "h": 200,
    "layers": [{"ty": 4, "nm": "Target", "ip": 0, "op": 60, "st": 0, "ind": 1, "shapes": []}]
  })");

  auto* layer = lottiepp::findLayer(doc, "Target");
  REQUIRE(layer != nullptr);
  lottiepp::addEffect(*layer, lottiepp::makeGaussianBlur(12.0));

  auto again = lottiepp::parse(lottiepp::dump(doc));
  auto* l2   = lottiepp::findLayer(again, "Target");
  REQUIRE(l2 != nullptr);
  REQUIRE(l2->extra.contains("ef"));
  REQUIRE(l2->extra["ef"].is_array());
  REQUIRE(l2->extra["ef"].get_array().size() == 1);
  REQUIRE(l2->extra["ef"][0]["nm"].as<std::string>() == "Gaussian Blur");
  REQUIRE(l2->extra["ef"][0]["ef"][1]["v"]["k"].as<double>() == Catch::Approx(12.0));
}

TEST_CASE("removeLayer removes by name") {
  auto doc = lottiepp::parse(R"({
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

  REQUIRE(lottiepp::removeLayer(doc, "Drop"));
  REQUIRE(doc.layers.size() == 1);
  REQUIRE((doc.layers[0].nm && *doc.layers[0].nm == "Keep"));
  REQUIRE((doc.assets && doc.assets->size() == 1));
  REQUIRE(doc.assets->at(0).layers);
  REQUIRE(doc.assets->at(0).layers->empty());

  // ラウンドトリップ後も削除が維持される
  auto again = lottiepp::parse(lottiepp::dump(doc));
  REQUIRE(again.layers.size() == 1);
  REQUIRE_FALSE(lottiepp::removeLayer(again, "Missing"));
}

TEST_CASE("makeTrimPath builds tm") {
  auto tm = lottiepp::makeTrimPath(25, 75, 0, true);
  REQUIRE(tm["ty"].as<std::string>() == "tm");
  REQUIRE(tm["m"].as<int>() == 1);
  REQUIRE(tm["s"]["k"].as<double>() == Catch::Approx(25.0));
  REQUIRE(tm["e"]["k"].as<double>() == Catch::Approx(75.0));
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
  auto again = lottiepp::parse(lottiepp::dump(doc));
  REQUIRE(again.layers.empty());
  REQUIRE(again.w.value() == 512);
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
