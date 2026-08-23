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
#include <map>

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
    /// Label-id allocator for select branch regions. Structured loop
    /// labels occupy [0, loops*2); select labels start at this named
    /// base so the two spaces never collide (Rule D.1: named, not a
    /// literal) and stay non-negative (the encoder's label table
    /// indexes by id).
    /// Base for select-region label ids (disjoint from loop labels,
    /// which start at 0); sized far beyond any real loop count.
    static constexpr uint64_t kSelectLabelBase{1'000'000};
    uint64_t select_label_next_{kSelectLabelBase};
    /// Per-select ownership views + flattened global view (see the
    /// ownership pass in lower()): nodes emitted inside a select's
    /// branch region instead of by the outer walks.
    std::map<NodeId, std::vector<NodeId>> select_owner_{};
    std::vector<NodeId> select_global_owner_{};
    /// node id -> per_node index (built once per lower()).
    std::vector<int64_t> instr_of_shared_{};

    // Structured emission for functions containing loops: per loop —
    // phi-init movs, head label, body closure (topo order, phis are
    // pre-defined leaves), jz-exit, back-edge phi updates, jmp, exit
    // label — then post-loop code from the return operand. Declared
    // here; defined in the .cpp as a free function friended by use of
    // the public NodeInstr vector.
    void emit_structured_loops(const std::vector<NodeId>& loops,
                               std::vector<NodeInstr>& per_node,
                               std::vector<VRegId>& vreg_per_node,
                               VRegId& next_vreg,
                               MachineFunction& mf);

    // Topological emission of one value's closure (shared by the flat
    // and structured paths; handles nested selects + call projs).
    void emit_value_closure(NodeId root,
                            const std::vector<NodeInstr>& per_node,
                            const std::vector<int64_t>& instr_of,
                            std::vector<uint8_t>& emitted,
                            std::vector<VRegId>& vreg_of,
                            VRegId& next_vreg, MachineFunction& mf);

    // Emit a merge phi as a REAL branch region (cmov evaluates both
    // arms: unsound for non-terminating/effectful arms).
    void emit_select_region(NodeId phi,
                            const std::vector<NodeInstr>& per_node,
                            const std::vector<int64_t>& instr_of,
                            std::vector<uint8_t>& emitted,
                            std::vector<VRegId>& vreg_of,
                            VRegId& next_vreg, MachineFunction& mf);
};
} // namespace aegis
