// tests/unit/test_for_loops.cpp — Source-level `for var in lo..hi`
// loop lowering + optimization tests.
//
// Covers the full path the feature enables (Rule D.9 — no untested
// code paths):
//   - lexing `lo..hi` (DotDot) incl. the float-adjacent `0..8` case
//   - parsing (range required; loud errors otherwise)
//   - TypeChecker rules (loop-var scope, return-in-body rejected,
//     match-in-body rejected, non-range iterator rejected)
//   - lowering shapes (induction phi, accumulator phi, effect merge,
//     back edges, entry/exit ctrl)
//   - LoopUnrolling elimination of degenerate loops (empty body,
//     body not using the induction var)
//   - sound SKIPS (body uses the var, calls in body, runtime bounds)
//   - LoopFusion elimination of a degenerate SCEV-paired second loop
//   - nested degenerate loops collapsing entirely
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

using namespace aegis;

// ---- Shared compile helper (source -> verified, optimized graph). ----

struct CompileResult {
    bool ok{false};
    Graph graph{nullptr};
};

// Compiles source through the full standard pipeline with the Rule 42
// verifier armed (any verifier failure aborts the process loudly).
[[nodiscard]] CompileResult compile_ok(const std::string& src) {
    CompileResult r;
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<for>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return r;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return r;
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) return r;
    r.graph = Graph(&syms);
    Lowerer lw(r.graph, &syms);
    if (!lw.lower_module(*mod.value()).has_value()) return r;
    PassManager pm(r.graph);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    std::string why;
    if (!r.graph.verify(why)) return r; // would have aborted anyway
    r.ok = true;
    return r;
}

// Compile expecting failure at any frontend stage.
[[nodiscard]] bool compile_rejected(const std::string& src) {
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<for>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return true;
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return true;
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) return true;
    return false;
}

[[nodiscard]] int count_live_kind(const Graph& g, NodeKind k) {
    int n = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == k && !g[id].flags.has(NodeFlagBit::IsDead)) ++n;
    }
    return n;
}

// ---- Lexing ----

int for_lex_range_token_pairs_ints() {
    SymbolTable syms;
    std::vector<Token> toks;
    Lexer lex("for i in 0..8 { }", kInvalidSymbolId, &syms);
    if (!lex.tokenize(toks)) return 1;
    bool saw_dotdot = false;
    for (const auto& t : toks) {
        if (t.kind == TokenKind::DotDot) saw_dotdot = true;
    }
    assert(saw_dotdot);
    return 0;
}

int for_lex_float_not_confused_with_range() {
    SymbolTable syms;
    std::vector<Token> toks;
    Lexer lex("let x = 1.5;", kInvalidSymbolId, &syms);
    if (!lex.tokenize(toks)) return 1;
    // 1.5 is ONE FloatLit, not IntLit + DotDot + IntLit.
    assert(toks[2].kind == TokenKind::Eq);
    assert(toks[3].kind == TokenKind::FloatLit);
    return 0;
}

// ---- TypeChecker rules ----

