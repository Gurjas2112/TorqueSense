# sunverify_procedure — the generic ProcedureEngine, IAlerter, OpcUaAlerter, narrator, XlsxReporter
# (spec Sections 6, 10, 18). Must never link cpp-httplib (I11; checked in the top-level CMakeLists).
add_library(sunverify_procedure STATIC src/Profile.cpp src/ProcedureEngine.cpp src/Describe.cpp src/OpcUaAlerter.cpp)
target_include_directories(sunverify_procedure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sunverify_procedure PUBLIC nlohmann_json::nlohmann_json PRIVATE sunverify_warnings open62541)

# The Excel shift report, kept out of sunverify_procedure so the interpreter's library stays small.
add_library(sunverify_report STATIC src/XlsxReporter.cpp)
target_include_directories(sunverify_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sunverify_report PRIVATE sunverify_warnings)
if(TARGET OpenXLSX::OpenXLSX)
  target_compile_definitions(sunverify_report PRIVATE SUNVERIFY_WITH_XLSX=1)
  target_link_libraries(sunverify_report PRIVATE OpenXLSX::OpenXLSX)
endif()

add_executable(procedure_test tests/ProcedureEngineTest.cpp tests/HeartbeatTest.cpp)
target_link_libraries(procedure_test PRIVATE sunverify_warnings sunverify_procedure GTest::gtest_main)
target_compile_definitions(procedure_test PRIVATE TS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(procedure_test DISCOVERY_MODE PRE_TEST TEST_PREFIX "Procedure.")
