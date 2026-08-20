# Aegis Compiler — Directory Layout

**Status:** Stable
**Owner:** Aegis Dev Team
**Last Updated:** 2026-08-21

The following tree is the prescribed layout for the Aegis repository.
Every commit must preserve this layout. Files added outside this tree
will be rejected at code-review.

```
aegis/
├── CMakeLists.txt              # Root build configuration (C++26, -fno-exceptions, -fno-rtti)
├── .clang-format               # Strict C++26 formatting rules
├── .clang-tidy                 # Static analysis rules
├── README.md
│
├── compiler/                   # THE COMPILER TOOLCHAIN (Libraries & Executables)
│   ├── CMakeLists.txt
│   │
│   ├── support/                # Zero-cost utilities, PMR, threading, error handling
│   │   ├── include/aegis/support/
│   │   │   ├── Allocator.hpp   # std::pmr wrappers, monotonic buffers
│   │   │   ├── Expected.hpp    # std::expected wrappers for compiler errors
│   │   │   ├── Parallel.hpp    # std::execution wrappers for parallel passes
│   │   │   └── StringIntern.hpp# Thread-safe string interning for identifiers
│   │   └── src/
│   │
│   ├── ir/                     # THE CORE: Effect-Typed Sea of Nodes (E-SoN)
│   │   ├── include/aegis/ir/
│   │   │   ├── Node.hpp        # Base Node, Pure/Altered/Crowded effect tags
│   │   │   ├── Graph.hpp       # The SoN data structure, hash-consing map
│   │   │   ├── Effects.hpp     # Effect typing system & alias analysis interfaces
│   │   │   └── Types.hpp       # Affine type system representations
│   │   └── src/
│   │
│   ├── frontend/               # Parsing, AST, and AST -> E-SoN lowering
│   │   ├── include/aegis/frontend/
│   │   │   ├── Lexer.hpp
│   │   │   ├── Parser.hpp
│   │   │   ├── AST.hpp
│   │   │   ├── TypeChecker.hpp # Affine type checking, borrow checker
│   │   │   └── Lowering.hpp    # AST to E-SoN translation
│   │   └── src/
│   │
│   ├── passes/                 # ALL OPTIMIZATION PASSES (Unified Pipeline)
│   │   ├── include/aegis/passes/
│   │   │   ├── mid/            # Standard Mid-Level (GVN, E-DCE, SCCP, PEA, SCEV)
│   │   │   ├── research/       # Advanced (CFL-Alias, PGDLO, SLP, Speculative Reorder)
│   │   │   └── PassManager.hpp # Orchestrates pass execution, handles PGO triggers
│   │   └── src/
│   │       ├── mid/
│   │       └── research/
│   │
│   ├── backend/                # CUSTOM BACKEND (No LLVM)
│   │   ├── include/aegis/backend/
│   │   │   ├── Target.hpp      # Abstract target interface (x86, ARM)
│   │   │   ├── MachineIR.hpp   # A simple, low-level IR for the backend
│   │   │   ├── InstrSel.hpp    # Instruction Selection (E-SoN to Machine IR)
│   │   │   ├── RegAlloc/       # Register Allocation
│   │   │   │   ├── RegAllocInterface.hpp # Interface for plugging in allocators
│   │   │   │   └── LinearScan.hpp        # The Linear Scan register allocator
│   │   │   └── Emitter.hpp     # Final machine code emission & object file writing
│   │   └── src/
│   │       ├── x86/            # x86_64 specific implementations
│   │       ├── arm/            # ARM64 specific implementations
│   │       └── RegAlloc/
│   │           └── LinearScan.cpp
│   │
│   ├── jit/                    # The AOT/JIT/PGO Hybrid Engine
│   │   ├── include/aegis/jit/
│   │   │   ├── JitEngine.hpp   # Manages hotness tracking and JIT compilation
│   │   │   ├── Deopt.hpp       # Deoptimization trampolines & state reconstruction
│   │   │   └── MemManager.hpp  # RWX memory page management for JIT code
│   │   └── src/
│   │
│   └── pgo/                    # Profile-Guided Optimization Infrastructure
│       ├── include/aegis/pgo/
│       │   ├── Profiler.hpp    # Instrumentation insertion & runtime counters
│       │   └── ProfileData.hpp # Serialization/deserialization of profile data
│       └── src/
│
├── runtime/                    # THE RUNTIME (Shipped with compiled binaries)
│   ├── CMakeLists.txt
│   ├── core/                   # Intrinsics, basic types, panic handler
│   ├── alloc/                   # The custom allocator (bump, pool, system)
│   ├── sync/                   # Atomics, locks, HTM wrappers, thread primitives
│   └── io/                     # Syscall wrappers, file/network I/O
│
├── tools/                      # USER-FACING EXECUTABLES
│   ├── CMakeLists.txt
│   ├── aegisc/                 # The main compiler CLI executable
│   ├── lsp/                    # Language Server Protocol implementation (for IDEs)
│   ├── repl/                   # Interactive REPL (heavily utilizes the JIT)
│   └── fmt/                    # Code formatter
│
├── tests/                      # TEST SUITES
│   ├── unit/                   # Unit tests for individual passes and IR
│   ├── integration/            # End-to-end compilation tests
│   ├── regression/             # Bug fix reproductions
│   └── perf/                   # Compile-time and runtime benchmark suites
│
└── docs/                       # DOCUMENTATION
    ├── ir_spec.md              # The formal specification of the E-SoN IR
    ├── effect_system.md        # How Pure/Altered/Crowded effects work
    └── abi.md                  # The C ABI and internal calling conventions
```

## Conventions

- **Header extension:** `.hpp` for C++ headers. `.h` is reserved for C-only
  headers shipped by `runtime/`.
- **Source extension:** `.cpp` for C++ source files.
- **Include paths:** Every header is referenced via its `aegis/...` path,
  never via a relative path. The `-I` flags are set to the
  `compiler/*/include` directories so includes look like:
  ```cpp
  #include "aegis/support/SmallVector.hpp"
  #include "aegis/ir/Graph.hpp"
  ```
- **Namespace:** Everything lives under `aegis::`. Sub-namespaces mirror
  the directory tree (`aegis::support`, `aegis::ir`, `aegis::frontend`,
  `aegis::passes`, `aegis::backend`, `aegis::jit`, `aegis::pgo`,
  `aegis::runtime::core`, `aegis::runtime::alloc`, etc.).
- **Hot-path TUs** (`compiler/ir/`, `compiler/passes/`, `compiler/backend/`,
  `compiler/support/src/{SmallVector,SparseSet,BitVector,SwissTable,StringIntern}.cpp`)
  are built with `-fno-exceptions` and `-fno-rtti` (Rules B.1, B.3).
  Frontend / diagnostics TUs are exempt from `-fno-exceptions` so that
  ergonomic diagnostics may use exceptions during the cold parse phase.

## Files not in the tree

These do not get committed:

- `build/` — build artifacts.
- `.secrets/` — credential files (the GitHub PAT lives at
  `/home/z/my-project/.secrets/github_token.txt`, *outside* the repo).
- Any `*.token`, `*.pat`, `*.secret`, `.env` patterns (see `.gitignore`).
- Replay artifacts (`replay-artifacts/`), coverage / profile data
  (`*.gcda`, `*.profraw`), AOT / JIT cache files (`*.aot-cache`,
  `*.jit-cache`).
