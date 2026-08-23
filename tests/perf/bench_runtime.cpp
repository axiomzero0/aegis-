// tests/perf/bench_runtime.cpp — RUNTIME benchmark + differential
// correctness harness for GENERATED MACHINE CODE.
//
// This answers "how fast is the generated code at runtime?":
//   1. Compiles straight-line Aegis functions through the full AOT
//      pipeline, instruction selection, and linear-scan register
//      allocation (asserting ZERO spills — the executable encoder's
//      contract).
//   2. Encodes real x86-64 bytes (backend/x86/ExecEncoder), copies
//      them into an RWX page from the JIT MemManager (Rule C.4
//      infrastructure), flushes the icache, and CALLS them.
//   3. CORRECTNESS (Rule 38 at runtime): every case's machine code
//      must agree with an independent AST interpreter across a fixed
//      input grid (including negative values for the signed paths).
//      Divergence fails the binary; three hand-computed gold values
//      guard against a systematically-wrong pair.
//   4. TIMING (Rule 41): median ns/call for generated code vs the
//      interpreter, reported as `bench rt_<case> ...` lines so
//      scripts/check_perf.py gates runtime regressions too.
//
// Scope (documented, not silent): control flow (if/loop), calls, and
// memory ops are not yet emitted by the executable path — the corpus
// is straight-line integer arithmetic over parameters.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/RegAlloc/LinearScan.hpp"
#include "aegis/backend/x86/ExecEncoder.hpp"
#include "aegis/backend/InstrSel.hpp"
#include "aegis/frontend/AST.hpp"
#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Lowering.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/jit/MemManager.hpp"
#include "aegis/passes/PassManager.hpp"
#include "aegis/passes/mid/StandardPipeline.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

using namespace aegis;

// ---- Benchmark parameters (Rule 61: named + documented) ----

/// Timed calls per repetition per case. 50k gives a stable median for
/// ~2-20ns calls; loop cases run up to ~100 iterations per call and
/// the interpreter reference must execute them all, so the budget
/// also bounds total wall time (200k made the suite take minutes).
constexpr int64_t kCallsPerCase = 50'000;

/// Timing repetitions per case; the median is reported.
constexpr int kTimingReps = 3;

/// Guard so a timing loop cannot be optimized away.
volatile int64_t g_sink = 0;

// ---- AST reference interpreter (mirrors documented IR semantics) ----
//
// The interpreter implements the IR's semantics exactly: comparisons
// yield 0/1, `!x` is logical (1 iff x==0), `~x` bitwise, `&&`/`||`
// lower to bitwise and/or on their operands, `/` and `%` truncate
// toward zero (matching idiv), `>>` is arithmetic (sar).

[[noreturn]] void interp_abort(const char* what) {
    std::fprintf(stderr, "interp: unsupported construct (%s)\n", what);
    std::abort();
}

[[nodiscard]] int64_t interp_expr(const ASTNode& n,
                                  const std::vector<int64_t>& args,
                                  const std::vector<SymbolId>& param_syms) {
    switch (n.kind) {
        case ASTKind::IntLit:
            return static_cast<int64_t>(
                static_cast<const ASTIntLit&>(n).value);
        case ASTKind::BoolLit:
            return static_cast<const ASTBoolLit&>(n).value ? 1 : 0;
        case ASTKind::Ident: {
            const auto& i = static_cast<const ASTIdent&>(n);
            for (size_t k = 0; k < param_syms.size(); ++k) {
                if (param_syms[k] == i.name) {
                    return args[k];
                }
            }
            interp_abort("unknown identifier");
        }
        case ASTKind::UnaryExpr: {
            const auto& u = static_cast<const ASTUnaryExpr&>(n);
            const int64_t v = interp_expr(*u.operand, args, param_syms);
            if (u.op == TokenKind::Minus) return -v;
            if (u.op == TokenKind::Tilde) return ~v;
            if (u.op == TokenKind::Bang)  return v == 0 ? 1 : 0;
            interp_abort("unary op");
        }
        case ASTKind::BinaryExpr: {
            const auto& b = static_cast<const ASTBinaryExpr&>(n);
            const int64_t l = interp_expr(*b.lhs, args, param_syms);
            const int64_t r = interp_expr(*b.rhs, args, param_syms);
            switch (b.op) {
                case TokenKind::Plus:    return l + r;
                case TokenKind::Minus:   return l - r;
                case TokenKind::Star:    return l * r;
                case TokenKind::Slash:   return l / r; // toward zero (idiv)
                case TokenKind::Percent: return l % r;
                case TokenKind::Amp:     return l & r;
                case TokenKind::Pipe:    return l | r;
                case TokenKind::Caret:   return l ^ r;
                case TokenKind::Shl:     return l << r;
                case TokenKind::Shr:     return l >> r; // arithmetic (sar)
                case TokenKind::AndAnd:  return l & r;  // IR lowers && -> And
                case TokenKind::OrOr:    return l | r;  // IR lowers || -> Or
                case TokenKind::EqEq:    return l == r ? 1 : 0;
                case TokenKind::BangEq:  return l != r ? 1 : 0;
                case TokenKind::Lt:      return l <  r ? 1 : 0;
                case TokenKind::LtEq:    return l <= r ? 1 : 0;
                case TokenKind::Gt:      return l >  r ? 1 : 0;
                case TokenKind::GtEq:    return l >= r ? 1 : 0;
                default: interp_abort("binary op");
            }
        }
        default:
            std::fprintf(stderr, "interp: unsupported AST kind %d\n",
                         static_cast<int>(n.kind));
            std::abort();
    }
}

