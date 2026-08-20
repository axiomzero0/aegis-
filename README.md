# Aegis Compiler

An experimental C++26 compiler with an Effect-Typed Sea-of-Nodes IR.

## Status

Work-in-progress prototype. The foundational layers are in place:

- **`compiler/support/`** — Zero-cost utilities: `SmallVector`, `SparseSet`,
  `BitVector`, `SwissTable`, `StringIntern` (symbol interning), `Allocator`
  (PMR arena wrappers), `Expected` (Rule 60 error propagation),
  `Parallel` (std::execution wrappers).
- **`compiler/ir/`** — Effect-Typed Sea of Nodes IR. `Node`, `Graph`
  (PMR-arena-backed, NodeId-based edges), `HashConsing` (native GVN),
  `Verifier` (Rule 42), `Printer`, `Effects` (Pure/Altered/Crowded
  typing + alias analysis interface), `Types` (affine type table).
- **`compiler/frontend/`** — Lexer, Pratt Parser producing AST,
  `EffectInference` (Pure/Altered/Crowded per §3 of the spec),
  `TypeChecker` (affine checker stub), `Lowering` (AST → E-SoN IR
  via HashCons for native GVN).
- **`compiler/passes/`** — `PassManager` (Rule 42 verification + idempotency
  fixpoint + monotonic budget enforcement), standard mid-level passes:
  `GVN`, `EDCE` (effect-aware DCE), `SCCP`, `SimplifyControl`. Plus the
  `research/` subdirectory for advanced passes (CFL-Alias, PGDLO, SLP,
  Speculative Reordering — to be filled in).
- **`compiler/backend/`** — `MachineIR`, `InstrSel` (SoN → MachineInstr),
  `LinearScan` (Phase 1 register allocator per the spec), `Target`
  (abstract target interface for x86/ARM), `Emitter` (machine code
  emission + object file writing), `RegAlloc/RegAllocInterface` for
  plugging in alternative allocators (Cranelift regalloc2 via FFI in
  Phase 2).
- **`compiler/jit/`** — `JitEngine` (hotness tracking + background
  compilation, Rule C.1), `Deopt` (deoptimization trampolines + state
  reconstruction, Rules A.3-A.5), `MemManager` (RWX page management +
  epoch-based reclamation, Rule C.4).
- **`compiler/pgo/`** — `Profiler` (instrumentation insertion + runtime
  counters), `ProfileData` (serialization/deserialization with versioning,
  Rule 50).
- **`runtime/`** — `core/` (intrinsics, basic types, panic handler),
  `alloc/` (Allocator, BumpAllocator, PoolAllocator, SystemAllocator),
  `sync/` (Atomics, Mutex, RwLock, Channel), `io/` (file, net, syscalls).
- **`tools/`** — `aegisc` (main compiler CLI), `lsp` (Language Server
  Protocol), `repl` (interactive REPL using the JIT), `fmt` (code formatter).
- **`tests/`** — `unit/` (per-pass unit tests), `integration/` (end-to-end
  + golden IR tests, Rule 37), `regression/` (bug-fix reproductions,
  Rule 36), `perf/` (compile-time + runtime benchmarks).
- **`docs/`** — `laws.md` (the full Laws & Rules 36-60), `ir_spec.md`
  (formal E-SoN IR specification), `effect_system.md` (Pure/Altered/
  Crowded semantics), `abi.md` (C ABI + internal calling conventions),
  `directory_layout.md` (this prescribed layout).

## Build

```sh
# With CMake:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build

# Without CMake (g++ 14+ fallback):
bash scripts/build.sh        # builds the aegisc driver
bash scripts/build_tests.sh # builds the unit tests
```

Requires a C++26 compiler (clang 19+ or g++ 14+).

## Laws Implemented

| Rule | Where |
|------|-------|
| Rule 36 (5 regression tests per bug fix) | `tests/regression/` (placeholder) |
| Rule 37 (golden tests per pass) | `tests/integration/golden/` |
| Rule 42 (verifier after every pass) | `passes/PassManager::maybe_verify()` |
| Rule 50 (versioned caches) | `ProfileData::ProfileHeader`, `Graph::version_` |
| Rule 51 (bitmasked flags) | `support/Flags.hpp`, `NodeFlagBit`, `EffectTag`, `TypeFlag` |
| Rule 53 (NodeId-based edges) | `support/Primitives.hpp`, `ir/Node.hpp` |
| Rule 54 (interned symbols) | `support/StringIntern.hpp` |
| Rule 55 (cache-friendly hash map) | `support/SwissTable.hpp` |
| Rule 56 (Sparse Sets / BitVectors) | `support/SparseSet.hpp`, `support/BitVector.hpp` |
| Rule 57 (SmallVector SBO) | `support/SmallVector.hpp` |
| Rule 58 (`[[assume]]`, `[[likely]]`) | throughout |
| Rule 59 (SoA-friendly Node struct) | `ir/Node.hpp` |
| Rule 60 (`std::expected`) | `support/Expected.hpp`, `AEGIS_TRY` macro |
| Rule B.1 (no exceptions on hot path) | `aegis_hot_path_options` CMake interface |
| Rule B.2 (PMR monotonic arena) | `support/Allocator.hpp::MonoArena`, `ir/Graph.hpp::IRArena` |
| Rule B.3 (no RTTI) | `-fno-rtti` in CMake; `enum class NodeKind` |
| Rule B.4 (no shared_ptr in hot IR) | IR uses raw NodeId indices |
| Rule B.5 (passes idempotent) | `PassManager::run` fixpoint loop |
| Rule B.6 (monotonic-decreasing budget) | `PassBudget` |
| Rule A.5 (FrameState on guards) | `Node::make_guard` sets `HasFrameState` flag |
| Rule C.4 (epoch-based reclamation) | `jit/MemManager.hpp` |

## License

See `LICENSE` (placeholder — currently unreleased, all rights reserved).
