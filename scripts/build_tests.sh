#!/usr/bin/env bash
# scripts/build_tests.sh — Build the unit + regression + perf test suites.
#
# Mirrors tests/CMakeLists.txt exactly (both build systems must stay
# in sync — the dual-build contract is documented in the README).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
UNIT="$ROOT/tests/unit"
REGRESSION="$ROOT/tests/regression"
PERF="$ROOT/tests/perf"

# Use the same flags as the main build.
CXX=g++
CXXFLAGS=(
    -std=c++26 -fno-rtti
    -Wall -Wextra -Wpedantic -Werror
    -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-unused-function -Wno-deprecated -Wno-unused-but-set-variable -Wno-unused-result
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

# Common sources needed by every test binary.
SUPPORT_SRC=(
    "$ROOT/compiler/support/src/Diagnostics.cpp"
    "$ROOT/compiler/support/src/StringIntern.cpp"
    "$ROOT/compiler/support/src/SmallVector.cpp"
    "$ROOT/compiler/support/src/SparseSet.cpp"
    "$ROOT/compiler/support/src/BitVector.cpp"
    "$ROOT/compiler/support/src/SwissTable.cpp"
)
IR_SRC=(
    "$ROOT/compiler/ir/src/Graph.cpp"
    "$ROOT/compiler/ir/src/HashConsing.cpp"
    "$ROOT/compiler/ir/src/Verifier.cpp"
    "$ROOT/compiler/ir/src/Printer.cpp"
    "$ROOT/compiler/ir/src/Types.cpp"
)
FRONTEND_SRC=(
    "$ROOT/compiler/frontend/src/Lexer.cpp"
    "$ROOT/compiler/frontend/src/Parser.cpp"
    "$ROOT/compiler/frontend/src/ASTPrinter.cpp"
    "$ROOT/compiler/frontend/src/Lowering.cpp"
    "$ROOT/compiler/frontend/src/EffectInference.cpp"
    "$ROOT/compiler/frontend/src/TypeChecker.cpp"
)
PASSES_SRC=(
    "$ROOT/compiler/passes/src/PassManager.cpp"
    "$ROOT/compiler/passes/src/mid/StandardPipeline.cpp"
    "$ROOT/compiler/passes/src/mid/GVN.cpp"
    "$ROOT/compiler/passes/src/mid/EDCE.cpp"
    "$ROOT/compiler/passes/src/mid/SCCP.cpp"
    "$ROOT/compiler/passes/src/mid/SimplifyControl.cpp"
    "$ROOT/compiler/passes/src/mid/BoundsCheckElim.cpp"
    "$ROOT/compiler/passes/src/mid/EscapeAnalysis.cpp"
    "$ROOT/compiler/passes/src/mid/LICM.cpp"
    "$ROOT/compiler/passes/src/mid/StrengthReduction.cpp"
    "$ROOT/compiler/passes/src/mid/CSE.cpp"
    "$ROOT/compiler/passes/src/mid/CopyPropagation.cpp"
    "$ROOT/compiler/passes/src/mid/DSE.cpp"
    "$ROOT/compiler/passes/src/mid/TCO.cpp"
    "$ROOT/compiler/passes/src/mid/SCEV.cpp"
    "$ROOT/compiler/passes/src/mid/LoopUnrolling.cpp"
    "$ROOT/compiler/passes/src/mid/LoopFusion.cpp"
    "$ROOT/compiler/passes/src/mid/LoopFission.cpp"
    "$ROOT/compiler/passes/src/mid/InductionVarSimplification.cpp"
    "$ROOT/compiler/passes/src/mid/NullPointerElimination.cpp"
    "$ROOT/compiler/passes/src/mid/RCOptimization.cpp"
)
# Research passes: needed by test_loop_speculative, test_regression,
# and the ResearchPipeline builder (Rule A.1 unified pipeline).
RESEARCH_SRC=(
    "$ROOT/compiler/passes/src/research/ResearchPipeline.cpp"
    "$ROOT/compiler/passes/src/research/CFLAliasAnalysis.cpp"
    "$ROOT/compiler/passes/src/research/ValueFlowAnalysis.cpp"
    "$ROOT/compiler/passes/src/research/PGDLO.cpp"
    "$ROOT/compiler/passes/src/research/MemPoolSynthesis.cpp"
    "$ROOT/compiler/passes/src/research/CacheObliviousLayout.cpp"
    "$ROOT/compiler/passes/src/research/SLPVectorization.cpp"
    "$ROOT/compiler/passes/src/research/AutoParallelization.cpp"
    "$ROOT/compiler/passes/src/research/SpeculativeLockElision.cpp"
    "$ROOT/compiler/passes/src/research/GuardedDevirtualization.cpp"
    "$ROOT/compiler/passes/src/research/SpeculativeEffectReordering.cpp"
    "$ROOT/compiler/passes/src/research/SpeculativeBCE.cpp"
    "$ROOT/compiler/passes/src/research/BOLTLayout.cpp"
)
BACKEND_SRC=(
    "$ROOT/compiler/backend/src/RegAlloc/LinearScan.cpp"
    "$ROOT/compiler/backend/src/x86/InstrSel_x86.cpp"
    "$ROOT/compiler/backend/src/x86/ExecEncoder.cpp"
)

# Per-test dependencies.
#
# Shared sources are compiled ONCE into $BUILD/test_obj/ (in parallel,
# one job per core) and every test binary links against them — CI on a
# 2-core runner stays well inside its timeout this way, and the object
# set is identical for every test by construction.
# Telemetry + runtime I/O sources (needed because PassManager now
# emits telemetry on budget exceeded + verifier failed, and the
# telemetry sink writes to stderr via runtime io).
PGO_SRC=(
    "$ROOT/compiler/pgo/src/Profiler.cpp"
    "$ROOT/compiler/pgo/src/ProfileData.cpp"
    "$ROOT/compiler/pgo/src/Telemetry.cpp"
)
RUNTIME_IO_SRC=(
    "$ROOT/runtime/io/src/syscall.cpp"
)

# ---- Shared compile-once object set ----
JIT_SRC=(
    "$ROOT/compiler/jit/src/MemManager.cpp"
)

# ---- Shared compile-once object set ----
SHARED_SRC=(
    "${SUPPORT_SRC[@]}"
    "${IR_SRC[@]}"
    "${FRONTEND_SRC[@]}"
    "${PASSES_SRC[@]}"
    "${RESEARCH_SRC[@]}"
    "${BACKEND_SRC[@]}"
    "${PGO_SRC[@]}"
    "${RUNTIME_IO_SRC[@]}"
)
export CXX
mkdir -p "$BUILD/test_obj"
JOB_LIST=()
SHARED_OBJ=()
for src in "${SHARED_SRC[@]}"; do
    obj="$BUILD/test_obj/$(basename "$src").o"
    SHARED_OBJ+=("$obj")
    JOB_LIST+=("$obj|$src")
done
printf '%s\n' "${JOB_LIST[@]}" | xargs -P "$(nproc)" -I{} bash -c '
    IFS="|" read -r obj src <<< "{}"
    echo "CXX  $(basename "$src")"
    "$CXX" "${@}" -c "$src" -o "$obj"
' _ "${CXXFLAGS[@]}"

build_test() {
    local name="$1"; shift
    local test_src="$1"; shift
    "$CXX" "${CXXFLAGS[@]}" "$test_src" "${SHARED_OBJ[@]}" "$@" -o "$BUILD/$name"
    echo "OK -> $BUILD/$name"
}


build_test test_core_containers "$UNIT/test_core_containers.cpp"
build_test test_ir_graph "$UNIT/test_ir_graph.cpp"
build_test test_passes "$UNIT/test_passes.cpp"
build_test test_frontend_smoke "$UNIT/test_frontend_smoke.cpp"
build_test test_new_passes "$UNIT/test_new_passes.cpp"
build_test test_telemetry "$UNIT/test_telemetry.cpp"
build_test test_sound_rewrites "$UNIT/test_sound_rewrites.cpp"
build_test test_loop_speculative "$UNIT/test_loop_speculative.cpp"

build_test test_for_loops "$UNIT/test_for_loops.cpp"
build_test test_exec_codegen "$UNIT/test_exec_codegen.cpp" "${JIT_SRC[@]}"

# Rule 36 regression suite (tests/regression/).
build_test test_regression "$REGRESSION/test_regression.cpp"

# Rule 41 perf suites (tests/perf/): compile-time pipeline + runtime
# generated-code execution (needs the JIT MemManager).
build_test bench_pipeline "$PERF/bench_pipeline.cpp"
build_test bench_runtime "$PERF/bench_runtime.cpp" "${JIT_SRC[@]}"
