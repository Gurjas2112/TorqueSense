# torquesense — the station engine executable (spec Sections 14, 22, 30).
add_executable(torquesense src/main.cpp)
target_include_directories(torquesense PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(torquesense PRIVATE TORQUESENSE_VERSION="${TORQUESENSE_VERSION}")
target_link_libraries(torquesense PRIVATE sunverify_warnings sunverify_perception sunverify_procedure
                      sunverify_ipc cad_extract nlohmann_json::nlohmann_json spdlog::spdlog Threads::Threads)
if(SUNVERIFY_WITH_CUDA)
  target_compile_definitions(torquesense PRIVATE SUNVERIFY_WITH_CUDA=1)
  target_link_libraries(torquesense PRIVATE onnxruntime)
endif()
