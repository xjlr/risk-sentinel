#include <catch2/catch_test_macros.hpp>
#include <string>
#include "sentinel/version.hpp"

TEST_CASE("version is non-empty") {
  REQUIRE(sentinel::kVersion != nullptr);
  REQUIRE(std::string(sentinel::kVersion).size() > 0);
}
