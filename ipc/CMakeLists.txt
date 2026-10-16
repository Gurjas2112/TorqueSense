# sunverify_ipc — SharedClipRing, Win32 and POSIX backends (spec Section 30).
add_library(sunverify_ipc STATIC src/SharedClipRing.cpp)
target_include_directories(sunverify_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sunverify_ipc PRIVATE sunverify_warnings)
if(UNIX AND NOT APPLE)
  target_link_libraries(sunverify_ipc PUBLIC rt)
endif()

add_executable(ipc_test tests/SharedClipRingTest.cpp)
target_link_libraries(ipc_test PRIVATE sunverify_warnings sunverify_ipc GTest::gtest_main Threads::Threads)
gtest_discover_tests(ipc_test DISCOVERY_MODE PRE_TEST)
