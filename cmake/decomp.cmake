# The decomp units on the host: the generators write <build>/gen, nuworld_decomp compiles every wrapper
# and stub unit with the decomp flags and the -Werror set.

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
add_custom_target(nuworld_gen DEPENDS ${nw_gen}/wrappers.stamp ${nw_gen}/stubs.stamp)

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

if(NUWORLD_TESTS)
  get_filename_component(nw_cc_dir ${CMAKE_C_COMPILER} DIRECTORY)
  find_program(NW_LLVM_NM llvm-nm HINTS ${nw_cc_dir})
  if(NW_LLVM_NM)
    add_test(NAME decomp-globals
      COMMAND ${CMAKE_COMMAND} -DNM=${NW_LLVM_NM} "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:nuworld_decomp>,|>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_globals.cmake)
  else()
    message(WARNING "no llvm-nm next to ${CMAKE_C_COMPILER}: the decomp-globals test is skipped")
  endif()
endif()
