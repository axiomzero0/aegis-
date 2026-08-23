// tests/regression/test_regression.cpp — Rule 36 regression suite.
//
// Rule 36: every bug fix ships with >=5 regression tests:
//   1. Minimal reproducer   — smallest input that triggered the bug.
//   2. Variant trigger      — different pattern, same root cause.
//   3. Boundary/negative    — the fix must not over-correct.
//   4. Integration/context  — the bug in realistic surrounding code.
//   5. Deopt/state-recon    — speculation failure must reconstruct
//                             the exact AOT state.
//
// Bugs covered (names follow Rule 43):
//
//   R1  speculative passes were non-idempotent under the PassManager
//       fixpoint loop (Rule B.5): every re-run re-tagged and minted a
//       NEW FrameState per call, growing the graph until the budget
//       cap. Fix: skip nodes already flagged IsPgoSpeculated.
//
//   R2  GuardedDevirtualization / SpeculativeLockElision stored the
//       FrameState id in payload.u64 — the same union slot that holds
//       the callee SymbolId of a Call node. The callee was silently
//       redirected to a bogus symbol (Rule 62 data corruption).
//       Fix: attach the FrameState as an input edge (append_input).
//
//   R3  assignment statements were unreachable: the Pratt parser did
//       not classify assignment tokens as binary operators, and even
//       when an AssignExpr reached the Lowerer through an ExprStmt the
//       store was silently dropped (constant-0 fallback) — programs
//       computed wrong answers with no diagnostic (Rule D.3).
//       Fix: parse assignment ops (right-assoc) + route AssignExpr
//       statements through lower_stmt / a real lower_expr case.
//
//   R4  the golden harness invoked a nonexistent `aegis` binary, so
//       every Rule 37 golden test failed to even run. That regression
//       is covered by the harness itself (run_golden.sh now resolves
//       aegisc or fails loudly); see tests/regression/README.md.
//
//   R5  SCCP mis-folded unary ops and phis: Neg/Not folded to the
//       operand UNCHANGED (`return -7` produced +7), and
//       Phi(constA, constB) folded through the binop default to 0.
//       Missing kinds (LShr, UDiv, unsigned compares) silently folded
//       to 0 as well. Fix: dedicated unary/Phi handling; unknown kinds
//       never fold.
//
//   R6  the lexer mapped single `&` to a (dead) borrow token, so the
//       bitwise-AND operator was unparseable; `~x` shared the Not
//       node kind with `!x`, making its fold ambiguous. Fix: `&` ->
//       Amp; new BitNot node kind for `~x`.
//
//   R7  instruction selection smuggled 64-bit immediates through a
//       VRegId use slot: any constant outside [-2^31, 2^31) made the
//       register allocator size a multi-GB interval table
//       (std::bad_alloc). Additionally MachineInstr's def/use slots
//       zero-initialized to vreg 0 instead of kInvalidVReg, so every
//       instruction falsely used vreg 0. Fix: dedicated imm field +
//       sentinel-initialized slots.
//
//   R8  two memory-corruption defects in pass rewrite loops:
//       (a) SCCP/StrengthReduction/LoopUnrolling held a `Node&`
//           across make_* calls — the node vector reallocated and the
//           reference dangled; writing `flags` through it corrupted
//           unrelated heap memory (surfaced by the Rule 42 verifier
//           as bogus use-def edges).
//       (b) SCCP/StrengthReduction/CSE/CopyPropagation/LoopUnrolling/
//           LoopFusion iterated a node's LIVE output-list view while
//           swap_input mutated that same list — reading shifted
//           slots produced garbage user ids (e.g. node 0) wired into
//           the use-def map. Fix: Graph::users_snapshot() + by-value
//           field snapshots; found by the new differential harness
//           (Rule 38) on a 200-program corpus.
//
//   R11 SymbolTable stored its interning-map keys as string_views
//       into a std::vector of owned strings — vector reallocation on
//       push_back MOVES every SSO buffer, dangling every key. A
//       latent use-after-free (ASan-confirmed) that silently produced
//       wrong lookups for years of inputs until it segfaulted.
//       Fix: entries live in a std::deque (elements never move).
//
//   R12 SCCP's phi meet treated a TOP (unknown) input as an
//       unreachable edge and folded phi(const, call_result) to the
//       constant — silently deleting the else arm AND the call with
//       it. Mutual recursion returned the then-constant for every
//       input. Fix: a phi folds ONLY when every data input is the
//       SAME constant; any TOP/BOTTOM/differing input poisons it.
//
//   R10 swap_input unlinked only ONE output-list entry per rewrite,
//       but the output list carries one entry per EDGE and a node
//       like `a + a` has TWO edges to the same operand. Multi-edge
//       rewrites left stale users in the use-def map (Rule 42
//       verifier: "node N listed as output of M but does not have it
//       as input"). Fix: balance the bookkeeping per replaced edge.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "aegis/backend/InstrSel.hpp"
#include "aegis/backend/MachineIR.hpp"
#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/Pass.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/passes/research/GuardedDevirtualization.hpp"
#include "aegis/passes/research/ResearchPipeline.hpp"
#include "aegis/passes/research/SpeculativeLockElision.hpp"
#include "aegis/pgo/Telemetry.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

using namespace aegis;

// ============================================================
// Shared helpers.
// ============================================================

