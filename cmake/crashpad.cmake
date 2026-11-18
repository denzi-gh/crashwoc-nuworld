include(FetchContent)
FetchContent_Declare(crashpad
  GIT_REPOSITORY https://github.com/getsentry/crashpad.git
  GIT_TAG 0bd6ca43a0958a86939a37569b61162f92f9a022
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(crashpad)

find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} -C ${crashpad_SOURCE_DIR} rev-parse HEAD
  OUTPUT_VARIABLE nw_crashpad_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(nw_crashpad ${CMAKE_CURRENT_BINARY_DIR}/_deps/crashpad-install)
set(nw_crashpad_built "")
if(EXISTS ${nw_crashpad}/commit.txt)
  file(READ ${nw_crashpad}/commit.txt nw_crashpad_built)
endif()
if(NOT nw_crashpad_built STREQUAL nw_crashpad_commit)
  message(STATUS "Building Crashpad ${nw_crashpad_commit}")
  set(nw_crashpad_log ${crashpad_BINARY_DIR}/build.log)
  file(REMOVE_RECURSE ${nw_crashpad} ${crashpad_BINARY_DIR})
  file(MAKE_DIRECTORY ${crashpad_BINARY_DIR})
  execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${crashpad_SOURCE_DIR} -B ${crashpad_BINARY_DIR} -G ${CMAKE_GENERATOR}
      -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl
      -DCMAKE_CXX_COMPILER=cl -DCMAKE_INSTALL_PREFIX=${nw_crashpad} -DCRASHPAD_ENABLE_INSTALL=ON
      -DCRASHPAD_ENABLE_INSTALL_DEV=ON
    RESULT_VARIABLE nw_crashpad_result OUTPUT_FILE ${nw_crashpad_log} ERROR_FILE ${nw_crashpad_log})
  if(NOT nw_crashpad_result)
    execute_process(COMMAND ${CMAKE_COMMAND} --build ${crashpad_BINARY_DIR} --target install
      RESULT_VARIABLE nw_crashpad_result OUTPUT_FILE ${nw_crashpad_log} ERROR_FILE ${nw_crashpad_log})
  endif()
  if(nw_crashpad_result)
    message(FATAL_ERROR "Crashpad did not build: ${nw_crashpad_log}")
  endif()
  file(WRITE ${nw_crashpad}/commit.txt ${nw_crashpad_commit})
endif()
find_package(crashpad REQUIRED CONFIG PATHS ${nw_crashpad}/lib/cmake/crashpad NO_DEFAULT_PATH)

add_library(nuworld_crashpad STATIC src/platform/crashpad.cpp)
target_include_directories(nuworld_crashpad PRIVATE src)
target_include_directories(nuworld_crashpad SYSTEM PRIVATE ${nw_crashpad}/include/crashpad)
target_compile_definitions(nuworld_crashpad PUBLIC NW_CRASHPAD PRIVATE NOMINMAX UNICODE _UNICODE _HAS_EXCEPTIONS=0)
target_compile_options(nuworld_crashpad PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
target_link_libraries(nuworld_crashpad PRIVATE crashpad::client)
configure_file(${nw_crashpad}/bin/crashpad_handler.exe ${CMAKE_CURRENT_BINARY_DIR}/crashpad_handler.exe COPYONLY)
