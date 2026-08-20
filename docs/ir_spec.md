# Aegis E-SoN IR Specification

**Status:** Draft
**Owner:** Aegis IR Dev Team
**Last Updated:** 2026-08-21
**Related Sections:** `laws.md`, `effect_system.md`, `abi.md`

---

## 1. Overview

The Aegis IR is an **Effect-Typed Sea of Nodes** ("E-SoN") — a unified
data-flow + control-flow graph where each node carries an explicit
effect class (`Pure` / `Altered` / `Crowded`) in addition to its opcode
and operands.

Key properties:

- **Index-based.** All edges are `NodeId` (`uint32_t`). No raw pointers
  (Rule 53).
- **Arena-allocated.** All nodes live in a `std::pmr::monotonic_buffer_resource`
  arena (Rule B.2). Bulk-freed at graph destruction.
- **Hash-consed.** Pure nodes are structurally deduplicated at construction
  time — native GVN.
- **Verifiable.** The verifier (Rule 42) runs in debug after every pass.

---

## 2. Node Structure

```cpp
struct Node {
    NodeKind             kind;         // enum class, no RTTI (Rule B.3)
    EffectClass          effect;       // Pure / Altered / Crowded
    NodeFlags            flags;        // bitmasked (Rule 51)
    TypeId               type_id;
    NodePayload          payload;      // 8-byte tagged union
    SmallVector<NodeId,3> inputs;      // SBO'd (Rule 57)
};
```

### 2.1 Input Convention

For **Pure** nodes, *all* inputs are data operands (no ctrl/eff slots):

```
inputs[0..N]  = data operands
```

For **Altered / Crowded** nodes:

```
inputs[0] = control-in    (predecessor in the CFG)
inputs[1] = effect-in     (predecessor in the effect chain)
inputs[2..N] = data operands
```

For nodes with no control dependence (e.g. `Start`), `inputs[0]` is
`kInvalidNodeId`.

### 2.2 `NodePayload` (8-byte tagged union)

| Node kind family    | Payload interpretation          |
|---------------------|---------------------------------|
| `Constant`          | `i64` / `u64` / `f64` value      |
| `Parameter`         | `sym` (interned name)            |
| `Proj`              | `proj_index` (output selector)   |
| `GetFieldPtr`       | `field_index`                    |
| `CallPure`/`Altered`/`Crowded` | `sym` (callee)         |
| `Guard`/`Deopt`/`FrameState` | `u64` (FrameState id)   |

### 2.3 `NodeFlags` (bitmasked, Rule 51)

| Flag bit          | Meaning |
|-------------------|---------|
| `IsLive`          | reachable from a Crowded/Return root |
| `IsConst`         | value is a compile-time constant |
| `IsHashed`        | node is in the GVN hash-cons table |
| `IsGuarded`       | PGO guard attached |
| `IsDead`          | marked for deletion at next sweep |
| `HasFrameState`   | `FrameState` attachment present (Rule A.5) |
| `IsPgoSpeculated` | speculation was based on PGO data |
| `IsMonomorphic`   | call site is PGO-monomorphic |
| `IsNoReturn`      | callee never returns |
| `CanOverflow`     | arithmetic may overflow (guard emission) |
| `IsBuiltin`       | resolves to a compiler builtin |
| `IsAffineMove`    | affine-owned value (no aliasing) |
| `IsStackPromoted`  | stack-promoted by escape analysis |
| `IsBoundsChecked` | runtime bounds check attached |
| `IsLowered`        | lowered to machine instrs |

---

## 3. NodeKind Table

| `NodeKind`        | Effect      | Inputs | Output |
|-------------------|-------------|--------|--------|
| `Start`           | Pure        | (none) | ctrl, eff |
| `Region`          | Pure (structural) | preds (≥2) | ctrl |
| `Loop`            | Pure (structural) | entry, back | ctrl |
| `If`              | Pure (structural) | ctrl, cond | bool×2 (true/false) |
| `Proj`            | Pure        | src, idx | projection |
| `Return`          | Pure (root) | ctrl, eff, val | (none) |
| `Stop`            | Pure (root) | ctrl, eff | (none) |
| `Constant`        | Pure        | — | scalar |
| `Parameter`       | Pure        | — | scalar |
| `Phi`             | Pure        | region, vals | scalar |
| `Add`/`Sub`/`Mul`/`Div`/`Mod` | Pure | a, b | scalar |
| `UDiv`/`UMod`     | Pure        | a, b | scalar |
| `And`/`Or`/`Xor`  | Pure        | a, b | scalar |
| `Shl`/`Shr`/`LShr`| Pure        | a, b | scalar |
| `CmpEq`/`Ne`/`Lt`/`Le`/`Gt`/`Ge` | Pure | a, b | bool |
| `CmpUlt`/`Ule`/`Ugt`/`Uge` | Pure | a, b | bool |
| `Neg`/`Not`       | Pure        | a | scalar |
| `Load`            | Altered     | ctrl, eff, ptr | scalar |
| `Store`           | Altered     | ctrl, eff, ptr, val | (none) |
| `Alloc`           | Altered     | ctrl, eff | ptr |
| `StackAlloc`      | Pure        | — | ptr |
| `GetElementPtr`   | Pure        | ptr, idx | ptr |
| `GetFieldPtr`     | Pure        | ptr, idx | ptr |
| `Cast`            | Pure        | val | scalar |
| `Select`          | Pure        | cond, a, b | scalar |
| `CallPure`        | Pure        | callee + args | scalar |
| `CallAltered`     | Altered     | ctrl, eff, callee + args | scalar |
| `CallCrowded`     | Crowded     | ctrl, eff, callee + args | scalar |
| `AtomicLoad`      | Crowded     | ctrl, eff, ptr | scalar |
| `AtomicStore`     | Crowded     | ctrl, eff, ptr, val | (none) |
| `AtomicRMW`       | Crowded     | ctrl, eff, ptr, val | scalar |
| `Fence`           | Crowded     | ctrl, eff | (none) |
| `Guard`           | Crowded     | ctrl, eff, cond | (none) |
| `Deopt`           | Crowded     | ctrl, eff | (none) |
| `FrameState`      | (metadata)  | snapshot | (none) |

