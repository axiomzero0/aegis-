// driver/main.cpp — Aegis compiler driver.
// ============================================================
// Usage:
//   aegis <input.aegis> [--dump-ast] [--dump-ir] [--dump-mir] [--run-passes]
//                        [--no-verify] [--aot | --jit]
// ============================================================
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"
#include "aegis/frontend/ASTPrinter.hpp"
#include "aegis/frontend/EffectInference.hpp"
#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/backend/InstrSel.hpp"
#include "aegis/backend/RegAlloc/LinearScan.hpp"

#ifdef AEGIS_VERIFY_IR
static constexpr bool kVerifyByDefault = true;
#else
static constexpr bool kVerifyByDefault = false;
#endif

namespace {

struct CLIDriver {
    std::string input_path;
    bool       dump_ast{false};
    bool       dump_ir{false};
    bool       dump_mir{false};
    bool       run_passes{true};
    bool       no_verify{false};
    bool       jit_mode{false};
    bool       print_help{false};
};

CLIDriver parse_args(int argc, char** argv) {
    CLIDriver d;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h") d.print_help = true;
        else if (a == "--dump-ast") d.dump_ast = true;
        else if (a == "--dump-ir")  d.dump_ir = true;
        else if (a == "--dump-mir") d.dump_mir = true;
        else if (a == "--no-passes") d.run_passes = false;
        else if (a == "--no-verify") d.no_verify = true;
        else if (a == "--jit") d.jit_mode = true;
        else if (a == "--aot") d.jit_mode = false;
        else if (a.starts_with("--")) {
            std::cerr << "unknown flag: " << a << "\n";
        } else {
            d.input_path = a;
        }
    }
    return d;
}

void print_usage(std::string_view argv0) {
    std::cerr << "Usage: " << argv0 << " <input.aegis> [options]\n"
              << "Options:\n"
              << "  --dump-ast   Print the AST after parsing.\n"
              << "  --dump-ir    Print the SoN IR after lowering.\n"
              << "  --dump-mir   Print the MachineInstr after selection.\n"
              << "  --no-passes  Skip the optimization pipeline.\n"
              << "  --no-verify  Skip the post-pass verifier.\n"
              << "  --jit        JIT mode (use PGO-driven passes).\n"
              << "  --aot        AOT mode (default).\n";
}

} // namespace

int main(int argc, char** argv) {
    auto cli = parse_args(argc, argv);
    if (cli.print_help || cli.input_path.empty()) {
        print_usage(argv[0]);
        return cli.print_help ? 0 : 1;
    }

    std::ifstream in(cli.input_path);
    if (!in) {
        std::cerr << "error: cannot open input '" << cli.input_path << "'\n";
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();

    aegis::SymbolTable syms;
    aegis::DiagnosticSink sink(stderr);
    auto file_sym = syms.intern(cli.input_path);

    // ---- Lexer ----
    std::vector<aegis::Token> tokens;
    aegis::Lexer lexer(src, file_sym, &syms);
    if (!lexer.tokenize(tokens)) {
        std::cerr << "lex error at " << lexer.error_line() << ":" << lexer.error_col()
                  << ": " << lexer.error_message() << "\n";
        return 1;
    }

    // ---- Parser ----
    aegis::Parser parser(std::move(tokens), &syms, &sink);
    auto mod_result = parser.parse_module();
    if (!mod_result.has_value()) {
        std::cerr << "parse error\n";
        return 1;
    }
    auto& mod = *mod_result.value();
    if (cli.dump_ast) {
        std::cout << aegis::format_ast(mod) << "\n";
    }

    // ---- Type checker (Pass 1) ----
    aegis::TypeChecker tc(&syms, &sink);
    auto tc_r = tc.check_module(mod);
    if (!tc_r.has_value()) {
        std::cerr << "type checking failed\n";
        return 1;
    }

    // ---- Lower to IR ----
    aegis::Graph g(&syms);
    aegis::Lowerer lowerer(g, &syms);
    auto lr = lowerer.lower_module(mod);
    if (!lr.has_value()) {
        std::cerr << "lowering failed\n";
        return 1;
    }

    if (kVerifyByDefault && !cli.no_verify) {
        std::string why;
        if (!aegis::verify_graph(g, why)) {
            std::cerr << "verify failed post-lowering: " << why << "\n";
            std::cerr << aegis::format_graph(g);
            return 1;
        }
    }
    if (cli.dump_ir) {
        std::cout << aegis::format_graph(g) << "\n";
    }

    // ---- Optimization pipeline ----
    if (cli.run_passes) {
        aegis::PassManager pm(g);
        auto passes = aegis::build_standard_pipeline();
        for (auto& p : passes) pm.add(std::move(p));
        pm.run(cli.jit_mode ? aegis::CompileMode::JIT
                             : aegis::CompileMode::AOT);
        if (cli.dump_ir) {
            std::cout << "----- After passes -----\n";
            std::cout << aegis::format_graph(g) << "\n";
        }
    }

    // ---- Instruction selection + register allocation ----
    aegis::InstrSelector sel(g);
    auto mf = sel.lower("main");
    aegis::LinearScanAllocator lsa(mf, /*num_gpr=*/12, /*num_fpr=*/12);
    uint32_t spills = lsa.run();
    (void)spills;
    if (cli.dump_mir) {
        std::cout << "fn " << mf.name << " (spills=" << spills << ")\n";
        for (uint32_t i = 0; i < mf.instrs.size(); ++i) {
            const auto& mi = mf.instrs[i];
            std::cout << "  " << i << ": " << mi.op;
            if (mi.defs[0] != aegis::kInvalidVReg)
                std::cout << " v" << mi.defs[0];
            for (int j = 0; j < 4; ++j) {
                if (mi.uses[j] != aegis::kInvalidVReg)
                    std::cout << " v" << mi.uses[j];
            }
            std::cout << "\n";
        }
    }
    return 0;
}
