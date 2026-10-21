cmake_minimum_required(VERSION 3.25)
project(crashwoc-nuworld VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(WARNING "${PROJECT_NAME} is built and tested with Clang (clang on Linux, CLANG64 on Windows).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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

# Dear ImGui with its SDL3 and OpenGL 3 backends (the GL backend has its own loader).
include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(nuworld_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(nuworld_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(nuworld_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(nuworld_imgui PRIVATE -fno-exceptions -fno-rtti)
target_link_libraries(nuworld_imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})

# Port-owned assets compiled in: build/gen/<name>.c defines <name>[] and <name>_size.
function(nw_embed target name file)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/gen/${name}.c)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DIN=${file} -DOUT=${out} -DNAME=${name} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${file} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    VERBATIM)
  target_sources(${target} PRIVATE ${out})
endfunction()

add_executable(crashwoc-nuworld
  src/main.c
  src/platform/platform.c
  src/gfx/gl.c
  src/frontend/ui.cpp
  src/frontend/wizard.cpp)
nw_embed(crashwoc-nuworld nw_font_regular ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Regular.ttf)
nw_embed(crashwoc-nuworld nw_font_bold ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Bold.ttf)

target_include_directories(crashwoc-nuworld PRIVATE src)
target_compile_definitions(crashwoc-nuworld PRIVATE NUWORLD_VERSION="${PROJECT_VERSION}")
target_compile_options(crashwoc-nuworld PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(crashwoc-nuworld PRIVATE nuworld_frontend nuworld_imgui SDL3::SDL3)
if(MINGW)
  # Keep the release a single .exe: libc++ (Dear ImGui) linked in, not libc++.dll.
  target_link_options(crashwoc-nuworld PRIVATE -static-libstdc++)
endif()

# Runtime for the generated stubs and function map; linked into crashwoc-nuworld in M1.
add_library(nuworld_port STATIC
  src/port/stub.c
  src/port/funcmap.c)
target_include_directories(nuworld_port PUBLIC src)
target_compile_options(nuworld_port PRIVATE -Wall -Wextra)

# Disc images, the data folder and the game file system.
add_library(nuworld_vfs STATIC
  src/platform/paths.c
  src/vfs/dat.c
  src/vfs/disc.c
  src/vfs/extract.c
  src/vfs/iso9660.c
  src/vfs/manifest.c
  src/vfs/sha256.c
  src/vfs/verify.c
  src/vfs/vfs.c)
target_include_directories(nuworld_vfs PUBLIC src)
target_compile_options(nuworld_vfs PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_vfs PUBLIC SDL3::SDL3)

# Wizard logic without the UI: import folder listing, background import job.
add_library(nuworld_frontend STATIC
  src/frontend/import.c)
target_include_directories(nuworld_frontend PUBLIC src)
target_compile_options(nuworld_frontend PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_frontend PUBLIC nuworld_vfs)

if(NUWORLD_TESTS)
  enable_testing()
  add_executable(nuworld_test_vfs tests/c/test_vfs.c)
  target_compile_options(nuworld_test_vfs PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_vfs PRIVATE nuworld_frontend)
  add_test(NAME vfs COMMAND nuworld_test_vfs ${CMAKE_CURRENT_BINARY_DIR}/test-vfs)
endif()
