# tests/CMakeLists.txt — golden + unit tests.
add_executable(aegis_tests
    test_core_containers.cpp
    test_ir_graph.cpp
    test_passes.cpp
    test_frontend_smoke.cpp
)
target_link_libraries(aegis_tests PRIVATE aegis_core aegis_compile_options)
add_test(NAME aegis_tests COMMAND aegis_tests)

# Golden IR tests: small .aegis programs + expected IR dumps.
add_test(NAME golden_hash_cons
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/hash_cons.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/hash_cons.expected.son)
add_test(NAME golden_gvn
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/gvn.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/gvn.expected.son)
add_test(NAME golden_edce
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/edce.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/edce.expected.son)
add_test(NAME golden_sccp
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/sccp.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/sccp.expected.son)
