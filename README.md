# Aegis Compiler

An experimental C++26 compiler with an Effect-Typed Sea-of-Nodes IR.

## Status

Work-in-progress prototype. Implements the foundational layers:

- **Core containers** (`src/core/`): `SmallVector<T,N>`, `SparseSet`, `BitVector`, `SwissTable` (open-addressing flat hash map), `SymbolTable` (string interning).
- **IR** (`src/ir/`): `NodeKind` enum (no RTTI), `Node` (32-byte struct with NodeId-based edges), `Graph` (PMR-arena-backed), `HashCons` (lookup-or-insert by structural signature), `Verifier` (Rule 42), `Printer`.
- **Frontend** (`src/frontend/`): Lexer, Pratt-style Parser producing an AST, Effect Inference (Pure/Altered/Crowded per §3 of the language spec), Type Checker (affine checker stub), AST-to-IR Lowerer that uses HashCons for native GVN on construction.
- **Passes** (`src/passes/`): `PassManager` (Rule 42 verification + idempotency fixpoint + monotonic budget enforcement), `GVNPass`, `EDCEPass` (effect-aware dead code elimination), `SCCPPass`, `SimplifyControlPass` (block merge + DSE + TCO stubs).
- **Backend** (`src/backend/`): `MachineInstr`, `InstrSelector` (SoN -> MachineInstr), `LinearScanAllocator` (Phase 1 register allocator per the project plan).
- **Driver** (`src/driver/main.cpp`): CLI entry point with `--dump-ast`, `--dump-ir`, `--dump-mir`, `--no-passes`, `--no-verify`, `--jit`/`--aot`.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Requires a C++26 compiler (clang 19+ or g++ 14+).

## Laws Implemented

| Rule | Where |
|------|-------|
| Rule 36 (5 regression tests per bug fix) | `tests/test_passes.cpp` per-pass tests + golden tests |
| Rule 37 (golden tests per pass) | `tests/golden/` |
| Rule 42 (verifier after every pass) | `passes/PassManager.cpp::maybe_verify()` |
| Rule 51 (bitmasked flags) | `common/Flags.h`, `NodeFlagBit` |
| Rule 53 (NodeId-based edges) | `common/Primitives.h`, `ir/Node.h` |
| Rule 54 (interned symbols) | `core/SymbolTable.h` |
| Rule 55 (cache-friendly hash map) | `core/SwissTable.h` |
| Rule 56 (Sparse Sets / BitVectors) | `core/SparseSet.h`, `core/BitVector.h` |
| Rule 57 (SmallVector SBO) | `core/SmallVector.h` |
| Rule 58 (`[[assume]]`, `[[likely]]`) | throughout |
| Rule 59 (SoA-friendly Node struct) | `ir/Node.h` |
| Rule 60 (`std::expected`) | `common/Expected.h`, `AEGIS_TRY` macro |
| Rule B.1 (no exceptions on hot path) | `aegis_hot_path_options` CMake interface |
| Rule B.2 (PMR monotonic arena) | `ir/Graph.h::IRArena` |
| Rule B.3 (no RTTI) | `-fno-rtti` in CMake; `enum class NodeKind` |
| Rule B.4 (no shared_ptr in hot IR) | IR uses raw NodeId indices |
| Rule B.5 (passes idempotent) | `PassManager::run` fixpoint loop |
| Rule B.6 (monotonic decreasing budget) | `PassBudget` |
| Rule A.5 (FrameState on guards) | `Node::make_guard` sets HasFrameState flag |

## License

See `LICENSE` (placeholder — currently unreleased, all rights reserved).
