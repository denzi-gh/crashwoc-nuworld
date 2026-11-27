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
set(nw_gen ${CMAKE_CURRENT_BINARY_DIR}/gen)

# The host compiles the patched tree (tools/patches.py), made at configure time and refreshed on changes.
set(nw_decomp_src ${CMAKE_CURRENT_SOURCE_DIR}/decomp)
set(nw_decomp ${CMAKE_CURRENT_BINARY_DIR}/decomp)
file(GLOB_RECURSE nw_decomp_pristine CONFIGURE_DEPENDS ${nw_decomp_src}/src/*.c ${nw_decomp_src}/include/*.h
  ${nw_decomp_src}/config/pal103/*)
file(GLOB_RECURSE nw_patch_inputs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/port/patches/*
  ${CMAKE_CURRENT_SOURCE_DIR}/port/values/*)
execute_process(COMMAND ${Python3_EXECUTABLE} ${nw_tools}/patches.py --decomp ${nw_decomp_src} apply --out ${nw_decomp}
  OUTPUT_QUIET ERROR_VARIABLE err RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "tools/patches.py apply failed:\n${err}")
endif()

file(GLOB_RECURSE nw_decomp_sources ${nw_decomp}/src/*.c ${nw_decomp}/include/*.h)
file(GLOB_RECURSE nw_decomp_headers ${nw_decomp}/include/*.h)
file(GLOB_RECURSE nw_port_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE nw_registries ${nw_decomp}/config/pal103/status/*.toml)
list(APPEND nw_registries ${nw_decomp}/config/pal103/units.toml ${nw_decomp}/config/pal103/functions.toml)
set(nw_data_registries ${nw_decomp}/config/pal103/data_symbols.toml ${nw_decomp}/config/pal103/data_map.toml
  ${nw_decomp}/config/pal103/sections.json ${nw_decomp}/config/pal103/symbol_addrs.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/port/units.toml
  ${nw_tools}/nwunits.py ${nw_tools}/patches.py)

# Every generator depends on the stamp: that, not the byproducts, puts this command into their targets.
set(nw_tree ${nw_decomp}/.nw-tree.stamp)
add_custom_command(OUTPUT ${nw_tree}
  BYPRODUCTS ${nw_decomp_sources} ${nw_registries} ${nw_data_registries}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/patches.py --decomp ${nw_decomp_src} apply --out ${nw_decomp}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_tree}
  DEPENDS ${nw_tools}/patches.py ${nw_patch_inputs} ${nw_decomp_pristine}
  COMMENT "Applying the port patches to the decomp"
  VERBATIM)
list(APPEND nw_registries ${nw_tree} ${CMAKE_CURRENT_SOURCE_DIR}/port/units.toml)

function(nw_gen_outputs tool var)
  execute_process(COMMAND ${Python3_EXECUTABLE} ${nw_tools}/${tool} --out ${nw_gen} --list-outputs ${ARGN}
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
nw_gen_outputs(tu_filter.py nw_wrappers --decomp ${nw_decomp})
nw_gen_outputs(gen_stubs.py nw_stubs --decomp ${nw_decomp})
nw_gen_outputs(gen_funcmap.py nw_funcmaps --decomp ${nw_decomp})
nw_gen_outputs(gen_ps2types.py nw_ps2types)
nw_gen_outputs(gen_converters.py nw_converters)
nw_gen_outputs(gen_dataimage.py nw_dataimage --decomp ${nw_decomp})

# The tools rewrite only changed files, so an unchanged wrapper is not recompiled.
add_custom_command(OUTPUT ${nw_gen}/wrappers.stamp
  BYPRODUCTS ${nw_wrappers}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/tu_filter.py --decomp ${nw_decomp} --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/wrappers.stamp
  DEPENDS ${nw_tools}/tu_filter.py ${nw_tools}/nwunits.py ${nw_tools}/nwclang.py ${nw_decomp_sources} ${nw_registries}
  COMMENT "Generating the decomp wrapper units"
  VERBATIM)
# Port definitions decide port vs stub, so these also follow src/.
add_custom_command(OUTPUT ${nw_gen}/stubs.stamp
  BYPRODUCTS ${nw_stubs} ${nw_funcmaps}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_stubs.py --decomp ${nw_decomp} --out ${nw_gen}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_funcmap.py --decomp ${nw_decomp} --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/stubs.stamp
  DEPENDS ${nw_tools}/gen_stubs.py ${nw_tools}/gen_funcmap.py ${nw_tools}/nwunits.py
    ${nw_decomp_sources} ${nw_registries} ${nw_port_sources}
  COMMENT "Generating the stubs and the function map"
  VERBATIM)
add_custom_command(OUTPUT ${nw_gen}/types.stamp
  BYPRODUCTS ${nw_gen}/typedb.json ${nw_ps2types}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/typedb.py --decomp ${nw_decomp} --out ${nw_gen}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_ps2types.py --typedb ${nw_gen}/typedb.json --out ${nw_gen}
  COMMAND ${CMAKE_COMMAND} -E touch ${nw_gen}/types.stamp
  DEPENDS ${nw_tools}/typedb.py ${nw_tools}/gen_ps2types.py ${nw_tools}/nwunits.py ${nw_tools}/nwclang.py
    ${nw_decomp_headers} ${nw_tree}
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
add_custom_command(OUTPUT ${nw_gen}/dataimage.stamp
  BYPRODUCTS ${nw_dataimage}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/gen_dataimage.py --decomp ${nw_decomp} --typedb ${nw_gen}/typedb.json
    --out ${nw_gen}
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

list(APPEND nw_decomp_flags -ffp-exception-behavior=maytrap)
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

# Linked whole: nuworld_port's function map reads tables these objects define, a cycle GNU ld does not resolve.
# No LTO: the data image defines its roots as raw bytes.
set(nw_game_sources ${nw_dataimage} src/gfx/dma.c src/gfx/fx2.c src/gfx/nulight.c src/gfx/nurndr.c src/gfx/nustream.c
  src/gfx/numtl.c src/gfx/nutex.c src/gfx/nuwater.c src/gfx/ps2txtr.c src/gfx/ptl.c src/hle/asmmem.c src/hle/asmstrm.c
  src/hle/eefloat.c src/hle/nuanim.c src/hle/nucamvu0.c src/hle/numath.c src/hle/numtxvu0.c src/hle/nuplnvu0.c
  src/hle/nuvecvu0.c src/port/fptrap.c src/port/game.c src/port/gcutscn.c src/port/heap.c src/port/nuanim.c
  src/port/hz.c src/port/nudat.c src/port/nuerror.c src/port/nugscn.c src/port/rev.c
  src/port/nuhgobj.c src/port/level.c src/port/options.c src/port/padscript.c src/port/presence.c
  src/port/capture.c src/port/disc.c src/port/dump.c src/port/ps2video.c src/port/saves.c src/port/scene.c src/port/shadcom.c
  src/port/autosave.c src/port/cloudfx.c src/port/crate.c src/port/font3d.c src/port/menu.c src/port/nameentry.c src/port/names.c src/port/savemenu.c src/port/nubridge.c src/port/terrain.c
  src/port/text.c src/port/subtitles.c src/port/trace.c src/port/wide.c
  src/gameplay/ai.c src/gameplay/air.c src/gameplay/airindicator.c src/gameplay/atlasphere.c src/gameplay/bosscut.c src/gameplay/bosshealth.c
  src/gameplay/bosspics.c
  src/gameplay/camera.c src/gameplay/chase.c src/gameplay/cheats.c src/gameplay/crash.c src/gameplay/creature.c src/gameplay/credits.c src/gameplay/cut.c src/gameplay/debris.c
  src/gameplay/edbits.c src/gameplay/edges.c src/gameplay/firefly.c src/gameplay/fixes.c
  src/gameplay/game.c src/gameplay/game_obj.c src/gameplay/gems.c src/gameplay/gameplay.c src/gameplay/gcutscn.c src/gameplay/ground.c src/gameplay/hazards.c
  src/gameplay/hitboxes.c src/gameplay/hubcrunch.c src/gameplay/hubgem.c src/gameplay/jeep.c src/gameplay/line2d.c src/gameplay/mech.c src/gameplay/minigame.c src/gameplay/monkeybars.c
  src/gameplay/move.c src/gameplay/numtl.c src/gameplay/nurndr.c src/gameplay/panel.c src/gameplay/plane.c src/gameplay/slidespin.c src/gameplay/spacefighter.c
  src/gameplay/steadycam.c src/gameplay/submarine.c
  src/gameplay/terrain.c src/gameplay/timetrial.c src/gameplay/titlemenu.c src/gameplay/vehicle.c src/gameplay/vehiclehints.c
  src/platform/highheap.c
  src/shim/cdvd.c
  src/shim/ee.c
  src/shim/file.c src/shim/gs.c src/shim/libc.c src/shim/pad.c src/shim/sif.c)
list(FILTER nw_game_sources INCLUDE REGEX "\\.c$")
add_library(nuworld_game STATIC $<TARGET_OBJECTS:nuworld_decomp> ${nw_game_sources})
nw_embed_dir(nuworld_game nw_prompt_icons ${CMAKE_CURRENT_SOURCE_DIR}/assets/prompts)
nw_embed(nuworld_game nw_hubgem_toml ${CMAKE_CURRENT_SOURCE_DIR}/assets/hubgem.toml)
set(nw_title_logo ${nw_gen}/nw_title_logo.ghg)
add_custom_command(OUTPUT ${nw_title_logo}
  COMMAND ${Python3_EXECUTABLE} ${nw_tools}/title_logo.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/nuworld-icon.svg
    ${nw_title_logo}
  DEPENDS ${nw_tools}/title_logo.py ${nw_tools}/nughg.py ${nw_tools}/nuvif.py
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/nuworld-icon.svg
  VERBATIM)
nw_embed(nuworld_game nw_title_logo_ghg ${nw_title_logo})
add_dependencies(nuworld_game nuworld_gen nuworld_types)
target_include_directories(nuworld_game PRIVATE ${nw_decomp_includes} ${nw_gen})
target_compile_options(nuworld_game PRIVATE ${nw_decomp_flags} -Wall -Wextra)
# The EE's double rounding.
set_target_properties(nuworld_decomp nuworld_game PROPERTIES
  C_COMPILER_LAUNCHER "${Python3_EXECUTABLE};${nw_tools}/softdouble.py")
set_property(SOURCE ${nw_sources} ${nw_game_sources} APPEND PROPERTY OBJECT_DEPENDS ${nw_tools}/softdouble.py)
# LINK_ONLY: nuworld_data's decomp flags must not reach the executable's own (C++) sources.
target_link_libraries(nuworld_game PUBLIC $<LINK_ONLY:nuworld_data> nuworld_audio nuworld_gfx nuworld_port nuworld_save
  nuworld_vfs)
target_link_libraries(crashwoc-nuworld PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
# The renderer draws the game thread's frame packets, so it comes with the decomp.
target_sources(crashwoc-nuworld PRIVATE src/frontend/cheatpanel.c src/frontend/controlpanel.c src/frontend/profiles.c src/frontend/saveexport.c src/frontend/saveimport.c src/frontend/video.c src/gfx/backend.c src/gfx/view.c src/platform/input.c)
target_compile_definitions(crashwoc-nuworld PRIVATE NW_WITH_GAME)

if(NUWORLD_FUZZ)
  add_executable(nuworld_fuzz_loaders tests/fuzz/fuzz_loaders.c)
  target_include_directories(nuworld_fuzz_loaders PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_definitions(nuworld_fuzz_loaders PRIVATE NW_HOST)
  target_compile_options(nuworld_fuzz_loaders PRIVATE -Wall -Wextra -fsanitize=fuzzer)
  target_link_options(nuworld_fuzz_loaders PRIVATE -fsanitize=fuzzer)
  target_link_libraries(nuworld_fuzz_loaders PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game> nuworld_frontend)
  if(NUWORLD_TESTS)
    add_test(NAME fuzz_loaders COMMAND nuworld_fuzz_loaders -runs=0 -detect_leaks=0 ${nw_fuzz_loader_seeds})
    set_tests_properties(fuzz_loaders PROPERTIES FIXTURES_REQUIRED fuzz_corpus)
  endif()
endif()

if(NUWORLD_TESTS)
  # Building it proves the mirror layouts (_Static_asserts); running it checks the bitfields.
  add_executable(nuworld_test_ps2types tests/c/test_ps2types.c)
  add_dependencies(nuworld_test_ps2types nuworld_types)
  target_include_directories(nuworld_test_ps2types PRIVATE ${nw_gen})
  target_compile_options(nuworld_test_ps2types PRIVATE -Wall -Wextra)
  add_test(NAME ps2types COMMAND nuworld_test_ps2types)

  add_test(NAME data-image-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --check-data)
  set_tests_properties(data-image-retail PROPERTIES SKIP_RETURN_CODE 77)
  # --loads 1: until the first load screen ends (BGLoadSfx's loader).
  add_test(NAME boot-check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --stepped --boot-check --loads 1)
  set_tests_properties(boot-check PROPERTIES SKIP_RETURN_CODE 77)
  # --loads 2: until the title cut movie has loaded (BGLoadCutScene's loader).
  add_test(NAME boot-cut
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --stepped --boot-check --loads 2)
  set_tests_properties(boot-cut PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME boot-cut-trace
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root})
  set_tests_properties(boot-cut-trace PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME boot-cut-pcsx2
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root}
      --pcsx2 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_cut.txt)
  set_tests_properties(boot-cut-pcsx2 PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME boot-cut-instant
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cut_trace.py $<TARGET_FILE:crashwoc-nuworld> {root}
      --pcsx2 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_cut.txt --instant)
  set_tests_properties(boot-cut-instant PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME title-frames-pcsx2
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/title_capture.py $<TARGET_FILE:crashwoc-nuworld> {root}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pcsx2_title_frames.txt ${CMAKE_CURRENT_BINARY_DIR}/title-frames)
  set_tests_properties(title-frames-pcsx2 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  add_test(NAME check-scenes-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --check-scenes)
  set_tests_properties(check-scenes-retail PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME scene-render-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py $<TARGET_FILE:crashwoc-nuworld>
      --data-dir {root} --scene-check all)
  set_tests_properties(scene-render-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  # Needs $CRASHWOC_TOOLING (skipped without it).
  add_test(NAME scene-hashes-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/gsc_hash.py --data-dir {root} --compare $<TARGET_FILE:crashwoc-nuworld>)
  set_tests_properties(scene-hashes-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  # Needs $CRASHWOC_TOOLING and $WRATHSDK.
  add_test(NAME disc-decode-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/disc_check.py --data-dir {root} --compare $<TARGET_FILE:crashwoc-nuworld>)
  set_tests_properties(disc-decode-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  add_test(NAME vu1-disasm-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/vu1dis.py --data-dir {root} --check --expect 16,24,44)
  set_tests_properties(vu1-disasm-retail PROPERTIES SKIP_RETURN_CODE 77)
  # Every gameplay level: LData 0-34, 36 and 37.
  add_test(NAME levels-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/level_check.py $<TARGET_FILE:crashwoc-nuworld> {root} -j 8
      cold_a volcano water west_a earth avalanch coral snow jungle snow_b castle jungle_a garden farm toonarmy
      fire_r castle_a castle_c fire_fly western droid earth_b fire_b water_b weath_b space_b space_a cast_bug west_b
      cold_b weather ballsof s_balls space_r seashell airship hub)
  set_tests_properties(levels-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  # The same levels as the PAL v1.03 PS2's game at 50 Hz, its recordings (.ppd) on the pad.
  add_test(NAME levels-pal103-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/level_check.py $<TARGET_FILE:crashwoc-nuworld> {root} -j 8 --revision 1.03
      cold_a volcano water west_a earth avalanch coral snow jungle snow_b castle jungle_a garden farm toonarmy
      fire_r castle_a castle_c fire_fly western droid earth_b fire_b water_b weath_b space_b space_a cast_bug west_b
      cold_b weather ballsof s_balls space_r seashell airship hub)
  set_tests_properties(levels-pal103-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  # Title, intro, hub, a level and the hub again keep as many scene GPU objects alive at both hubs.
  add_test(NAME scene-free-retail
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local.py ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/level_check.py $<TARGET_FILE:crashwoc-nuworld> {root} --vblanks 100
      intro,farm,hub)
  set_tests_properties(scene-free-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)

  add_executable(nuworld_test_nudat tests/c/test_nudat.c)
  target_include_directories(nuworld_test_nudat PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_nudat PRIVATE NW_HOST)
  target_compile_options(nuworld_test_nudat PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_nudat PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nudat COMMAND nuworld_test_nudat ${CMAKE_CURRENT_BINARY_DIR}/test-nudat)
  add_test(NAME nudat-retail COMMAND nuworld_test_nudat retail)
  set_tests_properties(nudat-retail PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(nuworld_test_heap tests/c/test_heap.c)
  target_include_directories(nuworld_test_heap PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_heap PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_heap PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME heap COMMAND nuworld_test_heap)
  add_executable(nuworld_test_wide tests/c/test_wide.c)
  target_include_directories(nuworld_test_wide PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_wide PRIVATE NW_HOST)
  target_compile_options(nuworld_test_wide PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_wide PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME wide COMMAND nuworld_test_wide)
  add_executable(nuworld_test_hz tests/c/test_hz.c)
  target_include_directories(nuworld_test_hz PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_hz PRIVATE NW_HOST)
  target_compile_options(nuworld_test_hz PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_hz PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME hz COMMAND nuworld_test_hz)
  add_executable(nuworld_test_rev tests/c/test_rev.c)
  target_include_directories(nuworld_test_rev PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_rev PRIVATE NW_HOST)
  target_compile_options(nuworld_test_rev PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_rev PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME rev COMMAND nuworld_test_rev)
  add_executable(nuworld_test_canon tests/c/test_canon.c)
  target_compile_options(nuworld_test_canon PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_canon PRIVATE nuworld_vfs)
  add_test(NAME canon COMMAND nuworld_test_canon ${CMAKE_CURRENT_BINARY_DIR}/test-canon)
  add_executable(nuworld_test_disc tests/c/test_disc.c)
  target_include_directories(nuworld_test_disc PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_disc PRIVATE NW_HOST)
  target_compile_options(nuworld_test_disc PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_disc PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME disc COMMAND nuworld_test_disc)
  add_executable(nuworld_test_gameplay tests/c/test_gameplay.c)
  target_include_directories(nuworld_test_gameplay PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_gameplay PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_gameplay PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME gameplay COMMAND nuworld_test_gameplay)
  add_executable(nuworld_test_menu tests/c/test_menu.c)
  target_include_directories(nuworld_test_menu PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_menu PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_menu PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME menu COMMAND nuworld_test_menu)
  add_executable(nuworld_test_subtitles tests/c/test_subtitles.c)
  target_include_directories(nuworld_test_subtitles PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_subtitles PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_subtitles PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME subtitles COMMAND nuworld_test_subtitles)
  # NUWORLD_FP_TRAPS: a NaN on the game thread stops the program with where it was made; "all" goes on past it.
  add_executable(nuworld_test_fptrap tests/c/test_fptrap.c)
  target_compile_options(nuworld_test_fptrap PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_fptrap PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME fp-traps COMMAND nuworld_test_fptrap)
  set_tests_properties(fp-traps PROPERTIES ENVIRONMENT NUWORLD_FP_TRAPS=1
    PASS_REGULAR_EXPRESSION "fp trap: invalid operation at .*nuworld_test_fptrap")
  add_test(NAME fp-traps-off COMMAND nuworld_test_fptrap)
  set_tests_properties(fp-traps-off PROPERTIES ENVIRONMENT NUWORLD_FP_TRAPS=0)
  add_test(NAME fp-traps-all COMMAND nuworld_test_fptrap)
  set_tests_properties(fp-traps-all PROPERTIES ENVIRONMENT NUWORLD_FP_TRAPS=all
    PASS_REGULAR_EXPRESSION "fp trap: invalid operation at .*fptrap: the game thread returned")

  add_executable(nuworld_test_nugscn tests/c/test_nugscn.c)
  add_dependencies(nuworld_test_nugscn nuworld_types)
  target_include_directories(nuworld_test_nugscn PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nugscn PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nugscn PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nugscn COMMAND nuworld_test_nugscn ${CMAKE_CURRENT_BINARY_DIR}/test-nugscn)
  add_test(NAME nugscn-retail COMMAND nuworld_test_nugscn retail)
  set_tests_properties(nugscn-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)

  add_executable(nuworld_test_nughg tests/c/test_nughg.c)
  add_dependencies(nuworld_test_nughg nuworld_types)
  target_include_directories(nuworld_test_nughg PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nughg PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nughg PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nughg COMMAND nuworld_test_nughg ${CMAKE_CURRENT_BINARY_DIR}/test-nughg)
  add_test(NAME nughg-retail COMMAND nuworld_test_nughg retail)
  set_tests_properties(nughg-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(nuworld_test_nucut tests/c/test_nucut.c)
  add_dependencies(nuworld_test_nucut nuworld_types)
  target_include_directories(nuworld_test_nucut PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nucut PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nucut PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nucut COMMAND nuworld_test_nucut ${CMAKE_CURRENT_BINARY_DIR}/test-nucut)
  add_test(NAME nucut-retail COMMAND nuworld_test_nucut retail)
  set_tests_properties(nucut-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(nuworld_test_nuanim tests/c/test_nuanim.c)
  add_dependencies(nuworld_test_nuanim nuworld_types)
  target_include_directories(nuworld_test_nuanim PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_nuanim PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nuanim PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nuanim COMMAND nuworld_test_nuanim ${CMAKE_CURRENT_BINARY_DIR}/test-nuanim)
  add_test(NAME nuanim-retail COMMAND nuworld_test_nuanim retail)
  set_tests_properties(nuanim-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(nuworld_test_terrain tests/c/test_terrain.c)
  add_dependencies(nuworld_test_terrain nuworld_types)
  target_include_directories(nuworld_test_terrain PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_terrain PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_terrain PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME terrain COMMAND nuworld_test_terrain ${CMAKE_CURRENT_BINARY_DIR}/test-terrain)
  add_test(NAME terrain-retail COMMAND nuworld_test_terrain retail)
  set_tests_properties(terrain-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)

  add_executable(nuworld_test_shadcom tests/c/test_shadcom.c)
  add_dependencies(nuworld_test_shadcom nuworld_types)
  target_include_directories(nuworld_test_shadcom PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_shadcom PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_shadcom PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME shadcom COMMAND nuworld_test_shadcom ${CMAKE_CURRENT_BINARY_DIR}/test-shadcom)
  add_test(NAME shadcom-retail COMMAND nuworld_test_shadcom retail)
  set_tests_properties(shadcom-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(nuworld_test_nusound tests/c/test_nusound.c)
  target_include_directories(nuworld_test_nusound PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_nusound PRIVATE NW_HOST)
  target_compile_options(nuworld_test_nusound PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_nusound PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nusound COMMAND nuworld_test_nusound ${CMAKE_CURRENT_BINARY_DIR}/test-nusound)
  add_test(NAME nusound-retail COMMAND nuworld_test_nusound retail)
  add_test(NAME nusound-retail-instant COMMAND nuworld_test_nusound retail instant)
  set_tests_properties(nusound-retail nusound-retail-instant PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(nuworld_test_rndr tests/c/test_rndr.c)
  add_dependencies(nuworld_test_rndr nuworld_types)
  target_include_directories(nuworld_test_rndr PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_rndr PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_rndr PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME rndr COMMAND nuworld_test_rndr)

  add_executable(nuworld_test_texanim tests/c/test_texanim.c)
  add_dependencies(nuworld_test_texanim nuworld_types)
  target_include_directories(nuworld_test_texanim PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_texanim PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_texanim PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME texanim COMMAND nuworld_test_texanim ${CMAKE_CURRENT_BINARY_DIR}/test-texanim)
  add_test(NAME texanim-retail COMMAND nuworld_test_texanim retail)
  set_tests_properties(texanim-retail PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(nuworld_test_font3d tests/c/test_font3d.c)
  add_dependencies(nuworld_test_font3d nuworld_types)
  target_include_directories(nuworld_test_font3d PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_font3d PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_font3d PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME font3d COMMAND nuworld_test_font3d ${CMAKE_CURRENT_BINARY_DIR}/test-font3d)
  add_test(NAME font3d-retail COMMAND nuworld_test_font3d retail)
  set_tests_properties(font3d-retail PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(nuworld_test_edunits tests/c/test_edunits.c)
  add_dependencies(nuworld_test_edunits nuworld_types)
  target_include_directories(nuworld_test_edunits PRIVATE ${nw_decomp_includes} ${nw_gen})
  target_compile_options(nuworld_test_edunits PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_edunits PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  foreach(unit edobj edanim edgra edptl)
    add_test(NAME ${unit}-retail COMMAND nuworld_test_edunits ${unit})
    set_tests_properties(${unit}-retail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  endforeach()

  add_executable(nuworld_test_clock tests/c/test_clock.c)
  target_include_directories(nuworld_test_clock PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_clock PRIVATE NW_HOST)
  target_compile_options(nuworld_test_clock PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_clock PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME clock COMMAND nuworld_test_clock)

  add_executable(nuworld_test_ee tests/c/test_ee.c)
  target_include_directories(nuworld_test_ee PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_ee PRIVATE NW_HOST)
  target_compile_options(nuworld_test_ee PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_ee PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME ee COMMAND nuworld_test_ee)

  add_executable(nuworld_test_pad tests/c/test_pad.c src/platform/controls.c src/platform/input.c
    src/platform/prompts.c)
  target_include_directories(nuworld_test_pad PRIVATE ${nw_decomp_includes})
  target_compile_definitions(nuworld_test_pad PRIVATE NW_HOST)
  target_compile_options(nuworld_test_pad PRIVATE -Wall -Wextra)
  target_link_libraries(nuworld_test_pad PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME pad COMMAND nuworld_test_pad)

  add_executable(nuworld_test_nameentry tests/c/test_nameentry.c)
  add_dependencies(nuworld_test_nameentry nuworld_types)
  target_include_directories(nuworld_test_nameentry PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_nameentry PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_nameentry PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME nameentry COMMAND nuworld_test_nameentry)

  # saves-ps2 converts NUWORLD_TEST_CARD (a .ps2 card image).
  add_executable(nuworld_test_saves tests/c/test_saves.c)
  add_dependencies(nuworld_test_saves nuworld_types)
  target_include_directories(nuworld_test_saves PRIVATE ${nw_decomp_includes})
  target_compile_options(nuworld_test_saves PRIVATE ${nw_decomp_flags} -Wall -Wextra)
  target_link_libraries(nuworld_test_saves PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nuworld_game>)
  add_test(NAME saves COMMAND nuworld_test_saves ${CMAKE_CURRENT_BINARY_DIR}/test-saves)
  add_test(NAME saves-ps2 COMMAND nuworld_test_saves ${CMAKE_CURRENT_BINARY_DIR}/test-saves-ps2 ps2)
  set_tests_properties(saves-ps2 PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME saves-toml COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/save_toml.py
    ${CMAKE_CURRENT_BINARY_DIR}/test-savefile ${CMAKE_CURRENT_BINARY_DIR}/test-saves)
  set_tests_properties(savefile saves PROPERTIES FIXTURES_SETUP save_files)
  set_tests_properties(saves-toml PROPERTIES FIXTURES_REQUIRED save_files)

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
    if(NOT NW_MSVC_ABI)
      file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/globals-objects.rsp
        CONTENT "\"$<JOIN:$<TARGET_OBJECTS:nuworld_decomp>;$<TARGET_OBJECTS:nuworld_game>,\"\n\">\"\n")
      add_test(NAME decomp-globals
        COMMAND ${CMAKE_COMMAND} -DNM=${NW_LLVM_NM} -DOBJECTS=${CMAKE_CURRENT_BINARY_DIR}/globals-objects.rsp
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_globals.cmake)
    endif()
    # In a file: the command line would be too long.
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/closure-objects.txt
      CONTENT "$<JOIN:$<TARGET_OBJECTS:nuworld_decomp>;$<TARGET_OBJECTS:nuworld_game>;$<TARGET_OBJECTS:nuworld_data>;$<TARGET_OBJECTS:nuworld_port>,\n>\n")
    add_test(NAME decomp-closure
      COMMAND ${Python3_EXECUTABLE} ${nw_tools}/check_closure.py --nm ${NW_LLVM_NM} --decomp ${nw_decomp}
        --objects ${CMAKE_CURRENT_BINARY_DIR}/closure-objects.txt)
  else()
    message(WARNING "no llvm-nm next to ${CMAKE_C_COMPILER}: the decomp-globals test is skipped")
  endif()
endif()