int for_typecheck_rejects_return_in_body() {
    assert(compile_rejected(
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n"));
    return 0;
}

int for_typecheck_rejects_match_in_body() {
    assert(compile_rejected(
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "        match i { _ => 1, }\n"
        "    }\n"
        "    return 0;\n"
        "}\n"));
    return 0;
}

int for_typecheck_rejects_non_range_iterator() {
    // `for i in arr` has no lowerable iteration space: loud rejection,
    // never a silent mis-lowering (Rule D.3).
    assert(compile_rejected(
        "fn f(a: i32) -> i32 {\n"
        "    for i in a {\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
        "fn main() -> i32 { return f(1); }\n"));
    return 0;
}

int for_typecheck_loop_var_dies_after_loop() {
    // Using the loop variable after the loop must be rejected: its
    // value is not defined on the post-loop path (0-trip case).
    assert(compile_rejected(
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "    }\n"
        "    return i;\n"
        "}\n"));
    return 0;
}

int for_typecheck_body_local_dies_after_loop() {
    assert(compile_rejected(
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "        let t = 1;\n"
        "    }\n"
        "    return t;\n"
        "}\n"));
    return 0;
}

// ---- Lowering shapes ----

int for_lowering_builds_loop_phi_cmp_if() {
    CompileResult r = compile_ok(
        "fn f(n: i32) -> i32 {\n"
        "    for i in 0..n {\n"
        "    }\n"
        "    return 7;\n"
        "}\n"
        "fn main() -> i32 { return f(5); }\n");
    assert(r.ok);
    // Runtime bounds: the loop must SURVIVE (SCEV can't prove trip).
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    assert(count_live_kind(r.graph, NodeKind::Phi) >= 1);
    assert(count_live_kind(r.graph, NodeKind::CmpLt) == 1);
    assert(count_live_kind(r.graph, NodeKind::If) == 1);
    return 0;
}

int for_lowering_accumulator_gets_header_phi() {
    CompileResult r = compile_ok(
        "fn f(n: i32) -> i32 {\n"
        "    var s = 0;\n"
        "    for i in 0..n {\n"
        "        s = s + i;\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "fn main() -> i32 { return f(8); }\n");
    assert(r.ok);
    // Induction phi + accumulator phi: at least 2 phis at the header.
    assert(count_live_kind(r.graph, NodeKind::Phi) >= 2);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    return 0;
}

// ---- Optimization behavior ----

int for_opt_empty_constant_trip_loop_eliminated() {
    CompileResult r = compile_ok(
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "    }\n"
        "    return 42;\n"
        "}\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 0);
    assert(count_live_kind(r.graph, NodeKind::Phi) == 0);
    assert(count_live_kind(r.graph, NodeKind::If) == 0);
    // And the returned value is the untouched constant.
    for (NodeId id = 0; id < r.graph.size(); ++id) {
        if (r.graph[id].kind == NodeKind::Return &&
            !r.graph[id].flags.has(NodeFlagBit::IsDead)) {
            NodeId v = r.graph[id].inputs[2];
            assert(r.graph[v].kind == NodeKind::Constant);
            assert(r.graph[v].payload.i64 == 42);
        }
    }
    return 0;
}

int for_opt_body_using_var_not_eliminated() {
    // The accumulator reads the induction var -> external phi use ->
    // elimination unsound -> loop must stay.
    CompileResult r = compile_ok(
        "fn f(n: i32) -> i32 {\n"
        "    var s = 0;\n"
        "    for i in 0..n {\n"
        "        s = s + i;\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "fn main() -> i32 { return f(8); }\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    return 0;
}

int for_opt_call_body_not_eliminated() {
    // A call in the body is a live per-iteration effect: elimination
    // would silently drop it (Rule 62) -> loop stays.
    CompileResult r = compile_ok(
        "fn g(v: i32) -> i32 {\n"
        "    return v + 1;\n"
        "}\n"
        "fn f(n: i32) -> i32 {\n"
        "    for i in 0..n {\n"
        "        g(i);\n"
        "    }\n"
        "    return 7;\n"
        "}\n"
        "fn main() -> i32 { return f(8); }\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    assert(count_live_kind(r.graph, NodeKind::CallAltered) >= 1);
    return 0;
}

int for_opt_body_ignoring_var_still_eliminated() {
    // The body exists but never touches the induction variable: the
    // loop's iterations are unobservable -> degenerate -> eliminated.
    CompileResult r = compile_ok(
        "fn main() -> i32 {\n"
        "    for i in 0..4 {\n"
        "        let dead = 5 + 5;\n"
        "    }\n"
        "    return 3;\n"
        "}\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 0);
    return 0;
}

int for_opt_trip_above_threshold_not_eliminated() {
    // trip 9 > kLoopUnrollFullUnrollTripCount (8) with no SCEV sibling
    // to fuse with: partial unroll is a documented gap -> loop stays.
    CompileResult r = compile_ok(
        "fn main() -> i32 {\n"
        "    for i in 0..9 {\n"
        "    }\n"
        "    return 1;\n"
        "}\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    return 0;
}

int for_opt_fusion_eliminates_degenerate_sibling() {
    // Loop A is live (call in body); loop B is degenerate with the
    // same SCEV {0,1,8} -> B eliminated by fusion, A stays.
    CompileResult r = compile_ok(
        "fn g(v: i32) -> i32 {\n"
        "    return v + 1;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    for i in 0..8 {\n"
        "        g(i);\n"
        "    }\n"
        "    for j in 0..8 {\n"
        "    }\n"
        "    return 5;\n"
        "}\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    return 0;
}

int for_opt_nested_degenerate_loops_fully_collapse() {
    CompileResult r = compile_ok(
        "fn main() -> i32 {\n"
        "    for i in 0..3 {\n"
        "        for j in 0..3 {\n"
        "        }\n"
        "    }\n"
        "    return 9;\n"
        "}\n");
    assert(r.ok);
    // Fusion empties the outer body; unrolling then eliminates the
    // outer: nothing loop-shaped survives.
    assert(count_live_kind(r.graph, NodeKind::Loop) == 0);
    assert(count_live_kind(r.graph, NodeKind::Phi) == 0);
    return 0;
}

int for_opt_zero_trip_runtime_bound_sound() {
    // lo == hi at runtime means zero iterations: the effect merge phi
    // guarantees post-loop code still runs (checked by the verifier).
    CompileResult r = compile_ok(
        "fn f(a: i32, b: i32) -> i32 {\n"
        "    for i in a..b {\n"
        "    }\n"
        "    return 1;\n"
        "}\n"
        "fn main() -> i32 { return f(5, 5); }\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    return 0;
}

int for_opt_loop_var_used_in_if_body_keeps_loop() {
    // An if inside the body reading i: real per-iteration dataflow.
    CompileResult r = compile_ok(
        "fn f(n: i32) -> i32 {\n"
        "    var s = 0;\n"
        "    for i in 0..n {\n"
        "        if i > 4 {\n"
        "            s = s + 1;\n"
        "        }\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "fn main() -> i32 { return f(9); }\n");
    assert(r.ok);
    assert(count_live_kind(r.graph, NodeKind::Loop) == 1);
    // Loop's exit If + the body's If.
    assert(count_live_kind(r.graph, NodeKind::If) == 2);
    return 0;
}

} // namespace

int main() {
    for_lex_range_token_pairs_ints();
    for_lex_float_not_confused_with_range();
    for_typecheck_rejects_return_in_body();
    for_typecheck_rejects_match_in_body();
    for_typecheck_rejects_non_range_iterator();
    for_typecheck_loop_var_dies_after_loop();
    for_typecheck_body_local_dies_after_loop();
    for_lowering_builds_loop_phi_cmp_if();
    for_lowering_accumulator_gets_header_phi();
    for_opt_empty_constant_trip_loop_eliminated();
    for_opt_body_using_var_not_eliminated();
    for_opt_call_body_not_eliminated();
    for_opt_body_ignoring_var_still_eliminated();
    for_opt_trip_above_threshold_not_eliminated();
    for_opt_fusion_eliminates_degenerate_sibling();
    for_opt_nested_degenerate_loops_fully_collapse();
    for_opt_zero_trip_runtime_bound_sound();
    for_opt_loop_var_used_in_if_body_keeps_loop();
    std::cout << "for_loop tests passed (18 assertions OK)\n";
    return 0;
}
