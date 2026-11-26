cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
# Airwindows Consolidated is built against the static MSVC runtime; use it everywhere to avoid link mismatches.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AirDAW VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Airwindows Consolidated (MIT) provides every Airwindows effect behind one registry API.
# It is compiled straight into AirDAW: no VST wrapper, no per-plugin DLLs.
add_subdirectory(third_party/airwin2rack EXCLUDE_FROM_ALL)

add_library(airdaw_core STATIC
  core/src/fx_host.cpp
  core/src/engine.cpp
  core/src/wav_io.cpp
  core/src/disk_writer.cpp
  core/src/session.cpp
  core/src/work_pool.cpp)
find_package(Threads REQUIRED)
target_include_directories(airdaw_core PUBLIC core/include)
target_link_libraries(airdaw_core PUBLIC airwin-registry Threads::Threads)

option(AIRDAW_BUILD_TESTS "Build headless engine tests" ON)
if(AIRDAW_BUILD_TESTS)
  add_executable(engine_tests tests/engine_tests.cpp)
  target_link_libraries(engine_tests PRIVATE airdaw_core)
  # main() holds ~40 test engines (~34 KB each) in separate blocks; MSVC gives most their own stack
  # space, which overflows Windows' 1 MB default. Match the 8 MB Linux default.
  if(MSVC)
    target_link_options(engine_tests PRIVATE /STACK:8388608)
  endif()
endif()

# ---------------- GUI application (JUCE) ----------------
option(AIRDAW_BUILD_APP "Build the AirDAW application" ON)
set(AIRDAW_ASIO_SDK "" CACHE PATH "Path to the Steinberg ASIO SDK (GPLv3 option); enables ASIO on Windows")
if(AIRDAW_BUILD_APP)
  add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
  juce_add_gui_app(AirDAW PRODUCT_NAME "AirDAW" COMPANY_NAME "trashmannightcrawler")
  target_sources(AirDAW PRIVATE app/Main.cpp app/MainComponent.cpp app/MainComponent.h app/AudioBridge.h app/Platform.cpp app/Vst3Insert.h app/PluginWindow.h)
  target_include_directories(AirDAW PRIVATE app)
  target_compile_definitions(AirDAW PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_AU=0 JUCE_APPLICATION_NAME_STRING="AirDAW" JUCE_APPLICATION_VERSION_STRING="0.1.0")
  if(WIN32)
    target_compile_definitions(AirDAW PRIVATE JUCE_WASAPI=1 JUCE_DIRECTSOUND=0)
    if(AIRDAW_ASIO_SDK)
      target_compile_definitions(AirDAW PRIVATE JUCE_ASIO=1)
      target_include_directories(AirDAW PRIVATE "${AIRDAW_ASIO_SDK}/common")
    else()
      message(WARNING "AIRDAW_ASIO_SDK not set: building without ASIO (WASAPI only).")
    endif()
  elseif(UNIX AND NOT APPLE)
    find_package(ALSA QUIET)
    if(NOT ALSA_FOUND)
      message(WARNING "ALSA headers not found: building without ALSA/JACK (compile check only).")
      target_compile_definitions(AirDAW PRIVATE JUCE_ALSA=0 JUCE_JACK=0)
    endif()
  endif()
  target_link_libraries(AirDAW PRIVATE airdaw_core
    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils
    juce::juce_gui_basics juce::juce_events juce::juce_core
    juce::juce_recommended_config_flags)
endif()
