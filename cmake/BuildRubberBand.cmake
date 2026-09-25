# Builds the vendored third_party/rubberband submodule as a static,
# -fPIC archive. Included only from FindRubberBand.cmake when
# BUILD_RUBBERBAND is ON - never included directly.
#
# Unlike every other Build<Lib>.cmake in this directory, this does NOT
# call vendored_build_static_subdirectory() - Rubber Band's upstream
# build is Meson-only (confirmed: third_party/rubberband has a
# meson.build and meson_options.txt but no CMakeLists.txt anywhere), so
# there's no CMakeLists.txt to add_subdirectory() into this project's own
# configure pass. Rather than shelling out to meson+ninja via
# ExternalProject_Add (a real earlier version of this file did exactly
# that), this instead compiles the library directly with a plain
# add_library() - the source list and defines below are read straight out
# of third_party/rubberband/meson.build's own 'library_sources'/
# 'feature_defines' logic for the same configuration meson's own 'auto'
# defaults pick on Linux (fft=builtin, resampler=builtin), so it produces
# the same object code meson would, just through this project's own
# single-configure-pass CMake build instead of a nested external one -
# no meson/ninja dependency, and a real CMake target other add_subdirectory-
# based libraries in this project already assume (a resolvable target
# name, not an imported-from-elsewhere archive path).

find_package(Threads REQUIRED)

set(RUBBERBAND_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rubberband")

if(NOT EXISTS "${RUBBERBAND_SRC_DIR}/meson.build")
  message(
    FATAL_ERROR
      "third_party/rubberband is empty - run 'git submodule update --init third_party/rubberband' first"
  )
endif()

# meson.build's 'library_sources' (public-API + faster/finer engine
# sources, shared by every FFT/resampler choice) plus
# src/common/BQResampler.cpp, which meson.build only adds when
# resampler == 'builtin' (this build's choice, see the defines below).
set(RUBBERBAND_SOURCES
    src/rubberband-c.cpp
    src/RubberBandStretcher.cpp
    src/RubberBandLiveShifter.cpp
    src/faster/AudioCurveCalculator.cpp
    src/faster/CompoundAudioCurve.cpp
    src/faster/HighFrequencyAudioCurve.cpp
    src/faster/SilentAudioCurve.cpp
    src/faster/PercussiveAudioCurve.cpp
    src/faster/R2Stretcher.cpp
    src/faster/StretcherChannelData.cpp
    src/faster/StretcherProcess.cpp
    src/common/Allocators.cpp
    src/common/FFT.cpp
    src/common/Log.cpp
    src/common/Profiler.cpp
    src/common/Resampler.cpp
    src/common/StretchCalculator.cpp
    src/common/sysutils.cpp
    src/common/mathmisc.cpp
    src/common/Thread.cpp
    src/finer/R3Stretcher.cpp
    src/finer/R3LiveShifter.cpp
    src/common/BQResampler.cpp)
list(TRANSFORM RUBBERBAND_SOURCES PREPEND "${RUBBERBAND_SRC_DIR}/")

add_library(rubberband STATIC ${RUBBERBAND_SOURCES})

# USE_BUILTIN_FFT/USE_BQRESAMPLER: meson.build's fft/resampler options
# both default to 'auto', which resolves to 'builtin' on every
# non-Apple platform (fft='vdsp' only on darwin) - no external FFT/
# resampler dependency needed, matching this project's minimal-deps
# preference elsewhere (e.g. FindAubio.cmake defaulting to the vendored
# build rather than requiring a system package).
#
# USE_PTHREADS/HAVE_POSIX_MEMALIGN: meson.build's own non-Apple,
# non-Windows branch ("system not darwin or windows").
#
# NO_THREAD_CHECKS/NO_TIMING/NDEBUG: meson.build's own release-build
# defines (its default_options sets buildtype: 'release') - this
# project has no interest in Rubber Band's internal profiling/timing
# instrumentation or debug-only thread-usage assertions.
#
# LACK_SINCOS: forces src/common/VectorOpsComplex.h's portable
# separate-sin()+cos() fallback instead of glibc's sincos()/sincosf()
# GNU extensions, which are only declared under math.h when _GNU_SOURCE
# is defined - simpler than threading _GNU_SOURCE through this target
# for a difference that's functionally identical and immaterial to
# correctness.
target_compile_definitions(
  rubberband
  PRIVATE USE_BUILTIN_FFT USE_BQRESAMPLER USE_PTHREADS HAVE_POSIX_MEMALIGN
          NO_THREAD_CHECKS NO_TIMING NDEBUG LACK_SINCOS)

# "rubberband" (public headers) and "src" (so the handful of
# not-relative-to-their-own-directory #includes inside src/*.cpp still
# resolve, matching meson.build's own general_include_dirs) - both
# PRIVATE, since a consumer only ever needs the public "rubberband" dir
# (that's what RUBBERBAND_INCLUDE_DIRS below actually points at).
target_include_directories(rubberband PRIVATE "${RUBBERBAND_SRC_DIR}"
                                              "${RUBBERBAND_SRC_DIR}/src")

set_target_properties(
  rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 11
                        CXX_STANDARD_REQUIRED ON)

target_link_libraries(rubberband PRIVATE Threads::Threads)

# std::atomic<int>/<double> (src/finer/R3Stretcher.cpp,
# R3LiveShifter.cpp) are lock-free without libatomic on every mainstream
# 64-bit target, but meson.build itself only skips -latomic after
# actually testing that the lock-free path links without it - do the
# same rather than assuming: link it if present, skip it otherwise
# (present-but-unused costs nothing; required-but-missing would be a
# real link failure on some other architecture).
find_library(RUBBERBAND_ATOMIC_LIBRARY NAMES atomic)
if(RUBBERBAND_ATOMIC_LIBRARY)
  target_link_libraries(rubberband PRIVATE "${RUBBERBAND_ATOMIC_LIBRARY}")
endif()

# librubberband.a is C++-implemented, but doc/rubberband.md's planned
# quickjs-rubberband.c targets the C API from a plain .c file - a C
# module target gets linked via the C compiler driver, which (unlike the
# C++ driver g++/CXX used for this project's other, .cpp-implemented
# bindings like quickjs-stk.cpp) doesn't auto-link libstdc++/libm.
# Confirmed by a standalone link test against a trivial C program calling
# rubberband_new()/_delete(): it failed with "undefined reference to
# exp@@GLIBC" (from libm) until -lstdc++ -lm were added explicitly.
# Included here so a consumer never has to rediscover this.
set(RUBBERBAND_INCLUDE_DIRS "${RUBBERBAND_SRC_DIR}/rubberband")
set(RUBBERBAND_LIBRARIES rubberband stdc++ m)
set(RUBBERBAND_FOUND TRUE)
