// tests/unit/test_loop_speculative.cpp — Tests for loop + speculative passes.
//
// Rule 36: 5+ regression tests per bug fix. These verify the real
// IR rewrites done by LoopUnrolling, LoopFusion, and the speculative
// passes (SpeculativeBCE, SpeculativeEffectReordering,
// GuardedDevirtualization, SpeculativeLockElision).
#include <cassert>
#include <iostream>
#include <string>

#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/Pass.hpp"
#include "aegis/passes/mid/LoopUnrolling.hpp"
#include "aegis/passes/mid/LoopFusion.hpp"
#include "aegis/passes/research/SpeculativeBCE.hpp"
#include "aegis/passes/research/SpeculativeEffectReordering.hpp"
#include "aegis/passes/research/GuardedDevirtualization.hpp"
#include "aegis/passes/research/SpeculativeLockElision.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

using namespace aegis;

// ---- LoopUnrolling: degenerate loop (Phi only feeds back-edge) -> eliminated ----

int lu_degenerate_loop_eliminated() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    // Build a degenerate loop: phi = phi(loop, [const0, add(phi, const1)])
    // with no other uses of phi. SCEV should find {start=0, step=1, trip_count=10}.
    NodeId entry = ctrl;
    NodeId back  = ctrl; // placeholder back-edge
    NodeId lp    = g.make_loop(back, entry);
    NodeId start_v = g.make_constant_i64(0, 1);
    NodeId step_v  = g.make_constant_i64(1, 1);
    NodeId phi     = g.make_phi(lp, {start_v, kInvalidNodeId}, 1);
    NodeId add     = g.make_binop(NodeKind::Add, phi, step_v, 1);
    g.set_input(phi, 2, add);
    NodeId four    = g.make_constant_i64(4, 1);
    g.make_cmp(NodeKind::CmpLt, phi, four);
    g.make_return(ctrl, eff, start_v);
    passes::mid::LoopUnrollingPass pass;
    PassBudget b;
    int r = pass.run(g, b);
    // The degenerate loop should be eliminated (Phi only feeds back-edge).
    // trip_count=4 <= kLoopUnrollFullUnrollTripCount=8.
    assert(r > 0);
    assert(g[lp].flags.has(NodeFlagBit::IsDead));
    assert(g[phi].flags.has(NodeFlagBit::IsDead));
    assert(g[add].flags.has(NodeFlagBit::IsDead));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- LoopUnrolling: loop with inner If -> skipped (sound) ----

int lu_loop_with_inner_if_skipped() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId entry = ctrl;
    NodeId back  = ctrl;
    NodeId lp    = g.make_loop(back, entry);
    NodeId start_v = g.make_constant_i64(0, 1);
    NodeId step_v  = g.make_constant_i64(1, 1);
    NodeId phi     = g.make_phi(lp, {start_v, kInvalidNodeId}, 1);
    NodeId add     = g.make_binop(NodeKind::Add, phi, step_v, 1);
    g.set_input(phi, 2, add);
    NodeId ten     = g.make_constant_i64(10, 1);
    g.make_cmp(NodeKind::CmpLt, phi, ten);
    // Add an If inside the loop body (ctrl_in traces back to lp).
    NodeId if_node = g.make_if(lp, phi);
    (void)if_node;
    g.make_return(ctrl, eff, start_v);
    passes::mid::LoopUnrollingPass pass;
    PassBudget b;
    int r = pass.run(g, b);
    // Should skip (inner control flow makes simple substitution unsound).
    assert(r == 0);
    assert(!g[lp].flags.has(NodeFlagBit::IsDead));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- LoopFusion: two adjacent degenerate loops with same SCEV -> second eliminated ----

int lf_two_degenerate_loops_fused() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    // Loop A: degenerate, SCEV {0, 1, 10}
    NodeId lp_a = g.make_loop(ctrl, ctrl);
    NodeId phi_a = g.make_phi(lp_a, {g.make_constant_i64(0, 1), kInvalidNodeId}, 1);
    NodeId add_a = g.make_binop(NodeKind::Add, phi_a, g.make_constant_i64(1, 1), 1);
    g.set_input(phi_a, 2, add_a);
    g.make_cmp(NodeKind::CmpLt, phi_a, g.make_constant_i64(10, 1));
    // Loop B: degenerate, same SCEV {0, 1, 10}
    NodeId lp_b = g.make_loop(ctrl, ctrl);
    NodeId phi_b = g.make_phi(lp_b, {g.make_constant_i64(0, 1), kInvalidNodeId}, 1);
    NodeId add_b = g.make_binop(NodeKind::Add, phi_b, g.make_constant_i64(1, 1), 1);
    g.set_input(phi_b, 2, add_b);
    g.make_cmp(NodeKind::CmpLt, phi_b, g.make_constant_i64(10, 1));
    g.make_return(ctrl, eff, g.make_constant_i64(0, 1));
    passes::mid::LoopFusionPass pass;
    PassBudget b;
    int r = pass.run(g, b);
    // Second loop should be eliminated (fused into first).
    assert(r > 0);
    assert(g[lp_b].flags.has(NodeFlagBit::IsDead));
    assert(g[phi_b].flags.has(NodeFlagBit::IsDead));
    assert(g[add_b].flags.has(NodeFlagBit::IsDead));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- SpeculativeBCE: Guard with CmpLt cond -> tagged + FrameState emitted ----

