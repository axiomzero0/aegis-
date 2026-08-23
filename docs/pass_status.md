# Aegis Pass Status — per-pass audit

**Status:** Stable
**Owner:** Aegis Systems Dev Team
**Last audited:** 2026-08-23

Every optimization pass in `compiler/passes/`, classified by what it
does TODAY, with the exact blocking dependency for anything less than
a full IR rewrite (Rules 70/71: documented, not tribal; Rule 74: gaps
are named, never hidden behind no-op silence — every analysis-stage
pass emits telemetry with its numbers per Rule 65).

Legend:
- **REWRITE** — performs real, verified IR transformations.
- **ANALYSIS** — computes and reports facts; no rewrite is possible
  or profitable in this IR yet (the reason is stated).
- **TAG+FUTURE** — annotates nodes for a downstream stage that is the
  documented blocking dependency.
- **JIT-MODE** — performs its rewrite only under PGO/JIT budgets
  (guard + FrameState installation per Rules A.3/A.5).

## Mid-level pipeline (`passes/mid/`) — runs in `build_standard_pipeline()`

| Pass | Status | What it does today | Blocking dependency (if any) |
|------|--------|--------------------|------------------------------|
| SCCP | REWRITE | Constant folding + propagation (binops incl. unsigned ops/shifts, unary Neg/Not/BitNot, phi meet semantics; unknown kinds never fold) | — |
| StrengthReduction | REWRITE | x*2^k→shl, x/2^k→lshr, identities (x+0, x*1, x-x, x&0, x^x, ...) | — |
| CopyPropagation | REWRITE | Identity copies, identical-input Select, phi(a,a)→a (region slot skipped BY POSITION) | — |
| GVN | REWRITE | Value numbering of Pure expressions via hash-cons | — |
| CSE | REWRITE | Effect-sensitive redundancy elimination on the effect chain (loads); Pure parts ride GVN | — |
| SCEV | ANALYSIS | Induction recurrences {start, step, trip}; feeds IVS, LoopFusion, LoopUnrolling | Full transfer functions (mulrec chains) — not needed by current consumers |
| LICM | ANALYSIS | Proves per-loop invariant Pure computations + counts blocked Load/Store hoists | Pure nodes are position-free in a sea of nodes (nothing to move); Altered hoisting needs CFLAlias integrated as a may_alias query |
| InductionVarSimplification | REWRITE | **Derived-IV strength reduction**: `primary_iv * K` inside a loop becomes a new induction `j = phi(i0*K, j + step*K)` — the per-iteration multiply is eliminated (soundness proof in source) | — |
| LoopFusion | REWRITE | Eliminates degenerate SCEV-paired loops (incl. nested-inner case) with live-body soundness gate | Fusing loops with REAL bodies requires dependence analysis (may_alias) |
| LoopUnrolling | REWRITE | Full elimination of degenerate constant-trip loops (≤ kLoopUnrollFullUnrollTripCount), incl. nested collapse after fusion | Partial unrolling (duplication machinery + remainder loop) — budget-guarded skip, telemetry `partial_unroll_not_implemented` |
| LoopFission | TAG+FUTURE | Skips with telemetry (needs multi-statement loop bodies worth splitting) | Source loops carry one accumulator today; fission needs ≥2 independent statement groups + cost model (Rule 47) |
| BoundsCheckElim | TAG+FUTURE | Skips | No source-level indexing/bounds checks lowered yet (frontend arrays feature) |
| DeadStoreElimination | TAG+FUTURE | Skips | No source-level stores (memory model feature) |
| EscapeAnalysis | TAG+FUTURE | Tags non-escaping Allocs for stack promotion | No source-level allocations (structs/boxes feature) |
| NullPointerElimination | TAG+FUTURE | Strips null-check guards on Alloc results | Depends on Alloc (see above) |
| RCOptimization | TAG+FUTURE | Merges redundant RC inc/dec pairs | RC ops not lowered yet (ownership runtime feature) |
| TCO | TAG+FUTURE | Tags direct tail calls for the backend | Backend call emission (calls not yet encoded) |
| SimplifyControl | REWRITE | Single-pred region collapse; constant-branch pruning **with phi collapse to the taken side** | — |
| EDCE | REWRITE | Effect-aware dead-code sweep (always last) | — |

## Research pipeline (`passes/research/`) — opt-in via `aegisc --research`

| Pass | Status | What it does today | Blocking dependency |
|------|--------|--------------------|---------------------|
| CFLAliasAnalysis | ANALYSIS | Abstract-location points-to + CFL-reachability facts | Consumed by LICM/reordering once the query interface lands (needs Load/Store) |
| ValueFlowAnalysis | ANALYSIS | Allocation-site stamping + value-flow sets | Alloc nodes (frontend feature) |
| PGDLO | JIT-MODE | Tags Allocs for profile-driven layout at emit | Alloc + struct layout table + PGO profile plumbing |
| MemPoolSynthesis | TAG+FUTURE | Skips | Loop-carried malloc/free pairs (alloc feature) |
| CacheObliviousLayout | TAG+FUTURE | Skips | Container types (arrays/structs-of-arrays) |
| SLPVectorization | TAG+FUTURE | Tags independent same-kind groups ≥ kSlpMinPackableNodes (dependence-checked) | `VectorOp` NodeKind + SIMD selection in the backend |
| AutoParallelization | TAG+FUTURE | Skips | Fork/join runtime + thread-safety proofs (Rules C.1-C.3 machinery exists in runtime/sync) |
| GuardedDevirtualization | JIT-MODE | Installs guard + FrameState on monomorphic call sites | Inline caches at call emission |
| SpeculativeBCE | JIT-MODE | Guard + FrameState on bounds-check patterns | Bounds checks (see BCE) |
| SpeculativeEffectReordering | JIT-MODE | Guard + FrameState on hoistable loads | Loads + alias query |
| SpeculativeLockElision | JIT-MODE | Guard + FrameState on uncontended critical sections | Crowded-call lowering |
| BOLTLayout | ANALYSIS | Graph level is a no-op BY DESIGN (post-link pass) | Linker stage |

## Backend-executable status (context)

Straight-line integer arithmetic AND branch merges execute natively
(ExecEncoder: full ALU/div/shift/setcc + branchless select via
mov/test/cmovne; `bench_runtime` proves correctness against an AST
interpreter and measures 13–22x over interpretation). Not yet
executable: loops (back-edge jumps), calls, memory operations — each
rejected loudly by the encoder and by the harness's no-silent-omission
guard, never silently dropped.
