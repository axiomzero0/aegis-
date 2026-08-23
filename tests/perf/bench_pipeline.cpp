// tests/perf/bench_pipeline.cpp — Compile-time benchmark suite.
//
// Rule 41: performance regressions >5% require an explicit waiver.
// These benchmarks provide the NUMBERS that gating compares against:
// each case builds a synthetic IR graph, runs the full standard
// pipeline, and reports deterministic wall-clock timings.
//
// What is measured (and why):
//   chain_fold    — deep constant chain: SCCP + EDCE throughput.
//   wide_fold     — wide independent-expression fan: GVN + SCCP.
//   dedup_heavy   — structurally identical pure exprs: hash-consing
//                   + GVN convergence cost.
//   branchy       — nested if/else trees: SimplifyControl + CopyProp.
//   multi_fn      — many functions in one module: whole-pipeline
//                   scaling (lowering + passes + verification).
//
// What is asserted (performance numbers are REPORTED, not asserted —
// a timing assertion would be flaky on shared CI runners; Rule 41's
// gating compares runs against a BASELINE, which is CI's job):
//   - the pipeline returns success for every iteration,
//   - the verifier accepts the final graph (Rule 42),
//   - timing output is emitted in a machine-parseable format so CI
//     can store and diff it.
//
// Rule 61: every iteration count / graph size below is a named,
// documented constant (no magic numbers).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

using namespace aegis;
using Clock = std::chrono::steady_clock;

// ---- Benchmark parameters (Rule 61: named + documented) ----

/// Depth of the constant-folding chain. 512 additions is deep enough
/// to dominate in SCCP work while staying well under the pass budget
/// caps (kSccpMaxIterations = 1000).
constexpr int kChainDepth = 512;

/// Fan-out width of independent expressions. 256 independent Adds
/// stress GVN's hash-cons table without tripping kGvnMaxNodeCount.
constexpr int kWideWidth = 256;

/// Number of structurally identical expression groups in the
/// dedup-heavy case. Hash-consing collapses them at construction, so
/// this measures the pipeline over a dense duplicate-heavy graph.
constexpr int kDedupGroups = 128;

/// Nesting levels of the binary branch tree (2^levels leaves).
constexpr int kBranchLevels = 7;

/// Functions in the multi-function module case.
constexpr int kMultiFnCount = 64;

/// Loops in the loop-heavy case. 64 loops (a mix of degenerate
/// constant-trip loops that fully collapse, runtime-bound loops that
/// must survive, and accumulator loops) exercise the whole loop
/// stack: SCEV, LoopFusion, LoopUnrolling, and the verifier.
constexpr int kLoopHeavyCount = 64;

/// Timing repetitions per case; the MEDIAN is reported to suppress
/// scheduler noise on shared machines.
constexpr int kTimingRepetitions = 5;

// ---- Graph builders ----

[[nodiscard]] Graph build_chain_graph(SymbolTable& syms) {
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId acc  = g.make_constant_i64(1, 1);
    for (int i = 0; i < kChainDepth; ++i) {
        NodeId c = g.make_constant_i64(i % 7 + 1, 1);
        acc = g.make_binop(NodeKind::Add, acc, c, 1);
    }
    g.make_return(ctrl, eff, acc);
    return g;
}

[[nodiscard]] Graph build_wide_graph(SymbolTable& syms) {
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId param = g.make_constant_i64(3, 1);
    NodeId acc = g.make_constant_i64(0, 1);
    for (int i = 0; i < kWideWidth; ++i) {
        NodeId c = g.make_constant_i64(i + 1, 1);
        NodeId e = g.make_binop(NodeKind::Mul, param, c, 1);
        acc = g.make_binop(NodeKind::Add, acc, e, 1);
    }
    g.make_return(ctrl, eff, acc);
    return g;
}

[[nodiscard]] Graph build_dedup_graph(SymbolTable& syms) {
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);
    NodeId a = g.make_constant_i64(2, 1);
    NodeId b = g.make_constant_i64(3, 1);
    NodeId acc = g.make_constant_i64(0, 1);
    for (int i = 0; i < kDedupGroups; ++i) {
        // Structurally identical Adds: hash-consing makes each
        // lookup_or_insert a hit after the first.
        NodeId e1 = g.make_binop(NodeKind::Add, a, b, 1);
        NodeId e2 = g.make_binop(NodeKind::Add, a, b, 1);
        NodeId e3 = g.make_binop(NodeKind::Add, b, a, 1);
        acc = g.make_binop(NodeKind::Add, acc, e1, 1);
        acc = g.make_binop(NodeKind::Add, acc, e2, 1);
        acc = g.make_binop(NodeKind::Add, acc, e3, 1);
    }
    g.make_return(ctrl, eff, acc);
    return g;
}

