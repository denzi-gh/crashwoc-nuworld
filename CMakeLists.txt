cmake_minimum_required(VERSION 3.25)
project(crashwoc-nuworld VERSION 0.7.0 LANGUAGES C CXX)

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
option(NUWORLD_DECOMP "Generate and compile the decomp units (needs Python 3.11+ with libclang)" ON)
option(NUWORLD_FUZZ "Build the libFuzzer targets for the VFS readers and the loaders (clang on Linux)" OFF)
option(NUWORLD_SANITIZE "Build the port, the decomp and the tests with ASan and UBSan (clang)" OFF)

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

# After SDL: everything from here on, the decomp included; with libFuzzer's coverage for the fuzz targets.
if(NUWORLD_FUZZ)
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()
if(NUWORLD_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
else()
  # Debug builds allocate nothing below 4 GB (src/platform/highheap.c); ASan keeps its own layout.
  add_compile_definitions($<$<CONFIG:Debug>:NW_HIGH_HEAP>)
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
  src/platform/audio.c
  src/platform/display.c
  src/platform/platform.c
  src/gfx/gl.c
  src/gfx/vu.c
  src/frontend/debug.cpp
  src/frontend/ui.cpp
  src/frontend/wizard.cpp)
nw_embed(crashwoc-nuworld nw_font_regular ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Regular.ttf)
nw_embed(crashwoc-nuworld nw_font_bold ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Bold.ttf)

