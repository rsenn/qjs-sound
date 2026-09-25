# Builds the vendored third_party/soundtouch submodule as a static,
# -fPIC archive via add_subdirectory(). Included only from
# FindSoundTouch.cmake when BUILD_SOUNDTOUCH is ON - never
# included directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

vendored_build_static_subdirectory(
  SOUNDTOUCH SUBMODULE_DIR third_party/soundtouch TARGETS SoundTouch OPTIONS
  -DSOUNDSTRETCH=OFF -DSOUNDTOUCH_DLL=OFF)
