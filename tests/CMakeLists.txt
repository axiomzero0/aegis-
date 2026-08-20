# tests/CMakeLists.txt — Test suites.

add_executable(aegis_unit_tests
    unit/test_core_containers.cpp
    unit/test_ir_graph.cpp
    unit/test_passes.cpp
    unit/test_frontend_smoke.cpp
)
target_link_libraries(aegis_unit_tests PRIVATE aegis_core aegis_compile_options)
add_test(NAME aegis_unit_tests COMMAND aegis_unit_tests)

# Golden IR tests (Rule 37).
add_test(NAME golden_hash_cons
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/hash_cons.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/hash_cons.expected.son)
add_test(NAME golden_gvn
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/gvn.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/gvn.expected.son)
add_test(NAME golden_edce
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/edce.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/edce.expected.son)
add_test(NAME golden_sccp
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_golden.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/sccp.in.aegis
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/sccp.expected.son)
