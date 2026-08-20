# Aegis Compiler — Laws and Rules

**Status:** Stable
**Owner:** Aegis Systems Dev Team
**Last Updated:** 2026-08-21
**Related Sections:** `ir_spec.md`, `effect_system.md`, `abi.md`

This document is the authoritative transcription of the laws that govern
the Aegis compiler. Every commit to the `compiler/`, `runtime/`, `tools/`,
and `tests/` trees must comply. CI verifies them.

---

## I. The Unified Pipeline & Speculation Laws

### A.1 — One Pipeline, Two Inputs

The sequence of optimization passes (1–51) is **identical** for both AOT and
JIT compilation.

- **AOT Input:** Static IR + Default Heuristics.
- **JIT Input:** Static IR + **PGO Data**.

There is no "JIT-only" pass list. If a pass exists, it must handle both
static and profile-driven modes via a unified interface.

### A.2 — PGO is a Force Multiplier, Not a New Logic

PGO data does not change *what* the compiler does; it changes *how
aggressively* it does it.

Example: Pass 49 (Speculative Effect Reordering) runs in both modes. In
AOT, it requires static CFL-Reachability proof. In JIT, it accepts
PGO-proven probability >99% and inserts a guard.

### A.3 — Every PGO-Driven Decision Requires a Guard

