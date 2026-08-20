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

---

## Rules 61–76 — Anti-Slop and Robustness Laws

Slop is any code pattern that sacrifices correctness, maintainability, or
performance for short-term convenience. Slop is technical debt with
compound interest. The following rules make slop impossible.

### Rule 61 — No Hard-Coded Constants in Optimization Logic

Magic numbers are forbidden.

- **The Law:** Every threshold, budget, limit, and heuristic constant
  used in any optimization pass (e.g. unroll factor, inline threshold,
  SLP vector width, PGO confidence cutoff) MUST be defined as a named,
  documented `constexpr` constant or configuration parameter.
- **The Why:** Hard-coded constants (`if (count > 4)`, `budget = 100`)
  are unmaintainable, untestable, and impossible to tune via PGO or
  target-specific profiles. They hide assumptions that will inevitably
  become wrong on new hardware or with new workloads.
- **Enforcement:** CI static analysis fails if numeric literals > 2
  appear in pass logic without a named constant reference.

### Rule 62 — No "Small Bug" or "Minor Edge Case" Rationalization

All defects are treated as critical until proven otherwise.

- **The Law:** The phrases "small bug," "minor edge case," "rarely
  happens," "only affects cold paths," and "good enough for now" are
  banned from code reviews, commit messages, issue trackers, and verbal
  discussions.
- **The Why:** In a systems compiler, "small" bugs cause silent data
  corruption, security vulnerabilities, or catastrophic performance
  cliffs. What seems like an edge case today becomes the hot path
  tomorrow when PGO shifts. Minimizing defects erodes trust and
  accumulates invisible technical debt.
- **Enforcement:** Any PR or issue using minimizing language is
  automatically rejected. All bugs must be triaged with explicit
  severity based on potential impact, not perceived frequency.

### Rule 63 — No Target-Specific Hacks in Generic Passes

Passes must be target-agnostic.

- **The Law:** Mid-level and research passes (GVN, LICM, SLP, CFL-Alias)
  MUST NOT contain `#ifdef X86` or target-specific conditionals. All
  target knowledge must be abstracted behind the `Target` interface and
  queried via cost models or capability flags.
- **The Why:** Hard-coding target assumptions in generic passes destroys
  portability, makes testing exponentially harder, and prevents the
  compiler from adapting to new hardware. If a pass behaves differently
  on ARM vs x86, it must be because the `Target` interface reported
  different capabilities/costs, not because of an `#ifdef`.
- **Enforcement:** Code review checklist item. Violations block merge.

### Rule 64 — No Heuristics Without Empirical Validation

Gut feelings are not engineering.