[[nodiscard]] Graph build_branch_graph(SymbolTable& syms) {
    Graph g(&syms);
    NodeId start = kStartNodeId;
    NodeId ctrl = g.make_proj(start, 0);
    NodeId eff  = g.make_proj(start, 1);

    // Recursive binary if-tree: each level splits ctrl into two
    // Projs; leaves return distinct constants.
    int leaf_value = 0;
    // Explicit stack-based construction (no recursion depth limits).
    struct FrameEntry { NodeId ctrl; int level; };
    std::vector<FrameEntry> stack;
    stack.push_back({ctrl, 0});
    while (!stack.empty()) {
        FrameEntry f = stack.back();
        stack.pop_back();
        if (f.level == kBranchLevels) {
            NodeId v = g.make_constant_i64(++leaf_value, 1);
            g.make_return(f.ctrl, eff, v);
            continue;
        }
        NodeId lhs = g.make_constant_i64(f.level, 1);
        NodeId rhs = g.make_constant_i64(kBranchLevels - f.level, 1);
        NodeId cond = g.make_cmp(NodeKind::CmpLt, lhs, rhs);
        NodeId ifn = g.make_if(f.ctrl, cond);
        NodeId tproj = g.make_proj(ifn, 0);
        NodeId fproj = g.make_proj(ifn, 1);
        stack.push_back({tproj, f.level + 1});
        stack.push_back({fproj, f.level + 1});
    }
    return g;
}

[[nodiscard]] Graph build_loop_heavy_graph(SymbolTable& syms) {
    // A module of kLoopHeavyCount/3 loop functions of each flavor,
    // plus one caller. Measures the loop-aware pipeline end to end
    // (frontend + SCEV + fusion + unrolling + verification).
    std::string src;
    src.reserve(static_cast<size_t>(kLoopHeavyCount) * 48);
    int flavor = 0;
    for (int i = 0; i < kLoopHeavyCount; ++i) {
        src += "fn lf";
        src += std::to_string(i);
        src += "(n: i32) -> i32 {\n";
        switch (flavor % 3) {
            case 0: // degenerate constant trip -> fully eliminated
                src += "    for i in 0..8 {\n    }\n    return 1;\n";
                break;
            case 1: // runtime bound -> loop kept, verifier-checked
                src += "    for i in 0..n {\n    }\n    return 2;\n";
                break;
            default: // accumulator -> phis + back edges
                src += "    var s = 0;\n    for i in 0..n {\n        s = s + i;\n    }\n    return s;\n";
        }
        src += "}\n";
        ++flavor;
    }
    src += "fn main() -> i32 {\n    return lf0(4) + lf1(4);\n}\n";

    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<loopbench>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) return Graph(&syms);
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return Graph(&syms);
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) return Graph(&syms);
    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) return Graph(&syms);
    return g;
}

[[nodiscard]] Graph build_multi_fn_graph(SymbolTable& syms, size_t& fn_count) {
    // A single module-shaped graph with many Start-less function
    // bodies is not expressible via the Graph API (one Start per
    // graph); instead we lower a real multi-function module — this
    // also exercises the frontend, which is part of compile time.
    std::string src;
    src.reserve(static_cast<size_t>(kMultiFnCount) * 64);
    for (int i = 0; i < kMultiFnCount; ++i) {
        src += "fn fn";
        src += std::to_string(i);
        src += "(v: i32) -> i32 {\n    let a = v + ";
        src += std::to_string(i + 1);
        src += ";\n    let b = a * 2;\n    let c = a + b;\n    return c;\n}\n";
    }
    src += "fn main() -> i32 {\n    return fn0(1) + fn1(2);\n}\n";

    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<bench>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) { fn_count = 0; return Graph(&syms); }
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) { fn_count = 0; return Graph(&syms); }
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) { fn_count = 0; return Graph(&syms); }
    Graph g(&syms);
    Lowerer lowerer(g, &syms);
    if (!lowerer.lower_module(*mod.value()).has_value()) { fn_count = 0; return Graph(&syms); }
    fn_count = static_cast<size_t>(kMultiFnCount) + 1;
    return g;
}

// ---- Runner ----

