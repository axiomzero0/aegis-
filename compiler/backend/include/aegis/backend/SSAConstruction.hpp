// backend/SSAConstruction.hpp — Convert SoN to Static Single Assignment form.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "SSA Construction: Converts SoN to Static Single Assignment form."
// ============================================================
#pragma once
#include "aegis/ir/Graph.hpp"
namespace aegis::backend {
class SSAConstructor {
public:
    explicit SSAConstructor(Graph& g) : g_(g) {}
    // Returns the number of Phi nodes inserted at merge points.
    int run() noexcept;
private:
    Graph& g_;
};
} // namespace aegis::backend
