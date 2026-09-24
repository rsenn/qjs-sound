# Locates libsndfile, either as a system install (via pkg-config's sndfile
# module, or explicit hints) or (if BUILD_SNDFILE is ON) built
# from the vendored third_party/libsndfile submodule (this project's own
# fork, git@github.com:rsenn/libsndfile.git - not upstream directly, see
# .gitmodules). See the quickjs-native-bindings skill's "CMake wiring for
# a vendored C/C++ library" section for the pattern this and its
# BuildSndFile.cmake companion follow.
#
# Sets SNDFILE_FOUND, SNDFILE_INCLUDE_DIRS, SNDFILE_LIBRARIES,
# SNDFILE_LIBRARY_DIRS on success. Consumed by the (not yet implemented,
# see doc/sndfile.md) quickjs-sndfile.c module.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(BUILD_SNDFILE
       "Build libsndfile from the vendored third_party/libsndfile submodule instead of using the system library" OFF)

if(BUILD_SNDFILE)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildSndFile.cmake")
else()
  # Pin an install explicitly with -DSNDFILE_PREFIX=/usr (or the
  # finer-grained -DSNDFILE_INCLUDE_DIR=.../-DSNDFILE_LIBRARY_DIR=...);
  # with neither given, falls back to pkg-config's sndfile module, then
  # the default system search paths.
  vendored_find_system_library(SNDFILE HEADER sndfile.h LIBRARY_NAMES sndfile PKGCONFIG_MODULE sndfile)

  if(NOT SNDFILE_FOUND)
    message(
      FATAL_ERROR
        "libsndfile not found - set -DSNDFILE_PREFIX=/path/to/prefix (or -DSNDFILE_INCLUDE_DIR=.../-DSNDFILE_LIBRARY_DIR=...), or -DBUILD_SNDFILE=ON to build the vendored copy in third_party/libsndfile"
    )
  endif()
endif()