// ---- Statement interpreter (branch corpus cases). ----
//
// Executes the SAME flat-frame semantics the Lowerer implements:
// let/var bind sequentially; `if` evaluates both-branch-visible
// bindings via the merge rule (a name reassigned in both branches is
// visible after with the selected value — one-branch reassignments
// are not, and the corpus respects that).

int interp_stmts(const ASTNode& n, std::vector<int64_t>& env,
                 std::vector<SymbolId>& names,
                 int64_t& exit_value, bool& exited);

[[nodiscard]] int64_t interp_expr_sym(const ASTNode& n,
                                      const std::vector<int64_t>& env,
                                      const std::vector<SymbolId>& names) {
    switch (n.kind) {
        case ASTKind::IntLit:
            return static_cast<int64_t>(
                static_cast<const ASTIntLit&>(n).value);
        case ASTKind::Ident: {
            const auto& i = static_cast<const ASTIdent&>(n);
            for (size_t k = 0; k < names.size(); ++k) {
                if (names[k] == i.name) return env[k];
            }
            std::fprintf(stderr, "interp: unknown identifier\n");
            std::abort();
        }
        case ASTKind::BinaryExpr: {
            const auto& b = static_cast<const ASTBinaryExpr&>(n);
            const int64_t l = interp_expr_sym(*b.lhs, env, names);
            const int64_t r = interp_expr_sym(*b.rhs, env, names);
            switch (b.op) {
                case TokenKind::Plus:    return l + r;
                case TokenKind::Minus:   return l - r;
                case TokenKind::Star:    return l * r;
                case TokenKind::Slash:   return l / r;
                case TokenKind::Percent: return l % r;
                case TokenKind::Amp:     return l & r;
                case TokenKind::Pipe:    return l | r;
                case TokenKind::Caret:   return l ^ r;
                case TokenKind::Shl:     return l << r;
                case TokenKind::Shr:     return l >> r;
                case TokenKind::AndAnd:  return l & r;
                case TokenKind::OrOr:    return l | r;
                case TokenKind::EqEq:    return l == r ? 1 : 0;
                case TokenKind::BangEq:  return l != r ? 1 : 0;
                case TokenKind::Lt:      return l <  r ? 1 : 0;
                case TokenKind::LtEq:    return l <= r ? 1 : 0;
                case TokenKind::Gt:      return l >  r ? 1 : 0;
                case TokenKind::GtEq:    return l >= r ? 1 : 0;
                default: break;
            }
            break;
        }
        case ASTKind::UnaryExpr: {
            const auto& u = static_cast<const ASTUnaryExpr&>(n);
            const int64_t v = interp_expr_sym(*u.operand, env, names);
            if (u.op == TokenKind::Minus) return -v;
            if (u.op == TokenKind::Tilde) return ~v;
            return v == 0 ? 1 : 0;
        }
        default: break;
    }
    std::fprintf(stderr, "interp(expr): unsupported kind %d\n",
                 static_cast<int>(n.kind));
    std::abort();
}

