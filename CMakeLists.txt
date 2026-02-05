cmake_minimum_required(VERSION 3.20)
project(risk_sentinel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TESTS "Build unit tests" ON)

# ----------------------------------------
# Dependencies / package management (CPM)
# ----------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

# nlohmann/json
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.11.3
)

# spdlog
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  VERSION 1.14.1
)

# libcurl
find_package(CURL REQUIRED)

# pqxx / pq via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(PQ REQUIRED libpq)

# ----------------------------------------
# Core library (all non-main code)
# ----------------------------------------
add_library(sentinel_core
  src/db_checkpoint_store.cpp
  src/log.cpp
  src/rpc/JsonRpcClient.cpp
  # src/polling/BlockPoller.cpp
  src/chains/arbitrum/ArbitrumAdapter.cpp
)

target_include_directories(sentinel_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${PQXX_INCLUDE_DIRS}
    ${PQ_INCLUDE_DIRS}
)

target_link_directories(sentinel_core PRIVATE
  ${PQXX_LIBRARY_DIRS}
  ${PQ_LIBRARY_DIRS}
)

target_link_libraries(sentinel_core
  PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
  PRIVATE
    CURL::libcurl
    ${PQXX_LIBRARIES}
    ${PQ_LIBRARIES}
)

target_compile_options(sentinel_core PRIVATE
  ${PQXX_CFLAGS_OTHER}
  ${PQ_CFLAGS_OTHER}
)

# Warnings (apply to core, where most code lives)
if(MSVC)
  target_compile_options(sentinel_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(sentinel_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ----------------------------------------
# Executable
# ----------------------------------------
add_executable(sentinel
  src/main.cpp
)

target_link_libraries(sentinel PRIVATE sentinel_core)

# ----------------------------------------
# Tests
# ----------------------------------------
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
