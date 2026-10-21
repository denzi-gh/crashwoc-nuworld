# Configures a preset only when its build folder has no CMakeCache.txt yet (used by .vscode/tasks.json):
#   cmake -P cmake/configure_once.cmake <preset>
set(preset "${CMAKE_ARGV3}")
if(NOT preset)
  message(FATAL_ERROR "usage: cmake -P configure_once.cmake <preset>")
endif()
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${source}/build/${preset}/CMakeCache.txt")
  execute_process(COMMAND "${CMAKE_COMMAND}" --preset "${preset}" WORKING_DIRECTORY "${source}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "cmake --preset ${preset} failed")
  endif()
endif()
