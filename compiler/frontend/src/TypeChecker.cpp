// frontend/TypeChecker.cpp — Name resolution + semantic checking (Pass 1).
//
// See TypeChecker.hpp for the enforced-check contract (Rules 70/71).
// The checker mirrors the Lowerer's flat per-function binding model:
// bindings accumulate sequentially; `if` branches are checked against
// copies of the enclosing frame and merge conservatively (a binding is
// visible after the `if` only if it was visible before or bound in
// BOTH branches — bindings from a single branch are not, which keeps
// the checker from accepting code the Lowerer would mis-lower).
//
// Law: Rule D.3 — every rejection is a loud typed diagnostic; there is
// no path through this file that silently accepts malformed input.
// Law: Rule 43 — diagnostic message ids are named constants below so
// tests and telemetry can reference them symbolically.
#include "aegis/frontend/TypeChecker.hpp"
#include "aegis/support/Diagnostics.hpp"

#include <utility>
#include <vector>

namespace aegis {

namespace {

// Diagnostic message ids (Rule 54 — interned-style numeric ids; the
// human-readable table lives in this comment and in TypeChecker.hpp).
//   0x200 duplicate function declaration at module scope
//   0x201 duplicate parameter name in one signature
//   0x202 duplicate let/var binding on one lexical path
//   0x203 use of an undefined identifier
//   0x204 assignment to an immutable (non-var) binding
//   0x205 call-argument count mismatch against the declared signature
//   0x206 construct is parseable but not yet lowered (Rule D.3)
constexpr uint32_t kErrDuplicateFn       = 0x200;
constexpr uint32_t kErrDuplicateParam    = 0x201;
constexpr uint32_t kErrDuplicateBinding  = 0x202;
constexpr uint32_t kErrUndefinedIdent    = 0x203;
constexpr uint32_t kErrAssignToImmutable = 0x204;
constexpr uint32_t kErrCallArity         = 0x205;
constexpr uint32_t kErrNotLowered        = 0x206;

// One flat lexical frame: every binding on the current path plus a
// mutability bit (`var`/`mut` param bindings may be assigned).
struct Frame {
    struct Binding {
        SymbolId name;
        bool     is_mutable;
    };
    std::vector<Binding> bindings;

    [[nodiscard]] const Binding* find(SymbolId name) const noexcept {
        for (const auto& b : bindings) {
            if (b.name == name) return &b;
        }
        return nullptr;
    }
    // Returns false (and reports nothing) when `name` is already bound.
    bool try_bind(SymbolId name, bool is_mutable) {
        if (find(name) != nullptr) return false;
        bindings.push_back({name, is_mutable});
        return true;
    }
};

class Checker {
public:
    Checker(SymbolTable* syms, DiagnosticSink* sink)
        : syms_(syms), sink_(sink) {}

    void check_module(const ASTModule& mod) {
        // Pass A: collect module-level function signatures.
        for (const auto& it : mod.items) {
            if (!it) continue;
            if (it->kind != ASTKind::FnDecl) continue;
            const auto& fn = static_cast<const ASTFnDecl&>(*it);
            if (fn_sigs_find(fn.name) != nullptr) {
                report(kErrDuplicateFn, fn.span, fn.name);
                continue;
            }
            uint32_t arity = 0;
            for (const auto& p : fn.params) {
                if (p) ++arity;
            }
            fn_sigs_.push_back({fn.name, arity});
        }
        // Pass B: check each function body.
        for (const auto& it : mod.items) {
            if (!it) continue;
            if (it->kind != ASTKind::FnDecl) {
                check_other_decl(*it); // struct/enum: decl-only, no body
                continue;
            }
            check_fn(static_cast<const ASTFnDecl&>(*it));
        }
    }

private:
    SymbolTable*              syms_;
    DiagnosticSink*           sink_;
    // Declared arity per module-level function name. Module-level decl
    // counts are tiny and the frontend is a cold path (Rule B.1 exempts
    // it from hot-path container rules), so a plain vector with linear
    // probe beats a hash table on cache grounds at this scale.
    std::vector<std::pair<SymbolId, uint32_t>> fn_sigs_;

    [[nodiscard]] const uint32_t* fn_sigs_find(SymbolId name) const {
        for (const auto& [n, arity] : fn_sigs_) {
            if (n == name) return &arity;
        }
        return nullptr;
    }

    void report(uint32_t msg_id, Span sp, SymbolId sym) noexcept {
        Error e = Error::type_(msg_id, sp);
        e.payload = static_cast<uint64_t>(sym);
        sink_->report(e);
        (void)syms_;
    }

    void check_other_decl(const ASTNode& n) {
        // Struct/enum declarations carry no executable semantics yet
        // (the Lowerer skips them); nothing to check beyond parse
        // validity, which the Parser already enforced.
        (void)n;
    }