- **The Law:** Every heuristic (e.g. "inline if size < 50 nodes," "unroll
  if trip count < 8") MUST be backed by:
  - Benchmark data showing measurable improvement.
  - A mechanism to override/tune it (via config or PGO).
  - Documentation explaining why this value was chosen.
- **The Why:** Untuned heuristics are the #1 source of performance
  regressions and missed optimizations. They encode outdated assumptions
  about hardware that rot over time.
- **Enforcement:** PRs introducing or modifying heuristics without
  benchmark evidence and tuning hooks are rejected.

### Rule 65 — No Silent Fallbacks Without Telemetry

Degradation must be observable.

- **The Law:** When the JIT falls back to AOT, when a speculative guard
  fails, when a pass skips an optimization due to budget/proof failure,
  or when regalloc spills excessively, the event MUST be recorded in
  telemetry/profile data.
- **The Why:** Silent fallbacks hide performance problems. If the JIT is
  constantly deopting but nobody knows, you're running at AOT speed
  while paying JIT overhead. Telemetry turns invisible failures into
  actionable PGO signals.
- **Enforcement:** All fallback/deopt/skip paths must emit a telemetry
  event. Missing telemetry blocks merge.

### Rule 66 — No Assumption of Stable Hardware

Hardware evolves; your compiler must adapt.

- **The Law:** No pass may assume fixed cache line sizes, SIMD widths,
  branch predictor behavior, or memory latency ratios. All hardware
  parameters MUST be queried at runtime (for JIT) or build-time (for AOT)
  via the `Target` interface.
- **The Why:** Code optimized for Skylake cache lines will be suboptimal
  on Sapphire Rapids. Vectorization tuned for AVX2 will leave AVX-512
  performance on the table. Hard-coded hardware assumptions guarantee
  obsolescence.
- **Enforcement:** Static analysis flags hardcoded cache/SIMD/latency
  constants. Must use `Target::cache_line_size()`, `Target::max_simd_width()`,
  etc.

### Rule 67 — No Optimization Without Measurable Win

Complexity must pay rent.

- **The Law:** Every optimization pass added to the pipeline MUST
  demonstrate a ≥1% geometric mean improvement across the benchmark
  suite, OR enable a correctness/safety property that cannot be achieved
  otherwise. Passes that don't meet this bar are removed.
- **The Why:** Compiler complexity is a liability. Each pass adds
  compile-time cost, maintenance burden, and bug surface area. If an
  optimization doesn't measurably improve output, it's dead weight.
- **Enforcement:** Quarterly audit of all passes. Underperforming passes
  are deprecated and removed unless justified by non-performance criteria
  (e.g. safety).

### Rule 68 — No Workarounds for Compiler/Runtime Bugs

Fix the root cause, always.

- **The Law:** Adding code to "work around" a bug in the compiler,
  runtime, or standard library is forbidden. The underlying defect MUST
  be fixed. Temporary mitigations require a tracking issue, a removal
  deadline ≤ 2 weeks, and explicit approval from the tech lead.
- **The Why:** Workarounds compound. They mask root causes, create
  fragile dependencies, and eventually become permanent undocumented
  behavior. In a systems language, working around a memory safety bug is
  indistinguishable from introducing one.
- **Enforcement:** PRs containing workaround comments (`// HACK`,
  `// WORKAROUND`, `// TODO: fix later`) without approved tracking
  issues are rejected.

### Rule 69 — No Implicit Conversions or Coercions in IR

Type safety is non-negotiable at every level.

- **The Law:** The E-SoN IR MUST NOT perform implicit type conversions,
  integer promotions, or pointer coercions. All conversions must be
  explicit nodes (`IntToPtr`, `SExt`, `ZExt`, `BitCast`). The frontend
  lowering pass inserts these explicitly.
- **The Why:** Implicit conversions in the IR hide semantic intent, make
  effect analysis unreliable, and create subtle bugs when passes assume
  type equivalence. Explicit conversions make the IR self-documenting
  and verifiable.
- **Enforcement:** Graph verifier (Rule 42) rejects any node with
  mismatched operand types lacking an explicit conversion node.

### Rule 70 — No Documentation Debt

Undocumented code is broken code.

- **The Law:** Every public API, every pass, every IR node kind, and
  every configuration parameter MUST have documentation explaining:
  - What it does.
  - Why it exists.
  - What invariants it assumes/enforces.
  - Known limitations or trade-offs.
- **The Why:** In a complex compiler, tribal knowledge kills velocity.
  New contributors waste weeks reverse-engineering undocumented behavior.
  Undocumented invariants lead to violated invariants.
- **Enforcement:** CI docs check fails if public symbols lack doc
  comments. PRs modifying pass behavior without updating docs are
  rejected.

### Rule 71 — Document Everything, Without Exception

Undocumented code is defective code.

- **The Law:** Every public API, internal helper, IR node, pass,
  configuration knob, and non-obvious algorithm MUST have documentation
  at the point of definition. Documentation must cover:
  - **Purpose:** What this exists to accomplish.
  - **Invariants:** What conditions must be true before/after execution.
  - **Rationale:** Why this approach was chosen over alternatives.
  - **Edge Cases:** Known limitations, failure modes, or special handling.
  - **Cross-References:** Links to related passes, IR specs, or research
    papers.
- **The Why:** Tribal knowledge is a single point of failure.
  Undocumented invariants are violated invariants. Future maintainers
  (including future you) cannot safely modify what they do not understand.
  Documentation is not optional overhead; it is part of the implementation.
- **Enforcement:** CI documentation linter fails on undocumented public
  symbols. PRs modifying complex logic without updating corresponding docs
  are rejected. Stale documentation is treated as a bug with the same
  severity as stale code.

### Rule 72 — No Lazy Logic

Convenience is not a design principle.

- **The Law:** The following patterns are strictly forbidden:
  - String parsing/matching where structured data exists.
  - Linear search where hash/index lookup is appropriate.
  - Redundant recomputation where caching/memoization is feasible.
  - Overly broad `try/catch` or error swallowing instead of precise
    error handling.
  - Copy-paste code instead of proper abstraction.
  - "Works for now" conditionals that encode unstated assumptions.
  - Using generic containers (`std::map`, `std::vector`) where
    domain-specific structures (`SparseSet`, `SmallVector`,
    `FlatHashMap`) are warranted.
- **The Why:** Lazy logic is technical debt with compound interest. It
  degrades performance, obscures intent, and creates fragile coupling.
  Every shortcut taken during implementation becomes a multiplier on
  future debugging and optimization effort. In a systems compiler,
  laziness directly translates to slower generated code and longer
  compile times.
- **Enforcement:** Code review checklist explicitly screens for lazy
  patterns. Violations block merge. Refactoring tickets created
  immediately when lazy logic is discovered in existing code.

### Rule 73 — No Fragile Implementations

Robustness is a first-class requirement, not an afterthought.

- **The Law:** All implementations MUST be resilient to:
  - Malformed or unexpected input (fail explicitly, never silently).
  - Concurrent access (thread-safe by design or explicitly documented
    as unsafe).
  - Resource exhaustion (graceful degradation, not crashes).
  - Platform/hardware variation (no implicit assumptions about
    endianness, alignment, cache size, etc.).
  - Future changes (loose coupling, stable interfaces, versioned formats).
- Fragile patterns are forbidden:
  - Implicit ordering dependencies between passes.
  - Global mutable state.
  - Pointer arithmetic without bounds validation.
  - Format/layout assumptions not enforced by `static_assert`.
  - Error codes or sentinel values that can be confused with valid data.
- **The Why:** Fragile code works until it doesn't. Failures manifest
  as silent corruption, intermittent crashes, or platform-specific bugs
  that are exponentially harder to diagnose. In a JIT compiler, fragility
  means security vulnerabilities and unpredictable runtime behavior.
  Robustness is not extra work; it is the minimum viable implementation.
- **Enforcement:** All new code must include explicit robustness tests
  (malformed input, boundary conditions, concurrent stress). Graph
  verifier (Rule 42) extended to detect fragile IR patterns. Static
  analysis flags global mutable state and unchecked pointer arithmetic.

### Rule 74 — No Deletion-by-Avoidance ("Too Hard" Is Not a Valid Reason)

Difficulty is a signal to invest, not to abandon.

- **The Law:** Deleting, disabling, commenting out, or stubbing
  functionality because it is "too hard," "too complex," "not worth the
  effort," or "we'll do it later" is strictly forbidden. When
  encountering difficult problems:
  - **Decompose:** Break the problem into tractable sub-problems.
  - **Research:** Consult literature, prior art, and team expertise.
  - **Prototype:** Build minimal proofs-of-concept to validate approaches.
  - **Document:** Record the difficulty, attempted solutions, and open
    questions.
  - **Escalate:** Raise blocking issues with clear problem statements
    and proposed paths forward.
- Temporary mitigations require:
  - Approved tracking issue with explicit scope.
  - Removal deadline ≤ 2 sprints.
  - Telemetry to measure impact of the gap.
  - Tech lead sign-off.
- **The Why:** Avoidance compounds. Deleted features become permanent
  gaps. Stubbed implementations become silent failures. "We'll do it
  later" becomes "we never did it." Difficulty indicates either
  insufficient understanding, inadequate tooling, or genuine complexity
  that requires architectural investment. None of these are solved by
  deletion. A compiler that avoids hard problems produces mediocre
  output.
- **Enforcement:** PRs removing/disabling functionality without
  approved mitigation plan are rejected. Code comments containing
  "TODO: too hard," "FIXME: complex," "HACK: temporary" without tracking
  issues trigger automatic review escalation. Quarterly audit of all
  mitigation tickets; overdue items block releases.

### Rule 75 — No Implicit Knowledge Transfer

If it isn't written down, it doesn't exist.

- **The Law:** All design decisions, trade-offs, historical context,
  and operational knowledge MUST be captured in persistent, searchable
  documentation (code comments, ADRs, wiki, specs). Oral tradition, chat
  messages, meeting notes, and individual memory are not valid knowledge
  stores.
- **The Why:** People leave. Memories fade. Chat logs are unsearchable.
  Implicit knowledge creates bus factors and onboarding cliffs. Every
  piece of knowledge that exists only in someone's head is a liability.
  Documentation is the mechanism by which organizational knowledge
  survives personnel changes.
- **Enforcement:** Architecture Decision Records (ADRs) required for
  all significant design choices. Onboarding checklist verifies
  documentation completeness. "Ask Bob" is not an acceptable answer to
  "how does X work?"

### Rule 76 — No Premature Simplification

Correctness precedes elegance.

- **The Law:** Do not simplify, abstract, or generalize code until:
  - The full problem space is understood.
  - At least two concrete use cases exist.
  - The abstraction has been validated against real requirements.
- Premature simplification includes:
  - Abstracting before understanding all edge cases.
  - Generalizing based on a single example.
  - Removing "redundant" checks that guard against unknown failure modes.
  - Collapsing similar-but-distinct code paths without proving
    equivalence.
- **The Why:** Premature simplification creates leaky abstractions that
  fail under real-world conditions. It encodes incomplete understanding as
  structural constraints. Undoing bad abstractions is harder than
  building correct ones incrementally. In a compiler, premature
  simplification manifests as missed optimizations, incorrect codegen,
  and fragile passes.
- **Enforcement:** Code review screens for premature abstraction. New
  abstractions require justification with ≥2 concrete consumers.
  Simplification PRs must demonstrate preserved correctness via
  differential testing.

---

## Section D — Anti-Slop Laws

Slop is any code pattern that sacrifices correctness, maintainability,
or performance for short-term convenience. Slop is technical debt with
compound interest. The following patterns are strictly forbidden.

### D.1 — No Magic Numbers or Unnamed Constants

Every numeric literal must have a name and a reason.

- Raw integers, floats, or booleans in logic are forbidden (except `0`,
  `1`, `true`, `false` in trivially obvious contexts).
- All thresholds, sizes, alignments, counts, and offsets must be named
  `constexpr` constants with documentation explaining why that specific
  value was chosen.
- Constants must be scoped to their domain (e.g.
  `abi::sysv::kStackAlignment`, not `STACK_ALIGN`).
- **Enforcement:** Static analysis flags numeric literals > 2 in
  pass/backend logic. PRs with unnamed constants are rejected.

### D.2 — No Copy-Paste Code or Structural Duplication

Duplication is a defect, not a shortcut.

- If two code blocks share structure, extract a helper, template, or
  data-driven approach.
- ABI definitions, register lists, and pass boilerplate must use
  generators, `constexpr` helpers, or declarative tables.
- Copy-pasting and modifying introduces divergence bugs that are
  invisible until they corrupt output.
- **Enforcement:** Code review checklist screens for duplication. New
  duplicated code blocks merge. Existing duplication is tracked as tech
  debt with removal deadlines.

### D.3 — No Silent Fallbacks or Default Returns

Invalid input must fail loudly, never silently.

- Switch statements on closed enums must be exhaustive.
  Non-exhaustive switches require `[[assume(false)]]` +
  `AEGIS_UNREACHABLE()`.
- Functions must not return arbitrary default values (`return 0;`,
  `return nullptr;`) when input is invalid. Use `std::expected`,
  `std::optional`, or explicit error returns.
- Silent fallbacks hide bugs. A wrong default return value propagates
  corruption through the entire pipeline.
- **Enforcement:** `-Wswitch-enum -Werror` enabled. Static analysis
  flags non-exhaustive switches and suspicious default returns.

### D.4 — No Lazy Data Structures or Algorithms

Use the right tool, not the convenient tool.

- `std::unordered_map` / `std::map` are forbidden in hot paths. Use
  cache-friendly alternatives (`FlatHashMap`, `SwissTable`).
- `std::vector` is forbidden for small, bounded collections. Use
  `SmallVector<T, N>` or inline arrays.
- Linear search is forbidden where O(1) lookup is feasible. Use hash
  maps, bitsets, or indexed arrays.
- String comparison is forbidden where symbol IDs suffice. Intern all
  identifiers.
- **Enforcement:** Code review checklist bans prohibited containers in
  hot paths. Performance regression tests catch lazy algorithm choices.

### D.5 — No Implicit Assumptions or Undocumented Invariants

If it isn't stated, it doesn't exist.

- All preconditions, postconditions, and invariants must be documented at
  the point of definition.
- Assumptions about hardware, ABI, IR structure, or pass ordering must
  be validated via `static_assert`, runtime checks, or graph verifier
  rules.
- Implicit assumptions rot. When hardware changes or passes are
  reordered, undocumented assumptions become silent bugs.
- **Enforcement:** Graph verifier (Rule 42) extended to check documented
  invariants. Missing invariant documentation blocks merge.

### D.6 — No Premature Abstraction or Over-Generalization

Abstractions must be earned, not anticipated.

- Do not abstract before understanding ≥2 concrete use cases.
- Do not generalize based on a single example or speculative future need.
- Do not create interfaces "just in case." YAGNI (You Aren't Gonna Need
  It) applies ruthlessly.
- Premature abstractions leak, constrain, and complicate. They encode
  incomplete understanding as structural debt.
- **Enforcement:** New abstractions require justification with ≥2
  consumers. Single-use abstractions are rejected.

### D.7 — No Workarounds, Hacks, or Temporary Fixes Without Tracking

Every compromise must be visible, bounded, and scheduled for removal.

- Comments containing `HACK`, `TODO`, `FIXME`, `WORKAROUND`,
  `TEMPORARY` are forbidden without an approved tracking issue and
  removal deadline ≤ 2 sprints.
- Workarounds for compiler/runtime/hardware bugs must include telemetry
  to measure impact and validate fix effectiveness.
- Untracked workarounds become permanent. They mask root causes and
  accumulate into unmaintainable spaghetti.
- **Enforcement:** CI scans for untracked workaround comments. Overdue
  mitigation tickets block releases.

### D.8 — No Target-Specific Logic in Generic Code

Portability is enforced by architecture, not discipline.

- Mid-level passes, IR definitions, and shared utilities must not
  contain `#ifdef TARGET_X86` or equivalent.
- All target knowledge must flow through the `Target` interface via
  capability queries and cost models.
- Target-specific hacks in generic code destroy testability,
  portability, and maintainability. They create N×M testing matrices.
- **Enforcement:** Static analysis flags target macros in non-backend
  directories. Violations block merge.

### D.9 — No Untested or Unverified Code Paths

Untested code is broken code.

- Every branch, edge case, and error path must have explicit test
  coverage.
- Golden tests required for all IR transformations. Differential
  testing required for all output-generating paths.
- "It compiles" is not verification. "It works on my machine" is not
  verification. Only automated, reproducible tests count.
- **Enforcement:** CI coverage gates. PRs adding new code paths without
  corresponding tests are rejected.

### D.10 — No Performance-Agnostic Implementation

Every line of hot-path code must justify its cost.

- Hot-path code must avoid allocations, exceptions, RTTI, virtual
  dispatch, and cache-unfriendly patterns.
- All hot-path data structures must be sized for cache lines. All
  hot-path loops must be vectorizable or provably optimal.
- Performance is a feature. Ignoring it in implementation guarantees
  degradation.
- **Enforcement:** Benchmark suite runs on every PR. Regressions >5%
  require waiver. Profiling-guided reviews for hot-path changes.

---

## Slop Detection Checklist (For Code Review)

Every PR reviewer must verify:

- [ ] No unnamed numeric constants in logic
- [ ] No duplicated code blocks or copy-paste patterns
- [ ] No silent fallbacks or unsafe default returns
- [ ] No prohibited containers (`std::unordered_map`, `std::vector` for
      small collections) in hot paths
- [ ] All invariants documented and validated
- [ ] No premature abstractions without ≥2 consumers
- [ ] No untracked workarounds or `HACK` comments
- [ ] No target-specific logic outside `backend/`
- [ ] All new code paths have test coverage
- [ ] Hot-path changes justified with profiling/benchmarks

Failure on any item blocks merge. No exceptions. No "small slop." No
"we'll fix it later." Slop is banned.