If a pass makes a decision based on PGO (e.g. "Pointers A and B never
alias", "This lock is uncontended"), it **must** emit a hardware guard.

- **Guard Success:** Execute the optimized path.
- **Guard Failure:** Trigger Deoptimization.

### A.4 — Deoptimization Must Reconstruct AOT State

When a JIT guard fails, the runtime must deoptimize to the **exact same
state** the AOT baseline would have been in at that instruction pointer.
This includes:

- Restoring register values.
- Re-materializing stack frames and affine regions.
- Rolling back any speculatively reordered memory writes (`Altered` nodes)
  to maintain sequential consistency.

### A.5 — FrameState is Mandatory for All Guards

Every node that introduces a speculative assumption (based on PGO) must
have a `FrameState` attachment. This snapshot allows the deoptimizer to
rebuild the world if the speculation fails.

---

## B. Compilation Pipeline Laws

### B.1 — NO EXCEPTIONS ON THE HOT PATH

**This is the highest priority rule.**

- The JIT compiler, the AOT compiler, and the Runtime Deoptimization
  engine **MUST** be compiled with `-fno-exceptions`.
- **Zero `throw` statements** are allowed in any code path executed during
  compilation or runtime specialization.
- All fallible operations (parsing, type checking, IR mutation, register
  allocation, memory mapping) **MUST** use `std::expected<T, Diagnostic>`
  or `Result<T, Error>`.
- If a JIT compilation fails, it must return an `Error` variant, causing
  the system to silently fall back to the AOT baseline. **No stack
  unwinding. No catch blocks. No overhead.**

### B.2 — Zero-Allocation Hot Path (Both Modes)

Both AOT and JIT compilers must use `std::pmr::monotonic_buffer_resource`
for IR allocation.

- **AOT:** Bulk-free after compilation.
- **JIT:** Bulk-free after compilation.

No `malloc`/`free` in the compiler hot path.

### B.3 — No RTTI (Both Modes)

Both pipelines are compiled with `-fno-rtti`. Use `enum class NodeKind`
for type switching. RTTI is forbidden in the IR and backend to ensure
maximum devirtualization and cache locality.

### B.4 — No `std::shared_ptr` / `std::function` in Hot IR Code

They allocate and incur atomic overhead. Use raw pointers + stable
`NodeId`s inside passes.

### B.5 — Every Pass Must Be Idempotent

Running the same pass twice must produce the identical IR. Otherwise,
fixpoint iteration will loop forever.

### B.6 — Every Pass Must Be Monotonic Decreasing in IR Size

A pass either reduces node count or moves the IR closer to a normal form.
If a pass can grow the IR (e.g. Loop Unrolling, SLP), it must run inside
a guarded fixpoint with a strict budget.

---

## C. Memory & Threading Laws

### C.1 — Mutator Threads Never Block on JIT

If a function becomes "hot" and triggers a JIT compilation, the mutator
thread continues executing the AOT version. The JIT runs asynchronously
on a background compiler thread. Once ready, a safe-point patch swaps
the function pointer.

### C.2 — Thread-Local Allocation for Mutators

Mutator threads use thread-local bump pointers (lexical regions) for
their own runtime allocations. Global synchronization happens only at
explicit yield points.

### C.3 — Compiler Threads Never Block on Mutator State

The compiler works on a frozen snapshot of the IR. Mutator updates after
the snapshot are picked up by the next compilation.

### C.4 — Epoch-Based Reclamation

Old JIT code and IR nodes are reclaimed using epoch-based garbage
collection. When the optimizer replaces a `Node`, the old node is tagged
with an epoch. Once all threads advance past that epoch, the memory is
bulk-freed. This avoids both locks and use-after-free.

---

## The Numbered Rules (36–60)

Compiler bugs are uniquely expensive. These rules make entire categories
of bugs impossible.

### Rule 36 — Five regression tests per bug fix

Every bug fix must include at least **5 regression tests**:

1. **Minimal reproducer** — smallest input triggering the bug.
2. **Variant trigger** — different code pattern, same root cause.
3. **Boundary/negative** — ensures the fix doesn't over-correct (e.g.
   doesn't disable PGO entirely).
4. **Integration/contextual** — bug in realistic surrounding code.
5. **Deopt/State Reconstruction** — verifies that if the JIT speculates
   wrongly, the deopt to AOT produces the exact same state.

**Enforcement:** CI fails if a PR labeled `bugfix` has fewer than 5 new
test cases.

### Rule 37 — Golden tests for every pass

Every optimization pass must have ≥10 golden IR tests. Checked-in
`.in.aegis` / `.expected.son` file pairs. Tests must run in both
**Static Mode** (AOT) and **Profile Mode** (JIT).

### Rule 38 — Differential testing is mandatory in CI

`AOT Baseline` ↔ `JIT Apex` ↔ `Reference Interpreter` comparisons run on
every PR. Divergence blocks merge. Assert byte-for-byte identical results
and memory layouts.

### Rule 39 — Deopt paths must be fuzzed weekly

Scheduled CI job. Results triaged within 24 hours. Untriaged deopt fuzz
failures block releases.

### Rule 40 — Replay logs retained for all CI failures

Failed test runs automatically save full compile replay artifacts
(IR snapshot + PGO profile + compiler options + RNG seed). Debugging
starts from replay, not reproduction.

### Rule 41 — Performance regressions require explicit waiver

If a benchmark regresses >5%, the PR must include root cause analysis,
justification, a tracking issue, and approval. No silent performance
degradation.

### Rule 42 — Graph verifier runs in debug builds after *every* pass

Not optional. The verifier checks:

- no dangling `NodeId`s,
- **Effect chain continuity** (no `Pure` node dominated by a `Crowded`
  node without a proper effect edge),
- control dominance,
- use-def consistency,
- `FrameState` attached to every PGO-driven guard.

### Rule 43 — Test names encode the bug/feature they cover

Bad: `test_pea_3`.
Good: `pea_non_escaping_region_object_with_deopt_materializes_correctly`.
Searchable, self-documenting.

### Rule 44 — No assumption without invalidation

Every PGO-driven assumption must have a registry entry (Watchdog), an
invalidation path (Trip), and a fallback to static proof.

### Rule 45 — No specialization without fallback

Every specialized clone (e.g. a bounds-check-eliminated loop) must have a
generic fallback, a deopt path, and a budget limit.

### Rule 46 — No profile data without confidence

Profile data must include sample count, stability, age, decay, variance,
and deopt correlation (Meter). Low-confidence data must not trigger
aggressive speculation.

### Rule 47 — No aggressive pass without a cost model

Inlining, cloning, unrolling, SLP vectorization, and PEA materialization
must all use a strict cost model (Regulator) based on target hardware
latencies.

### Rule 48 — No FFI optimization without ABI proof

FFI optimizations must prove calling convention correctness, stack
alignment, register clobbering, and memory ownership transfer.

### Rule 49 — No vectorization without dependence proof

Vectorization (SLP or loop) must prove no aliasing (or use versioned
checks), bounds safety, alignment, and correct scalar fallback.

### Rule 50 — No persistent state without versioning

Profile caches, code caches, and AOT artifacts must be versioned. A
change in the IR format or pass order invalidates the cache.

### Rule 51 — All orthogonal boolean state must be bitmasked

Any set of independent boolean properties on a hot-path data structure
(e.g. `NodeFlags`, `EffectTags`) must be represented as a bitmask with
type-safe `Flags<E>` wrappers. Raw integers are forbidden for flag-like
state.

### Rule 52 — No easy fixes — only correctness-preserving performance fixes

When fixing a bug, you must implement the fix that *simultaneously*
preserves performance and correctness. "Easy" fixes that sacrifice either
property are forbidden unless explicitly documented as temporary
mitigations with tracking issues and removal deadlines.

### Rule 53 — Index-Based Graph (No Raw Pointers in the IR)

**Never use raw pointers (`Node*`) for edges in the Sea of Nodes.**

- **The Law:** All node references must use a 32-bit integer index
  (`using NodeId = uint32_t;`).
- **The Why:** Pointers are 8 bytes. Indices are 4 bytes. This cuts the
  memory footprint of the graph's edges in half, effectively doubling the
  number of nodes that fit in the L1/L2 cache. It also makes the entire
  IR trivially serializable, relocatable, and immune to pointer
  invalidation during arena reallocation.

### Rule 54 — Interned Symbols (No Strings in the Hot Path)

**Never pass, compare, or store `std::string` or `std::string_view` in
the IR or passes.**

- **The Law:** All identifiers, variable names, and type names must be
  interned into a global `SymbolTable` at the frontend. The IR must only
  use a `SymbolId` (a `uint32_t` index).
- **The Why:** String comparison is O(N) and causes cache misses.
  `SymbolId` comparison is a single CPU cycle integer comparison. It
  reduces the size of identifier references from 24+ bytes (for
  `std::string`) to 4 bytes.

### Rule 55 — Cache-Friendly Hash Maps (Ban `std::unordered_map`)

**`std::unordered_map` and `std::map` are forbidden in the compiler hot
path.**

- **The Law:** For Global Value Numbering (GVN), Hash-Consing, and any
  pass requiring a hash table, you must use a cache-friendly,
  open-addressing hash map (e.g. a SwissTable implementation like
  Abseil's `flat_hash_map` or a custom C++26 equivalent).
- **The Why:** `std::unordered_map` allocates a new heap node for every
  insertion, destroying your `std::pmr` arena strategy and causing
  catastrophic pointer-chasing cache misses. A SwissTable stores keys
  and values contiguously in memory, keeping the hash table in the CPU
  cache.

### Rule 56 — Sparse Sets and BitVectors for Pass Data

**Ban `std::set`, `std::unordered_set`, and `std::vector<bool>` for
dataflow analysis.**

- **The Law:** When passes need to track sets of `NodeId`s (e.g.
  liveness analysis, dominator trees, visited nodes), they must use
  **Sparse Sets** (for small, dense sets) or **BitVectors** (for large,
  sparse sets).
- **The Why:** `std::set` allocates a tree node per element (memory
  fragmentation + pointer chasing). `std::vector<bool>` is a
  specialized, bit-packed container that is notoriously slow due to
  proxy objects and lack of contiguous memory access. A Sparse Set
  gives O(1) insert, delete, and clear, with zero allocation after the
  initial setup.

### Rule 57 — Small Buffer Optimization (SBO) for Variable-Length Data

**Ban `std::vector` for data that usually has 1 to 4 elements.**

- **The Law:** For Use-Def chains, instruction operands, and basic block
  predecessors/successors, use a `SmallVector<T, N>` (where `N` is
  typically 2, 3, or 4).
- **The Why:** 90% of instructions have 1 to 3 operands. Allocating a
  heap-backed `std::vector` for every instruction destroys throughput. A
  `SmallVector` stores the first `N` elements inline inside the object
  itself (on the stack or inside the arena). It only falls back to
  heap/arena allocation if the size exceeds `N`.

### Rule 58 — Exploit C++26 Compiler Hints (`[[assume]]`, `[[likely]]`)

**Do not rely on the compiler to guess hardware realities.**

- **The Law:**
  - Use `[[likely]]` and `[[unlikely]]` on all PGO-driven branches and
    deoptimization traps.
  - Use C++23/26 `[[assume(condition)]]` to tell the compiler about
    invariants (e.g. `[[assume(node_id < graph.size())]]`) to eliminate
    bounds checks in internal compiler data structures.
- **The Why:** The compiler cannot always prove that an index is in
  bounds or that a deopt path is cold. Explicit hints allow the backend
  to optimize branch prediction layouts and remove redundant safety
  checks in the compiler's own C++ code.

### Rule 59 — Structure of Arrays (SoA) for Bulk Pass Processing

**When a pass needs to process a specific field of millions of nodes, do
not iterate over the nodes.**

- **The Law:** For passes that require massive parallel processing of a
  single attribute (e.g. calculating types, computing liveness), extract
  that attribute into a contiguous `std::pmr::vector` (SoA layout)
  rather than iterating over the `Node` structs (AoS layout).
- **The Why:** Iterating over an array of `Node` structs to read just
  one `uint32_t` field means you are loading 60 bytes of useless data
  into the cache line for every 4 bytes of useful data. Extracting it
  into a contiguous array of `uint32_t` allows the CPU to prefetch
  perfectly and utilize SIMD vectorization on the compiler's own passes.

### Rule 60 — Zero-Cost Error Propagation (`std::expected` over `if` chains)

**Do not use verbose `if (err)` chains that ruin branch prediction.**

- **The Law:** Use `std::expected<T, Error>` and the proposed C++26
  `std::expected` monadic operations (`and_then`, `transform`) or a
  custom `TRY()` macro that compiles down to a single branch.
- **The Why:** In a compiler, errors are rare. The success path must be
  completely linear and branchless to keep the CPU pipeline full.
  `std::expected` allows the compiler to place the error-handling code
  in a completely separate, cold memory section, keeping the hot path
  instruction cache pristine.
