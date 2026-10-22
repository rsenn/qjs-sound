# Locates aubio built with FFTW3, either as a system install (explicit
# hints, pkg-config's aubio module, default search paths) or, if none
# qualifies or BUILD_AUBIO is ON, built from the vendored third_party/aubio
# submodule by BuildAubio.cmake.
#
# A system aubio qualifies only if new_aubio_fft(254) succeeds: aubio's own
# fallback FFT (ooura) takes powers of two only, the FFTW3 one any size
# (the moodbar spectrum is 254 points). Not cross-compiling-safe: the
# check runs a program, so a cross build always builds aubio itself.
#
# Sets AUBIO_FOUND, AUBIO_INCLUDE_DIRS, AUBIO_LIBRARIES, AUBIO_LIBRARY_DIRS.
# Consumed by quickjs-aubio.c.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")
include(CheckCSourceRuns)

option(BUILD_AUBIO
       "Always build aubio from third_party/aubio (with FFTW3) instead of using the system library" OFF)

if(NOT BUILD_AUBIO)
  vendored_find_system_library(AUBIO HEADER aubio.h LIBRARY_NAMES aubio
                               PKGCONFIG_MODULE aubio)

  # Distros install the headers under <include>/aubio/, and pkg-config
  # may report just <include>.
  if(AUBIO_INCLUDE_DIRS AND NOT EXISTS "${AUBIO_INCLUDE_DIRS}/aubio.h"
     AND EXISTS "${AUBIO_INCLUDE_DIRS}/aubio/aubio.h")
    set(AUBIO_INCLUDE_DIRS "${AUBIO_INCLUDE_DIRS}/aubio")
  elseif(NOT AUBIO_INCLUDE_DIRS)
    find_path(AUBIO_INCLUDE_DIRS NAMES aubio.h PATH_SUFFIXES aubio)
  endif()

  if(AUBIO_INCLUDE_DIRS AND AUBIO_LIBRARIES AND NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_REQUIRED_INCLUDES ${AUBIO_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES ${AUBIO_LIBRARIES})
    set(CMAKE_REQUIRED_LINK_OPTIONS)
    if(AUBIO_LIBRARY_DIRS)
      foreach(DIR ${AUBIO_LIBRARY_DIRS})
        list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-L${DIR}")
      endforeach()
    endif()
    check_c_source_runs(
      "#include <aubio.h>
       int main(void) {
         aubio_fft_t* fft = new_aubio_fft(254);
         if(!fft) return 1;
         del_aubio_fft(fft);
         return 0;
       }"
      AUBIO_HAVE_FFTW3)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
  endif()

  if(AUBIO_HAVE_FFTW3)
    message(STATUS "Using system aubio (FFTW3): ${AUBIO_LIBRARIES}")
  else()
    message(STATUS "No system aubio built with FFTW3, building third_party/aubio")
    unset(AUBIO_INCLUDE_DIRS)
    unset(AUBIO_LIBRARIES)
    unset(AUBIO_LIBRARY_DIRS)
    unset(AUBIO_FOUND)
  endif()
endif()

if(BUILD_AUBIO OR NOT AUBIO_HAVE_FFTW3)
  include("${CMAKE_CURRENT_LIST_DIR}/BuildAubio.cmake")
endif()
