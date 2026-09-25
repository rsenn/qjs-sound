# Shared helpers for the Find<Lib>.cmake/Build<Lib>.cmake pair every
# vendored third-party library in this project follows (see
# quickjs-native-bindings skill, "CMake wiring for a vendored C/C++
# library" section, for the pattern these implement). Six of the seven
# libraries wrapped by qjs-sound (portaudio, portmidi, libsndfile,
# libsamplerate, soundtouch, aubio) need the exact same two pieces of
# logic - "locate a system install via explicit hints or pkg-config" and
# "build a vendored submodule as a static, -fPIC archive via
# add_subdirectory()" - so it lives here once instead of being retyped in
# every Find<Lib>.cmake. Rubber Band is the one exception (Meson-only
# upstream build, no CMakeLists.txt to add_subdirectory()); its
# cmake/BuildRubberBand.cmake does its own ExternalProject_Add instead of
# calling vendored_build_static_subdirectory() below.

# vendored_find_system_library(<NAME>
#   HEADER <header.h>
#   LIBRARY_NAMES <lib1> [<lib2> ...]
#   [PKGCONFIG_MODULE <pkg-config .pc name>]
#   [EXTRA_LIBRARIES <lib1> ...])
#
# Locates a system install of <NAME>, in this priority order:
#   1. Explicit hints: -D<NAME>_PREFIX=/usr, -D<NAME>_INCLUDE_DIR=...,
#      -D<NAME>_LIBRARY_DIR=... (any subset; PREFIX alone implies
#      <PREFIX>/include and <PREFIX>/lib for whichever of the other two
#      isn't also given explicitly).
#   2. pkg-config, if PKGCONFIG_MODULE was given and no explicit hint
#      above resolved both the header and the library.
#   3. Plain find_path()/find_library() against the default system
#      search paths, as a last resort.
# Sets, in the parent scope: <NAME>_INCLUDE_DIRS, <NAME>_LIBRARIES (the
# located library/libraries plus EXTRA_LIBRARIES, e.g. asound for
# portmidi - see FindPortMIDI.cmake), <NAME>_LIBRARY_DIRS, <NAME>_FOUND.
function(vendored_find_system_library NAME)
  cmake_parse_arguments(VFSL "" "HEADER;PKGCONFIG_MODULE"
                        "LIBRARY_NAMES;EXTRA_LIBRARIES" ${ARGN})

  if(${NAME}_PREFIX)
    if(NOT ${NAME}_INCLUDE_DIR)
      set(${NAME}_INCLUDE_DIR "${${NAME}_PREFIX}/include")
    endif()
    if(NOT ${NAME}_LIBRARY_DIR)
      set(${NAME}_LIBRARY_DIR "${${NAME}_PREFIX}/lib")
    endif()
  endif()

  if(${NAME}_INCLUDE_DIR AND ${NAME}_LIBRARY_DIR)
    # Hints given explicitly (or derived from _PREFIX above) - search
    # only there, no silent fallback to some other copy on the system.
    find_path(${NAME}_INCLUDE_DIRS NAMES "${VFSL_HEADER}"
              PATHS "${${NAME}_INCLUDE_DIR}" NO_DEFAULT_PATH)
    set(${NAME}_LIBRARIES "")
    foreach(LIB ${VFSL_LIBRARY_NAMES})
      find_library(${NAME}_LIBRARY_${LIB} NAMES "${LIB}"
                   PATHS "${${NAME}_LIBRARY_DIR}" NO_DEFAULT_PATH)
      list(APPEND ${NAME}_LIBRARIES "${${NAME}_LIBRARY_${LIB}}")
    endforeach()
    set(${NAME}_LIBRARY_DIRS "${${NAME}_LIBRARY_DIR}")
  elseif(VFSL_PKGCONFIG_MODULE)
    find_package(PkgConfig QUIET)
    pkg_check_modules(${NAME}_PC QUIET "${VFSL_PKGCONFIG_MODULE}")
    if(${NAME}_PC_FOUND)
      set(${NAME}_INCLUDE_DIRS "${${NAME}_PC_INCLUDE_DIRS}")
      set(${NAME}_LIBRARIES "${${NAME}_PC_LIBRARIES}")
      set(${NAME}_LIBRARY_DIRS "${${NAME}_PC_LIBRARY_DIRS}")
    endif()
  endif()

  if(NOT ${NAME}_INCLUDE_DIRS)
    find_path(${NAME}_INCLUDE_DIRS NAMES "${VFSL_HEADER}")
  endif()
  if(NOT ${NAME}_LIBRARIES)
    set(${NAME}_LIBRARIES "")
    foreach(LIB ${VFSL_LIBRARY_NAMES})
      find_library(${NAME}_LIBRARY_${LIB} NAMES "${LIB}")
      if(${NAME}_LIBRARY_${LIB})
        list(APPEND ${NAME}_LIBRARIES "${${NAME}_LIBRARY_${LIB}}")
      endif()
    endforeach()
  endif()

  if(${NAME}_INCLUDE_DIRS AND ${NAME}_LIBRARIES)
    list(APPEND ${NAME}_LIBRARIES ${VFSL_EXTRA_LIBRARIES})
    set(${NAME}_FOUND TRUE PARENT_SCOPE)
    set(${NAME}_INCLUDE_DIRS "${${NAME}_INCLUDE_DIRS}" PARENT_SCOPE)
    set(${NAME}_LIBRARIES "${${NAME}_LIBRARIES}" PARENT_SCOPE)
    set(${NAME}_LIBRARY_DIRS "${${NAME}_LIBRARY_DIRS}" PARENT_SCOPE)
  else()
    set(${NAME}_FOUND FALSE PARENT_SCOPE)
  endif()
