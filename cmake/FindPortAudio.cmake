# Locates PortAudio, either as a system install (via pkg-config's
# portaudio-2.0 module, or explicit hints) or (if BUILD_PORTAUDIO
# is ON) built from the vendored third_party/portaudio submodule. See the
# quickjs-native-bindings skill's "CMake wiring for a vendored C/C++
# library" section for the pattern this and its BuildPortAudio.cmake
# companion follow.
#
# Sets PORTAUDIO_FOUND, PORTAUDIO_INCLUDE_DIRS, PORTAUDIO_LIBRARIES,
# PORTAUDIO_LIBRARY_DIRS on success.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(BUILD_PORTAUDIO
       "Build PortAudio from the vendored third_party/portaudio submodule instead of using the system library" OFF)

if(BUILD_PORTAUDIO)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildPortAudio.cmake")
else()
  # Pin an install explicitly with -DPORTAUDIO_PREFIX=/usr (or the
  # finer-grained -DPORTAUDIO_INCLUDE_DIR=.../-DPORTAUDIO_LIBRARY_DIR=...);
  # with neither given, falls back to pkg-config's portaudio-2.0 module,
  # then the default system search paths.
  vendored_find_system_library(PORTAUDIO HEADER portaudio.h LIBRARY_NAMES portaudio PKGCONFIG_MODULE portaudio-2.0)

  if(NOT PORTAUDIO_FOUND)
    message(
      FATAL_ERROR
        "PortAudio not found - set -DPORTAUDIO_PREFIX=/path/to/prefix (or -DPORTAUDIO_INCLUDE_DIR=.../-DPORTAUDIO_LIBRARY_DIR=...), or -DBUILD_PORTAUDIO=ON to build the vendored copy in third_party/portaudio"
    )
  endif()
endif()
