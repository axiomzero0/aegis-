# Aegis compiler toolchain — compiler/CMakeLists.txt
# Builds:
#   * aegis_core (static library) — the reusable compiler library
#     (support + ir + frontend + passes + backend).
#   * aegis_jit (static library) — the JIT engine + deopt + mem manager.
#   * aegis_pgo (static library) — the PGO infrastructure.

# Aggregate the include directories from all sub-modules so consumers
# (tools/aegisc, tests) can use `#include "aegis/ir/Graph.hpp"` etc.
add_library(aegis_core STATIC
    # support
    support/src/Diagnostics.cpp
    support/src/StringIntern.cpp
    support/src/SmallVector.cpp
    support/src/SparseSet.cpp
    support/src/BitVector.cpp
    support/src/SwissTable.cpp
    # ir
    ir/src/Graph.cpp
    ir/src/HashConsing.cpp
    ir/src/Verifier.cpp
    ir/src/Printer.cpp
    # frontend
    frontend/src/Lexer.cpp
    frontend/src/Parser.cpp
    frontend/src/ASTPrinter.cpp
    frontend/src/Lowering.cpp
    frontend/src/EffectInference.cpp
    frontend/src/TypeChecker.cpp
    # passes
    passes/src/PassManager.cpp
    passes/src/mid/StandardPipeline.cpp
    passes/src/mid/GVN.cpp
    passes/src/mid/EDCE.cpp
    passes/src/mid/SCCP.cpp
    passes/src/mid/SimplifyControl.cpp
    # backend
    backend/src/RegAlloc/LinearScan.cpp
    backend/src/x86/InstrSel_x86.cpp
)
target_link_libraries(aegis_core PUBLIC aegis_compile_options
                              PRIVATE aegis_hot_path_options)
target_include_directories(aegis_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/include
    ${CMAKE_CURRENT_SOURCE_DIR}/frontend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/passes/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/jit/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pgo/include
)

# JIT engine + deopt + mem manager (compiled in JIT mode only).
add_library(aegis_jit STATIC
    jit/src/JitEngine.cpp
    jit/src/Deopt.cpp
    jit/src/MemManager.cpp
)
target_link_libraries(aegis_jit PUBLIC aegis_core)

# PGO infrastructure.
add_library(aegis_pgo STATIC
    pgo/src/Profiler.cpp
    pgo/src/ProfileData.cpp
)
target_link_libraries(aegis_pgo PUBLIC aegis_core)
