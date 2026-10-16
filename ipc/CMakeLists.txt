# sunverify_ipc — SharedClipRing, Win32 and POSIX backends (spec Section 30).
# Phase 1: the target and its include path. Sources arrive in Phase 6.
add_library(sunverify_ipc INTERFACE)
target_include_directories(sunverify_ipc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
