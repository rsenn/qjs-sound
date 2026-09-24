# Builds the vendored third_party/portmidi submodule as a static, -fPIC
# archive via add_subdirectory() (confirmed safe: portmidi's top-level
# CMakeLists.txt doesn't call enable_testing() unconditionally and gates
# its own test/doc/Java subdirectories behind options, all turned off
# below). Included only from FindPortMIDI.cmake when
# BUILD_PORTMIDI is ON - never included directly.

include("${CMAKE_CURRENT_LIST_DIR}/VendoredLibrary.cmake")

vendored_build_static_subdirectory(
  PORTMIDI
  SUBMODULE_DIR
  third_party/portmidi
  TARGETS
  # porttime.c/ptlinux.c are compiled straight into the `portmidi` CMake
  # target itself (confirmed by building this and inspecting
  # pm_common/CMakeFiles/portmidi.dir/) - there is no separate `porttime`
  # target to list here. An earlier version of this list did list one,
  # which silently linked against a stray *system* libporttime.a on a
  # machine that happened to have one installed instead of failing loud
  # - caught by testing BUILD_PORTMIDI=ON on a machine with
  # portmidi's old (separate-porttime-library) apt package still present.
  portmidi
  OPTIONS
  -DBUILD_PORTMIDI_TESTS=OFF
  -DBUILD_JAVA_NATIVE_INTERFACE=OFF
  -DBUILD_DOC=OFF
  EXTRA_LIBRARIES
  ${PORTMIDI_SYSTEM_DEPS})