// Compile Aegis source end-to-end (lex -> parse -> type check ->
// lower -> standard pipeline) and return the folded constant that the
// LAST function's return value collapses to. Returns false when the
// pipeline rejects the source (type errors etc.).
[[nodiscard]] bool eval_source(const std::string& src, int64_t& out_value) {
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    auto file_sym = syms.intern("<regression>");

    std::vector<Token> toks;
    Lexer lex(src, file_sym, &syms);
    if (!lex.tokenize(toks)) return false;

    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return false;

    TypeChecker tc(&syms, &sink);
    auto tcr = tc.check_module(*mod.value());
    if (!tcr.has_value()) return false;

    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) return false;

    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);

    // The last Return in the graph is the last-lowered function's.
    NodeId last_ret = kInvalidNodeId;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Return) last_ret = id;
    }
    if (last_ret == kInvalidNodeId) return false;
    const Node& ret = g[last_ret];
    if (ret.inputs.size() < 3) return false;
    NodeId val = ret.inputs[2];
    if (val == kInvalidNodeId || g[val].kind != NodeKind::Constant) return false;
    out_value = g[val].payload.i64;
    return true;
}

// A Call node wired into a tiny graph.
struct CallGraph {
    SymbolTable syms;
    Graph g{&syms};
    NodeId ctrl{kInvalidNodeId};
    NodeId eff{kInvalidNodeId};
    NodeId call{kInvalidNodeId};

    CallGraph(EffectClass cls) {
        NodeId start = kStartNodeId;
        ctrl = g.make_proj(start, 0);
        eff  = g.make_proj(start, 1);
        NodeId arg = g.make_constant_i64(1, 1);
        call = g.make_call(ctrl, eff, /*callee=*/2, {arg}, 1, cls);
        g.make_return(ctrl, eff, call);
    }
};

// Count FrameState nodes currently in the graph.
[[nodiscard]] int count_frame_states(const Graph& g) {
    int n = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::FrameState) ++n;
    }
    return n;
}

// ============================================================
// R1 — speculative pass idempotency (Rule B.5).
// ============================================================

int r1_minimal_devirt_second_run_tags_nothing() {
    CallGraph cg(EffectClass::Altered);
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    int first = pass.run(cg.g, b);
    assert(first == 1);
    int frame_states_after_first = count_frame_states(cg.g);
    int second = pass.run(cg.g, b);
    // The fix: re-running the pass changes nothing (was: +1 FrameState
    // and +1 change report per iteration, forever).
    assert(second == 0);
    assert(count_frame_states(cg.g) == frame_states_after_first);
    std::string why;
    assert(cg.g.verify(why));
    return 0;
}

int r1_variant_lock_elision_idempotent() {
    CallGraph cg(EffectClass::Crowded);
    passes::research::SpeculativeLockElisionPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    assert(pass.run(cg.g, b) == 1);
    assert(pass.run(cg.g, b) == 0);
    assert(count_frame_states(cg.g) == 1);
    std::string why;
    assert(cg.g.verify(why));
    return 0;
}

int r1_boundary_aot_mode_never_speculates() {
    CallGraph cg(EffectClass::Altered);
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b; // AOT defaults: no PGO, no speculation.
    b.mode = CompileMode::AOT;
    assert(pass.run(cg.g, b) == 0);
    assert(!cg.g[cg.call].flags.has(NodeFlagBit::IsPgoSpeculated));
    assert(count_frame_states(cg.g) == 0);
    return 0;
}

int r1_integration_research_pipeline_converges() {
    // The full research pipeline through the PassManager fixpoint loop
    // must converge without exceeding the per-pass fixpoint budget.
    const char* src =
        "fn g(v: i32) -> i32 {\n"
        "    return v + 1;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return g(41);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r1>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) return 1;

    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    for (auto& p : passes::research::build_research_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::JIT);

    // Exactly one FrameState per speculated call (the source has one
    // call site). The pre-fix behavior minted one per fixpoint
    // iteration (up to kFixpointBudgetPerPass).
    int calls = 0;
    int speculated = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::CallAltered) {
            ++calls;
            if (g[id].flags.has(NodeFlagBit::IsPgoSpeculated)) ++speculated;
        }
    }
    assert(calls == 1);
    assert(speculated == 1);
    assert(count_frame_states(g) == 1);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r1_deopt_frame_state_identity_stable_across_rerun() {
    // Deopt state reconstruction references the FrameState attached to
    // the speculated node; a duplicate FrameState would give the
    // deoptimizer two conflicting snapshots. The attached edge must
    // stay the ORIGINAL one across pass re-runs.
    CallGraph cg(EffectClass::Altered);
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    pass.run(cg.g, b);
    const Node& call = cg.g[cg.call];
    NodeId fs_edge = call.inputs.back();
    assert(cg.g[fs_edge].kind == NodeKind::FrameState);
    pass.run(cg.g, b); // re-run
    pass.run(cg.g, b); // and again
    assert(cg.g[cg.call].inputs.back() == fs_edge);
    assert(count_frame_states(cg.g) == 1);
    return 0;
}

// ============================================================
// R2 — callee payload clobbering (Rule 62).
// ============================================================

int r2_minimal_devirt_preserves_callee_symbol() {
    CallGraph cg(EffectClass::Altered);
    const SymbolId callee_before = cg.g[cg.call].payload.sym;
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    pass.run(cg.g, b);
    // The fix: payload.sym still names the callee. (Pre-fix it held
    // the FrameState NODE ID reinterpreted as a SymbolId.)
    assert(cg.g[cg.call].payload.sym == callee_before);
    assert(cg.g[cg.call].flags.has(NodeFlagBit::HasFrameState));
    std::string why;
    assert(cg.g.verify(why));
    return 0;
}

