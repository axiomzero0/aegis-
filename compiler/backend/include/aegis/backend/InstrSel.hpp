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
};
} // namespace aegis
