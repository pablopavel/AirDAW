# Standalone build of the Airwindows library only (no JUCE): what the GitHub release workflow runs.
#   cmake -S fxlib -B build-fxlib -DAIRWIN2RACK_DIR=<airwin2rack checkout> && cmake --build build-fxlib --config Release
cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AirwindowsFX LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(AIRWIN2RACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/airwin2rack" CACHE PATH "airwin2rack checkout")
add_subdirectory(${AIRWIN2RACK_DIR} airwin2rack EXCLUDE_FROM_ALL)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AirwindowsFxLib.cmake)
if(NOT AIRDAW_FXLIB_BUILD)
  airdaw_airwin_build_id(${AIRWIN2RACK_DIR} AIRDAW_FXLIB_BUILD)
endif()
airdaw_add_fxlib(${CMAKE_CURRENT_SOURCE_DIR}/.. "${AIRDAW_FXLIB_BUILD}")
