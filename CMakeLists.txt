cmake_minimum_required(VERSION 3.20)
project(risk_sentinel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TESTS "Build unit tests" ON)

# -------------------------
# Interface lib (headers)
# -------------------------
add_library(sentinel_lib INTERFACE)
target_include_directories(sentinel_lib INTERFACE include)

# -------------------------
# Core library
# -------------------------
add_library(sentinel_core
  src/db_checkpoint_store.cpp
)
target_include_directories(sentinel_core PUBLIC include)

# nlohmann/json via CPM
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.11.3
)
target_link_libraries(sentinel_core PUBLIC nlohmann_json::nlohmann_json)

# libpqxx via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(PQ REQUIRED libpq)

target_include_directories(sentinel_core PRIVATE
  ${PQXX_INCLUDE_DIRS}
  ${PQ_INCLUDE_DIRS}
)
target_link_directories(sentinel_core PRIVATE
  ${PQXX_LIBRARY_DIRS}
  ${PQ_LIBRARY_DIRS}
)
target_link_libraries(sentinel_core PRIVATE
  ${PQXX_LIBRARIES}
  ${PQ_LIBRARIES}
)
target_compile_options(sentinel_core PRIVATE
  ${PQXX_CFLAGS_OTHER}
  ${PQ_CFLAGS_OTHER}
)

# -------------------------
# Executable
# -------------------------
add_executable(sentinel
  src/main.cpp
)

target_link_libraries(sentinel PRIVATE
  sentinel_lib
  sentinel_core
)

# -------------------------
# Warnings
# -------------------------
if(MSVC)
  target_compile_options(sentinel PRIVATE /W4 /permissive-)
else()
  target_compile_options(sentinel PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -------------------------
# Tests
# -------------------------
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
