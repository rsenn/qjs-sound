# check-cflag-result-cached-under-fixed-name: every check_cflag() probe
# after the first returned the first probe's answer.
cmake_minimum_required(VERSION 3.16)
project(check-cflag-result-cached C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/Functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/Checks.cmake)

check_cflag("-O0" HAVE_O0 TEST_FLAGS)
check_cflag("-fno-such-flag-qjs-sound" HAVE_BOGUS TEST_FLAGS)

if(NOT HAVE_O0)
  message(FATAL_ERROR "FAIL: -O0 should be supported")
endif()
if(HAVE_BOGUS)
  message(FATAL_ERROR "FAIL: bogus flag reported supported (cached first result)")
endif()
message(STATUS "PASS: probes are independent")
