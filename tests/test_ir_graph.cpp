// tests/test_ir_graph.cpp — Unit tests for the IR Graph + Verifier.
#include <cassert>
#include <iostream>
#include <string>

#include "core/SymbolTable.h"
#include "ir/Graph.h"
#include "ir/Verifier.h"
#include "ir/Printer.h"

namespace {

using namespace aegis;

int test_start_node_at_id_zero() {
    SymbolTable syms;
    Graph g(&syms);
    assert(g.size() == 1);
    assert(g[kStartNodeId].kind == NodeKind::Start);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int test_constant_creation() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId c1 = g.make_constant_i64(42, 1);
    NodeId c2 = g.make_constant_i64(42, 1);
    assert(c1 != c2); // (without hash-consing, these are distinct)
    assert(g[c1].payload.i64 == 42);
    assert(g[c2].payload.i64 == 42);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int test_binop_and_verify() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId a = g.make_constant_i64(10, 1);
    NodeId b = g.make_constant_i64(20, 1);
    NodeId add = g.make_binop(NodeKind::Add, a, b, 1);
    assert(g[add].kind == NodeKind::Add);
    assert(g[add].effect == EffectClass::Pure);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int test_altered_effect_chain() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId ptr  = g.make_constant_i64(0, 1);
    NodeId val  = g.make_constant_i64(42, 1);
    NodeId store = g.make_store(ctrl, eff, ptr, val);
    assert(g[store].effect == EffectClass::Altered);
    std::string why;
    assert(g.verify(why));
    return 0;
}

} // namespace

int main() {
    test_start_node_at_id_zero();
    test_constant_creation();
    test_binop_and_verify();
    test_altered_effect_chain();
    std::cout << "ir_graph tests passed (assertions OK)\n";
    return 0;
}
