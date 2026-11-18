# SoundLibs.cmake: the libraries the qjs-sound modules bind, registered with Deps.cmake.
#
# Each library is "installed or vendored", chosen by BUILD_<LIB>:
#
#   AUTO  the installed copy, the vendored one in third_party/ when there is none (default)
#   ON    always the vendored copy
#   OFF   the installed copy only
#
# An installed copy is looked for through -D<LIB>_PREFIX=/usr (or <LIB>_INCLUDE_DIR and
# <LIB>_LIBRARY_DIR), then pkg-config, then the default search paths. The BUILD scripts
# (Build<Lib>.cmake) run in a function scope and leave <LIB>_INCLUDE_DIRS and
# <LIB>_LIBRARIES (and <LIB>_TARGETS) behind; the callers read them with dep_include_dirs()
# and friends.

include("${CMAKE_CURRENT_LIST_DIR}/../Deps.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")
include(CheckCSourceRuns)
include(CheckCXXSourceCompiles)

set(_SOUNDLIBS_DIR "${CMAKE_CURRENT_LIST_DIR}")

# _vendored_dep_find(<name> <suffix>)
#
# FIND command of a library declared by vendored_dep(): the installed copy.
function(_vendored_dep_find name suffix)
  string(TOUPPER "${name}" upper)
  get_property(header GLOBAL PROPERTY "VDEP:${name}:HEADER")
  get_property(libs GLOBAL PROPERTY "VDEP:${name}:LIBRARY_NAMES")
  get_property(module GLOBAL PROPERTY "VDEP:${name}:PKGCONFIG_MODULE")
  get_property(extra GLOBAL PROPERTY "VDEP:${name}:EXTRA_LIBRARIES")

  vendored_find_system_library(${upper} HEADER "${header}" LIBRARY_NAMES ${libs}
                               PKGCONFIG_MODULE "${module}" EXTRA_LIBRARIES ${extra})

  if(${upper}_FOUND)
    list(GET ${upper}_LIBRARIES 0 first)
    dep_provide(${name} ${suffix} SOURCE system ORIGIN "${first}" INCLUDE_DIRS ${${upper}_INCLUDE_DIRS}
                LIBRARIES ${${upper}_LIBRARIES})
  endif()
endfunction(_vendored_dep_find)

# _vendored_dep_build(<name> <suffix> <pic> <binary> <source>)
#
# BUILD command of a library declared by vendored_dep(): runs Build<Lib>.cmake.
function(_vendored_dep_build name suffix pic binary source)
  string(TOUPPER "${name}" _vd_upper)
  get_property(_vd_file GLOBAL PROPERTY "VDEP:${name}:BUILD_FILE")

  include("${_SOUNDLIBS_DIR}/${_vd_file}")

  dep_provide(${name} ${suffix} SOURCE built ORIGIN "third_party/${name}" INCLUDE_DIRS ${${_vd_upper}_INCLUDE_DIRS}
              LIBRARIES ${${_vd_upper}_LIBRARIES} TARGETS ${${_vd_upper}_TARGETS})
endfunction(_vendored_dep_build)

# vendored_dep(<name> HEADER <file> LIBRARY_NAMES <lib>... [PKGCONFIG_MODULE <module>]
#              [EXTRA_LIBRARIES <lib>...] [DEPENDS <dep>...] BUILD_FILE <file>)
#
# Declares <name> with the generic installed-copy lookup and the BUILD script <file>
# (relative to this directory).
function(vendored_dep name)
  cmake_parse_arguments(VD "" "HEADER;PKGCONFIG_MODULE;BUILD_FILE;DESCRIPTION" "LIBRARY_NAMES;EXTRA_LIBRARIES;DEPENDS" ${ARGN})

  foreach(key HEADER PKGCONFIG_MODULE BUILD_FILE LIBRARY_NAMES EXTRA_LIBRARIES)
    set_property(GLOBAL PROPERTY "VDEP:${name}:${key}" "${VD_${key}}")
  endforeach(key)

  if(NOT VD_DESCRIPTION)
    set(VD_DESCRIPTION "${name}")
  endif()

  dep_declare(${name} DESCRIPTION "${VD_DESCRIPTION}" DEPENDS ${VD_DEPENDS} FIND _vendored_dep_find
              BUILD _vendored_dep_build)
endfunction(vendored_dep)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(ALSA QUIET)
  set(_SOUNDLIBS_ALSA ${ALSA_LIBRARIES})
endif()

vendored_dep(portaudio DESCRIPTION PortAudio HEADER portaudio.h LIBRARY_NAMES portaudio
             PKGCONFIG_MODULE portaudio-2.0 BUILD_FILE BuildPortAudio.cmake)

# no .pc file on Debian/Ubuntu; an installed libportmidi.so carries ALSA, the static one does not
vendored_dep(portmidi DESCRIPTION PortMIDI HEADER portmidi.h LIBRARY_NAMES portmidi
             EXTRA_LIBRARIES ${_SOUNDLIBS_ALSA} BUILD_FILE BuildPortMIDI.cmake)

vendored_dep(sndfile DESCRIPTION libsndfile HEADER sndfile.h LIBRARY_NAMES sndfile PKGCONFIG_MODULE sndfile
             BUILD_FILE BuildSndFile.cmake)