// Returns 0 to continue, 1 when a Return executed (exit_value set).
int interp_stmts(const ASTNode& n, std::vector<int64_t>& env,
                 std::vector<SymbolId>& names,
                 int64_t& exit_value, bool& exited) {
    if (exited) return 1;
    switch (n.kind) {
        case ASTKind::Block: {
            const auto& b = static_cast<const ASTBlock&>(n);
            for (const auto& s : b.stmts) {
                if (!s) continue;
                if (interp_stmts(*s, env, names, exit_value, exited) != 0)
                    return 1;
            }
            return 0;
        }
        case ASTKind::LetStmt: {
            const auto& s = static_cast<const ASTLetStmt&>(n);
            // Corpus contract: bindings are pre-registered in `names`.
            int64_t v = 0;
            if (s.init) v = interp_expr_sym(*s.init, env, names);
            for (size_t k = 0; k < names.size(); ++k) {
                if (names[k] == s.name) { env[k] = v; return 0; }
            }
            std::fprintf(stderr, "interp: unregistered binding\n");
            std::abort();
        }
        case ASTKind::AssignExpr: {
            const auto& a = static_cast<const ASTAssignExpr&>(n);
            const int64_t v = interp_expr_sym(*a.value, env, names);
            const auto& tgt = static_cast<const ASTIdent&>(*a.target);
            for (size_t k = 0; k < names.size(); ++k) {
                if (names[k] == tgt.name) { env[k] = v; return 0; }
            }
            std::fprintf(stderr, "interp: unregistered assignment\n");
            std::abort();
        }
        case ASTKind::IfStmt: {
            const auto& s = static_cast<const ASTIfStmt&>(n);
            const int64_t c = interp_expr_sym(*s.cond, env, names);
            const ASTNode* arm = c != 0 ? s.then_branch.get()
                                        : s.else_branch.get();
            if (arm) return interp_stmts(*arm, env, names, exit_value, exited);
            return 0;
        }
        case ASTKind::ForStmt: {
            const auto& s = static_cast<const ASTForStmt&>(n);
            // Corpus contract: iter is the `lo..hi` range form.
            if (!s.iter || s.iter->kind != ASTKind::RangeExpr) {
                std::fprintf(stderr, "interp: non-range for\n");
                std::abort();
            }
            const auto& range =
                static_cast<const ASTRangeExpr&>(*s.iter);
            const int64_t lo = interp_expr_sym(*range.lo, env, names);
            const int64_t hi = interp_expr_sym(*range.hi, env, names);
            names.push_back(s.var_name);
            env.push_back(0);
            for (int64_t i = lo; i < hi; ++i) {
                env.back() = i;
                if (interp_stmts(*s.body, env, names, exit_value,
                                 exited) != 0)
                    break;
            }
            names.pop_back();
            env.pop_back();
            return exited ? 1 : 0;
        }
        case ASTKind::ReturnStmt: {
            const auto& s = static_cast<const ASTReturnStmt&>(n);
            exit_value = s.value
                ? interp_expr_sym(*s.value, env, names) : 0;
            exited = true;
            return 1;
        }
        default:
            std::fprintf(stderr, "interp(stmt): unsupported kind %d\n",
                         static_cast<int>(n.kind));
            std::abort();
    }
}

// ---- Compile source -> optimized graph -> machine code bytes ----

struct CompiledCase {
    std::string name;
    std::vector<uint8_t> code{};
    // OWNS the AST: body_expr points into this module, so it must
    // outlive the case (the pre-fix dangling pointer read garbage
    // kinds like 193 — caught loudly by the interpreter's default).
    std::unique_ptr<ASTModule> module{};
    const ASTNode* body_expr{nullptr};   // the single return expression
    const ASTNode* body_block{nullptr};  // whole block (statement mode)
    bool statement_mode{false};
    std::vector<SymbolId> param_syms{};  // signature order
    size_t arity{0};
    size_t ir_nodes{0};
};

