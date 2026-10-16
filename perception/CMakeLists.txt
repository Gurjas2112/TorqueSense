# sunverify_perception — SpscRingBuffer, frame pools, StateVector, detectors (spec Sections 9, 14).
# Phase 1: the target and its include path. Sources arrive in Phases 4 and 6.
add_library(sunverify_perception INTERFACE)
target_include_directories(sunverify_perception INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