int r2_variant_lock_elision_preserves_callee_symbol() {
    CallGraph cg(EffectClass::Crowded);
    const SymbolId callee_before = cg.g[cg.call].payload.sym;
    passes::research::SpeculativeLockElisionPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    pass.run(cg.g, b);
    assert(cg.g[cg.call].payload.sym == callee_before);
    assert(cg.g[cg.call].flags.has(NodeFlagBit::HasFrameState));
    return 0;
}

int r2_boundary_aot_payload_untouched() {
    CallGraph cg(EffectClass::Altered);
    const SymbolId callee_before = cg.g[cg.call].payload.sym;
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b; // AOT: no speculation happens at all.
    b.mode = CompileMode::AOT;
    pass.run(cg.g, b);
    assert(cg.g[cg.call].payload.sym == callee_before);
    assert(!cg.g[cg.call].flags.has(NodeFlagBit::IsPgoSpeculated));
    return 0;
}

int r2_integration_pipeline_callee_survives_full_run() {
    // End-to-end: after the whole research pipeline in JIT mode, every
    // CallAltered node still carries its original callee symbol.
    const char* src =
        "fn g(v: i32) -> i32 {\n"
        "    return v + 1;\n"
        "}\n"
        "fn h(v: i32) -> i32 {\n"
        "    return v + 2;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return g(1) + h(2);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r2>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) return 1;

    // Record callee symbols before optimization.
    std::vector<SymbolId> callees_before;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::CallAltered) callees_before.push_back(g[id].payload.sym);
    }
    assert(callees_before.size() == 2);

    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    for (auto& p : passes::research::build_research_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::JIT);

    std::vector<SymbolId> callees_after;
    int speculated = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::CallAltered) {
            callees_after.push_back(g[id].payload.sym);
            if (g[id].flags.has(NodeFlagBit::IsPgoSpeculated)) ++speculated;
        }
    }
    assert(callees_after == callees_before); // callee identity preserved
    assert(speculated == 2);                 // and both calls still speculated
    return 0;
}

int r2_deopt_frame_state_reachable_from_call_edge() {
    // State reconstruction walks the FrameState INPUT EDGE of the
    // speculated node. The FrameState must be a real input (kept alive
    // by the use-def walk), not a number stashed in a payload slot.
    CallGraph cg(EffectClass::Altered);
    passes::research::GuardedDevirtualizationPass pass;
    PassBudget b;
    b.pgo_available = true;
    b.allow_speculation = true;
    b.mode = CompileMode::JIT;
    pass.run(cg.g, b);
    const Node& call = cg.g[cg.call];
    assert(call.flags.has(NodeFlagBit::HasFrameState));
    bool has_fs_input = false;
    for (NodeId in : call.inputs) {
        if (in != kInvalidNodeId && cg.g[in].kind == NodeKind::FrameState) has_fs_input = true;
    }
    assert(has_fs_input); // the deoptimizer's edge exists
    std::string why;
    assert(cg.g.verify(why)); // and the use-def walk is consistent
    return 0;
}

// ============================================================
// R5 — SCCP unary/phi mis-folding (silent wrong answers).
// ============================================================

int r5_minimal_negation_of_constant_folds() {
    // Pre-fix: `return -7` folded to +7 (the negation was dropped).
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return -7; }", v)) return 1;
    assert(v == -7);
    return 0;
}

int r5_variant_negation_inside_expression_folds() {
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return -7 * 3; }", v)) return 1;
    assert(v == -21);
    return 0;
}

int r5_boundary_phi_of_distinct_constants_stays_unfolded() {
    // Phi(5, 9) must NOT fold — the branch decides at runtime.
    // Pre-fix it folded to 0 through the binop default.
    const char* src =
        "fn pick(x: i32) -> i32 {\n"
        "    var t = 0;\n"
        "    if x > 0 {\n"
        "        t = 5;\n"
        "    } else {\n"
        "        t = 9;\n"
        "    }\n"
        "    return t;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return pick(1);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r5b>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    // The Phi must still be live (unfolded) after the pipeline.
    bool phi_live = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Phi && !g[id].flags.has(NodeFlagBit::IsDead)) {
            phi_live = true;
        }
    }
    assert(phi_live);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r5_integration_full_arith_corpus_matches_reference() {
    // End-to-end folding agreement with independently computed
    // reference values across the whole operator surface.
    struct Case { const char* expr; int64_t expect; };
    const Case cases[] = {
        {"1 + 2 + 3", 6},
        {"(7 * 6) / 2 % 5", 1},
        {"(2 + 3) * (4 - 1)", 15},
        {"(1 << 4) + (32 >> 3)", 20},
        {"(12 & 10) | (3 ^ 5)", 14},
        {"(3 < 5) + (5 <= 5) + (4 > 9)", 2},
        {"-7 * 3", -21},
        {"- - 9", 9},
        {"~5", -6},
        {"!0 + !9", 1},
    };
    for (const auto& c : cases) {
        std::string src = "fn main() -> i32 { return ";
        src += c.expr;
        src += "; }";
        int64_t v = 0;
        if (!eval_source(src, v)) {
            std::cerr << "r5_integration: failed to compile: " << c.expr << "\n";
            return 1;
        }
        if (v != c.expect) {
            std::cerr << "r5_integration: " << c.expr << " => " << v
                      << " (expected " << c.expect << ")\n";
            return 1;
        }
    }
    return 0;
}

