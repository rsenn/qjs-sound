# Locates libsamplerate, either as a system install (via pkg-config's
# samplerate module, or explicit hints) or (if BUILD_SAMPLERATE
# is ON) built from the vendored third_party/libsamplerate submodule
# (upstream: github.com/libsndfile/libsamplerate). See the
# quickjs-native-bindings skill's "CMake wiring for a vendored C/C++
# library" section for the pattern this and its BuildSampleRate.cmake
# companion follow.
#
# Sets SAMPLERATE_FOUND, SAMPLERATE_INCLUDE_DIRS, SAMPLERATE_LIBRARIES,
# SAMPLERATE_LIBRARY_DIRS on success. Consumed by the (not yet
# implemented, see doc/samplerate.md) quickjs-samplerate.c module - and
# already a runtime dependency of quickjs-labsound.cpp's LabSound link
# (see CMakeLists.txt's labsound_LIBRARIES, currently found via a bare
# `samplerate` link-library name; that stays as-is here, unaffected by
# this new SAMPLERATE_* variable set, until/unless it's worth unifying).

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(
  BUILD_SAMPLERATE
  "Build libsamplerate from the vendored third_party/libsamplerate submodule instead of using the system library"
  OFF)

if(BUILD_SAMPLERATE)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildSampleRate.cmake")
else()
  # Pin an install explicitly with -DSAMPLERATE_PREFIX=/usr (or the
  # finer-grained -DSAMPLERATE_INCLUDE_DIR=.../-DSAMPLERATE_LIBRARY_DIR=...);
  # with neither given, falls back to pkg-config's samplerate module,
  # then the default system search paths.
  vendored_find_system_library(SAMPLERATE HEADER samplerate.h LIBRARY_NAMES samplerate PKGCONFIG_MODULE samplerate)

  if(NOT SAMPLERATE_FOUND)
    message(
      FATAL_ERROR
        "libsamplerate not found - set -DSAMPLERATE_PREFIX=/path/to/prefix (or -DSAMPLERATE_INCLUDE_DIR=.../-DSAMPLERATE_LIBRARY_DIR=...), or -DBUILD_SAMPLERATE=ON to build the vendored copy in third_party/libsamplerate"
    )
  endif()
endif()
