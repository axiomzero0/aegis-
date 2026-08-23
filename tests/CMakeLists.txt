# tests/CMakeLists.txt — Test suites.
#
# Registration mirrors scripts/build_tests.sh exactly (the dual-build
# contract: both build systems must stay in sync).

add_executable(aegis_unit_tests
    unit/test_core_containers.cpp
    unit/test_ir_graph.cpp
    unit/test_passes.cpp
    unit/test_frontend_smoke.cpp
)
add_executable(aegis_for_loop_tests unit/test_for_loops.cpp)
target_link_libraries(aegis_for_loop_tests PRIVATE aegis_core aegis_pgo
                                                   aegis_compile_options)
add_test(NAME aegis_for_loop_tests COMMAND aegis_for_loop_tests)

# Rule 36 regression suite for the executable-codegen path.
add_executable(aegis_exec_codegen_tests unit/test_exec_codegen.cpp)
target_link_libraries(aegis_exec_codegen_tests PRIVATE aegis_core aegis_pgo
                                                       aegis_jit
                                                       aegis_compile_options)
add_test(NAME aegis_exec_codegen_tests COMMAND aegis_exec_codegen_tests)
target_link_libraries(aegis_unit_tests PRIVATE aegis_core aegis_compile_options)
add_test(NAME aegis_unit_tests COMMAND aegis_unit_tests)

# ---- Rule 36 regression suite (tests/regression/) ----
add_executable(aegis_regression_tests regression/test_regression.cpp)
target_link_libraries(aegis_regression_tests PRIVATE aegis_core aegis_pgo
                                                   aegis_compile_options)
add_test(NAME aegis_regression_tests COMMAND aegis_regression_tests)

# ---- Rule 41 perf suite (tests/perf/) ----
add_executable(aegis_perf_benchmarks perf/bench_pipeline.cpp)
target_link_libraries(aegis_perf_benchmarks PRIVATE aegis_core aegis_pgo
                                                    aegis_compile_options)
add_test(NAME aegis_perf_benchmarks COMMAND aegis_perf_benchmarks)
set_tests_properties(aegis_perf_benchmarks PROPERTIES LABELS "perf")

# Rule 41 (runtime half) + Rule 38 (runtime differential): executes
# generated x86-64 through the JIT MemManager and checks it against
# the AST interpreter.
add_executable(aegis_runtime_benchmarks perf/bench_runtime.cpp)
target_link_libraries(aegis_runtime_benchmarks PRIVATE aegis_core aegis_pgo
                                                      aegis_jit
                                                      aegis_compile_options)
add_test(NAME aegis_runtime_benchmarks COMMAND aegis_runtime_benchmarks)
set_tests_properties(aegis_runtime_benchmarks PROPERTIES LABELS "perf")

# ---- Rule 37 golden suite (tests/integration/golden/) ----
# Discovers every <pass>/<name>.in.aegis pair and registers a
# two-mode (AOT + JIT) test for it. Research pass directories run
# with --research (the unified pipeline opt-in, Rule A.1).
file(GLOB GOLDEN_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/integration/golden/*/")
set(RESEARCH_PASSES
    cfl_alias value_flow pgdlo mem_pool_synthesis cache_oblivious_layout
    slp_vectorization auto_parallelization guarded_devirtualization
    speculative_bce speculative_effect_reordering speculative_lock_elision
    bolt_layout)
foreach(dir ${GOLDEN_DIRS})
    get_filename_component(pass_name "${dir}" NAME)
    file(GLOB inputs "${dir}*.in.aegis")
    set(extra_args "")
    foreach(rp ${RESEARCH_PASSES})
        if(pass_name STREQUAL rp)
            set(extra_args "--research")
        endif()
    endforeach()
    foreach(input ${inputs})
        string(REGEX REPLACE "\\.in\\.aegis$" "" base "${input}")
        get_filename_component(test_name "${input}" NAME_WE)
        add_test(NAME "golden_${pass_name}_${test_name}"
                 COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_golden.sh
                         "${input}" "${base}" ${extra_args})
    endforeach()
endforeach()

# ---- Rule 38 differential testing (tests/integration/) ----
# Reference interpreter <-> AOT <-> JIT agreement over a seeded,
# reproducible corpus. Registered as a single ctest entry.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME differential_rule38
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_differential.py)
else()
    message(WARNING "Python3 not found: differential_rule38 test not registered")
endif()
