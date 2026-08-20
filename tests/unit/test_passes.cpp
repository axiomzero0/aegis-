// tests/test_passes.cpp — Tests for GVN, E-DCE, SCCP (Rules 37, 36).
#include <cassert>
#include <iostream>

#include "aegis/support/StringIntern.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/mid/GVN.hpp"
#include "aegis/passes/mid/EDCE.hpp"
#include "aegis/passes/mid/SCCP.hpp"
#include "aegis/passes/mid/SimplifyControl.hpp"
#include "aegis/passes/Pass.hpp"

namespace {

using namespace aegis;

// Test 1: GVN dedups identical Pure arithmetic nodes.
int test_gvn_dedups_constants() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId a = g.make_constant_i64(10, 1);
    NodeId b = g.make_constant_i64(10, 1); // structurally identical
    NodeId add1 = g.make_binop(NodeKind::Add, a, b, 1);
    NodeId c = g.make_constant_i64(10, 1);
    NodeId d = g.make_constant_i64(10, 1);
    NodeId add2 = g.make_binop(NodeKind::Add, c, d, 1);
    (void)add1; (void)add2;

    GVNPass pass;
    PassBudget b_;
    int removed = pass.run(g, b_);
    // GVN should dedup both constants and both adds (some number > 0).
    assert(removed > 0);
    std::string why;
    assert(g.verify(why));
    return 0;
}

// Test 2: E-DCE removes unreachable Pure nodes.
int test_edce_removes_dead_pure_nodes() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId val  = g.make_constant_i64(42, 1);
    // This Pure node has no use — should be removed by E-DCE.
    NodeId dead = g.make_binop(NodeKind::Add, val, val, 1);
    (void)dead;
    // Return root so we have at least one root.
    g.make_return(ctrl, eff, val);

    EDCEPass pass;
    PassBudget b_;
    int removed = pass.run(g, b_);
    assert(removed > 0);
    std::string why;
    assert(g.verify(why));
    return 0;
}

// Test 3: SCCP folds a + b when both are constants.
int test_sccp_folds_constants() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId a = g.make_constant_i64(10, 1);
    NodeId b = g.make_constant_i64(32, 1);
    NodeId add = g.make_binop(NodeKind::Add, a, b, 1);
    (void)add;

    SCCPPass pass;
    PassBudget b_;
    int removed = pass.run(g, b_);
    assert(removed > 0); // the Add should have folded to a Constant
    std::string why;
    assert(g.verify(why));
    return 0;
}

// Test 4: idempotency — running the same pass twice yields identical IR.
// Rule B.5.
int test_gvn_is_idempotent() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId a = g.make_constant_i64(1, 1);
    NodeId b = g.make_constant_i64(2, 1);
    NodeId add = g.make_binop(NodeKind::Add, a, b, 1);
    (void)add;

    GVNPass pass;
    PassBudget b_;
    int r1 = pass.run(g, b_);
    int r2 = pass.run(g, b_);
    // First pass removes some nodes; second pass should be a no-op (0 changes).
    assert(r2 == 0);
    std::string why;
    assert(g.verify(why));
    return 0;
}

} // namespace

int main() {
    test_gvn_dedups_constants();
    test_edce_removes_dead_pure_nodes();
    test_sccp_folds_constants();
    test_gvn_is_idempotent();
    std::cout << "passes tests passed (assertions OK)\n";
    return 0;
}
