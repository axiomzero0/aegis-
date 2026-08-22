# Aegis Compiler

An experimental C++26 compiler with an Effect-Typed Sea-of-Nodes IR.

## Status

Work-in-progress prototype. The foundational layers are in place and
green:

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
  `TypeChecker` (name resolution, immutability, arity + loud
  rejection of not-yet-lowered constructs), `Lowering` (AST → E-SoN IR
  via HashCons for native GVN).
- **`compiler/passes/`** — `PassManager` (Rule 42 verification + idempotency
  fixpoint + monotonic budget enforcement), the 19-pass standard
  mid-level pipeline (`mid/`), and the 12 research passes
  (`research/`: CFL-Alias, ValueFlow, PGDLO, MemPoolSynthesis,
  CacheObliviousLayout, SLP, AutoParallelization, GuardedDevirt,
  SpeculativeBCE, SpeculativeEffectReordering, SpeculativeLockElision,
  BOLTLayout) — appended to the unified pipeline via `aegisc --research`
  (Rule A.1: one pipeline, two inputs; speculative members no-op in
  AOT mode and install guard + FrameState in JIT mode).
- **`compiler/backend/`** — `MachineIR`, `InstrSel` (SoN → MachineInstr),
  `LinearScan` (Phase 1 register allocator per the spec), `Target`
  (abstract target interface for x86/ARM), `Emitter` (machine code
  emission + real ELF64 object file writing), `ElfConstants`
  (named ELF ABI constants per Rule D.1/D.2),
  `RegAlloc/RegAllocInterface` for plugging in alternative allocators.
- **`compiler/jit/`** — `JitEngine` (hotness tracking + background
  compilation, Rule C.1), `Deopt` (deoptimization trampolines + state
  reconstruction, Rules A.3-A.5), `MemManager` (RWX page management +
  epoch-based reclamation, Rule C.4).
- **`compiler/pgo/`** — `Profiler` (instrumentation insertion + runtime
  counters), `ProfileData` (serialization/deserialization with versioning,
  Rule 50), `Telemetry` (Rule 65 fallback observability).
- **`runtime/`** — `core/` (intrinsics, basic types, panic handler),
  `alloc/` (Allocator, BumpAllocator, PoolAllocator, SystemAllocator),
  `sync/` (Atomics, Mutex, RwLock, Channel), `io/` (file, net, syscalls).
- **`tools/`** — `aegisc` (main compiler CLI), `lsp` (Language Server
  Protocol), `repl` (interactive REPL using the JIT), `fmt` (code formatter).
- **`tests/`** — `unit/` (per-pass unit tests), `integration/golden/`
  (Rule 37 golden IR tests: **31 passes × ≥10 pairs each**, every pair
  run in BOTH Static (AOT) and Profile (JIT) modes),
  `regression/` (Rule 36 suite: 35 tests across 7 fixed bugs, each
  with the five mandatory categories), `perf/` (Rule 41 compile-time
  benchmarks), `integration/run_differential.py` (Rule 38
  reference-interpreter ↔ AOT ↔ JIT differential testing over a
  seeded, reproducible corpus).
- **`docs/`** — `laws.md` (the full Laws & Rules 36-76), `ir_spec.md`
  (formal E-SoN IR specification), `effect_system.md` (Pure/Altered/
  Crowded semantics), `abi.md` (C ABI + internal calling conventions),
  `directory_layout.md` (this prescribed layout).
- **CI (`docs/ci.workflow.yml`)** — the workflow enforcing the laws on
  every push and PR (checked in under docs/ because the push token
  lacks GitHub's `workflow` scope; copy it to
  `.github/workflows/ci.yml` with a workflow-scoped account to
  activate). It runs: build, unit tests, Rule 36 regression suite, Rule 37 golden
  suite (both modes + census), Rule 38 differential testing, Rule 41
  benchmarks, Rule 61/D.1 magic-number scan, Rule D.7 workaround scan,
  Rule 40 replay-artifact upload, weekly extended corpus (Rule 39
  cadence).

## Build

```sh
# With CMake:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build

# Without CMake (g++ 14+ fallback):
bash scripts/build.sh        # builds the aegisc driver + tools
bash scripts/build_tests.sh  # builds unit + regression + perf tests

# Run everything the CI runs:
for t in test_core_containers test_ir_graph test_passes \
         test_frontend_smoke test_new_passes test_telemetry \
         test_sound_rewrites test_loop_speculative; do ./build/$t; done
./build/test_regression            # Rule 36 suite
bash tests/integration/run_all_golden.sh   # Rule 37 suite (AOT+JIT)
python3 tests/integration/run_differential.py  # Rule 38
./build/bench_pipeline             # Rule 41 benchmarks
python3 scripts/scan_magic_numbers.py  # Rule 61/D.1
python3 scripts/scan_slop.py           # Rule D.7
```

Requires a C++26 compiler (clang 19+ or g++ 14+), plus Python 3 for
the differential harness and the law-enforcement scanners.

### Golden test maintenance (Rule 37)

Golden pairs live under `tests/integration/golden/<pass>/<name>.in.aegis`
with `.expected.son` (or `.expected.aot.son` + `.expected.jit.son` when
speculation makes the modes diverge). When a pass INTENTIONALLY changes
the IR it produces, regenerate with:

```sh
bash scripts/build.sh && python3 scripts/gen_golden.py
```

…review the `.expected.son` diff (that diff is the proof the change
was intended — Rule 52), and commit it together with the pass change.

## Laws Implemented

| Rule | Where |
|------|-------|
| Rule 36 (5 regression tests per bug fix) | `tests/regression/` (35 tests, 7 bugs) |
| Rule 37 (golden tests per pass) | `tests/integration/golden/` (31 passes × ≥10, both modes) |
| Rule 38 (differential testing in CI) | `tests/integration/run_differential.py` + CI |
| Rule 39 (weekly deopt-path testing) | CI `schedule:` job (extended corpus) |
| Rule 40 (replay artifacts on failure) | CI artifact upload + `replay-artifacts/` |
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
| Rule 61 / D.1 (no magic numbers) | `PassConstants.hpp`, `TargetConstants.hpp`, `ElfConstants.hpp` + `scripts/scan_magic_numbers.py` in CI |
| Rule B.1 (no exceptions on hot path) | `aegis_hot_path_options` CMake interface |
| Rule B.2 (PMR monotonic arena) | `support/Allocator.hpp::MonoArena`, `ir/Graph.hpp::IRArena` |
| Rule B.3 (no RTTI) | `-fno-rtti` in CMake; `enum class NodeKind` |
| Rule B.4 (no shared_ptr in hot IR) | IR uses raw NodeId indices |
| Rule B.5 (passes idempotent) | `PassManager::run` fixpoint loop |
| Rule B.6 (monotonic-decreasing budget) | `PassBudget` |
| Rule A.1 (one unified pipeline) | `--research` opt-in; `ResearchPipeline.hpp` |
| Rule A.5 (FrameState on guards) | `Node::make_guard` + speculation passes (edge-attached FrameStates) |
| Rule C.4 (epoch-based reclamation) | `jit/MemManager.hpp` |
| Rule D.7 (no untracked workarounds) | `scripts/scan_slop.py` in CI |

## License

See `LICENSE` (currently unreleased, all rights reserved).
