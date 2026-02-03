#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "sentinel/json.hpp"
#include "sentinel/model.hpp"

TEST_CASE("NormalizedEvent JSON semantic") {
  sentinel::NormalizedEvent ev;
  ev.block = {"arbitrum", 123, "0xabc", 1700000000};
  ev.tx    = {"0xtx", "0xfrom", "0xto", 7};
  ev.log   = {"0xaddr", {"0xt0", "0xt1"}, "0xdata", 9};

  const nlohmann::json got = sentinel::to_json(ev);

  const nlohmann::json expected = {
    {"block", {
      {"chain", "arbitrum"},
      {"number", 123},
      {"hash", "0xabc"},
      {"timestamp", 1700000000}
    }},
    {"tx", {
      {"hash", "0xtx"},
      {"from", "0xfrom"},
      {"to", "0xto"},
      {"nonce", 7}
    }},
    {"log", {
      {"address", "0xaddr"},
      {"topics", {"0xt0", "0xt1"}},
      {"data", "0xdata"},
      {"log_index", 9}
    }}
  };

  REQUIRE(got == expected);
}
