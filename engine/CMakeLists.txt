cmake_minimum_required(VERSION 3.21)

project(ShuoWenEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SHUOWEN_SOURCES
  src/config.cpp
  src/util/json.cpp
  src/util/log.cpp
  src/util/text.cpp
  src/store/settings.cpp
  src/store/history.cpp
  src/models/paths.cpp
  src/models/download.cpp
  src/audio/wasapi.cpp
  src/input/hotkey.cpp
  src/input/inserter.cpp
  src/input/cue.cpp
  src/asr/sherpa_api.cpp
  src/asr/sherpa_engine.cpp
  src/asr/dictation.cpp
  src/ipc/pipe_server.cpp
  src/ui/launcher.cpp
  src/ui/tray.cpp
  src/app.cpp
)

add_library(shuowen_core STATIC ${SHUOWEN_SOURCES})

target_include_directories(shuowen_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa
)

target_compile_definitions(shuowen_core PUBLIC
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(shuowen_core PUBLIC /utf-8 /permissive- /EHsc /W4)
endif()

target_link_libraries(shuowen_core PUBLIC
  ole32
  oleaut32
  uuid
  winhttp
  shell32
  advapi32
  user32
  gdi32
  comctl32
  shlwapi
  propsys
  winmm
)

add_executable(shuowen_engine WIN32 src/main.cpp)
target_link_libraries(shuowen_engine PRIVATE shuowen_core)

add_executable(shuowen_selftest src/selftest_main.cpp)
target_link_libraries(shuowen_selftest PRIVATE shuowen_core)

set_target_properties(shuowen_engine PROPERTIES
  OUTPUT_NAME "shuowen_engine"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_target_properties(shuowen_selftest PROPERTIES
  OUTPUT_NAME "shuowen_selftest"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
