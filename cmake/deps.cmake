# Third-party code that MSYS2 does not package, pinned by tag, identical on every preset.
# Everything else comes from the platform's package manager: pacman (MSYS2 UCRT64) on Windows,
# apt on Ubuntu. Nothing here needs vcpkg.
include(FetchContent)
# CMake 4 (MSYS2 ships 4.4) refuses projects that declare cmake_minimum_required below 3.5, and
# miniz 3.0.2 — which OpenXLSX v0.5.1 fetches — still does. Scoped to this file's fetched sources.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(USE_TLS OFF CACHE BOOL "" FORCE)                        # IXWebSocket: plain ws:// on localhost,
set(USE_ZLIB OFF CACHE BOOL "" FORCE)                         # no compression needed
set(HDR_LOG_REQUIRED OFF CACHE BOOL "" FORCE)                 # histograms in memory; no zlib log format
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(OPENXLSX_CREATE_DOCS OFF CACHE BOOL "" FORCE)
# OpenXLSX probes the system for pugixml first; MSYS2's pugixml 1.16 config defines `pugixml` but not
# the `pugixml::static` OpenXLSX asks for, so it then fetches 1.15 and the two target names collide.
# Fetch all of OpenXLSX's own dependencies instead: self-contained, and the same on every machine.
set(FORCE_FETCH_ALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(ixwebsocket   GIT_REPOSITORY https://github.com/machinezone/IXWebSocket   GIT_TAG v12.1.0 GIT_SHALLOW TRUE)
FetchContent_Declare(httplib       GIT_REPOSITORY https://github.com/yhirose/cpp-httplib       GIT_TAG v0.61.0 GIT_SHALLOW TRUE)
FetchContent_Declare(hdr_histogram GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c GIT_TAG 0.12.0 GIT_SHALLOW TRUE)
FetchContent_Declare(picosha2      GIT_REPOSITORY https://github.com/okdshin/PicoSHA2          GIT_TAG v1.0.1  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ixwebsocket httplib hdr_histogram picosha2)   # picosha2 defines its own target

if(NOT SUNVERIFY_HEADLESS)                                    # the Excel shift report: Windows build only
  FetchContent_Declare(openxlsx GIT_REPOSITORY https://github.com/troldal/OpenXLSX GIT_TAG v0.5.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(openxlsx)
endif()

# open62541 is vendored as its single-file amalgamation (third_party/open62541/), generated from
# release 1.4.21 for the target: -DUA_ENABLE_AMALGAMATION=ON -DUA_ARCHITECTURE=win32 (or posix).
# The soft PLC's heartbeat watchdog was tested against exactly this, built with MinGW.
if(WIN32)
  set(UA_ARCH win32)
else()
  set(UA_ARCH posix)
endif()
add_library(open62541 STATIC ${CMAKE_SOURCE_DIR}/third_party/open62541/${UA_ARCH}/open62541.c)
target_include_directories(open62541 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/open62541/${UA_ARCH})
if(WIN32)
  target_link_libraries(open62541 PUBLIC ws2_32 iphlpapi)
endif()

# ONNX Runtime: the official MSVC-built gpu_cuda13 package, linked from GCC through its import
# library. The C API is a plain C ABI, so this is safe; tested with MinGW-w64 GCC and ORT 1.28.0.
if(SUNVERIFY_WITH_CUDA)
  add_library(onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime PROPERTIES
    IMPORTED_IMPLIB   "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
endif()
