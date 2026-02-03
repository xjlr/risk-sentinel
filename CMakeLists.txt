cmake_minimum_required(VERSION 3.20)
project(risk_sentinel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TESTS "Build unit tests" ON)

# Good defaults
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(sentinel_lib INTERFACE)
target_include_directories(sentinel_lib INTERFACE include)

add_executable(sentinel src/main.cpp)
target_link_libraries(sentinel PRIVATE sentinel_lib)

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
