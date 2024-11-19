cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/utf-8)
endif()

add_definitions("-DV8_ENABLE_TURBOFAN")
add_subdirectory(v8-cmake)

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