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
    ir/src/Types.cpp
    # frontend
    frontend/src/Lexer.cpp
    frontend/src/Parser.cpp
    frontend/src/ASTPrinter.cpp
    frontend/src/Lowering.cpp
    frontend/src/EffectInference.cpp
    frontend/src/TypeChecker.cpp
    # passes (mid — Rules A.1/B.5: the standard pipeline)
    passes/src/PassManager.cpp
    passes/src/mid/StandardPipeline.cpp
    passes/src/mid/GVN.cpp
    passes/src/mid/EDCE.cpp
    passes/src/mid/SCCP.cpp
    passes/src/mid/SimplifyControl.cpp
    passes/src/mid/BoundsCheckElim.cpp
    passes/src/mid/EscapeAnalysis.cpp
    passes/src/mid/LICM.cpp
    passes/src/mid/StrengthReduction.cpp
    passes/src/mid/CSE.cpp
    passes/src/mid/CopyPropagation.cpp
    passes/src/mid/DSE.cpp
    passes/src/mid/TCO.cpp
    passes/src/mid/SCEV.cpp
    passes/src/mid/LoopUnrolling.cpp
    passes/src/mid/LoopFusion.cpp
    passes/src/mid/LoopFission.cpp
    passes/src/mid/InductionVarSimplification.cpp
    passes/src/mid/NullPointerElimination.cpp
    passes/src/mid/RCOptimization.cpp
    # passes (research — Rule A.1: same unified pipeline, opted into
    # by the driver via --research)
    passes/src/research/ResearchPipeline.cpp
    passes/src/research/CFLAliasAnalysis.cpp
    passes/src/research/ValueFlowAnalysis.cpp
    passes/src/research/PGDLO.cpp
    passes/src/research/MemPoolSynthesis.cpp
    passes/src/research/CacheObliviousLayout.cpp
    passes/src/research/SLPVectorization.cpp
    passes/src/research/AutoParallelization.cpp
    passes/src/research/SpeculativeLockElision.cpp
    passes/src/research/GuardedDevirtualization.cpp
    passes/src/research/SpeculativeEffectReordering.cpp
    passes/src/research/SpeculativeBCE.cpp
    passes/src/research/BOLTLayout.cpp
    # backend
    backend/src/RegAlloc/LinearScan.cpp
    backend/src/RegAlloc/GraphColoring.cpp
    backend/src/x86/InstrSel_x86.cpp
    backend/src/x86/Target_x86.cpp
    backend/src/x86/Emitter_x86.cpp
    backend/src/x86/ExecEncoder.cpp
    backend/src/SSAConstruction.cpp
    backend/src/Peephole.cpp
    backend/src/FrameLayout.cpp
    backend/src/InstrScheduling.cpp
    backend/src/EHTables.cpp
    backend/src/DebugInfo.cpp
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

# PGO infrastructure (Telemetry is linked into aegis_core consumers via
# aegis_pgo; PassManager emits telemetry events per Rule 65).
add_library(aegis_pgo STATIC
    pgo/src/Profiler.cpp
    pgo/src/ProfileData.cpp
    pgo/src/Telemetry.cpp
)
target_link_libraries(aegis_pgo PUBLIC aegis_core)
