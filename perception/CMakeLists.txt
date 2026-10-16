# sunverify_perception — SpscRingBuffer, frame pools, StateVector, detectors (spec Sections 9, 14).
add_library(sunverify_perception STATIC src/FramePool.cpp src/ReplayDetector.cpp src/Committer.cpp src/SchematicDetector.cpp
            src/DetectorMath.cpp)
target_include_directories(sunverify_perception PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sunverify_perception PRIVATE sunverify_warnings PUBLIC Threads::Threads nlohmann_json::nlohmann_json)
# The GPU detector: ONNX Runtime's CUDA and TensorRT providers (spec Section 30). Without ORT the type
# still exists and create() refuses with the reason, so the engine links in every build.
if(SUNVERIFY_WITH_CUDA)
  target_sources(sunverify_perception PRIVATE src/OnnxDetector.cpp)
  target_link_libraries(sunverify_perception PUBLIC onnxruntime)
  # Windows ships an older onnxruntime.dll in System32, and the loader prefers System32 to PATH: put
  # ours beside the executables that load it.
  add_custom_target(onnxruntime_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/control-app ${CMAKE_BINARY_DIR}/tests ${CMAKE_BINARY_DIR}/perception
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll
            ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_cuda.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_tensorrt.dll ${CMAKE_BINARY_DIR}/control-app
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll
            ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_cuda.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_tensorrt.dll ${CMAKE_BINARY_DIR}/tests
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll
            ${CMAKE_BINARY_DIR}/perception)
else()
  target_sources(sunverify_perception PRIVATE src/OnnxDetectorStub.cpp)
endif()

add_executable(perception_test tests/SpscRingBufferTest.cpp tests/DetectorMathTest.cpp)
target_link_libraries(perception_test PRIVATE sunverify_warnings sunverify_perception GTest::gtest_main)
gtest_discover_tests(perception_test DISCOVERY_MODE PRE_TEST)
