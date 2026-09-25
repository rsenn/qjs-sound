project(qjs-sound C CXX)

cmake_minimum_required(VERSION 3.19)

option(USE_LABSOUND "Use LabSound library" ON)
option(USE_STK "Use Stk library" ON)
option(USE_PORTAUDIO "Use PortAudio library" ON)
option(USE_AUBIO "Use aubio library" ON)
option(USE_PORTMIDI "Use PortMidi library" ON)
option(USE_SNDFILE "Use libsndfile library" ON)
option(USE_SAMPLERATE "Use libsamplerate library" ON)
option(USE_SOUNDTOUCH "Use SoundTouch library" ON)
option(USE_RUBBERBAND "Use Rubber Band library" ON)

include(FindPkgConfig)
include(CheckLibraryExists)
include(CheckSymbolExists)

#add_subdirectory(stk)
#add_subdirectory(sndobj)

#[[set(_LAB_CXX_FLAGS "${_LAB_CXX_FLAGS} -fPIC -w")
set(_NQR_CXX_FLAGS "${_NQR_CXX_FLAGS} -fPIC -w")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -w")]]

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)

check_flag("-w" F_NOWARN MODULE_COMPILE_FLAGS)
check_flag("-fPIC" F_PIC MODULE_COMPILE_FLAGS)
check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

#string(REGEX REPLACE "[\n\r;]+" " " MODULE_COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")
message("MODULE_COMPILE_FLAGS: ${MODULE_COMPILE_FLAGS}")

include(ExternalProject)

if(USE_LABSOUND)
  ExternalProject_Add(
    LabSound
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/LabSound"
    PREFIX LabSound
    CMAKE_ARGS
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      #"-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
      "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} ${MODULE_COMPILE_FLAGS}"
      "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
    INSTALL_COMMAND "")

  ExternalProject_Get_Property(LabSound BINARY_DIR)
  set(LABSOUND_BINARY_DIR "${BINARY_DIR}")
  message("Binary dir of LabSound = ${LABSOUND_BINARY_DIR}")
endif(USE_LABSOUND)

if(USE_STK)
  ExternalProject_Add(
    stk
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk"
    PREFIX stk
    CMAKE_ARGS
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      #"-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
      "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} ${MODULE_COMPILE_FLAGS}"
      "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
      "-DCOMPILE_PROJECTS:BOOL=OFF"
    INSTALL_COMMAND "")

  ExternalProject_Get_Property(stk BINARY_DIR)
  set(STK_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk")
  set(STK_LIBRARY stk)
  set(STK_BINARY_DIR "${BINARY_DIR}")
  add_library(libstk STATIC IMPORTED)
  file(GLOB STK_LIBRARY_PATH "${STK_LIBRARY_DIR}/*.a")
  if(NOT STK_LIBRARY_PATH)
    set(STK_LIBRARY_PATH "${STK_LIBRARY_DIR}/libstk.a")
  endif(NOT STK_LIBRARY_PATH)
  #dump(STK_LIBRARY_PATH)
  set_target_properties(libstk PROPERTIES IMPORTED_LOCATION
                                          "${STK_LIBRARY_PATH}")

  link_directories("${STK_LIBRARY_DIR}")

  message("Binary dir of stk = ${STK_BINARY_DIR}")
endif(USE_STK)

include(CheckCCompilerFlag)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)

set(labsound_LINK_DIRECTORIES ${LABSOUND_BINARY_DIR}/bin
                              ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib)

if(CMAKE_BUILD_TYPE MATCHES "Rel")
  #set(DEBUG_SUFFIX _d)
else(CMAKE_BUILD_TYPE MATCHES "Rel")
  set(DEBUG_SUFFIX "")
endif(CMAKE_BUILD_TYPE MATCHES "Rel")

