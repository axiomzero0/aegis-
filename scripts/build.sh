#!/usr/bin/env bash
# scripts/build.sh — Build the Aegis compiler without CMake.
# Builds the prescribed directory tree:
#   compiler/{support,ir,frontend,passes,backend,jit,pgo}
#   runtime/{core,alloc,sync,io}
#   tools/{aegisc,lsp,repl,fmt}
#   tests/{unit,integration,regression,perf}
#
# Laws enforced here:
#   B.1  No exceptions on the hot path  -> AEGIS_HOT_PATH_NO_EXCEPTIONS
#   B.2  PMR monotonic arena for IR      -> linked per-translation-unit
#   B.3  No RTTI                         -> -fno-rtti
#   Rule 58: -fassume* / hint macros supported by C++26
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

CXX=g++
# -fno-rtti everywhere (Rule B.3). -fno-exceptions on hot path files only.
# The hot path = IR, passes, backend. Frontend/diagnostics MAY use exceptions
# for ergonomics, but the IR/passes/backend must not.
CXXFLAGS_COMMON=(
    -std=c++26 -fno-rtti
    -Wall -Wextra -Wpedantic -Werror
    -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-unused-function -Wno-deprecated -Wno-unused-but-set-variable
    -O2 -g0
    -I"$ROOT/compiler/support/include"
    -I"$ROOT/compiler/ir/include"
    -I"$ROOT/compiler/frontend/include"
    -I"$ROOT/compiler/passes/include"
    -I"$ROOT/compiler/backend/include"
    -I"$ROOT/compiler/jit/include"
    -I"$ROOT/compiler/pgo/include"
    -I"$ROOT/runtime/core/include"
    -I"$ROOT/runtime/alloc/include"
    -I"$ROOT/runtime/sync/include"
    -I"$ROOT/runtime/io/include"
    -DAEGIS_HOT_PATH=1 -DAEGIS_VERIFY_IR=1
)
CXXFLAGS_HOT_PATH=(
    -fno-exceptions
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -DAEGIS_NO_EXCEPTIONS=1
)

mkdir -p "$BUILD/obj"

SUPPORT_HOT=(
    support/src/SmallVector.cpp
    support/src/SparseSet.cpp
    support/src/BitVector.cpp
    support/src/SwissTable.cpp
    support/src/StringIntern.cpp
)
SUPPORT_COLD=(
    support/src/Diagnostics.cpp
)
IR_HOT=(
    ir/src/Graph.cpp
    ir/src/HashConsing.cpp
    ir/src/Verifier.cpp
    ir/src/Printer.cpp
    ir/src/Types.cpp
)
FRONTEND=(
    frontend/src/Lexer.cpp
    frontend/src/Parser.cpp
    frontend/src/ASTPrinter.cpp
    frontend/src/Lowering.cpp
    frontend/src/EffectInference.cpp
    frontend/src/TypeChecker.cpp
)
PASSES=(
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
)
BACKEND=(
    backend/src/RegAlloc/LinearScan.cpp
    backend/src/RegAlloc/GraphColoring.cpp
    backend/src/x86/InstrSel_x86.cpp
    backend/src/x86/Target_x86.cpp
    backend/src/x86/Emitter_x86.cpp
    backend/src/SSAConstruction.cpp
    backend/src/Peephole.cpp
    backend/src/FrameLayout.cpp
    backend/src/InstrScheduling.cpp
    backend/src/EHTables.cpp
    backend/src/DebugInfo.cpp
)
JIT=(
    jit/src/JitEngine.cpp
    jit/src/Deopt.cpp
    jit/src/MemManager.cpp
)
PGO=(
    pgo/src/Profiler.cpp
    pgo/src/ProfileData.cpp
    pgo/src/Telemetry.cpp
)
RUNTIME_SRC=(
    runtime/core/src/panic.cpp
    runtime/alloc/src/bump.cpp
    runtime/alloc/src/pool.cpp
    runtime/alloc/src/system.cpp
    runtime/sync/src/atomic.cpp
    runtime/sync/src/mutex.cpp
    runtime/sync/src/rwlock.cpp
    runtime/sync/src/channel.cpp
    runtime/io/src/file.cpp
    runtime/io/src/net.cpp
    runtime/io/src/syscall.cpp
)

ALL_SOURCES=()
ALL_SOURCES+=("${SUPPORT_HOT[@]}")
ALL_SOURCES+=("${SUPPORT_COLD[@]}")
ALL_SOURCES+=("${IR_HOT[@]}")
ALL_SOURCES+=("${FRONTEND[@]}")
ALL_SOURCES+=("${PASSES[@]}")
ALL_SOURCES+=("${BACKEND[@]}")
ALL_SOURCES+=("${JIT[@]}")
ALL_SOURCES+=("${PGO[@]}")
ALL_SOURCES+=("${RUNTIME_SRC[@]}")

