# cad_extract — the CAD compiler: OCCT STEP reader, manifest join, pattern solvers, optics,
# coverage (spec Section 7). The library is shared by the cad-extract tool and the engine's
# conformance runner; neither runs on a pinned thread.
add_library(cad_extract STATIC src/Patterns.cpp src/Optics.cpp src/Compiler.cpp src/StepReader.cpp)
target_include_directories(cad_extract PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cad_extract PUBLIC nlohmann_json::nlohmann_json PRIVATE sunverify_warnings yaml-cpp::yaml-cpp picosha2)
if(OpenCASCADE_FOUND)
  target_compile_definitions(cad_extract PRIVATE SUNVERIFY_WITH_OCCT=1 _USE_MATH_DEFINES)   # OCCT uses M_PI
  target_include_directories(cad_extract SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(cad_extract PRIVATE TKernel TKMath TKBRep TKG3d TKGeomBase TKTopAlgo TKLCAF TKCAF
                        TKXCAF TKXSBase TKDESTEP TKDE)
endif()

add_executable(cad_extract_app src/main.cpp)
set_target_properties(cad_extract_app PROPERTIES OUTPUT_NAME cad-extract)
target_link_libraries(cad_extract_app PRIVATE sunverify_warnings cad_extract)

add_executable(cad_extract_test tests/PatternsTest.cpp tests/CompilerTest.cpp tests/StepReaderTest.cpp)
target_link_libraries(cad_extract_test PRIVATE sunverify_warnings cad_extract GTest::gtest_main)
target_compile_definitions(cad_extract_test PRIVATE
  TS_SOURCE_DIR="${CMAKE_SOURCE_DIR}" TS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
if(OpenCASCADE_FOUND)
  target_compile_definitions(cad_extract_test PRIVATE SUNVERIFY_WITH_OCCT=1 _USE_MATH_DEFINES)
  target_include_directories(cad_extract_test SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(cad_extract_test PRIVATE TKernel TKMath TKBRep TKG3d TKGeomBase TKTopAlgo TKPrim TKLCAF
                        TKCAF TKXCAF TKXSBase TKDESTEP TKDE)
endif()
gtest_discover_tests(cad_extract_test DISCOVERY_MODE PRE_TEST)

# Every sample's committed procedure.json and coverage.json match what the compiler produces now.
file(GLOB _profiles LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/samples/*)
foreach(_p IN LISTS _profiles)
  if(EXISTS ${_p}/profile.yaml AND EXISTS ${_p}/procedure.json)
    get_filename_component(_n ${_p} NAME)
    add_test(NAME CompiledProfileUpToDate.${_n} COMMAND cad_extract_app ${_p} --check)
  endif()
endforeach()