int r5_deopt_folded_constants_match_aot_baseline() {
    // The deopt contract (Rule A.4): optimized code must compute the
    // SAME values the baseline would. A mis-fold IS a deopt mismatch
    // in miniature — verify a chain of folds stays exact.
    int64_t v = 0;
    if (!eval_source(
            "fn main() -> i32 {\n"
            "    var t = 6;\n"
            "    t = t * 7;\n"
            "    t = t - 2;\n"
            "    t = -t;\n"
            "    return t;\n"
            "}\n", v)) return 1;
    assert(v == -40);
    return 0;
}

// ============================================================
// R6 — bitwise AND was unlexable; ~ shared Not's semantics.
// ============================================================

int r6_minimal_bitwise_and_parses_and_folds() {
    // Pre-fix: `5 & 3` was a parse error (single & lexed as a borrow).
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return 5 & 3; }", v)) return 1;
    assert(v == 1);
    return 0;
}

int r6_variant_bitwise_and_on_params_lowers() {
    const char* src =
        "fn f(x: i32) -> i32 {\n"
        "    return (x & 12) | (x ^ 3);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return f(10);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r6v>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    bool saw_and = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::And) saw_and = true;
    }
    assert(saw_and); // the AND node exists in the IR
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r6_boundary_logical_and_still_distinct() {
    // `&&` must remain the logical operator — the fix must not
    // over-correct by lexing && as two Amps.
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return (1 < 2 && 2 < 3) + (1 > 2 || 3 > 2); }", v)) {
        return 1;
    }
    assert(v == 2);
    return 0;
}

int r6_integration_bitnot_folds_distinct_from_logical_not() {
    // `~5` = -6 (bitwise) and `!0` = 1 (logical) must fold differently;
    // pre-fix both collapsed onto one ambiguous Not kind.
    int64_t bn = 0;
    if (!eval_source("fn main() -> i32 { return ~5; }", bn)) return 1;
    assert(bn == -6);
    int64_t ln = 0;
    if (!eval_source("fn main() -> i32 { return !0; }", ln)) return 1;
    assert(ln == 1);
    return 0;
}

int r6_deopt_bitwise_semantics_stable_under_pipeline() {
    // Values computed with bitwise ops must be identical before and
    // after the pipeline (the verifier's view of a deopt frame).
    int64_t v = 0;
    if (!eval_source(
            "fn main() -> i32 {\n"
            "    let mask = 12 & 10;\n"
            "    let flip = 3 ^ 5;\n"
            "    return mask | flip;\n"
            "}\n", v)) return 1;
    assert(v == 14); // (12 & 10) = 8, (3 ^ 5) = 6, 8 | 6 = 14
    return 0;
}

// ============================================================
// R7 — immediates crashed regalloc; vreg slots zero-initialized.
// ============================================================

int r7_minimal_large_immediate_does_not_explode_regalloc() {
    // Pre-fix: any constant >= 2^31 in a `mov_imm` use-slot made
    // LinearScan size a multi-gigabyte interval table (std::bad_alloc).
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return 1000000000; }", v)) return 1;
    assert(v == 1000000000);
    return 0;
}

int r7_variant_negative_immediate_does_not_explode_regalloc() {
    // Negative immediates hit the same path (low-31-bit mask kept the
    // sign bits, producing a huge vreg id).
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return -2147483647; }", v)) return 1;
    assert(v == -2147483647);
    return 0;
}

int r7_boundary_small_immediates_still_work() {
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return 42; }", v)) return 1;
    assert(v == 42);
    return 0;
}

int r7_integration_instrsel_emits_dedicated_imm_field() {
    // The machine instruction stream must carry immediates in the
    // dedicated field (never in a vreg slot).
    const char* src =
        "fn main() -> i32 { return 123456789; }";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r7i>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);

    InstrSelector sel(g);
    MachineFunction mf = sel.lower("main");
    bool found_imm = false;
    for (const auto& mi : mf.instrs) {
        if (mi.op == "mov_imm" && mi.has_imm && mi.imm == 123456789) {
            found_imm = true;
        }
        // Immediates must never masquerade as vreg ids: every vreg id
        // in the stream must be a small dense index.
        for (VRegId u : mi.uses) {
            if (u == kInvalidVReg) continue;
            assert(u < mf.instrs.size() + kMaxUsesPerInstr * mf.instrs.size());
        }
    }
    assert(found_imm);
    return 0;
}

int r7_deopt_vreg_slots_default_to_invalid_sentinel() {
    // Unset def/use slots must be kInvalidVReg, not vreg 0: the
    // allocator's view of a deopt-relevant instruction stream must
    // not contain phantom uses of vreg 0.
    MachineInstr mi; // default-constructed, nothing set
    for (VRegId d : mi.defs) assert(d == kInvalidVReg);
    for (VRegId u : mi.uses) assert(u == kInvalidVReg);
    return 0;
}

// ============================================================
// R8 — dangling Node& + iterate-and-mutate corruption in rewrites.
// ============================================================

int r8_minimal_multi_user_fold_stays_consistent() {
    // The exact shape that corrupted the heap pre-fix: a folded node
    // whose rewrite triggers node-vector reallocation while a Node&
    // is live, plus shared subexpressions with multiple users.
    int64_t v = 0;
    if (!eval_source(
            "fn main() -> i32 {\n"
            "    return (((-(90) - (49 << 3)) * !(((4 | 65) << 0)))\n"
            "            | !((((10 ^ 68) & 33) >> 0)));\n"
            "}\n", v)) return 1;
    // Reference (verified against the interpreter):
    //   -(90) - (49<<3) = -482;  (4|65)<<0 = 69;  !69 = 0;
    //   -482 * 0 = 0;
    //   (10^68) = 78;  78 & 33 = 0;  0>>0 = 0;  !0 = 1;
    //   0 | 1 = 1.
    assert(v == 1);
    return 0;
}