    void check_fn(const ASTFnDecl& fn) {
        Frame frame;
        for (const auto& p : fn.params) {
            if (!p) continue;
            const auto& param = static_cast<const ASTParam&>(*p);
            if (!frame.try_bind(param.name, param.mutable_)) {
                report(kErrDuplicateParam, param.span, param.name);
            }
        }
        if (fn.body) {
            check_stmt(*fn.body, frame);
        }
    }

    void check_stmt(const ASTNode& n, Frame& frame) {
        switch (n.kind) {
            case ASTKind::LetStmt: {
                const auto& s = static_cast<const ASTLetStmt&>(n);
                if (s.init) check_expr(*s.init, frame);
                if (!frame.try_bind(s.name, s.is_var)) {
                    report(kErrDuplicateBinding, s.span, s.name);
                }
                return;
            }
            case ASTKind::Block: {
                const auto& b = static_cast<const ASTBlock&>(n);
                for (const auto& s : b.stmts) {
                    if (s) check_stmt(*s, frame);
                }
                return;
            }
            case ASTKind::ExprStmt: {
                const auto& s = static_cast<const ASTExprStmt&>(n);
                if (s.expr) check_expr(*s.expr, frame);
                return;
            }
            case ASTKind::ReturnStmt: {
                const auto& s = static_cast<const ASTReturnStmt&>(n);
                if (s.value) check_expr(*s.value, frame);
                return;
            }
            case ASTKind::IfStmt: {
                const auto& s = static_cast<const ASTIfStmt&>(n);
                check_expr(*s.cond, frame);
                // Each branch is checked against its own copy of the
                // enclosing frame; bindings merge conservatively after.
                Frame then_frame = frame;
                if (s.then_branch) check_stmt(*s.then_branch, then_frame);
                Frame else_frame = frame;
                if (s.else_branch) check_stmt(*s.else_branch, else_frame);
                merge_branch_frames(frame, then_frame, else_frame);
                return;
            }
            case ASTKind::AssignExpr: {
                check_assign(static_cast<const ASTAssignExpr&>(n), frame);
                return;
            }
            case ASTKind::ForStmt: {
                const auto& s = static_cast<const ASTForStmt&>(n);
                // Only the `for var in lo..hi {}` iteration space is
                // lowerable; any other iterator shape is a loud error
                // (Rule D.3 — never a silently-misinterpreted expr).
                if (!s.iter || s.iter->kind != ASTKind::RangeExpr) {
                    report(kErrNotLowered, n.span, 0);
                    return;
                }
                const auto& range = static_cast<const ASTRangeExpr&>(*s.iter);
                check_expr(*range.lo, frame);
                check_expr(*range.hi, frame);
                // The body is checked in a CHILD frame: the loop var
                // and any body-local bindings die at the loop end.
                // Outer-frame names stay visible (reads are
                // loop-invariant; assignments lower to loop-header
                // phis in the Lowerer).
                Frame body_frame = frame;
                if (!body_frame.try_bind(s.var_name, /*is_mutable=*/false)) {
                    report(kErrDuplicateBinding, n.span, s.var_name);
                }
                if (s.body) {
                    if (body_contains_unlowerable(*s.body)) {
                        // return/match inside the body would break the
                        // back-edge wiring (still-unlowered shapes);
                        // reject loudly rather than mis-lower.
                        report(kErrNotLowered, s.body->span, 0);
                        return;
                    }
                    check_stmt(*s.body, body_frame);
                }
                return;
            }
            case ASTKind::MatchStmt:
                // Rule D.3: the Lowerer silently lowers match to a
                // constant — accepting it would compute a wrong
                // answer with no diagnostic. Fail loudly instead.
                report(kErrNotLowered, n.span, 0);
                return;
            default:
                // Expression in statement position.
                check_expr(n, frame);
                return;
        }
    }

    // Post-`if` visibility: a binding is visible iff it was visible
    // before the `if` or was bound in BOTH branches (the Lowerer
    // materializes a Phi only for such names; single-branch bindings
    // are not reliably live after the merge).
    static void merge_branch_frames(Frame& out,
                                    const Frame& then_frame,
                                    const Frame& else_frame) {
        for (const auto& b : then_frame.bindings) {
            if (out.find(b.name) != nullptr) continue;      // visible before
            if (else_frame.find(b.name) != nullptr) {       // bound in both
                out.bindings.push_back(b);
            }
        }
    }

    // True when the statement tree contains a construct that cannot
    // yet be lowered INSIDE a loop body (return breaks the back-edge
    // wiring; match lowers to a constant). Used by the ForStmt check
    // so those shapes are rejected loudly (Rule D.3), never silently
    // mis-lowered.
    static bool body_contains_unlowerable(const ASTNode& n) {
        switch (n.kind) {
            case ASTKind::ReturnStmt:
            case ASTKind::MatchStmt:
                return true;
            case ASTKind::Block: {
                const auto& b = static_cast<const ASTBlock&>(n);
                for (const auto& s : b.stmts) {
                    if (s && body_contains_unlowerable(*s)) return true;
                }
                return false;
            }
            case ASTKind::IfStmt: {
                const auto& s = static_cast<const ASTIfStmt&>(n);
                if (s.then_branch && body_contains_unlowerable(*s.then_branch)) return true;
                if (s.else_branch && body_contains_unlowerable(*s.else_branch)) return true;
                return false;
            }
            case ASTKind::ForStmt: {
                const auto& s = static_cast<const ASTForStmt&>(n);
                return s.body && body_contains_unlowerable(*s.body);
            }
            default:
                return false;
        }
    }

