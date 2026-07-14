#include "catch2/catch_all.hpp"
#include "lottie_edit.hpp"

TEST_CASE("parseHexColor") {
  auto c = lottie_edit::parseHexColor("#ff0000");
  REQUIRE(c);
  REQUIRE(c->r == Catch::Approx(1.0f));
  REQUIRE(c->g == Catch::Approx(0.0f));
  REQUIRE(c->b == Catch::Approx(0.0f));

  auto shortc = lottie_edit::parseHexColor("#0f0");
  REQUIRE(shortc);
  REQUIRE(shortc->g == Catch::Approx(1.0f));
}

TEST_CASE("recolor static fill") {
  auto doc = lottie_edit::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 0,
      "shapes": [{
        "ty": "fl",
        "c": {"a": 0, "k": [1, 0, 0, 1]}
      }]
    }]
  })");

  const auto n = lottie_edit::recolor(doc, "#ff0000", "#00ff00");
  REQUIRE(n == 1);
  REQUIRE(doc.layers.size() == 1);
  REQUIRE(doc.layers[0].shapes);
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][0].as<float>() == Catch::Approx(0.0f));
  REQUIRE((*doc.layers[0].shapes)[0]["c"]["k"][1].as<float>() == Catch::Approx(1.0f));
}

TEST_CASE("replaceText") {
  auto doc = lottie_edit::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 5, "nm": "Title", "ip": 0, "op": 60, "st": 0,
      "t": {"d": {"k": [{"s": {"t": "Hello"}, "t": 0}]}}
    }]
  })");

  REQUIRE(lottie_edit::replaceText(doc, "Title", "World"));
  REQUIRE(doc.layers[0].t);
  REQUIRE((*doc.layers[0].t)["d"]["k"][0]["s"]["t"].as<std::string>() == "World");
  REQUIRE_FALSE(lottie_edit::replaceText(doc, "Missing", "X"));
}

TEST_CASE("setSpeed scales timing") {
  auto doc = lottie_edit::parse(R"({
    "fr": 60, "ip": 0, "op": 60, "w": 100, "h": 100,
    "layers": [{
      "ty": 4, "nm": "Shape", "ip": 0, "op": 60, "st": 10,
      "ks": {"o": {"a": 1, "k": [{"t": 0, "s": [100]}, {"t": 60, "s": [0]}]}}
    }]
  })");

  const auto n = lottie_edit::setSpeed(doc, 2.0);
  REQUIRE(n >= 3);
  REQUIRE(doc.op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].op.value() == Catch::Approx(120.0));
  REQUIRE(doc.layers[0].st.value() == Catch::Approx(20.0));
  REQUIRE((*doc.layers[0].ks)["o"]["k"][1]["t"].as<double>() == Catch::Approx(120.0));
}

TEST_CASE("unknown fields preserved") {
  auto doc = lottie_edit::parse(R"({
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

  const auto text = lottie_edit::dump(doc);
  auto again = lottie_edit::parse(text);
  REQUIRE(again.extra.contains("ddd"));
  REQUIRE(again.extra.contains("meta"));
  REQUIRE(again.layers[0].extra.contains("ind"));
}

TEST_CASE("generateVariations") {
  auto doc = lottie_edit::parse(R"({
    "fr": 30, "ip": 0, "op": 30, "w": 10, "h": 10,
    "layers": [{
      "ty": 4, "nm": "S", "ip": 0, "op": 30, "st": 0,
      "shapes": [{"ty": "fl", "c": {"a": 0, "k": [0, 0, 0, 1]}}]
    }]
  })");

  std::vector<lottie_edit::VariationParams> params(2);
  params[0].recolor_to = "#ff0000";
  params[1].recolor_to = "#0000ff";
  params[1].speed     = 2.0;

  auto outs = lottie_edit::generateVariations(doc, params);
  REQUIRE(outs.size() == 2);
  REQUIRE((*outs[0].layers[0].shapes)[0]["c"]["k"][0].as<float>() == Catch::Approx(1.0f));
  REQUIRE(outs[1].op.value() == Catch::Approx(60.0));
}