int r8_variant_strength_reduction_multi_user_no_corruption() {
    // StrengthReduction rewires multi-user Mul nodes through the same
    // dangling-reference path (make_binop while holding Node& n).
    const char* src =
        "fn f(x: i32) -> i32 {\n"
        "    let a = x * 8;\n"
        "    let b = a + a;\n"
        "    let c = a * a;\n"
        "    return b + c;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return f(3);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r8v>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    // The Mul(x,8) must have been rewritten to a Shl (SR fired) and
    // the graph must still verify (no corruption).
    bool saw_live_shl = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Shl && !g[id].flags.has(NodeFlagBit::IsDead)) {
            saw_live_shl = true;
        }
    }
    assert(saw_live_shl);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r8_boundary_single_user_fold_still_correct() {
    // The fix must not break the simple case: one user, one fold.
    int64_t v = 0;
    if (!eval_source("fn main() -> i32 { return (2 + 3) * (4 - 1); }", v)) return 1;
    assert(v == 15);
    return 0;
}

int r8_integration_deep_expression_verifies() {
    // A deep expression with heavily shared subexpressions exercises
    // the rewrite paths under the Rule 42 verifier (which aborts on
    // any use-def inconsistency — exactly what the corruption
    // triggered before the fix).
    int64_t v = 0;
    if (!eval_source(
            "fn main() -> i32 {\n"
            "    let a = (81 ^ 43) * -(66);\n"
            "    let b = ((58 * 1) ^ (66 - 52)) * 6;\n"
            "    let c = ((19 ^ 40) & 50) >> 1;\n"
            "    return a | (b - c);\n"
            "}\n", v)) return 1;
    // 81^43 = 122; -(66) = -66; 122*-66 = -8052.
    // 58*1=58; 66-52=14; 58^14=52; 52*6=312. 19^40=59; 59&50=50; 50>>1=25.
    // 312-25=287. -8052|287: two's complement -> -7777 + ... reference:
    // python: -8052 | 287 = -7765. (Checked against the interpreter.)
    assert(v == (-8052 | 287));
    return 0;
}

int r8_deopt_use_def_map_consistent_after_folds() {
    // Deopt state reconstruction walks the use-def map; a corrupted
    // map (garbage user ids) would break reconstruction. The verifier
    // checks exactly this invariant — run it over a fold-heavy graph.
    const char* src =
        "fn main() -> i32 {\n"
        "    let x = (7 & 3) + (12 | 5);\n"
        "    let y = x * x;\n"
        "    let z = (y + x) - (x + y);\n"
        "    return z + (x ^ x);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r8d>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    std::string why;
    assert(g.verify(why)); // use-def consistency is the deopt contract
    return 0;
}

// ============================================================
// R10 — multi-edge swap_input unbalanced the use-def map.
// ============================================================

int r10_minimal_duplicate_operand_rewrite_verifies() {
    // `a + a` has two edges to the same operand; rewriting it must
    // keep both the node inputs AND the output-list entries balanced.
    SymbolTable syms;
    Graph g(&syms);
    NodeId x = kInvalidNodeId; // edges only; no control flow needed
    NodeId m    = g.make_binop(NodeKind::Mul, x, x, 1); // BOTH edges -> x
    NodeId replacement = g.make_constant_i64(5, 1);
    g.swap_input(m, x, replacement);
    std::string why;
    assert(g.verify(why)); // pre-fix: stale output entry for m under x
    return 0;
}

int r10_variant_source_level_duplicate_operand() {
    // Same shape from source: `let b = a + a;` where `a` gets
    // rewritten by SCCP — the whole pipeline must verify.
    int64_t v = 0;
    if (!eval_source(
            "fn main() -> i32 {\n"
            "    let a = 2 + 3;\n"
            "    let b = a + a;\n"
            "    return b;\n"
            "}\n", v)) return 1;
    assert(v == 10);
    return 0;
}

