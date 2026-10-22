# Locates single-precision FFTW3 (libfftw3f, what aubio's float smpl_t
# needs), either as a system install (explicit hints, pkg-config's fftw3f
# module, default search paths) or, if none is found or BUILD_FFTW3 is ON,
# downloaded and built by BuildFFTW3.cmake as a static, -fPIC archive.
#
# Sets FFTW3_FOUND, FFTW3_INCLUDE_DIRS, FFTW3_LIBRARIES, FFTW3_LIBRARY_DIRS.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

option(BUILD_FFTW3
       "Download and build FFTW3 (float, static, -fPIC) instead of using the system library" OFF)

if(NOT BUILD_FFTW3)
  # Pin an install with -DFFTW3_PREFIX=/usr (or FFTW3_INCLUDE_DIR/FFTW3_LIBRARY_DIR).
  vendored_find_system_library(FFTW3 HEADER fftw3.h LIBRARY_NAMES fftw3f
                               PKGCONFIG_MODULE fftw3f)
endif()

if(BUILD_FFTW3 OR NOT FFTW3_FOUND)
  if(NOT BUILD_FFTW3)
    message(STATUS "FFTW3 (fftw3f) not found on the system, building it")
  endif()
  include("${CMAKE_CURRENT_LIST_DIR}/BuildFFTW3.cmake")
endif()