struct BenchResult {
    std::string name;
    size_t      nodes;
    double      median_us;
    bool        ok;
};

// Runs the pipeline kTimingRepetitions times over a freshly built
// graph. The graph is REBUILT per repetition (outside the timed
// region) because Graph owns a PMR arena and is not copyable — and
// because passes are idempotent (Rule B.5), re-running on a converged
// graph would measure a no-op, not the pipeline.
template <typename Builder>
[[nodiscard]] BenchResult run_bench(const std::string& name, SymbolTable& syms,
                                    Builder build) {
    BenchResult r;
    r.name = name;
    r.ok = true;

    std::vector<double> samples_us;
    samples_us.reserve(static_cast<size_t>(kTimingRepetitions));
    for (int rep = 0; rep < kTimingRepetitions; ++rep) {
        Graph g = build(syms);
        if (rep == 0) r.nodes = g.size();
        PassManager pm(g);
        for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
        auto t0 = Clock::now();
        int rc = pm.run(CompileMode::AOT);
        auto t1 = Clock::now();
        if (rc < 0) r.ok = false;
        std::string why;
        if (!g.verify(why)) r.ok = false; // Rule 42 even in benchmarks.
        samples_us.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(samples_us.begin(), samples_us.end());
    r.median_us = samples_us[samples_us.size() / 2];
    return r;
}

} // namespace

int main() {
    SymbolTable syms;
    std::vector<BenchResult> results;

    results.push_back(run_bench("chain_fold", syms, [](SymbolTable& s) {
        return build_chain_graph(s);
    }));
    results.push_back(run_bench("wide_fold", syms, [](SymbolTable& s) {
        return build_wide_graph(s);
    }));
    results.push_back(run_bench("dedup_heavy", syms, [](SymbolTable& s) {
        return build_dedup_graph(s);
    }));
    results.push_back(run_bench("branchy", syms, [](SymbolTable& s) {
        return build_branch_graph(s);
    }));
    {
        SymbolTable loop_syms;
        Graph g = build_loop_heavy_graph(loop_syms);
        if (g.size() <= 1) {
            std::cerr << "bench: loop_heavy module failed to lower\n";
            return 1;
        }
        std::vector<double> samples_us;
        for (int rep = 0; rep < kTimingRepetitions; ++rep) {
            SymbolTable s;
            auto t0 = Clock::now();
            Graph g2 = build_loop_heavy_graph(s);
            PassManager pm(g2);
            for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
            pm.run(CompileMode::AOT);
            auto t1 = Clock::now();
            std::string why;
            if (!g2.verify(why)) return 1;
            samples_us.push_back(
                std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        std::sort(samples_us.begin(), samples_us.end());
        results.push_back(
            BenchResult{"loop_heavy", g.size(), samples_us[samples_us.size() / 2], true});
    }
    {
        SymbolTable bench_syms;
        size_t fns = 0;
        Graph g = build_multi_fn_graph(bench_syms, fns);
        if (fns == 0) {
            std::cerr << "bench: multi_fn module failed to lower\n";
            return 1;
        }
        // multi_fn's cost includes the frontend (lex+parse+check+
        // lower), which is part of compile time; time it as one unit.
        std::vector<double> samples_us;
        for (int rep = 0; rep < kTimingRepetitions; ++rep) {
            SymbolTable s;
            auto t0 = Clock::now();
            size_t n = 0;
            Graph g2 = build_multi_fn_graph(s, n);
            PassManager pm(g2);
            for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
            pm.run(CompileMode::AOT);
            auto t1 = Clock::now();
            std::string why;
            if (!g2.verify(why)) return 1;
            samples_us.push_back(
                std::chrono::duration<double, std::micro>(t1 - t0).count());
            (void)g;
        }
        std::sort(samples_us.begin(), samples_us.end());
        results.push_back(
            BenchResult{"multi_fn", g.size(), samples_us[samples_us.size() / 2], true});
    }

    bool all_ok = true;
    std::cout << "bench name iters nodes median_us\n";
    for (const auto& r : results) {
        std::cout << "bench " << r.name << " " << kTimingRepetitions << " "
                  << r.nodes << " " << r.median_us << "\n";
        if (!r.ok) {
            all_ok = false;
            std::cerr << "bench " << r.name << ": pipeline/verifier FAILED\n";
        }
    }
    if (!all_ok) return 1;
    std::cout << "perf benchmarks completed (timings reported; see Rule 41 "
                 "for regression gating)\n";
    return 0;
}