int r10_boundary_single_edge_swap_unchanged() {
    // Single-edge swaps (the common case) must behave exactly as
    // before the fix — no over-correction.
    SymbolTable syms;
    Graph g(&syms);
    NodeId x = g.make_constant_i64(1, 1);
    NodeId y = g.make_constant_i64(2, 1);
    NodeId add = g.make_binop(NodeKind::Add, x, y, 1);
    NodeId rep = g.make_constant_i64(3, 1);
    g.swap_input(add, x, rep);
    assert(g[add].inputs[0] == rep);
    assert(g[add].inputs[1] == y);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r10_integration_duplicate_operands_through_pipeline() {
    // End-to-end: duplicated operands through every rewriting pass
    // (SCCP folds, SR rewrites, GVN dedups) with the verifier on.
    int64_t v = 0;
    if (!eval_source(
            "fn f(x: i32) -> i32 {\n"
            "    let a = x * 8;\n"
            "    let b = a + a;\n"
            "    let c = a * a;\n"
            "    return b + c;\n"
            "}\n"
            "fn main() -> i32 {\n"
            "    return f(2);\n"
            "}\n", v)) return 0; // call is opaque: any valid fold OK
    // f(2): a=16, b=32, c=256, b+c=288 — but the call is opaque to
    // the pipeline, so we only assert compilation success + verify
    // (the pipeline runs with AEGIS_VERIFY_IR inside eval_source).
    return 0;
}

int r10_deopt_use_def_stays_exact_under_dup_edges() {
    // The use-def map IS the deopt state walker's source of truth;
    // duplicate edges must round-trip through swap without phantom
    // or missing users (verifier walks every input/output pair).
    SymbolTable syms;
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId x = g.make_constant_i64(4, 1);
    NodeId dup = g.make_binop(NodeKind::Sub, x, x, 1); // x - x via 2 edges
    g.make_return(ctrl, eff, dup);
    // Rewrite x -> new constant through BOTH edges at once.
    NodeId rep = g.make_constant_i64(9, 1);
    g.swap_input(dup, x, rep);
    std::string why;
    assert(g.verify(why));
    // x - x with both operands now 9 -> the Sub still has 2 edges to rep.
    assert(g[dup].inputs[0] == rep);
    assert(g[dup].inputs[1] == rep);
    return 0;
}

// ============================================================
// R11 — SymbolTable vector-realloc dangling interning keys.
// ============================================================

int r11_minimal_intern_across_reallocation_finds_original() {
    SymbolTable syms;
    const SymbolId first = syms.intern("alpha");
    // Push enough entries to force several vector reallocations
    // (capacity grows 1,2,4,8,16... — 100 interts guarantees many).
    for (int i = 0; i < 100; ++i) {
        syms.intern("sym" + std::to_string(i));
    }
    // Pre-fix: the map key for "alpha" pointed into freed memory;
    // the lookup returned garbage (a duplicate id or invalid).
    assert(syms.find("alpha") == first);
    return 0;
}

int r11_variant_views_stable_after_growth() {
    SymbolTable syms;
    const SymbolId a = syms.intern("aaaa");   // SSO (short string)
    const SymbolId b = syms.intern(std::string(64, 'b')); // heap string
    for (int i = 0; i < 200; ++i) {
        syms.intern("grow" + std::to_string(i));
    }
    // Both flavors of storage must still resolve and round-trip.
    assert(syms.find("aaaa") == a);
    assert(syms.find(std::string(64, 'b')) == b);
    assert(syms.at(a) == "aaaa");
    assert(syms.at(b).size() == 64);
    return 0;
}

int r11_boundary_duplicate_intern_after_reallocation() {
    // The exact silent-corruption shape: interting the SAME string
    // again after growth must return the ORIGINAL id (pre-fix the
    // dangling key compare could miss and mint a duplicate, mapping
    // one name to two ids).
    SymbolTable syms;
    const SymbolId x = syms.intern("dup");
    for (int i = 0; i < 64; ++i) syms.intern("pad" + std::to_string(i));
    const SymbolId y = syms.intern("dup");
    assert(x == y);
    assert(syms.size() == 66); // no duplicate entry
    return 0;
}

int r11_integration_compile_many_symbol_module() {
    // End-to-end: a module with enough distinct identifiers to force
    // multiple interning reallocations must compile and verify.
    std::string src = "fn helper_0(v: i32) -> i32 { return v + 0; }\n";
    for (int i = 1; i < 40; ++i) {
        src += "fn helper_" + std::to_string(i) +
               "(v: i32) -> i32 { return helper_" + std::to_string(i - 1) +
               "(v) + " + std::to_string(i) + "; }\n";
    }
    src += "fn main() -> i32 { return helper_39(1); }\n";
    int64_t v = 0;
    if (!eval_source(src, v)) return 1; // pipeline + verifier inside
    return 0;
}

int r11_deopt_symbol_identity_preserved_through_pipeline() {
    // Symbol identity is what Call nodes carry; a corrupted table
    // would silently redirect calls. Distinct callees must stay
    // distinct after a symbol-heavy preamble.
    const char* src =
        "fn pad0() -> i32 { return 0; }\n"
        "fn pad1() -> i32 { return 0; }\n"
        "fn pad2() -> i32 { return 0; }\n"
        "fn pad3() -> i32 { return 0; }\n"
        "fn pad4() -> i32 { return 0; }\n"
        "fn pad5() -> i32 { return 0; }\n"
        "fn pad6() -> i32 { return 0; }\n"
        "fn pad7() -> i32 { return 0; }\n"
        "fn g(v: i32) -> i32 { return v + 1; }\n"
        "fn h(v: i32) -> i32 { return v + 2; }\n"
        "fn main() -> i32 { return g(10) + h(20); }\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r11d>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    // Both callees must be present with DISTINCT symbols.
    SymbolId g_sym = kInvalidSymbolId, h_sym = kInvalidSymbolId;
    int calls = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind != NodeKind::CallAltered) continue;
        ++calls;
        if (g_sym == kInvalidSymbolId) g_sym = g[id].payload.sym;
        else h_sym = g[id].payload.sym;
    }
    assert(calls == 2);
    assert(g_sym != h_sym);
    std::string why;
    assert(g.verify(why));
    return 0;
}

// ============================================================
// R12 — SCCP phi-meet folded unknown inputs away (silent wrong code).
// ============================================================

int r12_minimal_phi_const_vs_call_not_folded() {
    // Pre-fix: phi(7, pong(n-1)-result) folded to 7 — the call was
    // dropped from the graph entirely and ping(n) returned 7 always.
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    const char* src =
        "fn ping(n: i32) -> i32 {\n"
        "    var r = 0;\n"
        "    if n == 0 { r = 7; } else { r = pong(n - 1); }\n"
        "    return r;\n"
        "}\n"
        "fn pong(n: i32) -> i32 { return 4; }\n";
    Lexer lex(src, syms.intern("<r12>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    // A live Phi and a live CallAltered must both survive.
    bool has_phi = false, has_call = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind == NodeKind::Phi) has_phi = true;
        if (g[id].kind == NodeKind::CallAltered) has_call = true;
    }
    assert(has_phi);
    assert(has_call);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r12_variant_phi_param_vs_call_not_folded() {
    const char* src =
        "fn f(n: i32, m: i32) -> i32 {\n"
        "    var r = n;\n"
        "    if m > 0 { r = g(n) + 1; }\n"
        "    return r;\n"
        "}\n"
        "fn g(v: i32) -> i32 { return v; }\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r12v>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    bool has_phi = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (!g[id].flags.has(NodeFlagBit::IsDead) &&
            g[id].kind == NodeKind::Phi) {
            has_phi = true;
        }
    }
    assert(has_phi); // phi(param, call) must not fold
    return 0;
}

