# sunverify_procedure — the generic ProcedureEngine, IAlerter, OpcUaAlerter, narrator, XlsxReporter
# (spec Sections 6, 10, 18). Must never link cpp-httplib (I11; checked in the top-level CMakeLists).
# Phase 1: the target and its include path. Sources arrive in Phase 5.
add_library(sunverify_procedure INTERFACE)
target_include_directories(sunverify_procedure INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