---

## 4. The Graph

### 4.1 Layout

```cpp
class Graph {
    std::pmr::vector<Node>        nodes_;      // contiguous, SoA-friendly (Rule 59)
    std::pmr::vector<OutputList>  outputs_;    // reverse edges
    SymbolTable*                  syms_;
    uint64_t                      version_;    // Rule 50 cache invalidation
};
```

- **`nodes_`** is contiguous; passes that process a single field of
  every node should extract it into a parallel SoA array for cache
  efficiency (Rule 59).
- **`outputs_`** holds reverse edges. Output lists use `SmallVector<NodeId,2>`
  (Rule 57) since most nodes have 1–2 users.
- **`version_`** is bumped on every structural mutation. The PGO profile
  cache and code cache check this stamp; mismatch invalidates (Rule 50).

### 4.2 Start Node Convention

The `Start` node is always `NodeId = 0` (the graph constructor allocates
it). The `Proj` of `Start` with `proj_index = 0` is the initial control
token; with `proj_index = 1` is the initial effect token.

### 4.3 Edge Mutations

- `swap_input(node, old, new)` — replaces all occurrences of `old` in
  `node`'s inputs with `new`. Also updates both nodes' output lists.
- `set_input(node, i, new)` — replaces the i-th input.
- `mark_dead(node)` — sets `IsDead` flag. The actual slot is reclaimed
  by a future compaction pass; ids of *other* nodes are stable.

---

## 5. Hash-Consing (Native GVN)

Pure nodes are canonicalized at construction. The hash-cons key is:

```
hash = FNV-1a over (kind, type_id, payload_bits, sorted_data_inputs)
```

Identical keys reuse the existing node id, so the IR never contains two
structurally identical Pure nodes — this gives GVN for free.

Altered/Crowded nodes are not hash-consed by default (their effect chain
position is part of their identity), but `CallPure` *is* hash-consed.

---

## 6. Verifier (Rule 42)

Runs after every pass in debug builds. Checks:

1. **No dangling `NodeId`s.** Every input edge points to an existing,
   non-dead node.
2. **Effect chain continuity.** Every Altered/Crowded node's `eff_in`
   points to a non-Pure node (`Start` or another Altered/Crowded node).
3. **`FrameState` on every PGO guard.** Nodes flagged
   `IsPgoSpeculated && IsGuarded` must have `HasFrameState` set (Rule A.5).
4. **Use-def consistency.** For every `(producer, consumer)` pair in the
   output list, `consumer.inputs` must contain `producer`.

A failed verifier aborts the pipeline and prints the broken graph. The
CI replay log captures the IR snapshot, PGO profile, and RNG seed for
offline reproduction (Rule 40).

---

## 7. Lowering

The frontend (`compiler/frontend/src/Lowering.cpp`) walks the AST and
constructs IR nodes via the graph's constructors. Every Pure node
construction goes through the `HashCons`, so:

```aegis
fn add(a: i32, b: i32) -> i32 { return a + b; }
```

lowers to:

```
n0 : Start            ins=[]
n1 : Proj proj=0      ins=[0]            # initial ctrl
n2 : Proj proj=1      ins=[0]            # initial eff
n3 : Parameter sym=a  ins=[]
n4 : Parameter sym=b  ins=[]
n5 : Add              ins=[3,4]          # hash-consed
n6 : Return           ins=[1,2,5]
```

---

## 8. Future Work

- `IRVerifier` will gain dominance checks once a real dominator-tree pass
  is in.
- `IRLiveness` will produce the parallel SoA liveness array per Rule 59.
- `IRPrinter` will gain a GraphViz DOT output mode.
