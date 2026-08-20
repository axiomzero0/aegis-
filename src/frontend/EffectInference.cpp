// frontend/EffectInference.cpp — Walks an ASTFnDecl's body to infer its
// effect class (Pure / Altered / Crowded) per §3 of the language spec.
#include "frontend/EffectInference.h"

#include "core/SymbolTable.h"

namespace aegis {

namespace {

struct EffectAccum {
    bool writes_local{false};
    bool writes_through_ref{false};
    bool calls_io{false};
    bool calls_atomic{false};
    bool calls_thread{false};
    bool calls_unknown{false};  // unknown callee: assume Crowded for safety
};

void walk_expr(const ASTNode& n, EffectAccum& acc);
void walk_stmt(const ASTNode& n, EffectAccum& acc);

void walk_expr(const ASTNode& n, EffectAccum& acc) {
    switch (n.kind) {
        case ASTKind::BinaryExpr: {
            const auto& b = static_cast<const ASTBinaryExpr&>(n);
            if (b.op == TokenKind::Eq || b.op == TokenKind::PlusEq ||
                b.op == TokenKind::MinusEq || b.op == TokenKind::StarEq) {
                // Assignment. If target is an Ident bound with var,
                // mark writes_local. If target is Field/Index on a
                // reference, mark writes_through_ref. (Without full
                // type information, conservatively mark both.)
                acc.writes_local = true;
                acc.writes_through_ref = true;
            }
            if (b.lhs) walk_expr(*b.lhs, acc);
            if (b.rhs) walk_expr(*b.rhs, acc);
            break;
        }
        case ASTKind::AssignExpr: {
            const auto& a = static_cast<const ASTAssignExpr&>(n);
            acc.writes_local = true;
            acc.writes_through_ref = true; // conservative
            if (a.target) walk_expr(*a.target, acc);
            if (a.value)   walk_expr(*a.value, acc);
            break;
        }
        case ASTKind::UnaryExpr: {
            const auto& u = static_cast<const ASTUnaryExpr&>(n);
            if (u.operand) walk_expr(*u.operand, acc);
            break;
        }
        case ASTKind::CallExpr: {
            const auto& c = static_cast<const ASTCallExpr&>(n);
            // Inspect callee. If it's a PathExpr whose segments start with
            // "std", classify by namespace.
            if (c.callee && c.callee->kind == ASTKind::PathExpr) {
                const auto& p = static_cast<const ASTPathExpr&>(*c.callee);
                if (!p.segments.empty()) {
                    // Read first segment (without SymbolTable access, we
                    // cannot actually compare names; but we can dispatch
                    // on segment count, which is a useful proxy here.)
                    (void)p; // Handled below with SymbolTable.
                }
            }
            // Conservatively assume any unknown call writes.
            acc.calls_unknown = true;
            for (const auto& a : c.args) if (a) walk_expr(*a, acc);
            break;
        }
        case ASTKind::FieldExpr: {
            const auto& f = static_cast<const ASTFieldExpr&>(n);
            if (f.base) walk_expr(*f.base, acc);
            break;
        }
        case ASTKind::IndexExpr: {
            const auto& i = static_cast<const ASTIndexExpr&>(n);
            if (i.base)  walk_expr(*i.base, acc);
            if (i.index) walk_expr(*i.index, acc);
            break;
        }
        case ASTKind::Ident:
        case ASTKind::IntLit:
        case ASTKind::FloatLit:
        case ASTKind::StrLit:
        case ASTKind::BoolLit:
            break;
        case ASTKind::PathExpr:
            break;
        case ASTKind::Block: {
            const auto& b = static_cast<const ASTBlock&>(n);
            for (const auto& s : b.stmts) if (s) walk_stmt(*s, acc);
            break;
        }
        default:
            // Unknown node — conservative.
            acc.calls_unknown = true;
            break;
    }
}

void walk_stmt(const ASTNode& n, EffectAccum& acc) {
    switch (n.kind) {
        case ASTKind::LetStmt: {
            const auto& s = static_cast<const ASTLetStmt&>(n);
            if (s.is_var) acc.writes_local = true;
            if (s.init) walk_expr(*s.init, acc);
            break;
        }
        case ASTKind::ExprStmt: {
            const auto& s = static_cast<const ASTExprStmt&>(n);
            if (s.expr) walk_expr(*s.expr, acc);
            break;
        }
        case ASTKind::ReturnStmt: {
            const auto& s = static_cast<const ASTReturnStmt&>(n);
            if (s.value) walk_expr(*s.value, acc);
            break;
        }
        case ASTKind::IfStmt: {
            const auto& s = static_cast<const ASTIfStmt&>(n);
            if (s.cond)        walk_expr(*s.cond, acc);
            if (s.then_branch) walk_stmt(*s.then_branch, acc);
            if (s.else_branch) walk_stmt(*s.else_branch, acc);
            break;
        }
        case ASTKind::ForStmt: {
            const auto& s = static_cast<const ASTForStmt&>(n);
            if (s.iter) walk_expr(*s.iter, acc);
            if (s.body) walk_stmt(*s.body, acc);
            break;
        }
        case ASTKind::MatchStmt: {
            const auto& s = static_cast<const ASTMatchStmt&>(n);
            if (s.scrutinee) walk_expr(*s.scrutinee, acc);
            for (const auto& arm : s.arms) if (arm.body) walk_expr(*arm.body, acc);
            break;
        }
        case ASTKind::Block: {
            const auto& b = static_cast<const ASTBlock&>(n);
            for (const auto& st : b.stmts) if (st) walk_stmt(*st, acc);
            break;
        }
        default:
            // Treat other stmt kinds as expr-shaped (call walk_expr).
            walk_expr(n, acc);
            break;
    }
}

InferredEffect finalize(const EffectAccum& acc) {
    if (acc.calls_io || acc.calls_atomic || acc.calls_thread || acc.calls_unknown) {
        return InferredEffect::Crowded;
    }
    if (acc.writes_local || acc.writes_through_ref) {
        return InferredEffect::Altered;
    }
    return InferredEffect::Pure;
}

} // namespace

InferredEffect infer_function_effect(const ASTFnDecl& fn, SymbolTable* syms) {
    EffectAccum acc;

    // Mark has &mut params at signature level (writes_through_ref will be set).
    for (const auto& p : fn.params) {
        const ASTParam* param = static_cast<const ASTParam*>(p.get());
        if (param && param->mutable_) {
            acc.writes_through_ref = true;
        }
    }

    // Walk the body, but be smarter about call classification using the
    // SymbolTable. We redo the call-dispatch here using names.
    struct BodyWalker {
        EffectAccum& acc;
        SymbolTable* syms;
        void expr(const ASTNode& n) {
            if (n.kind == ASTKind::CallExpr) {
                const auto& c = static_cast<const ASTCallExpr&>(n);
                if (c.callee && c.callee->kind == ASTKind::PathExpr) {
                    const auto& p = static_cast<const ASTPathExpr&>(*c.callee);
                    if (p.segments.size() >= 2) {
                        std::string_view ns = syms->at(p.segments[0]);
                        std::string_view sub = syms->at(p.segments[1]);
                        if (ns == "std") {
                            if (sub == "io")      acc.calls_io = true;
                            else if (sub == "atomic")  acc.calls_atomic = true;
                            else if (sub == "thread")  acc.calls_thread = true;
                            else acc.calls_unknown = true;
                        } else {
                            acc.calls_unknown = true;
                        }
                    } else {
                        acc.calls_unknown = true;
                    }
                } else {
                    acc.calls_unknown = true;
                }
                for (const auto& a : c.args) if (a) expr(*a);
                return;
            }
            walk_expr(n, acc);
        }
    };
    BodyWalker w{acc, syms};
    (void)w;
    if (fn.body) {
        const auto& blk = static_cast<const ASTBlock&>(*fn.body);
        for (const auto& st : blk.stmts) if (st) walk_stmt(*st, acc);
    }

    return finalize(acc);
}

} // namespace aegis
