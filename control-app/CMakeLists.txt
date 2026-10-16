# torquesense — the station engine executable (spec Sections 14, 22, 30).
# torquesense_core holds everything but main(), so the scenario, allocation and latency tests drive
# the same code the station runs.
add_library(torquesense_core STATIC src/Conformance.cpp src/VerifyCore.cpp src/Config.cpp src/Affinity.cpp
            src/Pipeline.cpp src/IoSide.cpp src/LineSimulator.cpp src/sources/FileSource.cpp)
target_include_directories(torquesense_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(torquesense_core PUBLIC sunverify_perception sunverify_procedure sunverify_report sunverify_ipc
                      cad_extract yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json Threads::Threads
                      PRIVATE sunverify_warnings open62541 ixwebsocket httplib::httplib hdr_histogram_static)
if(OpenCV_FOUND)
  target_compile_definitions(torquesense_core PRIVATE SUNVERIFY_WITH_OPENCV=1)
  target_link_libraries(torquesense_core PRIVATE ${OpenCV_LIBS})
endif()

add_executable(torquesense src/main.cpp)
target_compile_definitions(torquesense PRIVATE TORQUESENSE_VERSION="${TORQUESENSE_VERSION}")
target_link_libraries(torquesense PRIVATE sunverify_warnings torquesense_core spdlog::spdlog)
if(SUNVERIFY_WITH_CUDA)
  target_compile_definitions(torquesense PRIVATE SUNVERIFY_WITH_CUDA=1)
  target_link_libraries(torquesense PRIVATE onnxruntime)
endif()

add_executable(scenario_test tests/ScenarioTest.cpp)
target_link_libraries(scenario_test PRIVATE sunverify_warnings torquesense_core GTest::gtest_main)
target_compile_definitions(scenario_test PRIVATE TS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(scenario_test DISCOVERY_MODE PRE_TEST)

# I16: the C++ engine agrees with the trace expectations on every sample…
add_test(NAME Differential.conformance COMMAND torquesense --conformance ${CMAKE_SOURCE_DIR}/samples --template)
# …and with the reference interpreter on every violation, field for field (tests/differential).
find_program(TS_PYTHON NAMES python python3 HINTS ${CMAKE_SOURCE_DIR}/.venv/Scripts ${CMAKE_SOURCE_DIR}/.venv/bin NO_DEFAULT_PATH)
if(NOT TS_PYTHON)
  find_package(Python3 COMPONENTS Interpreter)
  set(TS_PYTHON ${Python3_EXECUTABLE})
endif()
if(TS_PYTHON)
  add_test(NAME Differential.reference
           COMMAND ${TS_PYTHON} ${CMAKE_SOURCE_DIR}/tests/differential/differential.py --engine $<TARGET_FILE:torquesense>)
endif()