endfunction()

# vendored_build_static_subdirectory(<NAME>
#   SUBMODULE_DIR <third_party/dir>
#   [CMAKE_SUBDIR <path within the submodule to add_subdirectory(), default "."]
#   TARGETS <cmake-target1> [<target2> ...]
#   [OPTIONS <-DFOO=BAR> ...]
#   [EXTRA_LIBRARIES <lib1> ...]
#   [PATCH <path/to/some.patch>])
#
# Builds a vendored git-submodule copy of a library as a static, -fPIC
# archive, via add_subdirectory() (in-process, one configure pass - not
# ExternalProject_Add, which needs a second configure+build pass; every
# library here ships a CMakeLists.txt that's safe to add_subdirectory()
# straight from a parent project, confirmed individually in each
# Find<Lib>.cmake's own comments before relying on it).
#
# Sets, in the parent scope: <NAME>_INCLUDE_DIRS (SUBMODULE_DIR itself,
# plus any extra dirs the caller already set in <NAME>_EXTRA_INCLUDE_DIRS
# before calling this), <NAME>_LIBRARIES (the real CMake target names
# from TARGETS, plus EXTRA_LIBRARIES - e.g. asound - so callers can
# target_link_libraries() against them directly rather than a resolved
# file path), <NAME>_FOUND.
function(vendored_build_static_subdirectory NAME)
  cmake_parse_arguments(VBSS "" "SUBMODULE_DIR;CMAKE_SUBDIR;PATCH"
                        "TARGETS;OPTIONS;EXTRA_LIBRARIES" ${ARGN})

  set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${VBSS_SUBMODULE_DIR}")
  if(NOT EXISTS "${SRC_DIR}/CMakeLists.txt" AND NOT VBSS_CMAKE_SUBDIR)
    message(
      FATAL_ERROR
        "${VBSS_SUBMODULE_DIR} is empty - run 'git submodule update --init ${VBSS_SUBMODULE_DIR}' first"
    )
  endif()

  if(VBSS_PATCH)
    find_package(Git REQUIRED)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${VBSS_PATCH}"
      WORKING_DIRECTORY "${SRC_DIR}" RESULT_VARIABLE PATCH_ALREADY_APPLIED
      OUTPUT_QUIET ERROR_QUIET)
    if(NOT PATCH_ALREADY_APPLIED EQUAL 0)
      message(STATUS "Patching ${VBSS_SUBMODULE_DIR} (${VBSS_PATCH})")
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${VBSS_PATCH}"
        WORKING_DIRECTORY "${SRC_DIR}" RESULT_VARIABLE PATCH_RESULT)
      if(NOT PATCH_RESULT EQUAL 0)
        message(
          FATAL_ERROR "Failed to apply ${VBSS_PATCH} to ${VBSS_SUBMODULE_DIR}")
      endif()
    endif()
  endif()

  # Every OPTIONS entry is a real -DFOO=BAR string; force each into the
  # cache so the vendored project's own option(FOO ...) call (further
  # down, inside add_subdirectory()) leaves it alone rather than
  # resetting it to that option()'s own default.
  #
  # This has to be a CACHE FORCE, not a plain set() guarded by
  # cmake_policy(SET CMP0077 NEW): CMP0077 ("option() honors normal
  # variables") only takes effect when the *subdirectory's own*
  # cmake_minimum_required() declares CMake >= 3.13, and several vendored
  # projects here declare less (third_party/soundtouch: 3.5,
  # third_party/portaudio: 3.10) - their own cmake_minimum_required()
  # call resets the policy back to OLD/warn regardless of what this
  # function sets beforehand, confirmed by a real CMake dev warning
  # ("option is clearing the normal variable 'SOUNDSTRETCH'") the first
  # version of this function produced while testing
  # BUILD_SOUNDTOUCH=ON. A pre-existing CACHE entry, by
  # contrast, is left alone by option() in both OLD and NEW policy modes
  # - option() only ever creates a cache entry if none exists yet.
  foreach(OPT ${VBSS_OPTIONS})
    string(REGEX REPLACE "^-D" "" OPT "${OPT}")
    string(REGEX MATCH "^[^:=]+" OPT_NAME "${OPT}")
    string(REGEX REPLACE "^[^=]+=" "" OPT_VALUE "${OPT}")
    set("${OPT_NAME}" "${OPT_VALUE}" CACHE BOOL "" FORCE)
  endforeach()

  # Forced into the cache (not restored afterward) for the same
  # CMP0077 reason as the OPTIONS loop above - and left that way
  # deliberately: every vendored library add_subdirectory()'d through
  # this function is meant to end up static, so there's no case in this
  # project where a later call should see BUILD_SHARED_LIBS revert to ON.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  set(CMAKE_POSITION_INDEPENDENT_CODE_SAVE "${CMAKE_POSITION_INDEPENDENT_CODE}")
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  if(VBSS_CMAKE_SUBDIR)
    add_subdirectory("${SRC_DIR}/${VBSS_CMAKE_SUBDIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/${VBSS_SUBMODULE_DIR}")
  else()
    add_subdirectory("${SRC_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/${VBSS_SUBMODULE_DIR}")
  endif()

  set(CMAKE_POSITION_INDEPENDENT_CODE "${CMAKE_POSITION_INDEPENDENT_CODE_SAVE}")

  if(VBSS_CMAKE_SUBDIR)
    set(HEADER_DIR "${SRC_DIR}/${VBSS_CMAKE_SUBDIR}")
  else()
    set(HEADER_DIR "${SRC_DIR}")
  endif()
  set(${NAME}_INCLUDE_DIRS "${HEADER_DIR}" ${${NAME}_EXTRA_INCLUDE_DIRS}
      PARENT_SCOPE)
  set(${NAME}_LIBRARIES ${VBSS_TARGETS} ${VBSS_EXTRA_LIBRARIES} PARENT_SCOPE)
  set(${NAME}_FOUND TRUE PARENT_SCOPE)
endfunction()