target_include_directories(crashwoc-nuworld PRIVATE src)
target_compile_definitions(crashwoc-nuworld PRIVATE NUWORLD_VERSION="${PROJECT_VERSION}")
target_compile_options(crashwoc-nuworld PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(crashwoc-nuworld PRIVATE nuworld_frontend nuworld_gfx nuworld_imgui SDL3::SDL3)
if(MINGW)
  # Keep the release a single .exe: libc++ (Dear ImGui) linked in, not libc++.dll.
  target_link_options(crashwoc-nuworld PRIVATE -static-libstdc++)
endif()

# Runtime for the generated stubs and function map, and the vblank clock the game thread runs on.
add_library(nuworld_port STATIC
  src/platform/clock.c
  src/port/stub.c
  src/port/funcmap.c)
target_include_directories(nuworld_port PUBLIC src)
target_compile_options(nuworld_port PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_port PUBLIC SDL3::SDL3)

# Disc images, the data folder and the game file system.
set(NW_VFS_SOURCES
  src/platform/paths.c
  src/vfs/dat.c
  src/vfs/disc.c
  src/vfs/extract.c
  src/vfs/iso9660.c
  src/vfs/manifest.c
  src/vfs/sha256.c
  src/vfs/verify.c
  src/vfs/vfs.c)
add_library(nuworld_vfs STATIC ${NW_VFS_SOURCES})
target_include_directories(nuworld_vfs PUBLIC src)
target_compile_options(nuworld_vfs PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_vfs PUBLIC SDL3::SDL3)

# Scenes without the decomp: the NU20 reader, the VIF and texture decoders, the material state, the frame packets.
add_library(nuworld_gfx STATIC
  src/gfx/gsc.c
  src/gfx/interp.c
  src/gfx/mtl.c
  src/gfx/packet.c
  src/gfx/tex.c
  src/gfx/vif.c
  src/gfx/vu1init.c)
target_include_directories(nuworld_gfx PUBLIC src)
target_compile_options(nuworld_gfx PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_gfx PUBLIC nuworld_vfs)

# NUSOUND: the IRX's RPC side, the VAG decoder and SFX.DAT.
add_library(nuworld_audio STATIC
  src/audio/libsd.c
  src/audio/nusound.c
  src/audio/sfx.c
  src/audio/spu2.c
  src/audio/vag.c)
target_include_directories(nuworld_audio PUBLIC src)
target_compile_options(nuworld_audio PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_audio PUBLIC nuworld_vfs)

# Frontend logic without the UI: import folder listing, background import job, settings.toml.
add_library(nuworld_frontend STATIC
  src/frontend/import.c
  src/frontend/settings.c)
target_include_directories(nuworld_frontend PUBLIC src)
target_compile_options(nuworld_frontend PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_frontend PUBLIC nuworld_vfs nuworld_save)

# Saves: the TOML document, the save records in it, the saves folder and PS2 card images (no decomp).
set(NW_SAVE_SOURCES
  src/save/ps2card.c
  src/save/savecodec.c
  src/save/savedoc.c
  src/save/savestore.c)
add_library(nuworld_save STATIC ${NW_SAVE_SOURCES})
target_include_directories(nuworld_save PUBLIC src)
target_compile_options(nuworld_save PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_save PUBLIC SDL3::SDL3)

if(NUWORLD_TESTS)
  enable_testing()
  add_executable(nuworld_test_vfs tests/c/test_vfs.c)
  target_compile_options(nuworld_test_vfs PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_vfs PRIVATE nuworld_frontend)
  add_test(NAME vfs COMMAND nuworld_test_vfs ${CMAKE_CURRENT_BINARY_DIR}/test-vfs)
  add_executable(nuworld_test_scene tests/c/test_scene.c)
  target_compile_options(nuworld_test_scene PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_scene PRIVATE nuworld_gfx)
  add_test(NAME scene COMMAND nuworld_test_scene ${CMAKE_CURRENT_BINARY_DIR}/test-scene)
  add_executable(nuworld_test_audio tests/c/test_audio.c)
  target_compile_options(nuworld_test_audio PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_audio PRIVATE nuworld_audio)
  add_test(NAME audio COMMAND nuworld_test_audio ${CMAKE_CURRENT_BINARY_DIR}/test-audio)
  add_test(NAME audio-retail COMMAND nuworld_test_audio retail)
  set_tests_properties(audio-retail PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(nuworld_test_interp tests/c/test_interp.c)
  target_compile_options(nuworld_test_interp PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_interp PRIVATE nuworld_gfx)
  add_test(NAME interp COMMAND nuworld_test_interp)
  add_executable(nuworld_test_settings tests/c/test_settings.c)
  target_compile_options(nuworld_test_settings PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_settings PRIVATE nuworld_frontend)
  add_test(NAME settings COMMAND nuworld_test_settings ${CMAKE_CURRENT_BINARY_DIR}/test-settings)
  add_executable(nuworld_test_display tests/c/test_display.c src/platform/display.c)
  target_include_directories(nuworld_test_display PRIVATE src)
  target_compile_options(nuworld_test_display PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_display PRIVATE SDL3::SDL3)
  add_test(NAME display COMMAND nuworld_test_display)
  add_executable(nuworld_test_savefile tests/c/test_savefile.c)
  target_compile_options(nuworld_test_savefile PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_savefile PRIVATE nuworld_save nuworld_vfs)
  add_test(NAME savefile COMMAND nuworld_test_savefile ${CMAKE_CURRENT_BINARY_DIR}/test-savefile)
endif()

if(NUWORLD_FUZZ)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR MINGW)
    message(FATAL_ERROR "NUWORLD_FUZZ needs clang on Linux or macOS (libFuzzer does not support MinGW).")
  endif()
  # The VFS readers, the VIF decoder and the save readers again, instrumented for libFuzzer, and the seed corpus from
  # the synthetic test builders.
  set(nw_fuzz_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_library(nuworld_vfs_fuzz STATIC ${NW_VFS_SOURCES} ${NW_SAVE_SOURCES} src/gfx/vif.c)
  target_include_directories(nuworld_vfs_fuzz PUBLIC src)
  target_compile_options(nuworld_vfs_fuzz PRIVATE -Wall -Wextra -fsanitize=fuzzer-no-link ${nw_fuzz_flags})
  target_link_libraries(nuworld_vfs_fuzz PUBLIC SDL3::SDL3)
  add_executable(nuworld_fuzz_vfs tests/fuzz/fuzz_vfs.c)
  target_compile_options(nuworld_fuzz_vfs PRIVATE -Wall -Wextra -fsanitize=fuzzer ${nw_fuzz_flags})
  target_link_options(nuworld_fuzz_vfs PRIVATE -fsanitize=fuzzer ${nw_fuzz_flags})
  target_link_libraries(nuworld_fuzz_vfs PRIVATE nuworld_vfs_fuzz)
  add_executable(nuworld_fuzz_seeds tests/fuzz/fuzz_seeds.c)
  target_include_directories(nuworld_fuzz_seeds PRIVATE tests/c)
  target_compile_options(nuworld_fuzz_seeds PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_fuzz_seeds PRIVATE nuworld_save nuworld_vfs)
  if(NUWORLD_TESTS)
    # Every seed once through the instrumented readers.
    set(nw_fuzz_seeds ${CMAKE_CURRENT_BINARY_DIR}/fuzz-seeds)
    set(nw_fuzz_loader_seeds ${CMAKE_CURRENT_BINARY_DIR}/fuzz-seeds-loaders)
    add_test(NAME fuzz_seeds COMMAND nuworld_fuzz_seeds ${nw_fuzz_seeds} ${nw_fuzz_loader_seeds})
    set_tests_properties(fuzz_seeds PROPERTIES FIXTURES_SETUP fuzz_corpus)
    add_test(NAME fuzz_vfs COMMAND nuworld_fuzz_vfs -runs=0 ${nw_fuzz_seeds})
    set_tests_properties(fuzz_vfs PROPERTIES FIXTURES_REQUIRED fuzz_corpus)
  endif()
endif()

if(NUWORLD_DECOMP)
  include(cmake/decomp.cmake)
endif()
