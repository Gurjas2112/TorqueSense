# Cross-component tests. Each component's own unit tests live beside it (perception/tests, …).

add_executable(contracts_test contracts/ContractsTest.cpp)
target_include_directories(contracts_test PRIVATE ${CMAKE_SOURCE_DIR}/control-app/include)
target_link_libraries(contracts_test PRIVATE sunverify_warnings nlohmann_json::nlohmann_json GTest::gtest_main)
gtest_discover_tests(contracts_test DISCOVERY_MODE PRE_TEST)

# I1: zero allocation in the steady-state hot path. Not built under ASan or TSan: both replace operator new
# itself, and the counting allocator's replacement conflicts with it. That exclusion is intended.
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize=(address|thread)")
  add_executable(no_allocation_test allocation/NoAllocationTest.cpp allocation/CountingAllocator.cpp)
  target_link_libraries(no_allocation_test PRIVATE sunverify_warnings torquesense_core
                        GTest::gtest_main)
  target_compile_definitions(no_allocation_test PRIVATE TS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  gtest_discover_tests(no_allocation_test DISCOVERY_MODE PRE_TEST)
endif()

# The I4 gate tool (spec Section 31). Run by hand or by Jenkins, with the soft PLC up; not a ctest.
add_executable(latency_test latency/latency_test.cpp)
target_link_libraries(latency_test PRIVATE sunverify_warnings torquesense_core)
