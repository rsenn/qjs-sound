project(qjs-sound C CXX)

cmake_minimum_required(VERSION 3.19)

option(USE_LABSOUND "Use LabSound library" ON)
option(USE_STK "Use Stk library" ON)

#add_subdirectory(stk)
#add_subdirectory(sndobj)

#[[set(_LAB_CXX_FLAGS "${_LAB_CXX_FLAGS} -fPIC -w")
set(_NQR_CXX_FLAGS "${_NQR_CXX_FLAGS} -fPIC -w")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -w")]]

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
#include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckFlags.cmake)

#check_cxx_standard_flag(c++20 c++23)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

check_flag("-std=c++20" STD_CXX20 MODULE_COMPILE_FLAGS)
check_flag("-w" F_NOWARN MODULE_COMPILE_FLAGS)
check_flag("-fPIC" F_PIC MODULE_COMPILE_FLAGS)
check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

dump(VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

include(ExternalProject)

if(USE_LABSOUND)
  ExternalProject_Add(
    LabSound
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/LabSound"
    PREFIX LabSound
    CMAKE_CACHE_ARGS
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
      "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}"
      "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
    INSTALL_COMMAND "")

  ExternalProject_Get_Property(LabSound BINARY_DIR)
  set(LABSOUND_BINARY_DIR "${BINARY_DIR}")
  message("Binary dir of LabSound = ${LABSOUND_BINARY_DIR}")
endif(USE_LABSOUND)

set(labsound_DEPS LabSound)


if(USE_STK)
  ExternalProject_Add(
    stk
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk"
    PREFIX stk
    CMAKE_CACHE_ARGS
      "-DCOMPILE_PROJECTS:BOOL=OFF"
      "-DBUILD_SHARED:BOOL=OFF"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
      "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}"
      "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
    INSTALL_COMMAND "")

  ExternalProject_Get_Property(stk BINARY_DIR)
  set(STK_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk")
  set(STK_LIBRARY libstk)
  set(STK_BINARY_DIR "${BINARY_DIR}")
  add_library(libstk STATIC IMPORTED)
  file(GLOB STK_LIBRARY_PATH "${STK_LIBRARY_DIR}/*.a")
  if(NOT STK_LIBRARY_PATH)
    set(STK_LIBRARY_PATH "${STK_LIBRARY_DIR}/libstk.a")
  endif(NOT STK_LIBRARY_PATH)
  #dump(STK_LIBRARY_PATH)
  set_target_properties(libstk PROPERTIES IMPORTED_LOCATION "${STK_LIBRARY_PATH}")

  link_directories("${STK_LIBRARY_DIR}")

  message("Binary dir of stk = ${STK_BINARY_DIR}")
endif()

set(stk_DEPS stk)

include(CheckCCompilerFlag)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)

set(labsound_LINK_DIRECTORIES ${LABSOUND_BINARY_DIR}/bin ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib)

if(CMAKE_BUILD_TYPE MATCHES "Deb")
  set(DEBUG_SUFFIX _d)
else(CMAKE_BUILD_TYPE MATCHES "Deb")
  set(DEBUG_SUFFIX "")
endif(CMAKE_BUILD_TYPE MATCHES "Deb")

add_library(libLabSound STATIC IMPORTED GLOBAL)
set_target_properties(
  libLabSound
  PROPERTIES IMPORTED_LOCATION "${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a"
             IMPORTED_LOCATION_DEBUG "${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a" IMPORTED_CONFIGURATIONS
                                                                                               "RELEASE;DEBUG")
add_library(libLabSoundRtAudio STATIC IMPORTED GLOBAL)
set_target_properties(
  libLabSoundRtAudio
  PROPERTIES IMPORTED_LOCATION "${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a"
             IMPORTED_LOCATION_DEBUG "${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a"
             IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")

link_directories(${LABSOUND_BINARY_DIR}/bin ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib)
set(labsound_LIBRARIES
    #${LABSOUND_BINARY_DIR}/bin/libLabSound${DEBUG_SUFFIX}.a
    #${LABSOUND_BINARY_DIR}/bin/libLabSoundRtAudio${DEBUG_SUFFIX}.a
    libLabSound libLabSoundRtAudio ${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib/liblibnyquist${DEBUG_SUFFIX}.a
    #LabSound_d libnyquist_d
    asound pulse pulse-simple samplerate pthread)

set(labsound_DEPS LabSound)

find_quickjs()
configure_quickjs()

#add_definitions(-fvisibility=hidden)

#dump(QUICKJS_INCLUDE_DIR QUICKJS_LIBRARY_DIR)
include_directories(${QUICKJS_INCLUDE_DIR})
link_directories(${QUICKJS_LIBRARY_DIR})
link_libraries(quickjs)

if(USE_STK)
  if(LINUX OR ANDROID)
    add_definitions(-D__LINUX_ALSA__)
    add_definitions(-D__LINUX_PULSE__)
  endif()

  #link_directories(${STK_LIBRARY_DIR})
  if(STK_LIBRARY)
    set(stk_LIBRARIES ${STK_LIBRARY})
    set(stk_LINK_DIRECTORIES "${STK_LIBRARY_DIR}")
  endif(STK_LIBRARY)

  unset(stk_LIBRARIES)

  #dump(stk_LIBRARIES stk_LINK_DIRECTORIES)

  file(GLOB stk_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stk/src/*.cpp)
  list(FILTER stk_SOURCES EXCLUDE REGEX "(Inet|Rt|Tcp|Udp|Messager|Thread|Mutex|Socket)")
  #list(APPEND stk_SOURCES quickjs-stk.cpp)

  #dump(stk_SOURCES)

  add_definitions(-DUSE_STK)

  make_module(stk cpp)

  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/include"
                      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/src/include")
  set_target_properties(
    qjs-stk
    PROPERTIES
      INCLUDE_DIRECTORIES
      "${QUICKJS_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk/src/include"
  )
  target_link_libraries(qjs-stk PRIVATE ${STK_LIBRARY})

endif(USE_STK)

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
  list(APPEND labsound_SOURCES cpputils.hpp defines.h)
  #dump(labsound_SOURCES)

  make_module(labsound cpp)
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include")
  set_target_properties(
    qjs-labsound PROPERTIES INCLUDE_DIRECTORIES
                            "${QUICKJS_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound/include")
endif(USE_LABSOUND)

add_executable(test-labsound test-labsound.cpp)

target_link_libraries(test-labsound PRIVATE ${labsound_LIBRARIES})