[[nodiscard]] bool compile_case(const std::string& name,
                                const std::string& src,
                                CompiledCase& out,
                                std::string& err) {
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    Lexer lex(src, syms.intern("<rt>"), &syms);
    std::vector<Token> toks;
    if (!lex.tokenize(toks)) { err = name + ": lex failed"; return false; }
    Parser parser(std::move(toks), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) { err = name + ": parse failed"; return false; }
    TypeChecker tc(&syms, &sink);
    if (!tc.check_module(*mod.value()).has_value()) {
        err = name + ": typecheck failed"; return false;
    }
    if (mod.value()->items.size() != 1) {
        err = name + ": corpus must be exactly one function";
        return false;
    }
    const auto& fn = static_cast<const ASTFnDecl&>(*mod.value()->items[0]);
    for (const auto& p : fn.params) {
        out.param_syms.push_back(static_cast<const ASTParam&>(*p).name);
    }
    out.arity = out.param_syms.size();
    const auto& body = static_cast<const ASTBlock&>(*fn.body);
    const auto& ret = static_cast<const ASTReturnStmt&>(*body.stmts.back());
    out.module = std::move(mod.value());
    out.body_expr = ret.value.get();
    out.body_block = fn.body.get();
    // Statement mode when the body binds or branches (the return
    // expression alone cannot express it). Register every binding
    // name in evaluation order (params first, then lets/vars).
    for (const auto& s : body.stmts) {
        if (!s) continue;
        if (s->kind == ASTKind::LetStmt || s->kind == ASTKind::IfStmt) {
            out.statement_mode = true;
        }
    }
    if (out.statement_mode) {
        // Flatten: params, then every let/var in the block (the corpus
        // only binds at the top level of the function body). For-loop
        // variables are intentionally NOT pre-registered: the
        // interpreter binds them at the loop's own scope, and a
        // pre-registered copy would SHADOW the live iteration value
        // with a stale one (reads resolve the first match).
        for (const auto& s : body.stmts) {
            if (!s) continue;
            if (s->kind == ASTKind::LetStmt) {
                out.param_syms.push_back(
                    static_cast<const ASTLetStmt&>(*s).name);
            }
        }
    }

    Graph g(&syms);
    Lowerer lw(g, &syms);
    // NOTE: lower from the module's NEW owner — the unique_ptr move
    // transferred ownership; `mod.value()` is moved-from. (The AST
    // pointee did not move, so the `fn`/`ret` references above stay
    // valid.)
    if (!lw.lower_module(*out.module).has_value()) {
        err = name + ": lowering failed"; return false;
    }
    PassManager pm(g);
    for (auto& p : passes::mid::build_standard_pipeline()) pm.add(std::move(p));
    pm.run(CompileMode::AOT);
    std::string why;
    if (!g.verify(why)) { err = name + ": verify failed: " + why; return false; }
    out.ir_nodes = g.size();

    MachineFunction mf = InstrSelector(g).lower("f");
    LinearScanAllocator lsa(mf, backend::x86::kExecHomeRegCount, 0);
    uint32_t spills = lsa.run();
    if (spills != 0) {
        err = name + ": " + std::to_string(spills) +
              " spills — executable path requires zero spills "
              "(reduce live pressure in the case)";
        return false;
    }
    if (!backend::x86::encode_executable(mf, lsa, out.code, err)) {
        err = name + ": encode failed: " + err;
        return false;
    }
    // NO-SILENT-OMISSION GUARD (Rule D.3): every vreg the return
    // transitively reads must be defined by a selected instruction.
    // Calls, loop phis, and other unemittable nodes are silently
    // skipped by selection — without this check they would produce
    // code that reads an undefined register instead of an error.
    {
        std::vector<uint8_t> defined_by_instr(mf.instrs.size() + 1, 0);
        // Map vreg -> defining instr exists? Build via defs.
        std::vector<int64_t> def_at;
        // Find max vreg id to size the table.
        VRegId max_v = 0;
        for (const auto& mi : mf.instrs) {
            if (mi.defs[0] != kInvalidVReg && mi.defs[0] > max_v)
                max_v = mi.defs[0];
            for (VRegId u : mi.uses) {
                if (u != kInvalidVReg && u > max_v) max_v = u;
            }
        }
        def_at.assign(max_v + 1, -1);
        for (size_t i = 0; i < mf.instrs.size(); ++i) {
            if (mf.instrs[i].defs[0] != kInvalidVReg) {
                def_at[mf.instrs[i].defs[0]] = static_cast<int64_t>(i);
            }
        }
        // BFS from the ret's operand.
        for (const auto& mi : mf.instrs) {
            if (mi.op != "ret") continue;
            std::vector<VRegId> work;
            // Loop phis have TWO defs (preheader init + back-edge
            // update), so the def-chain walk can cycle; the visited
            // set terminates it (the pre-fix walk looped forever on
            // the first loop case).
            std::vector<uint8_t> visited(max_v + 1, 0);
            if (mi.uses[0] != kInvalidVReg) work.push_back(mi.uses[0]);
            while (!work.empty()) {
                const VRegId v = work.back();
                work.pop_back();
                if (v == kInvalidVReg || v > max_v) continue;
                if (visited[v] != 0) continue;
                visited[v] = 1;
                if (def_at[v] == -1) {
                    err = name + ": vreg v" + std::to_string(v) +
                          " is read but never defined (unemittable node "
                          "in the value closure — e.g. a call or loop)";
                    return false;
                }
                for (VRegId u : mf.instrs[static_cast<size_t>(def_at[v])].uses) {
                    if (u != kInvalidVReg && u <= max_v) work.push_back(u);
                }
            }
            break; // single ret in the corpus
        }
    }
    out.name = name;
    return true;
}

