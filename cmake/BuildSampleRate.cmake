# Builds the vendored third_party/libsamplerate submodule as a static,
# -fPIC archive via add_subdirectory() (BUILD_SHARED_LIBS already
# defaults OFF upstream; forced OFF regardless by
# vendored_build_static_subdirectory() for consistency with the other
# libraries here). Included only from FindSampleRate.cmake when
# BUILD_SAMPLERATE is ON - never included directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

vendored_build_static_subdirectory(
  SAMPLERATE
  SUBMODULE_DIR
  third_party/libsamplerate
  TARGETS
  samplerate
  OPTIONS
  -DLIBSAMPLERATE_EXAMPLES=OFF
  -DLIBSAMPLERATE_INSTALL=OFF)
