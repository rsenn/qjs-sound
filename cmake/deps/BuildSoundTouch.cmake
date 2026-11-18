# Builds the vendored third_party/soundtouch submodule as a static,
# -fPIC archive via add_subdirectory(). Run by the BUILD command of the soundtouch dependency
# (SoundLibs.cmake).


vendored_build_static_subdirectory(
  SOUNDTOUCH SUBMODULE_DIR third_party/soundtouch TARGETS SoundTouch OPTIONS
  -DSOUNDSTRETCH=OFF -DSOUNDTOUCH_DLL=OFF)