vendored_dep(samplerate DESCRIPTION libsamplerate HEADER samplerate.h LIBRARY_NAMES samplerate
             PKGCONFIG_MODULE samplerate BUILD_FILE BuildSampleRate.cmake)

# the header lives in soundtouch/SoundTouch.h, find_path() locates the parent
vendored_dep(soundtouch DESCRIPTION SoundTouch HEADER soundtouch/SoundTouch.h LIBRARY_NAMES SoundTouch
             PKGCONFIG_MODULE soundtouch BUILD_FILE BuildSoundTouch.cmake)

# the C API (rubberband-c.h), not the C++ RubberBandStretcher
vendored_dep(rubberband DESCRIPTION "Rubber Band" HEADER rubberband/rubberband-c.h LIBRARY_NAMES rubberband
             PKGCONFIG_MODULE rubberband BUILD_FILE BuildRubberBand.cmake)

vendored_dep(fftw3 DESCRIPTION "FFTW3 (float)" HEADER fftw3.h LIBRARY_NAMES fftw3f PKGCONFIG_MODULE fftw3f
             BUILD_FILE BuildFFTW3.cmake)

# aubio: an installed copy qualifies only when built with FFTW3
#
# aubio's own fallback FFT (ooura) takes powers of two only, the FFTW3 one any size (the
# moodbar spectrum is 254 points). The check runs a program, so a cross build always builds
# aubio itself.
function(_aubio_find name suffix)
  vendored_find_system_library(AUBIO HEADER aubio.h LIBRARY_NAMES aubio PKGCONFIG_MODULE aubio)

  # distros install the headers under <include>/aubio/, and pkg-config may report just <include>
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
    foreach(DIR ${AUBIO_LIBRARY_DIRS})
      list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-L${DIR}")
    endforeach()

    check_c_source_runs(
      "#include <aubio.h>
       int main(void) {
         aubio_fft_t* fft = new_aubio_fft(254);
         if(!fft) return 1;
         del_aubio_fft(fft);
         return 0;
       }"
      AUBIO_HAVE_FFTW3)
  endif()

  if(AUBIO_HAVE_FFTW3)
    list(GET AUBIO_LIBRARIES 0 first)
    dep_provide(aubio ${suffix} SOURCE system ORIGIN "${first}" INCLUDE_DIRS ${AUBIO_INCLUDE_DIRS}
                LIBRARIES ${AUBIO_LIBRARIES})
  else()
    message(STATUS "No installed aubio built with FFTW3")
  endif()
endfunction(_aubio_find)

function(_aubio_build name suffix pic binary source)
  include("${_SOUNDLIBS_DIR}/BuildAubio.cmake")
  dep_provide(aubio ${suffix} SOURCE built ORIGIN "third_party/aubio" INCLUDE_DIRS ${AUBIO_INCLUDE_DIRS}
              LIBRARIES ${AUBIO_LIBRARIES})
endfunction(_aubio_build)

dep_declare(aubio DESCRIPTION aubio DEPENDS fftw3 FIND _aubio_find BUILD _aubio_build)

# stk: an installed copy qualifies only when it has the STK 5 API the binding calls
#
# Installed headers are in <include>/stk/. Older releases lack BiQuad::setHighPass and
# RtAudio::getDeviceIds, so the probe compiles a call to each (compile only: no link).
function(_stk_find name suffix)
  find_path(_stk_include NAMES Stk.h PATH_SUFFIXES stk)
  find_library(_stk_library NAMES stk)

  if(_stk_include AND _stk_library)
    set(CMAKE_REQUIRED_INCLUDES "${_stk_include}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles(
      "#include <BiQuad.h>
       #include <RtAudio.h>
       void f(stk::BiQuad& b, RtAudio& a) { b.setHighPass(1000.0, 1.0); a.getDeviceIds(); }"
      STK_HAVE_V5_API)

    if(STK_HAVE_V5_API)
      dep_provide(stk ${suffix} SOURCE system ORIGIN "${_stk_library}" INCLUDE_DIRS "${_stk_include}"
                  LIBRARIES "${_stk_library}")
    else()
      message(STATUS "The installed STK is too old for the binding")
    endif()
  endif()
endfunction(_stk_find)

function(_stk_build name suffix pic binary source)
  include("${_SOUNDLIBS_DIR}/BuildStk.cmake")
  dep_provide(stk ${suffix} SOURCE built ORIGIN "${STK_BINARY_DIR}/libstk.a" INCLUDE_DIRS ${STK_INCLUDE_DIRS}
              LIBRARIES ${STK_LIBRARIES} TARGETS ${STK_TARGETS})
endfunction(_stk_build)

dep_declare(stk DESCRIPTION STK FIND _stk_find BUILD _stk_build)

# LabSound: no installed copy is looked for, the patched vendored tree is what the engine
# tests in tests/vendor/ run against
function(_labsound_build name suffix pic binary source)
  include("${_SOUNDLIBS_DIR}/BuildLabSound.cmake")
  dep_provide(labsound ${suffix} SOURCE built ORIGIN "${LABSOUND_BINARY_DIR}/bin" INCLUDE_DIRS ${LABSOUND_INCLUDE_DIRS}
              LIBRARIES ${LABSOUND_LIBRARIES} TARGETS ${LABSOUND_TARGETS})
endfunction(_labsound_build)

dep_declare(labsound DESCRIPTION LabSound BUILD _labsound_build)
