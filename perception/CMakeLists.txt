# sunverify_perception — SpscRingBuffer, frame pools, StateVector, detectors (spec Sections 9, 14).
add_library(sunverify_perception STATIC src/FramePool.cpp src/ReplayDetector.cpp src/Committer.cpp)
target_include_directories(sunverify_perception PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sunverify_perception PRIVATE sunverify_warnings PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

add_executable(perception_test tests/SpscRingBufferTest.cpp)
target_link_libraries(perception_test PRIVATE sunverify_warnings sunverify_perception GTest::gtest_main)
gtest_discover_tests(perception_test DISCOVERY_MODE PRE_TEST)
