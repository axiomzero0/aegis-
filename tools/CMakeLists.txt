# tools/CMakeLists.txt — User-facing executables.

add_executable(aegisc aegisc/main.cpp)
target_link_libraries(aegisc PRIVATE aegis_core)
set_target_properties(aegisc PROPERTIES OUTPUT_NAME "aegisc")

add_executable(aegis_lsp lsp/main.cpp)
target_link_libraries(aegis_lsp PRIVATE aegis_core)
set_target_properties(aegis_lsp PROPERTIES OUTPUT_NAME "aegis-lsp")

add_executable(aegis_repl repl/main.cpp)
target_link_libraries(aegis_repl PRIVATE aegis_core aegis_jit)
set_target_properties(aegis_repl PROPERTIES OUTPUT_NAME "aegis-repl")

add_executable(aegis_fmt fmt/main.cpp)
target_link_libraries(aegis_fmt PRIVATE aegis_core)
set_target_properties(aegis_fmt PROPERTIES OUTPUT_NAME "aegis-fmt")