    void check_assign(const ASTAssignExpr& a, Frame& frame) {
        check_expr(*a.value, frame);
        if (!a.target) return;
        if (a.target->kind != ASTKind::Ident) {
            // Compound targets (field/index) are not lowerable yet.
            report(kErrNotLowered, a.target->span, 0);
            return;
        }
        const auto& ident = static_cast<const ASTIdent&>(*a.target);
        const Frame::Binding* b = frame.find(ident.name);
        if (b == nullptr) {
            report(kErrUndefinedIdent, ident.span, ident.name);
            return;
        }
        if (!b->is_mutable) {
            report(kErrAssignToImmutable, ident.span, ident.name);
        }
    }

    void check_expr(const ASTNode& n, Frame& frame) {
        switch (n.kind) {
            case ASTKind::IntLit:
            case ASTKind::FloatLit:
            case ASTKind::StrLit:
            case ASTKind::BoolLit:
                return;
            case ASTKind::Ident: {
                const auto& i = static_cast<const ASTIdent&>(n);
                if (frame.find(i.name) == nullptr) {
                    // Without this check the Lowerer silently emits a
                    // Parameter node for the unknown name — a classic
                    // silent-fallback bug (Rule D.3).
                    report(kErrUndefinedIdent, i.span, i.name);
                }
                return;
            }
            case ASTKind::BinaryExpr: {
                const auto& b = static_cast<const ASTBinaryExpr&>(n);
                check_expr(*b.lhs, frame);
                check_expr(*b.rhs, frame);
                return;
            }
            case ASTKind::UnaryExpr: {
                const auto& u = static_cast<const ASTUnaryExpr&>(n);
                check_expr(*u.operand, frame);
                return;
            }
            case ASTKind::CallExpr: {
                const auto& c = static_cast<const ASTCallExpr&>(n);
                SymbolId callee = kInvalidSymbolId;
                if (c.callee && c.callee->kind == ASTKind::Ident) {
                    callee = static_cast<const ASTIdent&>(*c.callee).name;
                } else if (c.callee && c.callee->kind == ASTKind::PathExpr) {
                    const auto& p = static_cast<const ASTPathExpr&>(*c.callee);
                    if (!p.segments.empty()) callee = p.segments.back();
                } else if (c.callee) {
                    // Higher-order call through an arbitrary expression
                    // is not lowerable yet (Rule D.3 loud failure).
                    report(kErrNotLowered, c.callee->span, 0);
                }
                if (callee != kInvalidSymbolId) {
                    const uint32_t* arity = fn_sigs_find(callee);
                    if (arity != nullptr && *arity != c.args.size()) {
                        Error e = Error::type_(kErrCallArity, n.span);
                        e.payload = (static_cast<uint64_t>(callee) << 32)
                                  | c.args.size();
                        sink_->report(e);
                    }
                }
                for (const auto& arg : c.args) {
                    if (arg) check_expr(*arg, frame);
                }
                return;
            }
            case ASTKind::Block: {
                const auto& b = static_cast<const ASTBlock&>(n);
                for (const auto& s : b.stmts) {
                    if (s) check_stmt(*s, frame);
                }
                return;
            }
            case ASTKind::RangeExpr: {
                // `lo..hi` — check both bounds. (Today only the
                // for-statement parser produces this node, but if it
                // ever appears elsewhere the bounds must still be
                // checked — never silently skipped. Rule D.3.)
                const auto& r = static_cast<const ASTRangeExpr&>(n);
                if (r.lo) check_expr(*r.lo, frame);
                if (r.hi) check_expr(*r.hi, frame);
                return;
            }
            case ASTKind::FieldExpr:
            case ASTKind::IndexExpr:
            case ASTKind::PathExpr:
                // Rule D.3: these lower to a silent constant 0 today.
                report(kErrNotLowered, n.span, 0);
                return;
            case ASTKind::AssignExpr:
                check_assign(static_cast<const ASTAssignExpr&>(n), frame);
                return;
            default:
                // Remaining node kinds (patterns, decls) cannot appear
                // in expression position after a successful parse; the
                // Parser rejects them. Nothing to check.
                return;
        }
    }
};

} // namespace

Expected<bool> TypeChecker::check_module(const ASTModule& mod) {
    Checker c(syms_, sink_);
    c.check_module(mod);
    if (sink_->has_errors()) return std::unexpected(Error::type_(0, Span{}));
    return true;
}

} // namespace aegis
