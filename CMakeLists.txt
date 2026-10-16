cmake_minimum_required(VERSION 3.25)
# TorqueSense — the C++ engine, the CAD compiler and the soft PLC (spec Sections 14, 22, 28).
# Presets: CMakePresets.json. Windows builds use MSYS2 UCRT64 GCC 16.2; the linux-* presets carry
# the sanitizers. Nothing here uses vcpkg or MSVC-only features.
project(torquesense VERSION 5.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(SUNVERIFY_WITH_CUDA "Build the ONNX Runtime adapter (gpu_cuda13 package at ONNXRUNTIME_ROOT)" OFF)
option(SUNVERIFY_HEADLESS  "No OpenCV, OCCT or Excel: the Linux sanitizer and container builds" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime gpu_cuda13 package root (C:/dev/tools/onnxruntime)")
set(TORQUESENSE_VERSION "${PROJECT_VERSION}-dev" CACHE STRING "Version baked into torquesense --version")

if(SUNVERIFY_WITH_CUDA)
  if(NOT ONNXRUNTIME_ROOT OR NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
      "SUNVERIFY_WITH_CUDA=ON but no ONNX Runtime at ONNXRUNTIME_ROOT='${ONNXRUNTIME_ROOT}'.\n"
      "Install onnxruntime-win-x64-gpu_cuda13-1.28.0.zip to C:/dev/tools/onnxruntime and set the user "
      "variable ORT_ROOT (spec Section 28 step 3, Section 29) — never the CUDA 12 '-gpu-' asset. "
      "Until then, build with the windows-ci preset (SUNVERIFY_WITH_CUDA=OFF).")
  endif()
endif()

# I5: clean under -Wall -Wextra -Wpedantic -Werror. Applied to our targets only, never to vendored
# or fetched code.
add_library(sunverify_warnings INTERFACE)
target_compile_options(sunverify_warnings INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Werror -Wno-interference-size>
  $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wpedantic -Werror>)

find_package(Threads REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)             # load time only — docs/adr/0011
find_package(GTest REQUIRED)
if(NOT SUNVERIFY_HEADLESS)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)
  find_package(OpenCASCADE CONFIG)          # the STEP reader; optional so a machine without OCCT still builds
endif()

include(cmake/deps.cmake)

enable_testing()

add_subdirectory(perception)
add_subdirectory(procedure)
add_subdirectory(ipc)
add_subdirectory(cad-extract)
add_subdirectory(control-app)
add_subdirectory(softplc)
add_subdirectory(tests)

# I11, enforced structurally: the verify thread's library must never link the HTTP client, so a
# direct call from the hot path to the sidecar fails to link.
get_target_property(_proc_libs sunverify_procedure LINK_LIBRARIES)
if(_proc_libs)
  foreach(_lib IN LISTS _proc_libs)
    if(_lib MATCHES "httplib")
      message(FATAL_ERROR "I11: sunverify_procedure links ${_lib}; Florence-2 must stay off the alert path")
    endif()
  endforeach()
endif()
