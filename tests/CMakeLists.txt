# Cross-component tests. Each component's own unit tests live beside it (perception/tests, …).

add_executable(contracts_test contracts/ContractsTest.cpp)
target_include_directories(contracts_test PRIVATE ${CMAKE_SOURCE_DIR}/control-app/include)
target_link_libraries(contracts_test PRIVATE sunverify_warnings nlohmann_json::nlohmann_json GTest::gtest_main)
gtest_discover_tests(contracts_test DISCOVERY_MODE PRE_TEST)