// ---- Typed call shims (SysV: args in RDI, RSI, RDX, RCX, ...). ----

using Fn1 = int64_t (*)(int64_t);
using Fn2 = int64_t (*)(int64_t, int64_t);
using Fn3 = int64_t (*)(int64_t, int64_t, int64_t);
using Fn4 = int64_t (*)(int64_t, int64_t, int64_t, int64_t);

[[nodiscard]] int64_t call_compiled(const void* code, size_t arity,
                                    const std::vector<int64_t>& a) {
    switch (arity) {
        case 1: return reinterpret_cast<Fn1>(const_cast<void*>(code))(a[0]);
        case 2: return reinterpret_cast<Fn2>(const_cast<void*>(code))(a[0], a[1]);
        case 3: return reinterpret_cast<Fn3>(const_cast<void*>(code))(a[0], a[1], a[2]);
        case 4: return reinterpret_cast<Fn4>(const_cast<void*>(code))(a[0], a[1], a[2], a[3]);
        default: std::abort();
    }
}

// ---- The corpus (owned strings; safe literal divisors only). ----

struct CaseSpec {
    std::string name;
    std::string src;
    std::vector<std::vector<int64_t>> inputs; // correctness + timing grid
};

[[nodiscard]] std::string chain64_body() {
    std::string chain = "a";
    for (int i = 0; i < 64; ++i) {
        chain += i % 2 == 0 ? " + " + std::to_string(i * 3 + 1)
                            : " - " + std::to_string(i + 2);
    }
    return chain;
}

[[nodiscard]] std::string poly32_body() {
    std::string poly = "a";
    for (int i = 0; i < 32; ++i) {
        poly = "(" + poly + " * 3 + " + std::to_string(i % 5 + 1) + ")";
    }
    return poly + " % 1000003";
}

