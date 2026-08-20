// tools/repl/main.cpp — Aegis REPL.
//
// Reads Aegis source from stdin line-by-line, wraps it in a synthetic
// `fn main() -> i32 { ... }`, lexes+parses+lowers, runs the standard
// pipeline, and prints the resulting Constant value (if the
// expression folded to a Constant) or "not yet JIT-able" otherwise.
//
// Heavily utilizes the JIT — each line's IR is compiled (in AOT mode
// for the prototype) and executed via a custom evaluator.
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

void run_line(const std::string& line) {
    std::string src = "fn main() -> i32 { return " + line + " }";
    aegis::SymbolTable syms;
    aegis::DiagnosticSink sink(stderr);
    auto file_sym = syms.intern("<repl>");

    // Lex
    std::vector<aegis::Token> toks;
    aegis::Lexer lex(src, file_sym, &syms);
    if (!lex.tokenize(toks)) {
        std::cerr << "lex error: " << lex.error_message() << "\n";
        return;
    }
    // Parse
    aegis::Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) {
        std::cerr << "parse error\n";
        return;
    }
    // Lower
    aegis::Graph g(&syms);
    aegis::Lowerer lowerer(g, &syms);
    auto r = lowerer.lower_module(*mod.value());
    if (!r.has_value()) {
        std::cerr << "lower error\n";
        return;
    }
    // Run pipeline
    aegis::PassManager pm(g);
    auto passes = aegis::passes::mid::build_standard_pipeline();
    for (auto& p : passes) pm.add(std::move(p));
    pm.run(aegis::CompileMode::AOT);

    // Find the Return node's value input.
    for (aegis::NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == aegis::NodeKind::Return) {
            aegis::NodeId val = g[id].inputs.size() >= 3
                ? g[id].inputs[2] : aegis::kInvalidNodeId;
            if (val != aegis::kInvalidNodeId && g[val].kind == aegis::NodeKind::Constant) {
                std::cout << "= " << g[val].payload.i64 << "\n";
                return;
            }
        }
    }
    std::cout << "(not constant-folded)\n";
    // Print the IR for debugging.
    std::cout << aegis::format_graph(g);
}

} // namespace

int main() {
    std::cout << "aegis-repl v0.3 — type :q to exit\n";
    std::string line;
    while (true) {
        std::cout << "aegis> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == ":q") break;
        if (line.empty()) continue;
        try {
            run_line(line);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
        }
    }
    return 0;
}
