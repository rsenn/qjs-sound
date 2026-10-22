# Downloads FFTW3 into third_party/fftw3 and builds the float library
# (libfftw3f.a, -fPIC) with ExternalProject_Add. Included only from
# FindFFTW3.cmake - never directly.
#
# The archive is installed into <build>/third_party/fftw3-install, which is
# what FFTW3_INCLUDE_DIRS / FFTW3_LIBRARIES point at. Anything linking
# FFTW3_LIBRARIES must add_dependencies() on `fftw3_external` so the
# library exists before it builds (the path is a plain file, not a target).

include(ExternalProject)

set(FFTW3_VERSION 3.3.10)
set(FFTW3_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fftw3")
set(FFTW3_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/fftw3-install")

# FFTW's CMakeLists.txt declares an old cmake_minimum_required(); newer
# CMake refuses that without the policy floor below.
ExternalProject_Add(
  fftw3_external
  URL "https://www.fftw.org/fftw-${FFTW3_VERSION}.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
  DOWNLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
  SOURCE_DIR "${FFTW3_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/fftw3"
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${FFTW3_INSTALL_DIR}
             -DCMAKE_INSTALL_LIBDIR=lib
             -DCMAKE_BUILD_TYPE=Release
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON
             -DCMAKE_POLICY_VERSION_MINIMUM=3.5
             -DBUILD_SHARED_LIBS=OFF
             -DBUILD_TESTS=OFF
             -DENABLE_FLOAT=ON
             -DENABLE_THREADS=OFF
  BUILD_BYPRODUCTS "${FFTW3_INSTALL_DIR}/lib/libfftw3f.a")

set(FFTW3_INCLUDE_DIRS "${FFTW3_INSTALL_DIR}/include")
set(FFTW3_LIBRARY_DIRS "${FFTW3_INSTALL_DIR}/lib")
set(FFTW3_LIBRARIES "${FFTW3_INSTALL_DIR}/lib/libfftw3f.a" m)
set(FFTW3_FOUND TRUE)
set(FFTW3_BUILT_IN_TREE TRUE)