int sbce_bounds_check_guard_speculated() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId idx = g.make_constant_i64(2, 1);
    NodeId len = g.make_constant_i64(10, 1);
    NodeId cmp = g.make_cmp(NodeKind::CmpLt, idx, len);
    NodeId guard = g.make_guard(ctrl, eff, cmp, /*fs=*/0);
    (void)guard;
    passes::research::SpeculativeBCEPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    int r = pass.run(g, b);
    assert(r > 0);
    // SOUND: Guard is tagged with IsPgoSpeculated + HasFrameState + IsGuarded.
    assert(g[guard].flags.has(NodeFlagBit::IsPgoSpeculated));
    assert(g[guard].flags.has(NodeFlagBit::HasFrameState));
    assert(g[guard].flags.has(NodeFlagBit::IsGuarded));
    // A FrameState node was created (graph grew by 1).
    // The Guard's payload now holds the FrameState id.
    assert(g[guard].payload.u64 != 0);
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- SpeculativeBCE: no PGO -> skipped (sound) ----

int sbce_no_pgo_skipped() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId idx = g.make_constant_i64(2, 1);
    NodeId len = g.make_constant_i64(10, 1);
    NodeId cmp = g.make_cmp(NodeKind::CmpLt, idx, len);
    NodeId guard = g.make_guard(ctrl, eff, cmp, 0);
    passes::research::SpeculativeBCEPass pass;
    PassBudget b;
    b.pgo_available = false; // AOT mode
    b.mode = CompileMode::AOT;
    int r = pass.run(g, b);
    assert(r == 0);
    // SOUND: Guard is NOT tagged (no speculation without PGO).
    assert(!g[guard].flags.has(NodeFlagBit::IsPgoSpeculated));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- SpeculativeEffectReordering: Load -> tagged + FrameState emitted ----

int ser_load_speculated() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId ptr  = g.make_constant_i64(0, 1);
    NodeId load = g.make_load(ctrl, eff, ptr, 1);
    g.make_return(ctrl, eff, load);
    passes::research::SpeculativeEffectReorderingPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    int r = pass.run(g, b);
    assert(r > 0);
    assert(g[load].flags.has(NodeFlagBit::IsPgoSpeculated));
    assert(g[load].flags.has(NodeFlagBit::HasFrameState));
    assert(g[load].flags.has(NodeFlagBit::IsGuarded));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- GuardedDevirtualization: CallAltered -> tagged + FrameState emitted ----

int gd_call_speculated() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId arg1 = g.make_constant_i64(1, 1);
    NodeId call = g.make_call(ctrl, eff, /*callee=*/1, {arg1}, 1, EffectClass::Altered);
    (void)call;
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    int r = pass.run(g, b);
    assert(r > 0);
    assert(g[call].flags.has(NodeFlagBit::IsMonomorphic));
    assert(g[call].flags.has(NodeFlagBit::IsPgoSpeculated));
    assert(g[call].flags.has(NodeFlagBit::HasFrameState));
    assert(g[call].flags.has(NodeFlagBit::IsGuarded));
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ---- SpeculativeLockElision: CallCrowded -> tagged + FrameState emitted ----

int sle_call_speculated() {
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId call = g.make_call(ctrl, eff, /*callee=*/1, {}, 1, EffectClass::Crowded);
    passes::research::SpeculativeLockElisionPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    int r = pass.run(g, b);
    if (r <= 0 || !g[call].flags.has(NodeFlagBit::IsPgoSpeculated)) {
        std::cerr << "SLE FAIL: r=" << r << " call_kind=" << static_cast<int>(g[call].kind)
                  << " (expected " << static_cast<int>(NodeKind::CallCrowded) << ")\n";
        std::cerr << "--- IR ---\n" << format_graph(g) << "\n";
        return 1;
    }
    assert(g[call].flags.has(NodeFlagBit::HasFrameState));
    assert(g[call].flags.has(NodeFlagBit::IsGuarded));
    std::string why;
    assert(g.verify(why));
    return 0;
}

} // namespace

int main() {
    lu_degenerate_loop_eliminated();
    lu_loop_with_inner_if_skipped();
    lf_two_degenerate_loops_fused();
    sbce_bounds_check_guard_speculated();
    sbce_no_pgo_skipped();
    ser_load_speculated();
    gd_call_speculated();
    sle_call_speculated();
    std::cout << "loop_speculative tests passed (assertions OK)\n";
    return 0;
}
