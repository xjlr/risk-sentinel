include(${CMAKE_CURRENT_LIST_DIR}/../cmake/CPM.cmake)

CPMAddPackage(
  NAME Catch2
  GITHUB_REPOSITORY catchorg/Catch2
  VERSION 3.5.2
)

add_executable(unit_tests
  test_basic.cpp
  test_model_json.cpp
  test_large_transfer_rule.cpp
  test_governance_normalize.cpp
  test_governance_rule.cpp
  test_alert_formatter.cpp
  test_mint_burn_normalize.cpp
  test_mint_burn_rule.cpp
  test_approval_normalize.cpp
  test_approval_rule.cpp
  test_regression_existing_flows.cpp
  test_crypto.cpp
  test_webhook_alert_channel.cpp
  test_alert_deduplicator.cpp
  test_health_checks.cpp
  test_bridge_transfer_rule.cpp
)

target_link_libraries(unit_tests PRIVATE
  sentinel_core
  Catch2::Catch2WithMain
)

include(CTest)
add_test(NAME unit_tests COMMAND unit_tests)
