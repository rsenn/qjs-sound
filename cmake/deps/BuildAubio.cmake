# Builds the vendored third_party/aubio submodule as a static, -fPIC
# archive via add_subdirectory(), always with FFTW3 (float): aubio's own
# CMake build never enables it, and its fallback FFT is power-of-2 only.
# Run by the BUILD command of the aubio dependency (SoundLibs.cmake).

# FFTW3 is a dependency of aubio: Deps.cmake resolved it (installed, or
# built into the build tree) before this runs.
dep_include_dirs(fftw3 main FFTW3_INCLUDE_DIRS)
dep_libraries(fftw3 main FFTW3_LIBRARIES)
dep_targets(fftw3 main FFTW3_TARGETS)

# Upstream third_party/aubio/src/CMakeLists.txt hardcodes
# `add_library(aubio SHARED)`, ignoring BUILD_SHARED_LIBS, so it can't
# produce the static libaubio.a this project needs. PATCH below changes
# that one line in a copy under the build tree (the submodule is not ours
# and stays untouched), see cmake/patches/aubio-static-lib.patch.
#
# Only third_party/aubio/src is added (CMAKE_SUBDIR), not aubio's
# top-level CMakeLists.txt, which also pulls in examples/ and tests/
# (extra dependencies aubio's own build needs but this binding doesn't).
vendored_build_static_subdirectory(
  AUBIO SUBMODULE_DIR third_party/aubio CMAKE_SUBDIR src TARGETS aubio PATCH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/aubio-static-lib.patch"
  "${CMAKE_CURRENT_SOURCE_DIR}/patches/aubio-pitch-confidence-reads-zero.patch")

# What aubio's wscript defines for FFTW3 (float): fft.c, dct_fftw.c and
# aubio_priv.h select the FFTW code path on these.
target_compile_definitions(aubio PRIVATE HAVE_FFTW3 HAVE_FFTW3F)
target_include_directories(aubio PRIVATE ${FFTW3_INCLUDE_DIRS})
target_link_libraries(aubio PRIVATE ${FFTW3_LIBRARIES})
if(FFTW3_TARGETS)
  add_dependencies(aubio ${FFTW3_TARGETS})
endif()
