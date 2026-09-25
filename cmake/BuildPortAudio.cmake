# Builds the vendored third_party/portaudio submodule as a static, -fPIC
# archive via add_subdirectory() (confirmed safe: only calls subdirs()
# for its own tests/examples, both gated behind PA_BUILD_TESTS/
# PA_BUILD_EXAMPLES, off by default and turned off explicitly below
# anyway). Included only from FindPortAudio.cmake when
# BUILD_PORTAUDIO is ON - never included directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

# A static libportaudio.a still calls directly into ALSA/JACK/pthread at
# runtime (its host-API backends, selected by cmake_dependent_option()
# on ALSA_FOUND/JACK_FOUND inside portaudio's own CMakeLists.txt); those
# symbols aren't embedded in the archive, so link them into whichever
# target consumes PORTAUDIO_LIBRARIES explicitly - same defensive
# approach this project already takes for the statically-linked STK build
# (see CMakeLists.txt's stk_LIBRARIES list).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(ALSA REQUIRED)
  set(PORTAUDIO_SYSTEM_DEPS ${ALSA_LIBRARIES} pthread m)
  find_library(JACK_LIBRARY jack)
  if(JACK_LIBRARY)
    list(APPEND PORTAUDIO_SYSTEM_DEPS "${JACK_LIBRARY}")
  endif()
else()
  set(PORTAUDIO_SYSTEM_DEPS "")
endif()

vendored_build_static_subdirectory(
  PORTAUDIO SUBMODULE_DIR third_party/portaudio TARGETS portaudio OPTIONS
  -DPA_BUILD_SHARED_LIBS=OFF -DPA_BUILD_TESTS=OFF -DPA_BUILD_EXAMPLES=OFF
  EXTRA_LIBRARIES ${PORTAUDIO_SYSTEM_DEPS})
