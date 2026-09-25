# Locates SoundTouch, either as a system install (via pkg-config's
# soundtouch module, or explicit hints) or (if BUILD_SOUNDTOUCH
# is ON) built from the vendored third_party/soundtouch submodule
# (upstream: codeberg.org/soundtouch/soundtouch). See the
# quickjs-native-bindings skill's "CMake wiring for a vendored C/C++
# library" section for the pattern this and its BuildSoundTouch.cmake
# companion follow.
#
# Sets SOUNDTOUCH_FOUND, SOUNDTOUCH_INCLUDE_DIRS, SOUNDTOUCH_LIBRARIES,
# SOUNDTOUCH_LIBRARY_DIRS on success. Consumed by the (not yet
# implemented, see doc/soundtouch.md) quickjs-soundtouch.cpp module.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(
  BUILD_SOUNDTOUCH
  "Build SoundTouch from the vendored third_party/soundtouch submodule instead of using the system library"
  OFF)

if(BUILD_SOUNDTOUCH)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildSoundTouch.cmake")
else()
  # Debian/Ubuntu's libsoundtouch-dev does ship a soundtouch.pc. Pin an
  # install explicitly with -DSOUNDTOUCH_PREFIX=/usr (or the
  # finer-grained -DSOUNDTOUCH_INCLUDE_DIR=.../-DSOUNDTOUCH_LIBRARY_DIR=...);
  # with neither given, falls back to pkg-config, then the default system
  # search paths. The header lives under soundtouch/SoundTouch.h - see
  # doc/soundtouch.md's own note on this - but HEADER only needs to name
  # the leaf file for find_path() to locate the parent include dir.
  vendored_find_system_library(
    SOUNDTOUCH HEADER soundtouch/SoundTouch.h LIBRARY_NAMES SoundTouch
    PKGCONFIG_MODULE soundtouch)

  if(NOT SOUNDTOUCH_FOUND)
    message(
      FATAL_ERROR
        "SoundTouch not found - set -DSOUNDTOUCH_PREFIX=/path/to/prefix (or -DSOUNDTOUCH_INCLUDE_DIR=.../-DSOUNDTOUCH_LIBRARY_DIR=...), or -DBUILD_SOUNDTOUCH=ON to build the vendored copy in third_party/soundtouch"
    )
  endif()
endif()
