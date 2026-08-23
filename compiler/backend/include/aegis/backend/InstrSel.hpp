// backend/InstrSelection.h — Instruction selection (SoN -> MachineInstr).
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Instruction Selection (BURS/DAG Combiner): Maps SoN nodes to target
//    machine instructions."
//
// For the prototype, we use a simple pattern-match-based selector: each
// Pure arithmetic node becomes a single MOV + op instruction; each
// Altered node (Load/Store/Alloc) becomes a memory op.
// ============================================================
#pragma once
#include <cstdint>
#include <map>
#include <vector>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/ir/Graph.hpp"

namespace aegis {

// Per-node instruction collected during selection (node id + its
// machine instruction) — shared between the flat scheduler and the
// structured loop emitter.
struct NodeInstr {
    NodeId node;
    MachineInstr mi;
};

class InstrSelector {
public:
    InstrSelector(Graph& g) : g_(g) {}

    // Lowers the IR graph into a MachineFunction. Each Pure arithmetic
    // node produces one MachineInstr; the effect chain is linearized.
    MachineFunction lower(std::string_view fn_name);

private:
    Graph& g_;

    // ---- Shared emission context (threaded through every emitter). ----
    // One bundle instead of six parameters per recursive call; the
    // members below (walkers, loop/select regions) all take `EmitCtx&`.
    struct EmitCtx {
        const std::vector<NodeInstr>* per_node{};
        const std::vector<int64_t>* instr_of{};
        std::vector<uint8_t>* emitted{};     // node id -> already emitted
        std::vector<VRegId>* vreg_of{};      // node id -> vreg
        VRegId* next_vreg{};
        MachineFunction* mf{};
    };

    /// Label-id allocator for select branch regions; loop labels start
    /// at 0, select labels at this named base so the spaces never
    /// collide (Rule D.1) and stay non-negative (the encoder's label
    /// table indexes by id).
    static constexpr uint64_t kSelectLabelBase{1'000'000};
    uint64_t select_label_next_{kSelectLabelBase};
    uint64_t loop_label_next_{0};
    /// Per-select ownership views + flattened global view (see the
    /// ownership pass in lower()): nodes emitted inside a select's
    /// branch region instead of by the outer walks.
    std::map<NodeId, std::vector<NodeId>> select_owner_{};
    std::vector<NodeId> select_global_owner_{};
    /// node id -> per_node index (built once per lower()).
    std::vector<int64_t> instr_of_shared_{};
    /// loop node id -> region already emitted (nested loops emit on
    /// first encounter from the value walk; the top-level sweep skips).
    std::vector<uint8_t> loop_emitted_{};

    // ---- Emitters (flat + structured paths). ----

    // Structured emission for functions containing loops. Order of
    // side effects comes from the EFFECT CHAIN (walked as events in
    // chain order): pre-loop calls, loop barriers (each emits its
    // region), then the return's value+effect closures. Nested loops
    // emit recursively when the walk reaches their phis.
    void emit_structured_loops(const std::vector<NodeId>& loops,
                               std::vector<NodeInstr>& per_node,
                               std::vector<VRegId>& vreg_per_node,
                               VRegId& next_vreg,
                               MachineFunction& mf);

    // One loop's region: preheader phi-init movs, head label, body
    // closure (phi back values + exit condition + body effect tail),
    // jz exit, back-edge updates, jmp, exit label. Recursively emits
    // nested loops/selects the body reaches.
    void emit_loop_region(NodeId loop, EmitCtx& c);

    // Iterative post-order emission of one value's computation
    // closure. Special cases: select phis -> guarded branch region;
    // loop phis -> trigger their loop region (once) and act as leaves;
    // call-value projections -> follow to the call; effect-merge phis
    // -> pass through to their value inputs. `eff_scope` (when valid)
    // restricts effect-edge descent to calls under that control node
    // (used by select arm effects so pre-branch calls are not pulled
    // inside the guarded region).
    void emit_value_closure(NodeId root, EmitCtx& c,
                            NodeId eff_scope = kInvalidNodeId);

    // Emit a merge phi as a REAL branch region (cmov evaluates both
    // arms: unsound for non-terminating/effectful arms). Roots each
    // arm's VALUE closure and the arm's effect calls (calls whose
    // ctrl is the arm's projection — a result-unused call inside a
    // branch is reachable only through the effect chain).
    void emit_select_region(NodeId phi, EmitCtx& c);

    // vreg lookup/assign for a node (shared map).
    VRegId vreg_of_node(NodeId id, EmitCtx& c);
};

} // namespace aegis
