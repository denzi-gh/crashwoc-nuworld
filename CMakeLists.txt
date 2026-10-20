cmake_minimum_required(VERSION 3.25)
project(NuWorld VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(WARNING "NuWorld is built and tested with Clang (clang on Linux, CLANG64 on Windows).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NUWORLD_SYSTEM_SDL3 "Use an installed SDL3 instead of building the pinned release" OFF)
option(NUWORLD_TESTS "Build the C tests (ctest)" ON)

if(NUWORLD_SYSTEM_SDL3)
  find_package(SDL3 REQUIRED CONFIG)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
endif()

add_executable(nuworld
  src/main.c
  src/platform/platform.c
  src/gfx/gl.c)

target_include_directories(nuworld PRIVATE src)
target_compile_definitions(nuworld PRIVATE NUWORLD_VERSION="${PROJECT_VERSION}")
target_compile_options(nuworld PRIVATE -Wall -Wextra)
target_link_libraries(nuworld PRIVATE SDL3::SDL3)

# Runtime for the generated stubs and function map; linked into nuworld in M1.
add_library(nuworld_port STATIC
  src/port/stub.c
  src/port/funcmap.c)
target_include_directories(nuworld_port PUBLIC src)
target_compile_options(nuworld_port PRIVATE -Wall -Wextra)

# Disc images, verification and the game file system.
add_library(nuworld_vfs STATIC
  src/vfs/disc.c
  src/vfs/iso9660.c
  src/vfs/manifest.c
  src/vfs/sha256.c
  src/vfs/verify.c)
target_include_directories(nuworld_vfs PUBLIC src)
target_compile_options(nuworld_vfs PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_vfs PUBLIC SDL3::SDL3)

if(NUWORLD_TESTS)
  enable_testing()
  add_executable(nuworld_test_vfs tests/c/test_vfs.c)
  target_compile_options(nuworld_test_vfs PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_vfs PRIVATE nuworld_vfs)
  add_test(NAME vfs COMMAND nuworld_test_vfs ${CMAKE_CURRENT_BINARY_DIR}/test-vfs)
endif()
