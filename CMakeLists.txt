project(qjs-sound C CXX)

cmake_minimum_required(VERSION 3.19)

enable_testing()

add_test(NAME check-cflag-result-cached
  COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/check-cflag-result-cached
          -B ${CMAKE_CURRENT_BINARY_DIR}/tests/check-cflag-result-cached)

# MODULE_<name> turns a module on or off: AUTO (default) builds it when its library is
# installed or can be built, ON asks for it, OFF drops it. BUILD_<lib> says where the library
# comes from: AUTO (installed, else the vendored copy in third_party/), ON (always vendored),
# OFF (installed only); see cmake/deps/SoundLibs.cmake.
foreach(_m labsound stk portaudio aubio portmidi sndfile samplerate soundtouch rubberband sndobj)
  string(TOUPPER "${_m}" _u)
  set(MODULE_${_u} AUTO CACHE STRING "Build the '${_m}' module: AUTO, ON or OFF")
  set_property(CACHE MODULE_${_u} PROPERTY STRINGS AUTO ON OFF)

  # USE_<NAME>=OFF, the old switch, still drops a module
  if(DEFINED USE_${_u} AND NOT USE_${_u})
    set(MODULE_${_u} OFF)
  endif()
endforeach(_m)

include(FindPkgConfig)
include(CheckLibraryExists)
include(CheckSymbolExists)

#add_subdirectory(stk)
#add_subdirectory(sndobj)

#[[set(_LAB_CXX_FLAGS "${_LAB_CXX_FLAGS} -fPIC -w")
set(_NQR_CXX_FLAGS "${_NQR_CXX_FLAGS} -fPIC -w")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -w")]]

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Compat.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Checks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)

check_flag("-w" F_NOWARN MODULE_COMPILE_FLAGS)
check_flag("-fPIC" F_PIC MODULE_COMPILE_FLAGS)
check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

#string(REGEX REPLACE "[\n\r;]+" " " MODULE_COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")
message("MODULE_COMPILE_FLAGS: ${MODULE_COMPILE_FLAGS}")

include(ExternalProject)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/SoundLibs.cmake)

# sound_module(<name>)
#
# Decides whether the module <name> is built: SOUND_<NAME> is TRUE unless MODULE_<NAME> is OFF
# or its library is neither installed nor built. Resolves the library of the same name through
# Deps.cmake and leaves <NAME>_INCLUDE_DIRS, <NAME>_LIBRARIES and <name>_DEPS behind.
macro(sound_module MODULE)
  string(TOUPPER "${MODULE}" _SM_U)
  dep_module_mode(${MODULE} _SM_MODE)
  get_property(_SM_DECLARED GLOBAL PROPERTY "DEP:${MODULE}:DECLARED")
  set(SOUND_${_SM_U} FALSE)

  if(NOT "${_SM_MODE}" STREQUAL "OFF")
    set(SOUND_${_SM_U} TRUE)

    if(_SM_DECLARED)
      dep_resolve(${MODULE} SUFFIX main PIC ON)
      dep_get(${MODULE} main SOURCE _SM_SOURCE)

      if("${_SM_SOURCE}" STREQUAL "missing")
        set(SOUND_${_SM_U} FALSE)
        message(WARNING "Module '${MODULE}' skipped: its library is neither installed nor built")
      else()
        dep_include_dirs(${MODULE} main ${_SM_U}_INCLUDE_DIRS)
        dep_libraries(${MODULE} main ${_SM_U}_LIBRARIES)
        dep_link_flags(${MODULE} main _SM_FLAGS)
        dep_targets(${MODULE} main ${MODULE}_DEPS)
        list(APPEND ${_SM_U}_LIBRARIES ${_SM_FLAGS})
      endif()
    endif()
  endif()
endmacro(sound_module)

foreach(_m labsound stk portaudio aubio portmidi sndfile samplerate soundtouch rubberband sndobj)
  sound_module(${_m})
endforeach(_m)

dep_summary(DEFER)

include(CheckCCompilerFlag)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
quickjs_module_options(SHARED_DEFAULT ON STATIC_DEFAULT OFF)

