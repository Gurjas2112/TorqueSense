# cad_extract — the CAD compiler: OCCT STEP reader, manifest join, pattern solvers, optics,
# coverage (spec Section 7). Phase 1: the target and its include path. Sources arrive in Phase 4.
add_library(cad_extract INTERFACE)
target_include_directories(cad_extract INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