add_library(libLabSound STATIC IMPORTED GLOBAL)
set_target_properties(
  libLabSound
  PROPERTIES IMPORTED_LOCATION
             "${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a"
             IMPORTED_LOCATION_DEBUG
             "${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a"
             IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
add_library(libLabSoundRtAudio STATIC IMPORTED GLOBAL)
set_target_properties(
  libLabSoundRtAudio
  PROPERTIES IMPORTED_LOCATION
             "${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a"
             IMPORTED_LOCATION_DEBUG
             "${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a"
             IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")

link_directories(${LABSOUND_BINARY_DIR}/bin
                 ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib)
set(labsound_LIBRARIES
    #  ${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a
    #A ${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a
    libLabSound
    libLabSoundRtAudio
    ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib/liblibnyquist.a
    #LabSound_d libnyquist_d
    pulse
    pulse-simple
    samplerate
    pthread)

link_directories(${labsound_LINK_DIRECTORIES})

find_quickjs()
configure_quickjs()
add_definitions(-fvisibility=hidden)

#dump(QUICKJS_INCLUDE_DIR QUICKJS_LIBRARY_DIR)
include_directories(${QUICKJS_INCLUDE_DIR})
link_directories(${QUICKJS_LIBRARY_DIR})

if(USE_STK)
  if(LINUX OR ANDROID)
    add_definitions(-D__LINUX_ALSA__ #-D__LINUX_PULSE__
    )
  endif()

  #link_directories(${STK_LIBRARY_DIR})

  set(stk_LIBRARIES libstk ${ALSA_LIBRARY})
  if(LINUX OR ANDROID)
    # RtAudio/RtMidi (ALSA + JACK backends) and Thread/Mutex are compiled
    # into libstk.a; the JS bindings now call into them, so their runtime
    # deps must be linked into qjs-stk too.
    list(APPEND stk_LIBRARIES pthread asound jack)
  endif()
  set(stk_LINK_DIRECTORIES "${STK_LIBRARY_DIR}")

  #dump(stk_LIBRARIES stk_LINK_DIRECTORIES)

  file(GLOB stk_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stk/src/*.cpp)
  list(FILTER stk_SOURCES EXCLUDE REGEX
       "(Inet|Rt|Tcp|Udp|Messager|Thread|Mutex|Socket)")
  #list(APPEND stk_SOURCES quickjs-stk.cpp)

  #dump(stk_SOURCES)

  add_definitions(-DUSE_STK)

  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(stk cpp)

  unset(COMMON_SOURCES)

  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/include"
                      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/src/include")
  set_target_properties(
    qjs-stk
    PROPERTIES
      COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
      INCLUDE_DIRECTORIES
      "${QUICKJS_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/src/include"
  )
endif(USE_STK)

if(USE_AUBIO)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindAubio.cmake")

  set(aubio_LIBRARIES ${AUBIO_LIBRARIES})

  make_module(aubio c)

  include_directories(${AUBIO_INCLUDE_DIRS})
  set_target_properties(
    qjs-aubio PROPERTIES INCLUDE_DIRECTORIES
                         "${QUICKJS_INCLUDE_DIR};${AUBIO_INCLUDE_DIRS}")
endif(USE_AUBIO)

if(USE_LABSOUND)
  #[[file(
    GLOB
    labsound_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/extended/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/backends/RtAudio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/backends/miniaudio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/internal/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/internal/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/src/core/*.cpp)]]

  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(labsound cpp)

  unset(COMMON_SOURCES)

  include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include"
  )
  set_target_properties(
    qjs-labsound
    PROPERTIES
      COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
      INCLUDE_DIRECTORIES
      "${QUICKJS_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/third_party/libnyquist/include"
  )

  add_executable(test-labsound test-labsound.cpp)

  target_link_libraries(test-labsound PRIVATE ${labsound_LIBRARIES})
endif(USE_LABSOUND)

if(USE_PORTAUDIO)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPortAudio.cmake")

  make_module(portaudio c)

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
    target_compile_definitions(qjs-portaudio
                               PRIVATE HAVE_GETSTREAMHOSTAPITYPE=1)
  endif(HAVE_GETSTREAMHOSTAPITYPE)

  target_include_directories(qjs-portaudio PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
  target_link_libraries(qjs-portaudio PRIVATE ${PORTAUDIO_LIBRARIES})
endif(USE_PORTAUDIO)

if(USE_PORTMIDI)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPortMIDI.cmake")

  make_module(portmidi c)

  target_include_directories(qjs-portmidi PRIVATE ${PORTMIDI_INCLUDE_DIRS})
  target_link_libraries(qjs-portmidi PRIVATE ${PORTMIDI_LIBRARIES})
endif(USE_PORTMIDI)

if(USE_SNDFILE)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSndFile.cmake")

  set(sndfile_LIBRARIES ${SNDFILE_LIBRARIES})
  # quickjs-typedarray.c is shared TypedArray/ArrayBuffer marshalling
  # code, not part of libsndfile itself - see doc/sndfile.md's "Shared
  # TypedArray/buffer interop" section.
  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(sndfile c)

  unset(COMMON_SOURCES)

  include_directories(${SNDFILE_INCLUDE_DIRS})
  set_target_properties(
    qjs-sndfile
    PROPERTIES COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
               INCLUDE_DIRECTORIES
               "${QUICKJS_INCLUDE_DIR};${SNDFILE_INCLUDE_DIRS}")
endif(USE_SNDFILE)

if(USE_SAMPLERATE)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSampleRate.cmake")

  set(samplerate_LIBRARIES ${SAMPLERATE_LIBRARIES})
  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(samplerate c)

  unset(COMMON_SOURCES)

  include_directories(${SAMPLERATE_INCLUDE_DIRS})
  set_target_properties(
    qjs-samplerate
    PROPERTIES COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
               INCLUDE_DIRECTORIES
               "${QUICKJS_INCLUDE_DIR};${SAMPLERATE_INCLUDE_DIRS}")
endif(USE_SAMPLERATE)

if(USE_SOUNDTOUCH)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSoundTouch.cmake")

  set(soundtouch_LIBRARIES ${SOUNDTOUCH_LIBRARIES})
  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(soundtouch cpp)

  unset(COMMON_SOURCES)

  include_directories(${SOUNDTOUCH_INCLUDE_DIRS})
  set_target_properties(
    qjs-soundtouch
    PROPERTIES COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
               INCLUDE_DIRECTORIES
               "${QUICKJS_INCLUDE_DIR};${SOUNDTOUCH_INCLUDE_DIRS}")
endif(USE_SOUNDTOUCH)

if(USE_RUBBERBAND)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindRubberBand.cmake")

  set(rubberband_LIBRARIES ${RUBBERBAND_LIBRARIES})
  set(COMMON_SOURCES quickjs-typedarray.c)

  make_module(rubberband c)

  unset(COMMON_SOURCES)

  include_directories(${RUBBERBAND_INCLUDE_DIRS})
  set_target_properties(
    qjs-rubberband
    PROPERTIES COMPILE_FLAGS ${MODULE_COMPILE_FLAGS}
               INCLUDE_DIRECTORIES
               "${QUICKJS_INCLUDE_DIR};${RUBBERBAND_INCLUDE_DIRS}")
endif(USE_RUBBERBAND)