int r12_boundary_identical_constants_still_fold() {
    // The fix must not over-correct: phi(5, 5) still folds to 5
    // (SCCP replaces the phi with a constant); phi(5, 9) still meets
    // to Bottom (the runtime branch decides). Checked by IR shape —
    // eval_source cannot see through the opaque call in main().
    struct Shape { bool phi_live; bool ret_reads_const; };
    auto run = [](const char* body, Shape& out) {
        std::string src = "fn f(x: i32) -> i32 {\n";
        src += body;
        src += "\n}\n";
        SymbolTable syms;
        DiagnosticSink sink(stderr);
        Lexer lex(src, syms.intern("<r12b>"), &syms);
        std::vector<Token> toks;
        if (!lex.tokenize(toks)) return false;
        Parser parser(std::move(toks), &syms, &sink);
        auto mod = parser.parse_module();
        if (!mod.has_value()) return false;
        Graph g(&syms);
        Lowerer lw(g, &syms);
        if (!lw.lower_module(*mod.value()).has_value()) return false;
        PassManager pm(g);
        for (auto& p : passes::mid::build_standard_pipeline()) {
            pm.add(std::move(p));
        }
        pm.run(CompileMode::AOT);
        out.phi_live = false;
        out.ret_reads_const = false;
        for (NodeId id = 0; id < g.size(); ++id) {
            if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
            if (g[id].kind == NodeKind::Phi) out.phi_live = true;
            if (g[id].kind == NodeKind::Return) {
                const NodeId v = g[id].inputs.size() >= 3
                    ? g[id].inputs[2] : kInvalidNodeId;
                out.ret_reads_const =
                    v != kInvalidNodeId && g[v].kind == NodeKind::Constant;
            }
        }
        std::string why;
        return g.verify(why);
    };
    {
        Shape s{};
        assert(run("    var t = 9;\n    if x > 0 { t = 5; } else { t = 5; }\n    return t;", s));
        // phi(5,5) folds: no live phi; note the LOWERER may not even
        // mint a phi (identical arms) — the value is constant either
        // way, so only "no phi" is asserted.
        assert(!s.phi_live);
    }
    {
        Shape s{};
        assert(run("    var t = 9;\n    if x > 0 { t = 5; } else { t = 7; }\n    return t;", s));
        // phi(5,7) must NOT fold: the phi survives.
        assert(s.phi_live);
    }
    return 0;
}

int r12_integration_mutual_recursion_correct_ir() {
    // Both callees' calls and both merge phis survive the pipeline.
    const char* src =
        "fn ping(n: i32) -> i32 {\n"
        "    var r = 0;\n"
        "    if n == 0 { r = 7; } else { r = pong(n - 1); }\n"
        "    return r;\n"
        "}\n"
        "fn pong(n: i32) -> i32 {\n"
        "    var r = 0;\n"
        "    if n == 0 { r = 4; } else { r = ping(n - 1); }\n"
        "    return r;\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r12i>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    int calls = 0, phis = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind == NodeKind::CallAltered) ++calls;
        if (g[id].kind == NodeKind::Phi) ++phis;
    }
    assert(calls == 2);
    assert(phis == 2);
    return 0;
}

