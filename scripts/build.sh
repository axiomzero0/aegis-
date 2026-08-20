#!/usr/bin/env bash
# scripts/build.sh — Build the Aegis compiler without CMake.
# Requires g++ 14+ (C++26 mode). Uses -fno-rtti + -fno-exceptions on the
# hot path (Rules B.1, B.3) and aggressive warnings.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SRC="$ROOT/src"

CXX=g++
CXXFLAGS=(
    -std=c++26 -fno-rtti
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-deprecated -Wno-unused-function
    -O2 -g0
    -DAEGIS_HOT_PATH=1 -DAEGIS_NO_EXCEPTIONS=1 -DAEGIS_VERIFY_IR=1
    -I"$SRC"
)

mkdir -p "$BUILD/obj"

# Compile sources. Add new sources to this list as they appear.
SOURCES=(
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
    driver/main.cpp
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD/obj/$(basename "${src%.cpp}").o"
    OBJECTS+=("$obj")
    if [ "$src" -nt "$obj" ] 2>/dev/null || true; then
        echo "CXX  $src"
        "$CXX" "${CXXFLAGS[@]}" -c "$SRC/$src" -o "$obj"
    fi
done

echo "LINK aegis"
"$CXX" "${CXXFLAGS[@]}" "${OBJECTS[@]}" -o "$BUILD/aegis"

echo "OK -> $BUILD/aegis"