# module_include_directories <name> [dirs...]
#
# Set the include directories of the shared (qjs-<name>) and static (qjs-<name>-static)
# target of a module, whichever of them BUILD_SHARED/BUILD_STATIC created. Replaces the
# directory-wide ones, so another library's same-named header (stk's ADSR.h) is not found.
function(module_include_directories NAME)
  foreach(T qjs-${NAME} qjs-${NAME}-static)
    if(TARGET ${T})
      set_target_properties(${T} PROPERTIES INCLUDE_DIRECTORIES "${QUICKJS_INCLUDE_DIR};${ARGN}")
    endif(TARGET ${T})
  endforeach(T)
endfunction(module_include_directories)

# module_link_libraries <name> [libs...]
#
# Link libs into the shared and static target of a module.
function(module_link_libraries NAME)
  foreach(T qjs-${NAME} qjs-${NAME}-static)
    if(TARGET ${T})
      target_link_libraries(${T} PRIVATE ${ARGN})
    endif(TARGET ${T})
  endforeach(T)

  # make_module() wrote quickjs-<name>.module.cmake before these libraries were known
  if(TARGET qjs-${NAME}-static AND ARGN)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/quickjs-${NAME}.module.cmake"
         "list(APPEND ${NAME}_MODULE_LIBRARIES \"${ARGN}\")\n"
         "set(${NAME}_MODULE \"\${${NAME}_MODULE_LIBRARY};\${${NAME}_MODULE_LIBRARIES}\")\n")
  endif()
endfunction(module_link_libraries)

# module_compile_definitions <name> [defs...]
function(module_compile_definitions NAME)
  foreach(T qjs-${NAME} qjs-${NAME}-static)
    if(TARGET ${T})
      target_compile_definitions(${T} PRIVATE ${ARGN})
    endif(TARGET ${T})
  endforeach(T)
endfunction(module_compile_definitions)

set(labsound_LINK_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/third_party/LabSound/bin
                              ${CMAKE_CURRENT_BINARY_DIR}/third_party/LabSound/third_party/libnyquist/lib)

find_quickjs()
configure_quickjs()
add_definitions(-fvisibility=hidden)

#dump(QUICKJS_INCLUDE_DIR QUICKJS_LIBRARY_DIR)
include_directories(${QUICKJS_INCLUDE_DIR})
link_directories(${QUICKJS_LIBRARY_DIR})

file(GLOB JS_HELPER_SOURCES js-utils.[ch] js-typedarray.[ch])

