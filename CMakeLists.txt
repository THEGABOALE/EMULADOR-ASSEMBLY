cmake_minimum_required(VERSION 3.16)
project(EmuladorX86 VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- objetivos
add_library(emu_core STATIC
  src/cpu.cpp
  src/parser.cpp
  src/interpreter.cpp
  src/trace.cpp)
target_include_directories(emu_core PUBLIC src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(emu_core PRIVATE -Wall -Wextra)
endif()
