# Builds the vendored third_party/stk submodule with ExternalProject_Add
# as a static, -fPIC libstk.a.
# Run by the BUILD command of the stk dependency (SoundLibs.cmake).
#
# Sets STK_INCLUDE_DIRS, STK_LIBRARIES, STK_TARGETS.

include(ExternalProject)

set(STK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stk")
set(STK_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/stk")

ExternalProject_Add(
  stk
  SOURCE_DIR "${STK_SOURCE_DIR}"
  BINARY_DIR "${STK_BINARY_DIR}"
  PREFIX stk
  CMAKE_ARGS
    "-DBUILD_SHARED_LIBS:BOOL=OFF"
    "-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
    "-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF"
    "-DCMAKE_INSTALL_PREFIX:PATH=${STK_BINARY_DIR}/install"
    "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
    "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} ${MODULE_COMPILE_FLAGS}"
    "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
    "-DCOMPILE_PROJECTS:BOOL=OFF"
  INSTALL_COMMAND "")

add_library(libstk STATIC IMPORTED)
set_target_properties(libstk PROPERTIES IMPORTED_LOCATION "${STK_BINARY_DIR}/libstk.a")
link_directories("${STK_BINARY_DIR}")

set(STK_INCLUDE_DIRS "${STK_SOURCE_DIR}/include" "${STK_SOURCE_DIR}/src/include")
set(STK_LIBRARIES libstk)
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
  # RtAudio/RtMidi (ALSA + JACK backends) and Thread/Mutex are compiled
  # into libstk.a, so their runtime libraries go with it
  list(APPEND STK_LIBRARIES pthread asound jack)
endif()
set(STK_TARGETS stk)
