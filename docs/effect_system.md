# Aegis Effect System

**Status:** Stable
**Owner:** Aegis IR Dev Team
**Last Updated:** 2026-08-21
**Related Sections:** `ir_spec.md`, `laws.md`

---

## 1. The Three Effect Classes

The Aegis IR tags every value-producing node with exactly one of three
effect classes. The programmer never writes these keywords — the
compiler's frontend infers them by inspecting the AST.

### Pure

A Pure node only reads its arguments and local immutable variables,
returns a value, and does not call any `Crowded` or `Altered` function.
Pure nodes may be freely reordered, constant-folded, vectorized, and
CSE'd against each other.

### Altered

An Altered node writes to mutable memory — through a `&mut` reference,
a mutable global, or a `var` local. Altered nodes participate in the
effect chain: their order is observable and they cannot be reordered
past each other unless provably non-aliasing.

### Crowded

A Crowded node synchronizes with the outside world — I/O, atomics,
thread sync, FFI calls. Crowded nodes are never reordered past
anything (they imply a full memory barrier) and they are roots for the
E-DCE pass.

---

## 2. Effect Inference (Frontend)

The compiler walks the AST of each function and computes:

1. **Pure Inference** — If a function only reads its arguments and
   immutable locals, and calls only Pure functions, the compiler tags
   it as `Pure`. Pure functions can be constant-folded and run at
   compile time.

2. **Altered Inference** — If a function writes through a `&mut`
   reference, a mutable global, or a `var` local, the compiler tags it
   as `Altered`.

3. **Crowded Inference** — If a function calls any function in
   `std.io`, `std.atomic`, `std.thread`, or any FFI function, the
   compiler tags it as `Crowded`.

The inference walks every call site. The callee's inferred effect
class determines the call node's `NodeKind`:

- `Pure` callee → `CallPure` node (Pure effect)
- `Altered` callee → `CallAltered` node (Altered effect)
- `Crowded` callee → `CallCrowded` node (Crowded effect)

---

## 3. Effect Tags (Fine-grained, Rule 51)

Each coarse effect class can be subdivided into specific observable
operations. These are stored as a bitmask (`EffectTags`) on the node:

| Tag | Coarse class | Meaning |
|-----|--------------|---------|
| `WritesMemory` | Altered | mutates a memory location |
| `ReadsMemory` | Altered | reads a memory location that may be mutated elsewhere |
| `Allocates` | Altered | allocates heap memory |
| `Frees` | Altered | frees heap memory |
| `MutatesReference` | Altered | writes through `&mut` |
| `IoWrite` | Crowded | writes to a file / network / stderr |
| `IoRead` | Crowded | reads from a file / network / stdin |
| `AtomicAccess` | Crowded | `std::atomic` load/store/rmw |
| `ThreadSync` | Crowded | mutex / condvar / channel |
| `FfiCall` | Crowded | FFI call (must respect C ABI) |
| `MayDeoptimize` | Crowded | emits a guard that may deopt |
| `MayTrap` | Crowded | integer div-by-zero, etc. |
| `MayOverflow` | Pure (analysis only) | arithmetic may overflow |
| `MayBeNaN` | Pure (analysis only) | float result may be NaN |

---

## 4. The Effect Chain

Every Altered/Crowded node has an `eff_in` input pointing to its
predecessor in the effect chain. The chain starts at `Start`'s `eff`
projection and threads through every effectful node up to `Return`.

```
Start
  └─eff─→ Store ──┐
                    ├─eff─→ AtomicStore ──┐
                                          ├─eff─→ Return
```

The verifier (Rule 42) checks that the chain is continuous — every
Altered/Crowded node's `eff_in` must be `Start` or another Altered/
Crowded node.

---

## 5. Alias Analysis Interface

Speculative Effect Reordering (Pass 49), Speculative Bounds Check
Elimination (Pass 51), and Partial Escape Analysis (Pass 10) all
require alias information. The IR exposes a single interface:

```cpp
class AliasAnalysisInterface {
public:
    virtual bool may_alias(NodeId a, NodeId b) const noexcept = 0;
    virtual bool is_non_escaping(NodeId a) const noexcept = 0;
};
```

The default implementation, `NoAliasAnalysis`, is conservative — it
returns `may-alias` and `not-non-escaping` for everything. The
research-grade passes will swap in CFL-Reachability Alias Analysis
(Pass 40) and Value-Flow Analysis (Pass 41) for more precise aliasing
information.

---

## 6. PGO + Guards (Rules A.3, A.4, A.5)

In JIT mode, passes that reorder Altered nodes use PGO data to insert
hardware guards. A `Guard` node:

- Is `Crowded` effect (so it's never reordered past).
- Has a `cond` data input (the condition being guarded).
- Has a `FrameState` attachment (Rule A.5) — a snapshot of the
  mutator state at the guard site.
- On failure, transfers control to `aegis::jit::deoptimize()`, which
  uses the `FrameState` to reconstruct the AOT baseline state (Rule A.4)
  and transfers control to the AOT function at the equivalent IP.

### Deoptimization Checklist

| What | Restored from |
|------|----------------|
| Register values | `FrameState::register_snapshot` |
| Stack frame layout | AOT baseline at equivalent IP |
| Lexical region stack | `FrameState::region_id` |
| Speculatively reordered writes | rollback of `FrameState::aliased_nodes` |

---

## 7. References

- Spec §1 "The Syntax" — the language grammar that produces these effects.
- Spec §3 "How the Compiler Infers Effects" — the inference algorithm.
- `compiler/ir/include/aegis/ir/Effects.hpp` — the C++ types.
- `compiler/frontend/src/EffectInference.cpp` — the inference pass.
