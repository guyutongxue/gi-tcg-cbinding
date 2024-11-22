cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

### Pending v8-cmake patches

# https://github.com/bnoordhuis/v8-cmake/pull/54
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# https://github.com/bnoordhuis/v8-cmake/pull/87
set(MSVC_COMPILE_OPTIONS /utf-8
  /wd4661 /wd4819 /wd4267 /wd4996 /wd4244
  /wd4005 /wd4146 /wd4715 /wd4530 /wd4838
  /wd4506 /wd4309 /wd4312 /wd4334)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:${MSVC_COMPILE_OPTIONS}>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:${MSVC_COMPILE_OPTIONS}>")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

# https://github.com/bnoordhuis/v8-cmake/pull/83
add_definitions(-DV8_ENABLE_TURBOFAN)

### Fetch and configure v8-cmake

include(FetchContent)
FetchContent_Declare(
  v8-cmake
  GIT_REPOSITORY https://github.com/bnoordhuis/v8-cmake.git
  # GIT_REPOSITORY https://git.tencent.xqm32.org/guyutongxue/v8-cmake.git
  GIT_TAG        11.6.189.4
)
FetchContent_MakeAvailable(v8-cmake)

### My project

project(V8Test)

add_executable(V8Test
  src/environment.cpp
  src/game.cpp
  src/main.cpp)

target_link_libraries(V8Test
  PRIVATE
  v8_base_without_compiler
  v8_compiler
  v8_snapshot
  v8_inspector
  v8_libplatform
  )