# Locates Rubber Band, either as a system install (via pkg-config's
# rubberband module, or explicit hints) or (if BUILD_RUBBERBAND
# is ON) built from the vendored third_party/rubberband submodule
# (upstream mirror: github.com/breakfastquay/rubberband; canonical repo
# is Mercurial on sourcehut, per doc/rubberband.md's survey). See the
# quickjs-native-bindings skill's "CMake wiring for a vendored C/C++
# library" section for the pattern this and its BuildRubberBand.cmake
# companion follow - **Rubber Band is the one exception to that
# pattern's usual add_subdirectory() approach**: its upstream build is
# Meson-only (no CMakeLists.txt at all), so BuildRubberBand.cmake compiles
# the needed sources directly via a plain add_library() instead.
#
# Sets RUBBERBAND_FOUND, RUBBERBAND_INCLUDE_DIRS, RUBBERBAND_LIBRARIES,
# RUBBERBAND_LIBRARY_DIRS on success. Consumed by the (not yet
# implemented, see doc/rubberband.md) quickjs-rubberband.c module.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(
  BUILD_RUBBERBAND
  "Build Rubber Band from the vendored third_party/rubberband submodule instead of using the system library" OFF)

if(BUILD_RUBBERBAND)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildRubberBand.cmake")
else()
  # Pin an install explicitly with -DRUBBERBAND_PREFIX=/usr (or the
  # finer-grained -DRUBBERBAND_INCLUDE_DIR=.../-DRUBBERBAND_LIBRARY_DIR=...);
  # with neither given, falls back to pkg-config's rubberband module
  # (confirmed present on this system: `pkg-config --modversion
  # rubberband` reports 3.3.0), then the default system search paths.
  # Targets the C API (rubberband-c.h/librubberband) per doc/rubberband.md's
  # own stated design choice, not the C++ RubberBandStretcher class
  # directly - same header/library either way, just documenting why a
  # C-linkage header name isn't required here.
  vendored_find_system_library(RUBBERBAND HEADER rubberband/rubberband-c.h LIBRARY_NAMES rubberband
                                PKGCONFIG_MODULE rubberband)

  if(NOT RUBBERBAND_FOUND)
    message(
      FATAL_ERROR
        "Rubber Band not found - set -DRUBBERBAND_PREFIX=/path/to/prefix (or -DRUBBERBAND_INCLUDE_DIR=.../-DRUBBERBAND_LIBRARY_DIR=...), or -DBUILD_RUBBERBAND=ON to build the vendored copy in third_party/rubberband"
    )
  endif()
endif()