# Hot-path TUs: support (containers), ir, passes, backend, jit, pgo.
# Frontend + runtime + diagnostics are allowed exceptions (cold).
HOT_SET=(
    "support/src/SmallVector.cpp"
    "support/src/SparseSet.cpp"
    "support/src/BitVector.cpp"
    "support/src/SwissTable.cpp"
    "support/src/StringIntern.cpp"
    "ir/src/Graph.cpp"
    "ir/src/HashConsing.cpp"
    "ir/src/Verifier.cpp"
    "ir/src/Printer.cpp"
    "ir/src/Types.cpp"
    "passes/src/PassManager.cpp"
    "passes/src/mid/StandardPipeline.cpp"
    "passes/src/mid/GVN.cpp"
    "passes/src/mid/EDCE.cpp"
    "passes/src/mid/SCCP.cpp"
    "passes/src/mid/SimplifyControl.cpp"
    "passes/src/mid/BoundsCheckElim.cpp"
    "passes/src/mid/EscapeAnalysis.cpp"
    "passes/src/mid/LICM.cpp"
    "passes/src/mid/StrengthReduction.cpp"
    "passes/src/mid/CSE.cpp"
    "passes/src/mid/CopyPropagation.cpp"
    "passes/src/mid/DSE.cpp"
    "passes/src/mid/TCO.cpp"
    "passes/src/mid/SCEV.cpp"
    "passes/src/mid/LoopUnrolling.cpp"
    "passes/src/mid/LoopFusion.cpp"
    "passes/src/mid/LoopFission.cpp"
    "passes/src/mid/InductionVarSimplification.cpp"
    "passes/src/mid/NullPointerElimination.cpp"
    "passes/src/mid/RCOptimization.cpp"
    "passes/src/research/CFLAliasAnalysis.cpp"
    "passes/src/research/ValueFlowAnalysis.cpp"
    "passes/src/research/PGDLO.cpp"
    "passes/src/research/MemPoolSynthesis.cpp"
    "passes/src/research/CacheObliviousLayout.cpp"
    "passes/src/research/SLPVectorization.cpp"
    "passes/src/research/AutoParallelization.cpp"
    "passes/src/research/SpeculativeLockElision.cpp"
    "passes/src/research/GuardedDevirtualization.cpp"
    "passes/src/research/SpeculativeEffectReordering.cpp"
    "passes/src/research/SpeculativeBCE.cpp"
    "passes/src/research/BOLTLayout.cpp"
    "backend/src/RegAlloc/LinearScan.cpp"
    "backend/src/RegAlloc/GraphColoring.cpp"
    "backend/src/x86/InstrSel_x86.cpp"
    "backend/src/x86/Target_x86.cpp"
    "backend/src/x86/Emitter_x86.cpp"
    "backend/src/SSAConstruction.cpp"
    "backend/src/Peephole.cpp"
    "backend/src/FrameLayout.cpp"
    "backend/src/InstrScheduling.cpp"
    "backend/src/EHTables.cpp"
    "backend/src/DebugInfo.cpp"
    "jit/src/JitEngine.cpp"
    "jit/src/Deopt.cpp"
    "jit/src/MemManager.cpp"
    "pgo/src/Profiler.cpp"
    "pgo/src/ProfileData.cpp"
    "pgo/src/Telemetry.cpp"
)

is_hot() {
    local src="$1"
    for h in "${HOT_SET[@]}"; do
        if [[ "$h" == "$src" ]]; then return 0; fi
    done
    return 1
}

OBJECTS=()
for src in "${ALL_SOURCES[@]}"; do
    obj="$BUILD/obj/$(echo "$src" | tr '/' '_').o"
    OBJECTS+=("$obj")
    SRC_PATH="$ROOT/compiler/$src"
    if [[ ! -f "$SRC_PATH" ]]; then
        SRC_PATH="$ROOT/$src"
    fi
    if [[ ! -f "$SRC_PATH" ]]; then
        echo "skip missing source: $src"
        continue
    fi
    if is_hot "$src"; then
        FLAGS=("${CXXFLAGS_COMMON[@]}" "${CXXFLAGS_HOT_PATH[@]}")
    else
        FLAGS=("${CXXFLAGS_COMMON[@]}")
    fi
    echo "CXX  $src"
    "$CXX" "${FLAGS[@]}" -c "$SRC_PATH" -o "$obj"
done

echo "LINK aegisc"
"$CXX" "${CXXFLAGS_COMMON[@]}" "${OBJECTS[@]}" "$ROOT/tools/aegisc/main.cpp" -o "$BUILD/aegisc"

echo "LINK aegis-lsp"
"$CXX" "${CXXFLAGS_COMMON[@]}" "${OBJECTS[@]}" "$ROOT/tools/lsp/main.cpp" -o "$BUILD/aegis-lsp"

echo "LINK aegis-repl"
"$CXX" "${CXXFLAGS_COMMON[@]}" "${OBJECTS[@]}" "$ROOT/tools/repl/main.cpp" -o "$BUILD/aegis-repl"

echo "LINK aegis-fmt"
"$CXX" "${CXXFLAGS_COMMON[@]}" "${OBJECTS[@]}" "$ROOT/tools/fmt/main.cpp" -o "$BUILD/aegis-fmt"

echo "OK -> $BUILD/aegisc"
echo "OK -> $BUILD/aegis-lsp"
echo "OK -> $BUILD/aegis-repl"
echo "OK -> $BUILD/aegis-fmt"