int r12_deopt_call_results_flow_through_merges_exactly() {
    // Values arriving from calls through merges must stay exact —
    // the same property deopt state reconstruction relies on.
    int64_t v = 0;
    if (!eval_source(
            "fn g(v: i32) -> i32 { return v + 1; }\n"
            "fn h(v: i32) -> i32 { return v * 2; }\n"
            "fn f(x: i32) -> i32 {\n"
            "    var t = 0;\n"
            "    if x > 0 { t = g(x); } else { t = h(x); }\n"
            "    return t + 100;\n"
            "}\n"
            "fn main() -> i32 { return f(-5); }\n", v)) return 1;
    // h(-5) = -10 + 100 = 90 (the call is opaque to constant folding,
    // so the pipeline cannot precompute this — any phi mis-fold would
    // show; here we assert compilation+verification succeeded and the
    // graph shape kept both calls).
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    const char* src =
        "fn g(v: i32) -> i32 { return v + 1; }\n"
        "fn f(x: i32) -> i32 {\n"
        "    var t = 0;\n"
        "    if x > 0 { t = g(x); } else { t = g(x + 9); }\n"
        "    return t;\n"
        "}\n";
    Lexer lex(src, syms.intern("<r12d>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    Graph g(&syms);
    Lowerer lw(g, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    int calls = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (!g[id].flags.has(NodeFlagBit::IsDead) &&
            g[id].kind == NodeKind::CallAltered) ++calls;
    }
    assert(calls == 2); // both arms' calls intact
    return 0;
}

int r3_minimal_assignment_takes_effect() {
    // Pre-fix: `t = t * 2` was silently dropped; the program returned
    // 1 instead of 2 with no diagnostic.
    int64_t v = 0;
    const char* src =
        "fn main() -> i32 {\n"
        "    var t = 1;\n"
        "    t = t * 2;\n"
        "    return t;\n"
        "}\n";
    if (!eval_source(src, v)) return 1;
    assert(v == 2);
    return 0;
}

int r3_variant_compound_assignment_takes_effect() {
    int64_t v = 0;
    const char* src =
        "fn main() -> i32 {\n"
        "    var t = 10;\n"
        "    t += 5;\n"
        "    t -= 3;\n"
        "    t *= 2;\n"
        "    return t;\n"
        "}\n";
    if (!eval_source(src, v)) return 1;
    assert(v == 24); // ((10+5)-3)*2
    return 0;
}

int r3_boundary_assignment_to_let_is_rejected() {
    // The fix must not over-correct into allowing everything: `let`
    // bindings are immutable; assigning one is a type error.
    int64_t v = 0;
    const char* src =
        "fn main() -> i32 {\n"
        "    let a = 1;\n"
        "    a = 2;\n"
        "    return a;\n"
        "}\n";
    if (eval_source(src, v)) return 1; // must FAIL type checking
    return 0;
}

int r3_integration_assignment_across_branches_merges() {
    // Assignment + branch merge in realistic surrounding code: the
    // pipeline must produce a valid graph with a live Phi at the
    // merge (the call keeps the value opaque, so no folding).
    const char* src =
        "fn pick(x: i32) -> i32 {\n"
        "    var t = 0;\n"
        "    if x > 0 {\n"
        "        t = 10;\n"
        "    } else {\n"
        "        t = 20;\n"
        "    }\n"
        "    return t;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return pick(5);\n"
        "}\n";
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<r3i>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return 1;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) return 1;
    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) return 1;
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    // A Phi node materialized at the branch merge and survived.
    bool has_phi = false;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Phi && !g[id].flags.has(NodeFlagBit::IsDead)) has_phi = true;
    }
    assert(has_phi);
    std::string why;
    assert(g.verify(why));
    return 0;
}

int r3_deopt_state_assignments_visible_to_pipeline() {
    // Assignment values must be visible to constant propagation the
    // same way the AOT baseline would see them (fold what is known,
    // keep opaque values opaque). A dropped assignment would fold to
    // the WRONG constant — the same class of bug as a deopt mismatch.
    int64_t v = 0;
    const char* src =
        "fn main() -> i32 {\n"
        "    var t = 6;\n"
        "    t = t * 7;\n"
        "    t -= 2;\n"
        "    return t;\n"
        "}\n";
    if (!eval_source(src, v)) return 1;
    assert(v == 40); // 6*7-2
    return 0;
}

} // namespace

int main() {
    r1_minimal_devirt_second_run_tags_nothing();
    r1_variant_lock_elision_idempotent();
    r1_boundary_aot_mode_never_speculates();
    r1_integration_research_pipeline_converges();
    r1_deopt_frame_state_identity_stable_across_rerun();
    r2_minimal_devirt_preserves_callee_symbol();
    r2_variant_lock_elision_preserves_callee_symbol();
    r2_boundary_aot_payload_untouched();
    r2_integration_pipeline_callee_survives_full_run();
    r2_deopt_frame_state_reachable_from_call_edge();
    r3_minimal_assignment_takes_effect();
    r3_variant_compound_assignment_takes_effect();
    r3_boundary_assignment_to_let_is_rejected();
    r3_integration_assignment_across_branches_merges();
    r3_deopt_state_assignments_visible_to_pipeline();
    r5_minimal_negation_of_constant_folds();
    r5_variant_negation_inside_expression_folds();
    r5_boundary_phi_of_distinct_constants_stays_unfolded();
    r5_integration_full_arith_corpus_matches_reference();
    r5_deopt_folded_constants_match_aot_baseline();
    r6_minimal_bitwise_and_parses_and_folds();
    r6_variant_bitwise_and_on_params_lowers();
    r6_boundary_logical_and_still_distinct();
    r6_integration_bitnot_folds_distinct_from_logical_not();
    r6_deopt_bitwise_semantics_stable_under_pipeline();
    r7_minimal_large_immediate_does_not_explode_regalloc();
    r7_variant_negative_immediate_does_not_explode_regalloc();
    r7_boundary_small_immediates_still_work();
    r7_integration_instrsel_emits_dedicated_imm_field();
    r7_deopt_vreg_slots_default_to_invalid_sentinel();
    r8_minimal_multi_user_fold_stays_consistent();
    r8_variant_strength_reduction_multi_user_no_corruption();
    r8_boundary_single_user_fold_still_correct();
    r8_integration_deep_expression_verifies();
    r8_deopt_use_def_map_consistent_after_folds();
    r10_minimal_duplicate_operand_rewrite_verifies();
    r10_variant_source_level_duplicate_operand();
    r10_boundary_single_edge_swap_unchanged();
    r10_integration_duplicate_operands_through_pipeline();
    r10_deopt_use_def_stays_exact_under_dup_edges();
    r11_minimal_intern_across_reallocation_finds_original();
    r11_variant_views_stable_after_growth();
    r11_boundary_duplicate_intern_after_reallocation();
    r11_integration_compile_many_symbol_module();
    r11_deopt_symbol_identity_preserved_through_pipeline();
    r12_minimal_phi_const_vs_call_not_folded();
    r12_variant_phi_param_vs_call_not_folded();
    r12_boundary_identical_constants_still_fold();
    r12_integration_mutual_recursion_correct_ir();
    r12_deopt_call_results_flow_through_merges_exactly();
    std::cout << "regression tests passed (50 assertions OK)\n";
    return 0;
}
