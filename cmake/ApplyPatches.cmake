# apply patches to a source tree in place, skipping the ones already applied.
#
#   cmake -DSOURCE_DIR=<dir> "-DPATCHES=<a.patch>;<b.patch>" -P ApplyPatches.cmake
#
# used as PATCH_COMMAND of ExternalProject_Add, which has no copy+stamp
# logic; a patch whose reverse applies cleanly is already in the tree.

find_program(GIT git REQUIRED)

foreach(_patch IN LISTS PATCHES)
  execute_process(COMMAND ${GIT} apply --reverse --check "${_patch}"
                  WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE _applied
                  OUTPUT_QUIET ERROR_QUIET)
  if(_applied EQUAL 0)
    message(STATUS "already applied: ${_patch}")
    continue()
  endif()

  execute_process(COMMAND ${GIT} apply "${_patch}"
                  WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "cannot apply ${_patch} to ${SOURCE_DIR}")
  endif()
  message(STATUS "applied: ${_patch}")
endforeach()
