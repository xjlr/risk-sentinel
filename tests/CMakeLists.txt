include(${CMAKE_CURRENT_LIST_DIR}/../cmake/CPM.cmake)

CPMAddPackage(
  NAME Catch2
  GITHUB_REPOSITORY catchorg/Catch2
  VERSION 3.5.2
)

add_executable(unit_tests
  test_basic.cpp
  test_model_json.cpp
)

target_link_libraries(unit_tests PRIVATE
  sentinel_core
  Catch2::Catch2WithMain
)

include(CTest)
add_test(NAME unit_tests COMMAND unit_tests)
