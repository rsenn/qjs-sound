# Builds the vendored third_party/libsndfile submodule as a static,
# -fPIC archive via add_subdirectory(). Included only from
# FindSndFile.cmake when BUILD_SNDFILE is ON - never included
# directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

# ENABLE_EXTERNAL_LIBS (FLAC/Vorbis/Opus) is left at its own upstream
# default (ON, self-disabling per libsndfile's own CMakeLists.txt if none
# of those dev packages are actually present) rather than forced OFF here
# - more format support at no cost when the system already has them,
# and no build failure when it doesn't.
vendored_build_static_subdirectory(
  SNDFILE SUBMODULE_DIR third_party/libsndfile TARGETS sndfile OPTIONS
  -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF)