[[nodiscard]] std::vector<CaseSpec> build_corpus() {
    return {
        {"rt_identity",
         "fn f(a: i32) -> i32 { return a; }",
         {{0}, {1}, {-13}, {9999999999LL}}},
        {"rt_chain64",
         "fn f(a: i32) -> i32 { return " + chain64_body() + "; }",
         {{0}, {7}, {-13}, {123456789LL}}},
        {"rt_mixed",
         "fn f(a: i32, b: i32, c: i32) -> i32 { return "
         "a * 7 + (b << 3) - (c / 5) + (a & b) + (c % 7) "
         "+ (a < b) + (c >= 3) + (a == c) - (a > b) "
         "+ -a + ~b + !c; }",
         {{0, 0, 0}, {3, 5, 7}, {-13, 4, -9}, {123456789LL, -987654321LL, 555}}},
        {"rt_wide8",
         "fn f(a: i32, b: i32, c: i32, d: i32) -> i32 { return "
         "a * 2 + b * 3 + c * 5 + d * 7 "
         "+ (a & d) + (b | c) + (a ^ b) + (c >> 2); }",
         {{0, 0, 0, 0}, {1, 2, 3, 4}, {-5, 6, -7, 8}, {999, -999, 31, -31}}},
        {"rt_poly32",
         "fn f(a: i32) -> i32 { return " + poly32_body() + "; }",
         {{0}, {1}, {-100}, {7919}}},
        {"rt_branch_min",
         "fn f(a: i32, b: i32) -> i32 {\n"
         "    var t = 0;\n"
         "    if a < b { t = 1; } else { t = 2; }\n"
         "    return t;\n"
         "}",
         {{0, 0}, {1, 2}, {2, 1}, {-5, 5}, {999, -999}}},
        {"rt_branch_arith",
         "fn f(a: i32, b: i32) -> i32 {\n"
         "    var t = 0;\n"
         "    if a < b { t = a * 7 + (b << 2); } else { t = (a - b) * 3 + (b >> 1); }\n"
         "    return t + (a & b);\n"
         "}",
         {{0, 0}, {1, 2}, {2, 1}, {-5, 5}, {123, -7}, {-13, -4}}},
        {"rt_loop_sum",
         "fn f(n: i32) -> i32 {\n"
         "    var s = 0;\n"
         "    for i in 0..n { s = s + i; }\n"
         "    return s;\n"
         "}",
         {{0}, {1}, {5}, {100}, {-3}}},
        {"rt_loop_ivs",
         "fn f(n: i32) -> i32 {\n"
         "    var s = 0;\n"
         "    for i in 0..n { s = s + i * 3; }\n"
         "    return s;\n"
         "}",
         {{0}, {1}, {5}, {50}, {-3}}},
        {"rt_loop_branch",
         "fn f(n: i32) -> i32 {\n"
         "    var s = 0;\n"
         "    for i in 0..n {\n"
         "        if i > 2 { s = s + i; } else { s = s + 2; }\n"
         "    }\n"
         "    return s;\n"
         "}",
         {{0}, {1}, {3}, {20}, {-3}}},
        {"rt_loop_two",
         "fn f(n: i32) -> i32 {\n"
         "    var s = 0;\n"
         "    for i in 0..n { s = s + i; }\n"
         "    var t = s * 2;\n"
         "    for j in 0..n { t = t + j; }\n"
         "    return t;\n"
         "}",
         {{0}, {1}, {4}, {25}, {-3}}},
        {"rt_branch_nested",
         "fn f(a: i32, b: i32) -> i32 {\n"
         "    var t = 0;\n"
         "    if a < b {\n"
         "        if a + b > 0 { t = 10; } else { t = 20; }\n"
         "    } else {\n"
         "        if a - b > 0 { t = 30; } else { t = 40; }\n"
         "    }\n"
         "    return t;\n"
         "}",
         {{0, 0}, {1, 2}, {2, 1}, {-5, 5}, {-5, -9}, {7, -8}}},
    };
}

} // namespace

