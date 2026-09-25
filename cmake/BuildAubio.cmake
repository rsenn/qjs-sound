# Builds the vendored third_party/aubio submodule as a static, -fPIC
# archive via add_subdirectory(). Included only from FindAubio.cmake when
# BUILD_AUBIO is ON (the default) - never included directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

# Upstream third_party/aubio/src/CMakeLists.txt hardcodes
# `add_library(aubio SHARED)`, ignoring BUILD_SHARED_LIBS, so it can't
# produce the static libaubio.a this project needs - PATCH below applies
# the one-line SHARED->STATIC fix at configure time (checked first with
# `git apply --reverse --check`, so re-running cmake is a no-op once
# already applied), keeping third_party/aubio itself a plain checkout of
# upstream rather than carrying a local commit.
#
# Only third_party/aubio/src is added (CMAKE_SUBDIR), not aubio's
# top-level CMakeLists.txt, which also pulls in examples/ and tests/
# (extra dependencies aubio's own build needs but this binding doesn't).
vendored_build_static_subdirectory(
  AUBIO SUBMODULE_DIR third_party/aubio CMAKE_SUBDIR src TARGETS aubio PATCH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/aubio-static-lib.patch")
