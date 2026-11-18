# Builds the vendored third_party/libsamplerate submodule as a static,
# -fPIC archive via add_subdirectory() (BUILD_SHARED_LIBS already
# defaults OFF upstream; forced OFF regardless by
# vendored_build_static_subdirectory() for consistency with the other
# libraries here). Run by the BUILD command of the samplerate dependency
# (SoundLibs.cmake).


vendored_build_static_subdirectory(
  SAMPLERATE SUBMODULE_DIR third_party/libsamplerate TARGETS samplerate OPTIONS
  -DLIBSAMPLERATE_EXAMPLES=OFF -DLIBSAMPLERATE_INSTALL=OFF)