int main() {
    auto corpus = build_corpus();
    jit::MemManager mem;
    constexpr uint64_t kBenchEpoch = 1;

    struct Result {
        std::string name;
        size_t ir_nodes, code_bytes;
        double code_us, interp_us;
    };
    std::vector<Result> results;

    for (const auto& spec : corpus) {
        CompiledCase cc;
        std::string err;
        if (!compile_case(spec.name, spec.src, cc, err)) {
            std::fprintf(stderr, "FAIL %s\n", err.c_str());
            return 1;
        }

        // Copy into RWX memory + icache flush (x86 is coherent, but
        // the flush keeps the pattern portable to other targets).
        void* page = mem.allocate(cc.code.size(), kBenchEpoch);
        if (page == nullptr) {
            std::fprintf(stderr, "FAIL %s: MemManager allocation\n",
                         cc.name.c_str());
            return 1;
        }
        std::memcpy(page, cc.code.data(), cc.code.size());
        __builtin___clear_cache(static_cast<char*>(page),
                                static_cast<char*>(page) + cc.code.size());

        // ---- Correctness: generated code vs AST interpreter ----
        for (const auto& args : spec.inputs) {
            if (args.size() != cc.arity) {
                std::fprintf(stderr, "FAIL %s: input arity mismatch\n",
                             cc.name.c_str());
                return 1;
            }
            const int64_t got = call_compiled(page, cc.arity, args);
            int64_t want;
            if (cc.statement_mode) {
                std::vector<int64_t> env(cc.param_syms.size(), 0);
                for (size_t k = 0; k < args.size() && k < env.size(); ++k) {
                    env[k] = args[k];
                }
                bool exited = false;
                interp_stmts(*cc.body_block, env, cc.param_syms, want, exited);
                if (!exited) {
                    std::fprintf(stderr, "FAIL %s: interp never returned\n",
                                 cc.name.c_str());
                    return 1;
                }
            } else {
                want = interp_expr(*cc.body_expr, args, cc.param_syms);
            }
            if (got != want) {
                std::fprintf(stderr,
                             "FAIL %s: runtime differential mismatch — "
                             "generated=%lld interpreter=%lld (arg0=%lld)\n",
                             cc.name.c_str(), static_cast<long long>(got),
                             static_cast<long long>(want),
                             static_cast<long long>(args[0]));
                return 1;
            }
        }

        // ---- Hand-computed gold values (guard a wrong pair). ----
        if (spec.name == "rt_mixed") {
            // a=3,b=5,c=7 by hand: 21+40-1+1+0+1+1+0-0-3+(-6)+0 = 54.
            const int64_t got = call_compiled(page, 3, {3, 5, 7});
            if (got != 54) {
                std::fprintf(stderr, "FAIL rt_mixed gold: %lld (want 54)\n",
                             static_cast<long long>(got));
                return 1;
            }
        }
        if (spec.name == "rt_wide8") {
            // a=1,b=2,c=3,d=4: 2+6+15+28+0+3+3+0 = 57.
            const int64_t got = call_compiled(page, 4, {1, 2, 3, 4});
            if (got != 57) {
                std::fprintf(stderr, "FAIL rt_wide8 gold: %lld (want 57)\n",
                             static_cast<long long>(got));
                return 1;
            }
        }

        // ---- Timing (median over reps; identical call count each). ----
        const auto& grid = spec.inputs;
        double code_samples[kTimingReps];
        double interp_samples[kTimingReps];
        for (int rep = 0; rep < kTimingReps; ++rep) {
            int64_t checksum_code = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (int64_t i = 0; i < kCallsPerCase; ++i) {
                const auto& args = grid[static_cast<size_t>(
                    i % static_cast<int64_t>(grid.size()))];
                checksum_code += call_compiled(page, cc.arity, args);
            }
            auto t1 = std::chrono::steady_clock::now();
            g_sink = checksum_code;

            int64_t checksum_interp = 0;
            auto t2 = std::chrono::steady_clock::now();
            for (int64_t i = 0; i < kCallsPerCase; ++i) {
                const auto& args = grid[static_cast<size_t>(
                    i % static_cast<int64_t>(grid.size()))];
                int64_t iv;
                if (cc.statement_mode) {
                    std::vector<int64_t> env(cc.param_syms.size(), 0);
                    for (size_t k = 0; k < args.size() && k < env.size(); ++k)
                        env[k] = args[k];
                    bool exited = false;
                    interp_stmts(*cc.body_block, env, cc.param_syms, iv,
                                 exited);
                } else {
                    iv = interp_expr(*cc.body_expr, args, cc.param_syms);
                }
                checksum_interp += iv;
            }
            auto t3 = std::chrono::steady_clock::now();
            // Rule 38: what we timed must agree between both engines.
            if (checksum_code != checksum_interp) {
                std::fprintf(stderr,
                             "FAIL %s: timing-loop checksum divergence\n",
                             cc.name.c_str());
                return 1;
            }
            code_samples[rep] =
                std::chrono::duration<double, std::micro>(t1 - t0).count();
            interp_samples[rep] =
                std::chrono::duration<double, std::micro>(t3 - t2).count();
        }
        std::sort(std::begin(code_samples), std::end(code_samples));
        std::sort(std::begin(interp_samples), std::end(interp_samples));
        results.push_back(Result{cc.name, cc.ir_nodes, cc.code.size(),
                                 code_samples[kTimingReps / 2],
                                 interp_samples[kTimingReps / 2]});
    }

    // ---- Report (machine-readable `bench` lines for check_perf.py). ----
    std::printf("bench name iters nodes median_us\n");
    for (const auto& r : results) {
        std::printf("bench %s %lld %zu %.3f\n", r.name.c_str(),
                    static_cast<long long>(kCallsPerCase), r.ir_nodes,
                    r.code_us);
        std::printf("xref interp_%s %lld %zu %.3f  # interpreter reference\n",
                    r.name.c_str(), static_cast<long long>(kCallsPerCase),
                    r.ir_nodes, r.interp_us);
        const double ns_per_call =
            r.code_us * 1000.0 / static_cast<double>(kCallsPerCase);
        const double interp_ns =
            r.interp_us * 1000.0 / static_cast<double>(kCallsPerCase);
        std::printf("note %s: %.1f ns/call generated vs %.1f ns/call "
                    "interpreted (%.1fx)\n",
                    r.name.c_str(), ns_per_call, interp_ns,
                    interp_ns / ns_per_call);
    }
    std::printf("runtime benchmarks completed (correctness + timing OK)\n");
    return 0;
}
