# Aegis Compiler — src/CMakeLists
# Builds the static library `aegis_core` and the `aegis` driver executable.

add_library(aegis_core STATIC
    common/Diagnostics.cpp
    core/SymbolTable.cpp
    core/SmallVector.cpp
    core/BitVector.cpp
    core/SparseSet.cpp
    core/SwissTable.cpp
    ir/Graph.cpp
    ir/Verifier.cpp
    ir/Printer.cpp
    ir/HashConsing.cpp
    frontend/Lexer.cpp
    frontend/Parser.cpp
    frontend/ASTPrinter.cpp
    frontend/Lowerer.cpp
    frontend/EffectInference.cpp
    frontend/TypeChecker.cpp
    passes/PassManager.cpp
    passes/Passes_Standard.cpp
    passes/GVN.cpp
    passes/EDCE.cpp
    passes/SCCP.cpp
    passes/SimplifyControl.cpp
    backend/LinearScan.cpp
    backend/InstrSelection.cpp
)
target_link_libraries(aegis_core PUBLIC aegis_compile_options PRIVATE aegis_hot_path_options)
target_include_directories(aegis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(aegis driver/main.cpp)
target_link_libraries(aegis PRIVATE aegis_core)
set_target_properties(aegis PROPERTIES OUTPUT_NAME "aegis")