if(SOUND_STK)
  if(LINUX OR ANDROID)
    add_definitions(-D__LINUX_ALSA__ #-D__LINUX_PULSE__
    )
  endif()

  set(stk_LIBRARIES ${STK_LIBRARIES})
  set(stk_LINK_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk")

  #dump(stk_LIBRARIES stk_LINK_DIRECTORIES)

  file(GLOB stk_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stk/src/*.cpp)
  list(FILTER stk_SOURCES EXCLUDE REGEX
       "(Inet|Rt|Tcp|Udp|Messager|Thread|Mutex|Socket)")
  #list(APPEND stk_SOURCES quickjs-stk.cpp)

  #dump(stk_SOURCES)

  add_definitions(-DUSE_STK)

  make_module(stk quickjs-stk.cpp ${JS_HELPER_SOURCES})

  include_directories(${STK_INCLUDE_DIRS})
  module_include_directories(stk ${STK_INCLUDE_DIRS})

  find_program(QJSM qjsm)
  if(QJSM)
    add_test(NAME stk-tostringtag-order
             COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/stk-tostringtag-order.js
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(stk-tostringtag-order PROPERTIES
      ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
  else()
    message(WARNING "qjsm not found; stk binding tests will be skipped")
  endif()
endif(SOUND_STK)

if(SOUND_AUBIO)
  set(aubio_LIBRARIES ${AUBIO_LIBRARIES})

  make_module(aubio quickjs-aubio.c ${JS_HELPER_SOURCES})

  include_directories(${AUBIO_INCLUDE_DIRS})
  module_include_directories(aubio ${AUBIO_INCLUDE_DIRS})

  # only the vendored copy carries the patch
  dep_get(aubio main SOURCE _aubio_source)
  if("${_aubio_source}" STREQUAL "built")
    add_executable(test-aubio-pitch-confidence tests/vendor/aubio-pitch-confidence-reads-zero.cpp)
    target_include_directories(test-aubio-pitch-confidence PRIVATE ${AUBIO_INCLUDE_DIRS})
    target_link_libraries(test-aubio-pitch-confidence PRIVATE ${AUBIO_LIBRARIES})
    add_test(NAME aubio-pitch-confidence-reads-zero COMMAND test-aubio-pitch-confidence)
  endif()
endif(SOUND_AUBIO)

if(SOUND_LABSOUND)
  #[[file(
    GLOB
    labsound_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/extended/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/backends/RtAudio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/backends/miniaudio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/internal/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/internal/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/core/*.cpp)]]

  set(labsound_LIBRARIES ${LABSOUND_LIBRARIES})

  make_module(labsound quickjs-labsound.cpp ${JS_HELPER_SOURCES})

  include_directories(${LABSOUND_INCLUDE_DIRS})
  module_include_directories(labsound ${LABSOUND_INCLUDE_DIRS})

  # the engine tests run against the patched vendored tree only
  dep_get(labsound main SOURCE _labsound_source)
  if("${_labsound_source}" STREQUAL "built")
    add_executable(test-labsound-disconnect-deferred
      tests/vendor/labsound-disconnect-deferred.cpp)
    target_link_libraries(test-labsound-disconnect-deferred PRIVATE ${labsound_LIBRARIES})
    target_include_directories(test-labsound-disconnect-deferred PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include")
    add_dependencies(test-labsound-disconnect-deferred LabSound)
    add_test(NAME labsound-disconnect-deferred
             COMMAND test-labsound-disconnect-deferred)

    add_executable(test-labsound-scheduler-epoch
      tests/vendor/labsound-scheduler-epoch.cpp)
    target_link_libraries(test-labsound-scheduler-epoch PRIVATE ${labsound_LIBRARIES})
    target_include_directories(test-labsound-scheduler-epoch PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include")
    add_dependencies(test-labsound-scheduler-epoch LabSound)
    add_test(NAME labsound-scheduler-epoch
             COMMAND test-labsound-scheduler-epoch)

    add_executable(test-labsound-compressor-channel-assert
      tests/vendor/labsound-compressor-channel-assert.cpp)
    target_link_libraries(test-labsound-compressor-channel-assert PRIVATE ${labsound_LIBRARIES})
    target_include_directories(test-labsound-compressor-channel-assert PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include")
    add_dependencies(test-labsound-compressor-channel-assert LabSound)
    add_test(NAME labsound-compressor-channel-assert
             COMMAND test-labsound-compressor-channel-assert)

    add_executable(test-labsound-stopped-source-stuck-stopping
      tests/vendor/labsound-stopped-source-stuck-stopping.cpp)
    target_link_libraries(test-labsound-stopped-source-stuck-stopping PRIVATE ${labsound_LIBRARIES})
    target_include_directories(test-labsound-stopped-source-stuck-stopping PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include")
    add_dependencies(test-labsound-stopped-source-stuck-stopping LabSound)
    add_test(NAME labsound-stopped-source-stuck-stopping
             COMMAND test-labsound-stopped-source-stuck-stopping)

    add_executable(test-labsound-functionnode-offline-silent
      tests/vendor/labsound-functionnode-offline-silent.cpp)
    target_link_libraries(test-labsound-functionnode-offline-silent PRIVATE ${labsound_LIBRARIES})
    target_include_directories(test-labsound-functionnode-offline-silent PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include")
    add_dependencies(test-labsound-functionnode-offline-silent LabSound)
    add_test(NAME labsound-functionnode-offline-silent
             COMMAND test-labsound-functionnode-offline-silent)
  endif()

  find_program(QJSM qjsm)
  if(QJSM)
    add_test(NAME labsound-waveshapernode-offline-silent
             COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/labsound-waveshapernode-offline-silent.js
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(labsound-waveshapernode-offline-silent PROPERTIES
      ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME portaudio-pastream-reopen-after-close
             COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/portaudio-pastream-reopen-after-close.js
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(portaudio-pastream-reopen-after-close PROPERTIES
      ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME portaudio-pastream-getters
             COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/portaudio-pastream-getters.js
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(portaudio-pastream-getters PROPERTIES
      ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME portaudio-pastream-buffer-format
             COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/portaudio-pastream-buffer-format.js
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(portaudio-pastream-buffer-format PROPERTIES
      ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
  else()
    message(WARNING "qjsm not found; binding tests will be skipped")
  endif()
endif(SOUND_LABSOUND)

if(SOUND_PORTAUDIO)
  make_module(portaudio quickjs-portaudio.c ${JS_HELPER_SOURCES})

  # check_symbol_exists (a header check) rather than check_library_exists
  # (a compiled-library check): the latter needs an already-built library
  # file/name it can hand the linker, which doesn't exist yet at configure
  # time when BUILD_PORTAUDIO is ON (PORTAUDIO_LIBRARIES is
  # just the not-yet-built CMake target name `portaudio` in that case) -
  # confirmed by testing BUILD_PORTAUDIO=ON, which silently
  # left HAVE_GETSTREAMHOSTAPITYPE unset under the old check even though
  # the vendored PortAudio (19.7) does export the symbol.
  set(CMAKE_REQUIRED_INCLUDES "${PORTAUDIO_INCLUDE_DIRS}")
  check_symbol_exists(Pa_GetStreamHostApiType "portaudio.h"
                      HAVE_GETSTREAMHOSTAPITYPE)
  unset(CMAKE_REQUIRED_INCLUDES)

  if(HAVE_GETSTREAMHOSTAPITYPE)
    module_compile_definitions(portaudio HAVE_GETSTREAMHOSTAPITYPE=1)
  endif(HAVE_GETSTREAMHOSTAPITYPE)

  module_include_directories(portaudio ${PORTAUDIO_INCLUDE_DIRS})
  module_link_libraries(portaudio ${PORTAUDIO_LIBRARIES})
endif(SOUND_PORTAUDIO)

if(SOUND_PORTMIDI)
  make_module(portmidi quickjs-portmidi.c ${JS_HELPER_SOURCES})

  module_include_directories(portmidi ${PORTMIDI_INCLUDE_DIRS})
  module_link_libraries(portmidi ${PORTMIDI_LIBRARIES})
endif(SOUND_PORTMIDI)

if(SOUND_SNDFILE)
  set(sndfile_LIBRARIES ${SNDFILE_LIBRARIES})
  # js-typedarray.c is shared TypedArray/ArrayBuffer marshalling
  # code, not part of libsndfile itself - see doc/native/sndfile.md's "Shared
  # TypedArray/buffer interop" section.

  make_module(sndfile quickjs-sndfile.c ${JS_HELPER_SOURCES})

  include_directories(${SNDFILE_INCLUDE_DIRS})
  module_include_directories(sndfile ${SNDFILE_INCLUDE_DIRS})
endif(SOUND_SNDFILE)

if(SOUND_SAMPLERATE)
  set(samplerate_LIBRARIES ${SAMPLERATE_LIBRARIES})

  make_module(samplerate quickjs-samplerate.c ${JS_HELPER_SOURCES})

  include_directories(${SAMPLERATE_INCLUDE_DIRS})
  module_include_directories(samplerate ${SAMPLERATE_INCLUDE_DIRS})
endif(SOUND_SAMPLERATE)

if(SOUND_SOUNDTOUCH)
  set(soundtouch_LIBRARIES ${SOUNDTOUCH_LIBRARIES})

  make_module(soundtouch quickjs-soundtouch.cpp ${JS_HELPER_SOURCES})

  include_directories(${SOUNDTOUCH_INCLUDE_DIRS})
  module_include_directories(soundtouch ${SOUNDTOUCH_INCLUDE_DIRS})
endif(SOUND_SOUNDTOUCH)

if(SOUND_RUBBERBAND)
  set(rubberband_LIBRARIES ${RUBBERBAND_LIBRARIES})

  make_module(rubberband quickjs-rubberband.c ${JS_HELPER_SOURCES})

  include_directories(${RUBBERBAND_INCLUDE_DIRS})
  module_include_directories(rubberband ${RUBBERBAND_INCLUDE_DIRS})
endif(SOUND_RUBBERBAND)

if(SOUND_SNDOBJ)
  # Only the sources needed by the 11 classes this module binds (see
  # doc/native/sndobj.md) - not a glob, since most of third_party/sndobj/src
  # (file/realtime/MIDI IO, the FFT/phase-vocoder family) is out of scope
  # and the FFT family additionally needs a vendored rfftw/rfftw.h this
  # tree doesn't ship in its include path.
  set(sndobj_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/SndObj.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/HarmTable.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Oscil.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Oscili.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Buzz.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Rand.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Randh.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Randi.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/ADSR.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Filter.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Reson.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/DelayLine.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Comb.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Allpass.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/VDelay.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Gain.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src/Mix.cpp)


  make_module(sndobj quickjs-sndobj.cpp ${JS_HELPER_SOURCES})

  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src")
  module_include_directories(sndobj "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sndobj/src")
endif(SOUND_SNDOBJ)
