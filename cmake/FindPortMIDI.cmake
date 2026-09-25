# Locates PortMIDI, either as a system install or (if BUILD_PORTMIDI
# is ON) built from the vendored third_party/portmidi submodule. See the
# quickjs-native-bindings skill's "CMake wiring for a vendored C/C++
# library" section for the pattern this and its BuildPortMIDI.cmake
# companion follow.
#
# Sets PORTMIDI_FOUND, PORTMIDI_INCLUDE_DIRS, PORTMIDI_LIBRARIES,
# PORTMIDI_LIBRARY_DIRS on success.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(
  BUILD_PORTMIDI
  "Build PortMIDI from the vendored third_party/portmidi submodule instead of using the system library"
  OFF)

# PortMIDI's Linux backend (pm_linux/pmlinuxalsa.c) calls directly into
# ALSA; a system libportmidi.so already carries that as a DT_NEEDED
# dependency, but a static libportmidi.a (the BUILD_PORTMIDI
# case) does not embed it, so ALSA has to be linked into whichever target
# actually consumes PortMIDI either way - located here, once, rather than
# in every place PORTMIDI_LIBRARIES is used.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(ALSA REQUIRED)
  set(PORTMIDI_SYSTEM_DEPS ${ALSA_LIBRARIES})
else()
  set(PORTMIDI_SYSTEM_DEPS "")
endif()

if(BUILD_PORTMIDI)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildPortMIDI.cmake")
else()
  # No .pc file ships for libportmidi on Debian/Ubuntu, so pkg-config
  # detection isn't attempted (unlike FindPortAudio.cmake's portaudio-2.0).
  # Pin an install explicitly with -DPORTMIDI_PREFIX=/usr, or the
  # finer-grained -DPORTMIDI_INCLUDE_DIR=/usr/include
  # -DPORTMIDI_LIBRARY_DIR=/usr/lib/x86_64-linux-gnu; with neither given,
  # falls back to the default system search paths.
  vendored_find_system_library(PORTMIDI HEADER portmidi.h LIBRARY_NAMES
                               portmidi EXTRA_LIBRARIES ${PORTMIDI_SYSTEM_DEPS})

  if(NOT PORTMIDI_FOUND)
    message(
      FATAL_ERROR
        "PortMIDI not found - set -DPORTMIDI_PREFIX=/path/to/prefix (or -DPORTMIDI_INCLUDE_DIR=.../-DPORTMIDI_LIBRARY_DIR=...), or -DBUILD_PORTMIDI=ON to build the vendored copy in third_party/portmidi"
    )
  endif()
endif()
