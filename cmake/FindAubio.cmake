# Locates aubio, either as a system install (via pkg-config's aubio
# module, or explicit hints) or (if BUILD_AUBIO is ON, the
# default - see below) built from the vendored third_party/aubio
# submodule. See the quickjs-native-bindings skill's "CMake wiring for a
# vendored C/C++ library" section for the pattern this and its
# BuildAubio.cmake companion follow.
#
# Sets AUBIO_FOUND, AUBIO_INCLUDE_DIRS, AUBIO_LIBRARIES,
# AUBIO_LIBRARY_DIRS on success. Consumed by quickjs-aubio.c.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

# Defaults ON, unlike every other Find<Lib>.cmake here (all OFF) - aubio
# has no widely-packaged system dev package on the distros this project
# targets (unlike portaudio/portmidi/sndfile/samplerate/soundtouch, all
# routinely available as system packages), and this project already
# built exclusively from third_party/aubio before this option existed;
# defaulting ON preserves that behavior unchanged rather than silently
# requiring a system libaubio that most machines won't have.
option(
  BUILD_AUBIO
  "Build aubio from the vendored third_party/aubio submodule instead of using the system library"
  ON)

if(BUILD_AUBIO)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildAubio.cmake")
else()
  # Pin an install explicitly with -DAUBIO_PREFIX=/usr (or the
  # finer-grained -DAUBIO_INCLUDE_DIR=.../-DAUBIO_LIBRARY_DIR=...); with
  # neither given, falls back to pkg-config's aubio module (present on
  # distros that do package it), then the default system search paths.
  vendored_find_system_library(AUBIO HEADER aubio.h LIBRARY_NAMES aubio
                               PKGCONFIG_MODULE aubio)

  if(NOT AUBIO_FOUND)
    message(
      FATAL_ERROR
        "aubio not found - set -DAUBIO_PREFIX=/path/to/prefix (or -DAUBIO_INCLUDE_DIR=.../-DAUBIO_LIBRARY_DIR=...), or -DBUILD_AUBIO=ON (the default) to build the vendored copy in third_party/aubio"
    )
  endif()
endif()
