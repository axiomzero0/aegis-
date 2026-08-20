// passes/mid/SCEV.hpp — Scalar Evolution (SCEV) analysis.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Scalar Evolution (SCEV): Analyzes loop induction variables for
//    exact bounds/strides."
// ============================================================
//
// SCEV models every value as a recurrence: {start, step, count}. For
// the prototype we recognize linear recurrences:
//
//   induction = {start, step, trip_count}  (AddRec)
//   affine    = base + i * stride          (AddRecExpr)
//
// The analysis is queried by other passes (LICM, loop unrolling,
// bounds check elimination) via the SCEV handle.
#pragma once
#include <cstdint>

#include "aegis/ir/Graph.hpp"
#include "aegis/passes/Pass.hpp"
#include "aegis/support/Primitives.hpp"
#include "aegis/support/SwissTable.hpp"

namespace aegis::passes::mid {

// A SCEV expression describes how a value evolves over loop iterations.
enum class SCEVKind : uint8_t {
    Constant,   // {value = c, no evolution}
    AddRec,      // {start, step, trip_count} = start + i*step, i in [0, trip)
    Add,         // scev_a + scev_b
    Mul,         // scev_a * scev_b
    Unknown,     // we don't know
};

struct SCEVExpr {
    SCEVKind kind{SCEVKind::Unknown};
    int64_t  start{0};
    int64_t  step{0};
    int64_t  trip_count{-1}; // -1 = unknown
    NodeId   operand_a{kInvalidNodeId};
    NodeId   operand_b{kInvalidNodeId};
    bool     operator==(const SCEVExpr&) const noexcept = default;
};
static_assert(std::is_trivially_destructible_v<SCEVExpr>,
              "SCEVExpr must be trivially destructible so it can be stored "
              "as a value in SwissTable (Rule D.4 / Rule 55).");

class SCEVAnalysis {
public:
    explicit SCEVAnalysis(Graph& g) : g_(g) {}

    // Run the analysis and populate the per-node SCEV map. Returns
    // the number of AddRec expressions discovered.
    int run() noexcept;

    // Query the SCEV of a node. Returns Unknown if the node was not
    // analyzed or is not a recurrence.
    [[nodiscard]] SCEVExpr scev_of(NodeId id) const noexcept;

    // Get the trip count of the loop that this AddRec is associated
    // with. Returns -1 if unknown.
    [[nodiscard]] int64_t trip_count_of(NodeId id) const noexcept;

private:
    Graph& g_;
    // Law: Rule D.4 — use SwissTable, not std::unordered_map. SCEVExpr
    // is trivially destructible (static_asserted above) so it can be
    // stored as a value type in SwissTable's flat array.
    SwissTable<NodeId, SCEVExpr> map_;
};

class SCEVPass : public Pass {
public:
    SCEVPass() : Pass("scev") {}
    int run(Graph& g, const PassBudget& budget) override;
};

} // namespace aegis::passes::mid
