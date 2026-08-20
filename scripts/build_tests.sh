#!/usr/bin/env bash
# scripts/build_tests.sh — Build the unit test suites against the new layout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
TESTS="$ROOT/tests/unit"

# Use the same flags as the main build.
CXX=g++
CXXFLAGS=(
    -std=c++26 -fno-rtti
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-unused-function -Wno-deprecated -Wno-unused-but-set-variable -Wno-unused-result
    -O2 -g0
    -I"$ROOT/compiler/support/include"
    -I"$ROOT/compiler/ir/include"
    -I"$ROOT/compiler/frontend/include"
    -I"$ROOT/compiler/passes/include"
    -I"$ROOT/compiler/backend/include"
    -I"$ROOT/compiler/jit/include"
    -I"$ROOT/compiler/pgo/include"
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
)
BACKEND_SRC=(
    "$ROOT/compiler/backend/src/RegAlloc/LinearScan.cpp"
    "$ROOT/compiler/backend/src/x86/InstrSel_x86.cpp"
)

# Per-test dependencies.
build_test() {
    local name="$1"; shift
    local test_src="$TESTS/$name.cpp"
    "$CXX" "${CXXFLAGS[@]}" "$test_src" "$@" -o "$BUILD/$name"
    echo "OK -> $BUILD/$name"
}

build_test test_core_containers "${SUPPORT_SRC[@]}"
build_test test_ir_graph "${SUPPORT_SRC[@]}" "${IR_SRC[@]}"
build_test test_passes "${SUPPORT_SRC[@]}" "${IR_SRC[@]}" "${PASSES_SRC[@]}"
build_test test_frontend_smoke "${SUPPORT_SRC[@]}" "${IR_SRC[@]}" "${FRONTEND_SRC[@]}"
build_test test_new_passes "${SUPPORT_SRC[@]}" "${IR_SRC[@]}" "${PASSES_SRC[@]}"
