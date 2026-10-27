# The decomp units on the host: the generators write <build>/gen, nuworld_decomp compiles every wrapper
# and stub unit with the decomp flags and the -Werror set. nuworld_types generates the type DB, the PS2
# mirror structs and the converters from the decomp headers; nuworld_data builds the converters.

# The interpreter on the PATH first (on Windows the registry would win, often one without libclang).
set(Python3_FIND_REGISTRY LAST)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import clang.cindex as c; c.Index.create()"
  RESULT_VARIABLE nw_libclang OUTPUT_QUIET ERROR_QUIET)
if(NOT nw_libclang EQUAL 0)
  message(FATAL_ERROR "${Python3_EXECUTABLE} cannot load libclang: python -m pip install -r tools/requirements.txt, "
    "or pass another interpreter with -DPython3_EXECUTABLE=<python>")
endif()

set(nw_tools ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set(nw_decomp ${CMAKE_CURRENT_SOURCE_DIR}/decomp)
set(nw_gen ${CMAKE_CURRENT_BINARY_DIR}/gen)

file(GLOB_RECURSE nw_decomp_sources CONFIGURE_DEPENDS ${nw_decomp}/src/*.c ${nw_decomp}/include/*.h)
file(GLOB_RECURSE nw_decomp_headers CONFIGURE_DEPENDS ${nw_decomp}/include/*.h)
file(GLOB_RECURSE nw_port_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE nw_registries CONFIGURE_DEPENDS ${nw_decomp}/config/pal103/status/*.toml)
list(APPEND nw_registries ${nw_decomp}/config/pal103/units.toml ${nw_decomp}/config/pal103/functions.toml
  ${CMAKE_CURRENT_SOURCE_DIR}/port/units.toml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${nw_registries} ${nw_tools}/nwunits.py)

# The files a generator writes, known at configure time (nwunits only, no libclang).
function(nw_gen_outputs tool var)
  execute_process(COMMAND ${Python3_EXECUTABLE} ${nw_tools}/${tool} --out ${nw_gen} --list-outputs
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${tool} --list-outputs failed:\n${err}")
  endif()
  string(REPLACE "\r" "" out "${out}")
  string(STRIP "${out}" out)
  string(REPLACE "\n" ";" out "${out}")
  list(TRANSFORM out PREPEND ${nw_gen}/)
  set(${var} ${out} PARENT_SCOPE)
endfunction()
nw_gen_outputs(tu_filter.py nw_wrappers)
nw_gen_outputs(gen_stubs.py nw_stubs)
nw_gen_outputs(gen_funcmap.py nw_funcmaps)
nw_gen_outputs(gen_ps2types.py nw_ps2types)
nw_gen_outputs(gen_converters.py nw_converters)
nw_gen_outputs(gen_dataimage.py nw_dataimage)
set(nw_data_registries ${nw_decomp}/config/pal103/data_symbols.toml ${nw_decomp}/config/pal103/data_map.toml
  ${nw_decomp}/config/pal103/sections.json ${nw_decomp}/config/pal103/symbol_addrs.txt)

# The tools rewrite only changed files, so an unchanged wrapper is not recompiled.
add_custom_command(OUTPUT ${nw_gen}/wrappers.stamp
  BYPRODUCTS ${nw_wrappers}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/tu_filter.py --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/wrappers.stamp
  DEPENDS ${nw_tools}/tu_filter.py ${nw_tools}/nwunits.py ${nw_tools}/nwclang.py ${nw_decomp_sources} ${nw_registries}
  COMMENT "Generating the decomp wrapper units"
  VERBATIM)
# Port definitions decide port vs stub, so these also follow src/.
add_custom_command(OUTPUT ${nw_gen}/stubs.stamp
  BYPRODUCTS ${nw_stubs} ${nw_funcmaps}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_stubs.py --out ${nw_gen}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_funcmap.py --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/stubs.stamp
  DEPENDS ${nw_tools}/gen_stubs.py ${nw_tools}/gen_funcmap.py ${nw_tools}/nwunits.py
    ${nw_decomp_sources} ${nw_registries} ${nw_port_sources}
  COMMENT "Generating the stubs and the function map"
  VERBATIM)
add_custom_command(OUTPUT ${nw_gen}/types.stamp
  BYPRODUCTS ${nw_gen}/typedb.json ${nw_ps2types}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/typedb.py --out ${nw_gen}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_ps2types.py --typedb ${nw_gen}/typedb.json --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/types.stamp
  DEPENDS ${nw_tools}/typedb.py ${nw_tools}/gen_ps2types.py ${nw_tools}/nwunits.py ${nw_tools}/nwclang.py
    ${nw_decomp_headers}
  COMMENT "Generating the type DB and the PS2 mirror structs"
  VERBATIM)
add_custom_command(OUTPUT ${nw_gen}/converters.stamp
  BYPRODUCTS ${nw_converters}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_converters.py --typedb ${nw_gen}/typedb.json --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/converters.stamp
  DEPENDS ${nw_gen}/types.stamp ${nw_tools}/gen_converters.py ${nw_tools}/gen_ps2types.py
    ${CMAKE_CURRENT_SOURCE_DIR}/port/convert.toml
  COMMENT "Generating the converters"
  VERBATIM)
add_custom_target(nuworld_types DEPENDS ${nw_gen}/types.stamp ${nw_gen}/converters.stamp)
# The data image: root definitions and tables (dataimage*.c) and the <unit>.data.inc every wrapper ends with.
add_custom_command(OUTPUT ${nw_gen}/dataimage.stamp
  BYPRODUCTS ${nw_dataimage}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_dataimage.py --typedb ${nw_gen}/typedb.json --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/dataimage.stamp
  DEPENDS ${nw_gen}/types.stamp ${nw_tools}/gen_dataimage.py ${nw_tools}/gen_converters.py ${nw_tools}/typedb.py
    ${nw_tools}/nwunits.py ${nw_tools}/nwclang.py ${nw_decomp_sources} ${nw_registries} ${nw_data_registries}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/data.toml ${CMAKE_CURRENT_SOURCE_DIR}/port/convert.toml
  COMMENT "Generating the data image"
  VERBATIM)
add_custom_target(nuworld_gen DEPENDS ${nw_gen}/wrappers.stamp ${nw_gen}/stubs.stamp ${nw_gen}/dataimage.stamp)

set(nw_decomp_flags
  -std=gnu11 -fgnu89-inline -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -ffp-contract=off
  -fsigned-char -DNW_HOST
  -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion -Werror=pointer-to-int-cast
  -Werror=int-to-pointer-cast -Werror=incompatible-pointer-types -Werror=incompatible-function-pointer-types
  -Werror=return-type)
if(MINGW)
  list(APPEND nw_decomp_flags -mno-ms-bitfields)
endif()
# The decomp's libm, not values clang folds with the build machine's libm (glibc and UCRT differ).
foreach(fn sin cos tan asin acos atan atan2 exp log log10 pow fmod)
  list(APPEND nw_decomp_flags -fno-builtin-${fn} -fno-builtin-${fn}f)
endforeach()
set(nw_decomp_includes ${nw_decomp}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(nw_sources ${nw_wrappers} ${nw_stubs} ${nw_funcmaps})
list(FILTER nw_sources INCLUDE REGEX "\\.c$")

add_library(nuworld_decomp OBJECT ${nw_sources})
add_dependencies(nuworld_decomp nuworld_gen)
target_include_directories(nuworld_decomp PRIVATE ${nw_decomp_includes})
target_compile_options(nuworld_decomp PRIVATE ${nw_decomp_flags})

# The converters include every decomp header, so they build with the decomp flags.
set(nw_data_sources src/data/convert.c src/data/elf.c src/data/image.c src/data/xhooks.c ${nw_converters})
list(FILTER nw_data_sources INCLUDE REGEX "\\.c$")
add_library(nuworld_data STATIC ${nw_data_sources})
add_dependencies(nuworld_data nuworld_types)
target_include_directories(nuworld_data PUBLIC ${nw_decomp_includes} ${nw_gen})
target_compile_options(nuworld_data PUBLIC ${nw_decomp_flags} PRIVATE -Wall -Wextra)
target_link_libraries(nuworld_data PUBLIC nuworld_port)

# Everything the decomp needs at link time: its objects, the data image and the SDK shims. Everything links it
# whole: nuworld_port's function map reads tables its decomp objects define, a cycle one-pass linkers (GNU ld) do
# not resolve, and for the executable the link is what proves the closure.
# Never build it with LTO: the data image defines the roots as raw bytes.
set(nw_game_sources ${nw_dataimage} src/gfx/dma.c src/gfx/nurndr.c src/gfx/nustream.c src/gfx/numtl.c
  src/gfx/nutex.c src/gfx/ps2txtr.c src/gfx/ptl.c src/hle/asmmem.c src/hle/asmstrm.c
  src/hle/nucamvu0.c src/hle/numath.c src/hle/numtxvu0.c src/hle/nuplnvu0.c src/hle/nuvecvu0.c src/port/game.c
  src/port/gcutscn.c src/port/heap.c src/port/nuanim.c src/port/nudat.c src/port/nuerror.c src/port/nugscn.c
  src/port/nuhgobj.c src/port/level.c src/port/options.c
  src/port/capture.c src/port/dump.c src/port/ps2video.c src/port/saveload.c src/port/scene.c src/port/shadcom.c
  src/port/cloudfx.c src/port/crate.c src/port/nubridge.c src/port/terrain.c src/port/trace.c
  src/shim/cdvd.c
  src/shim/ee.c
  src/shim/file.c src/shim/gs.c src/shim/libc.c src/shim/mc.c src/shim/pad.c src/shim/sif.c)
list(FILTER nw_game_sources INCLUDE REGEX "\\.c$")
add_library(nuworld_game STATIC $<TARGET_OBJECTS:nuworld_decomp> ${nw_game_sources})
add_dependencies(nuworld_game nuworld_gen nuworld_types)
target_include_directories(nuworld_game PRIVATE ${nw_decomp_includes} ${nw_gen})
target_compile_options(nuworld_game PRIVATE ${nw_decomp_flags} -Wall -Wextra)
# LINK_ONLY: nuworld_data's decomp flags must not reach the executable's own (C++) sources.
target_link_libraries(nuworld_game PUBLIC $<LINK_ONLY:nuworld_data> nuworld_audio nuworld_gfx nuworld_port nuworld_save
  nuworld_vfs)
target_link_libraries(crashwoc-nuworld PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
# The renderer draws the game thread's frame packets, so it comes with the decomp.
target_sources(crashwoc-nuworld PRIVATE src/gfx/backend.c src/gfx/view.c src/platform/input.c)
target_compile_definitions(crashwoc-nuworld PRIVATE NW_WITH_GAME)

if(NUWORLD_TESTS)
  # Building it proves the mirror layouts (_Static_asserts); running it checks the bitfields.
  add_executable(nuworld_test_ps2types tests/c/test_ps2types.c)
  add_dependencies(nuworld_test_ps2types nuworld_types)
  target_include_directories(nuworld_test_ps2types PRIVATE ${nw_gen})
  target_compile_options(nuworld_test_ps2types PRIVATE -Wall -Wextra)
  if(MINGW)
    target_compile_options(nuworld_test_ps2types PRIVATE -mno-ms-bitfields)
  endif()
  add_test(NAME ps2types COMMAND nuworld_test_ps2types)

  # Local only: the data image from the imported disc of NUWORLD_TEST_DATA (skipped without it).
  add_test(NAME data-image-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --check-data)
  set_tests_properties(data-image-retail PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: the game from the imported disc until it leaves its first load screen (BGLoadSfx's loader ends).
  add_test(NAME boot-check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --stepped --boot-check --loads 1)
  set_tests_properties(boot-check PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: on until the title cut movie has loaded (BGLoadCutScene's loader ends).
  add_test(NAME boot-cut
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --stepped --boot-check --loads 2)
  set_tests_properties(boot-cut PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: the title cut's sound trace on the stepped clock is the same on every run.
  add_test(NAME boot-cut-trace
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root})
  set_tests_properties(boot-cut-trace PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: that trace against PCSX2's (tools/pcsx2_trace.py).
  add_test(NAME boot-cut-pcsx2
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root}
      --pcsx2 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_cut.txt)
  set_tests_properties(boot-cut-pcsx2 PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: with instant loading the cut keeps PCSX2's timing from the logos key-on, and the stub hits.
  add_test(NAME boot-cut-instant
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root}
      --pcsx2 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_cut.txt --instant)
  set_tests_properties(boot-cut-instant PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: the title cut's frames (--capture) labelled as PCSX2's savestates (tools/pcsx2_capture.py); with
  # PCSX2's screens in {root}/captures/pcsx2 also side by side in title-frames/pairs.
  add_test(NAME title-frames-pcsx2
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/title_capture.py $<TARGET_FILE:crashwoc-nuworld> {root}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_frames.txt ${CMAKE_CURRENT_BINARY_DIR}/title-frames)
  set_tests_properties(title-frames-pcsx2 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  # Local only: every .gsc of the imported disc loads with no unknown block and its geometry decodes.
  add_test(NAME check-scenes-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --check-scenes)
  set_tests_properties(check-scenes-retail PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: every .gsc and .ghg of the imported disc loaded and drawn by the decomp into a frame packet with
  # draws.
  add_test(NAME scene-render-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --scene-check all)
  set_tests_properties(scene-render-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  # Local only: those lines against the crashwoc tooling's decode ($CRASHWOC_TOOLING; skipped without it).
  add_test(NAME scene-hashes-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/gsc_hash.py --data-dir {root} --compare $<TARGET_FILE:crashwoc-nuworld>)
  set_tests_properties(scene-hashes-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  # Local only: the VU1 microcode of the imported boot ELF decodes and maps completely.
  add_test(NAME vu1-disasm-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/vu1dis.py --data-dir {root} --check --expect 16,24,44)
  set_tests_properties(vu1-disasm-retail PROPERTIES SKIP_RETURN_CODE 77)
  # Local only: levels entered headless (--level) run with no stub hit beyond the shared ones their issues own.
  add_test(NAME levels-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/level_check.py $<TARGET_FILE:crashwoc-nuworld> {root} hub
      farm fire_fly airship space_a)
  set_tests_properties(levels-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)

  # The NuDat override under the kept nufile layer; nudat-retail is local only (NUWORLD_TEST_DATA).
  add_executable(nuworld_test_nudat tests/c/test_nudat.c)
  target_include_directories(nuworld_test_nudat PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_nudat PRIVATE NW_HOST)
  target_compile_options(nuworld_test_nudat PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_nudat PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nudat COMMAND nuworld_test_nudat ${CMAKE_CURRENT_BINARY_DIR}/test-nudat)
  add_test(NAME nudat-retail COMMAND nuworld_test_nudat retail)
  set_tests_properties(nudat-retail PROPERTIES SKIP_RETURN_CODE 77)

  # The loaders' host records beside the game's heaps.
  add_executable(nuworld_test_heap tests/c/test_heap.c)
  target_include_directories(nuworld_test_heap PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_heap PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_heap PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME heap COMMAND nuworld_test_heap)

  # The .gsc loader override on a synthetic scene; built with the decomp flags for its struct layouts. nugscn-retail
  # checks the animation library of every .gsc of NUWORLD_TEST_DATA.
  add_executable(nuworld_test_nugscn tests/c/test_nugscn.c)
  add_dependencies(nuworld_test_nugscn nuworld_types)
  target_include_directories(nuworld_test_nugscn PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nugscn PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nugscn PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nugscn COMMAND nuworld_test_nugscn ${CMAKE_CURRENT_BINARY_DIR}/test-nugscn)
  add_test(NAME nugscn-retail COMMAND nuworld_test_nugscn retail)
  set_tests_properties(nugscn-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)

  # The .ghg loader override on a synthetic file; nughg-retail loads every .ghg of NUWORLD_TEST_DATA.
  add_executable(nuworld_test_nughg tests/c/test_nughg.c)
  add_dependencies(nuworld_test_nughg nuworld_types)
  target_include_directories(nuworld_test_nughg PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nughg PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nughg PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nughg COMMAND nuworld_test_nughg ${CMAKE_CURRENT_BINARY_DIR}/test-nughg)
  add_test(NAME nughg-retail COMMAND nuworld_test_nughg retail)
  set_tests_properties(nughg-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  # The .cut loader override on a synthetic file; nucut-retail loads every .cut of NUWORLD_TEST_DATA.
  add_executable(nuworld_test_nucut tests/c/test_nucut.c)
  add_dependencies(nuworld_test_nucut nuworld_types)
  target_include_directories(nuworld_test_nucut PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nucut PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nucut PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nucut COMMAND nuworld_test_nucut ${CMAKE_CURRENT_BINARY_DIR}/test-nucut)
  add_test(NAME nucut-retail COMMAND nuworld_test_nucut retail)
  set_tests_properties(nucut-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  # The .ani loader override on a synthetic file; nuanim-retail loads every .ani and .bsa of NUWORLD_TEST_DATA.
  add_executable(nuworld_test_nuanim tests/c/test_nuanim.c)
  add_dependencies(nuworld_test_nuanim nuworld_types)
  target_include_directories(nuworld_test_nuanim PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nuanim PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nuanim PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nuanim COMMAND nuworld_test_nuanim ${CMAKE_CURRENT_BINARY_DIR}/test-nuanim)
  add_test(NAME nuanim-retail COMMAND nuworld_test_nuanim retail)
  set_tests_properties(nuanim-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  # The .ter loader override on a synthetic file, then under terraininit; terrain-retail loads every .ter of
  # NUWORLD_TEST_DATA over its level scene.
  add_executable(nuworld_test_terrain tests/c/test_terrain.c)
  add_dependencies(nuworld_test_terrain nuworld_types)
  target_include_directories(nuworld_test_terrain PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_terrain PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_terrain PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME terrain COMMAND nuworld_test_terrain ${CMAKE_CURRENT_BINARY_DIR}/test-terrain)
  add_test(NAME terrain-retail COMMAND nuworld_test_terrain retail)
  set_tests_properties(terrain-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)

  # The .shd loader override on a synthetic file; shadcom-retail loads every .shd in CHARS.DAT of NUWORLD_TEST_DATA.
  add_executable(nuworld_test_shadcom tests/c/test_shadcom.c)
  add_dependencies(nuworld_test_shadcom nuworld_types)
  target_include_directories(nuworld_test_shadcom PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_shadcom PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_shadcom PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME shadcom COMMAND nuworld_test_shadcom ${CMAKE_CURRENT_BINARY_DIR}/test-shadcom)
  add_test(NAME shadcom-retail COMMAND nuworld_test_shadcom retail)
  set_tests_properties(shadcom-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  # The NUSOUND.IRX port under the decomp's own nusound.c.
  add_executable(nuworld_test_nusound tests/c/test_nusound.c)
  target_include_directories(nuworld_test_nusound PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_nusound PRIVATE NW_HOST)
  target_compile_options(nuworld_test_nusound PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_nusound PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nusound COMMAND nuworld_test_nusound ${CMAKE_CURRENT_BINARY_DIR}/test-nusound)
  add_test(NAME nusound-retail COMMAND nuworld_test_nusound retail)
  add_test(NAME nusound-retail-instant COMMAND nuworld_test_nusound retail instant)
  set_tests_properties(nusound-retail nusound-retail-instant PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  # The port's Nu render layer under the decomp code that drives it.
  add_executable(nuworld_test_rndr tests/c/test_rndr.c)
  add_dependencies(nuworld_test_rndr nuworld_types)
  target_include_directories(nuworld_test_rndr PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_rndr PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_rndr PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME rndr COMMAND nuworld_test_rndr)

  # A texanim script stepping a material's texture into the frame packet; texanim-retail runs the title logo's
  # flipbook from NUWORLD_TEST_DATA.
  add_executable(nuworld_test_texanim tests/c/test_texanim.c)
  add_dependencies(nuworld_test_texanim nuworld_types)
  target_include_directories(nuworld_test_texanim PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_texanim PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_texanim PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME texanim COMMAND nuworld_test_texanim ${CMAKE_CURRENT_BINARY_DIR}/test-texanim)
  add_test(NAME texanim-retail COMMAND nuworld_test_texanim retail)
  set_tests_properties(texanim-retail PROPERTIES SKIP_RETURN_CODE 77)

  # Text3D's glyphs on a synthetic scene; font3d-retail loads STUFF\FONT.GSC of NUWORLD_TEST_DATA with the data image.
  add_executable(nuworld_test_font3d tests/c/test_font3d.c)
  add_dependencies(nuworld_test_font3d nuworld_types)
  target_include_directories(nuworld_test_font3d PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_font3d PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_font3d PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME font3d COMMAND nuworld_test_font3d ${CMAKE_CURRENT_BINARY_DIR}/test-font3d)
  add_test(NAME font3d-retail COMMAND nuworld_test_font3d retail)
  set_tests_properties(font3d-retail PROPERTIES SKIP_RETURN_CODE 77)

  # The editor units' runtime half on every level file of NUWORLD_TEST_DATA, loaded over the level scene.
  add_executable(nuworld_test_edunits tests/c/test_edunits.c)
  add_dependencies(nuworld_test_edunits nuworld_types)
  target_include_directories(nuworld_test_edunits PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_edunits PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_edunits PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  foreach(unit edobj edanim edgra edptl)
    add_test(NAME ${unit}-retail COMMAND nuworld_test_edunits ${unit})
    set_tests_properties(${unit}-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  endforeach()

  # The clock, the game thread's yield points, the vblank interrupts and the EE semaphores, on a stepped clock.
  add_executable(nuworld_test_clock tests/c/test_clock.c)
  target_include_directories(nuworld_test_clock PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_clock PRIVATE NW_HOST)
  target_compile_options(nuworld_test_clock PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_clock PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME clock COMMAND nuworld_test_clock)

  # The EE threads under the decomp's bgload, on a stepped clock, then on a real game thread.
  add_executable(nuworld_test_ee tests/c/test_ee.c)
  target_include_directories(nuworld_test_ee PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_ee PRIVATE NW_HOST)
  target_compile_options(nuworld_test_ee PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_ee PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME ee COMMAND nuworld_test_ee)

  # The pad shim under the decomp's NuPs2ReadPad, and a virtual gamepad and the keyboard as the pad.
  add_executable(nuworld_test_pad tests/c/test_pad.c src/platform/input.c)
  target_include_directories(nuworld_test_pad PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_pad PRIVATE NW_HOST)
  target_compile_options(nuworld_test_pad PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_pad PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME pad COMMAND nuworld_test_pad)

  # The port's gamelib/saveload under the decomp's UpdateSaveSlots. saveload-card imports the PS2 card image
  # NUWORLD_TEST_CARD (skipped without it); saveload-toml reads what the save tests wrote with Python's tomllib.
  add_executable(nuworld_test_saveload tests/c/test_saveload.c)
  add_dependencies(nuworld_test_saveload nuworld_types)
  target_include_directories(nuworld_test_saveload PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_saveload PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_saveload PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME saveload COMMAND nuworld_test_saveload ${CMAKE_CURRENT_BINARY_DIR}/test-saveload)
  add_test(NAME saveload-card COMMAND nuworld_test_saveload ${CMAKE_CURRENT_BINARY_DIR}/test-saveload-card card)
  set_tests_properties(saveload-card PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME saveload-toml COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/save_toml.py
    ${CMAKE_CURRENT_BINARY_DIR}/test-savefile ${CMAKE_CURRENT_BINARY_DIR}/test-saveload)
  set_tests_properties(savefile saveload PROPERTIES FIXTURES_SETUP save_files)
  set_tests_properties(saveload-toml PROPERTIES FIXTURES_REQUIRED save_files)

  add_executable(nuworld_test_hle tests/c/test_hle.c)
  target_include_directories(nuworld_test_hle PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_hle PRIVATE NW_HOST)
  target_compile_options(nuworld_test_hle PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_hle PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME hle COMMAND nuworld_test_hle)

  # Brings its own funcmap (fake code addresses).
  add_executable(nuworld_test_convert tests/c/test_convert.c)
  target_compile_options(nuworld_test_convert PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_convert PRIVATE nuworld_data)
  add_test(NAME convert COMMAND nuworld_test_convert)

  get_filename_component(nw_cc_dir ${CMAKE_C_COMPILER} DIRECTORY)
  find_program(NW_LLVM_NM llvm-nm HINTS ${nw_cc_dir})
  if(NW_LLVM_NM)
    add_test(NAME decomp-globals
      COMMAND ${CMAKE_COMMAND} -DNM=${NW_LLVM_NM}
        "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:nuworld_decomp>;$<TARGET_OBJECTS:nuworld_game>,|>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_globals.cmake)
    # Everything the executable links for the game, listed in a file (the command line would be too long).
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/closure-objects.txt
      CONTENT "$<JOIN:$<TARGET_OBJECTS:nuworld_decomp>;$<TARGET_OBJECTS:nuworld_game>;$<TARGET_OBJECTS:nuworld_data>;$<TARGET_OBJECTS:nuworld_port>,\n>\n")
    add_test(NAME decomp-closure
      COMMAND ${Python3_EXECUTABLE} ${nw_tools}/check_closure.py --nm ${NW_LLVM_NM} --decomp ${nw_decomp}
        --objects ${CMAKE_CURRENT_BINARY_DIR}/closure-objects.txt)
  else()
    message(WARNING "no llvm-nm next to ${CMAKE_C_COMPILER}: the decomp-globals test is skipped")
  endif()
endif()
