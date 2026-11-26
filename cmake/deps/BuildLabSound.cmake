# Builds the vendored third_party/LabSound submodule with
# ExternalProject_Add, after applying patches/labsound-*.patch to it
# in place (cmake/ApplyPatches.cmake skips those already applied).
# Run by the BUILD command of the labsound dependency (SoundLibs.cmake).
#
# Sets LABSOUND_INCLUDE_DIRS, LABSOUND_LIBRARIES, LABSOUND_TARGETS.

include(ExternalProject)

set(LABSOUND_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/LabSound")
set(LABSOUND_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/LabSound")

# applied in order, in place; each one is skipped when already in the tree
file(GLOB LABSOUND_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/patches/labsound-*.patch")
list(SORT LABSOUND_PATCHES)

vendored_reset_stale_external(LabSound "${LABSOUND_BINARY_DIR}"
  "${CMAKE_C_COMPILER}|${CMAKE_CXX_COMPILER}|${CMAKE_C_FLAGS}|${CMAKE_CXX_FLAGS}|${MODULE_COMPILE_FLAGS}")

# a cross build: the nested project needs the same toolchain file (target, sysroot, find roots)
set(LABSOUND_TOOLCHAIN_ARGS)
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND LABSOUND_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()

ExternalProject_Add(
  LabSound
  SOURCE_DIR "${LABSOUND_SOURCE_DIR}"
  BINARY_DIR "${LABSOUND_BINARY_DIR}"
  PREFIX LabSound
  PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${LABSOUND_SOURCE_DIR}
    "-DPATCHES=${LABSOUND_PATCHES}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatches.cmake
  CMAKE_ARGS
    "-DBUILD_SHARED_LIBS:BOOL=OFF"
    "-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
    "-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF"
    "-DCMAKE_INSTALL_PREFIX:PATH=${LABSOUND_BINARY_DIR}/install"
    "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
    ${LABSOUND_TOOLCHAIN_ARGS}
    "-DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} ${MODULE_COMPILE_FLAGS}"
    "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${MODULE_COMPILE_FLAGS}"
  INSTALL_COMMAND "")

foreach(_lib LabSound LabSoundRtAudio)
  add_library(lib${_lib} STATIC IMPORTED GLOBAL)
  set_target_properties(lib${_lib} PROPERTIES
    IMPORTED_LOCATION "${LABSOUND_BINARY_DIR}/bin/lib${_lib}.a"
    IMPORTED_LOCATION_DEBUG "${LABSOUND_BINARY_DIR}/bin/lib${_lib}.a"
    IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
endforeach(_lib)

link_directories("${LABSOUND_BINARY_DIR}/bin" "${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib")

set(LABSOUND_INCLUDE_DIRS "${LABSOUND_SOURCE_DIR}/include"
                          "${LABSOUND_SOURCE_DIR}/third_party/libnyquist/include")
set(LABSOUND_LIBRARIES libLabSound libLabSoundRtAudio
    "${LABSOUND_BINARY_DIR}/third_party/libnyquist/lib/liblibnyquist.a"
    pulse pulse-simple samplerate pthread)
set(LABSOUND_TARGETS LabSound)
