# torquesense — the station engine executable (spec Sections 14, 22, 30).
add_executable(torquesense src/main.cpp src/Conformance.cpp)
target_include_directories(torquesense PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(torquesense PRIVATE TORQUESENSE_VERSION="${TORQUESENSE_VERSION}")
target_link_libraries(torquesense PRIVATE sunverify_warnings sunverify_perception sunverify_procedure
                      sunverify_ipc cad_extract yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json spdlog::spdlog
                      Threads::Threads)
if(SUNVERIFY_WITH_CUDA)
  target_compile_definitions(torquesense PRIVATE SUNVERIFY_WITH_CUDA=1)
  target_link_libraries(torquesense PRIVATE onnxruntime)
endif()

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
