// passes/GVN.h — Global Value Numbering (native to SoN).
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Hash-Consing: Global hash map for nodes based on opcode/inputs.
//    Provides O(1) GVN."
//   "Global Value Numbering (GVN): Identifies redundant calculations.
//    Native to SoN."
//
// We do GVN by re-hash-consing every Pure node in the graph. Each Pure
// node's structural signature (kind + type_id + payload + data inputs)
// becomes its key; if an identical entry already exists, the redundant
// node is replaced and its uses rewired.
// ============================================================
#pragma once
#include "passes/Pass.h"

namespace aegis {
class GVNPass : public Pass {
public:
    GVNPass() : Pass("gvn") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis
