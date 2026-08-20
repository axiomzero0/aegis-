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
class InstrSelector {
public:
    InstrSelector(Graph& g) : g_(g) {}

    // Lowers the IR graph into a MachineFunction. Each Pure arithmetic
    // node produces one MachineInstr; the effect chain is linearized.
    MachineFunction lower(std::string_view fn_name);
private:
    Graph& g_;
};
} // namespace aegis
