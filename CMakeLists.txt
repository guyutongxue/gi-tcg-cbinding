cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

### Pending v8-cmake patches

# https://github.com/bnoordhuis/v8-cmake/pull/54
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# https://github.com/bnoordhuis/v8-cmake/pull/87
if (MSVC)
  add_compile_options(/utf-8)
endif()

# https://github.com/bnoordhuis/v8-cmake/pull/83
add_definitions(-DV8_ENABLE_TURBOFAN)

### Fetch and configure v8-cmake

include(FetchContent)
FetchContent_Declare(
  v8-cmake
  # GIT_REPOSITORY https://github.com/bnoordhuis/v8-cmake.git
  GIT_REPOSITORY https://git.tencent.xqm32.org/guyutongxue/v8-cmake.git
  GIT_TAG        11.6.189.4
)
FetchContent_MakeAvailable(v8-cmake)

### My project

project(V8Test)

add_executable(V8Test src/main.cpp)

target_link_libraries(V8Test
  PRIVATE
  v8_base_without_compiler
  v8_compiler
  v8_snapshot
  v8_inspector
  v8_libplatform
  )