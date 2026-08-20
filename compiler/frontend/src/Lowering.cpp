// frontend/Lowering.cpp — AST -> E-SoN lowering (uses HashCons for GVN).
//
// Tracks let-bindings so that `let c = a + b; return c;` correctly
// returns the Add node rather than a Parameter placeholder.
//
// Includes the HashCons machinery for native GVN: Pure nodes are
// canonicalized at construction time so the IR never contains two
// structurally identical Pure nodes.
//
// Law: Rule D.4 — uses SwissTable for bindings/params (not std::unordered_map).
#include "aegis/frontend/Lowering.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/frontend/EffectInference.hpp"

namespace aegis {

namespace {
NodeKind binop_to_node_kind(TokenKind k) noexcept {
    switch (k) {
        case TokenKind::Plus:    return NodeKind::Add;
        case TokenKind::Minus:   return NodeKind::Sub;
        case TokenKind::Star:     return NodeKind::Mul;
        case TokenKind::Slash:    return NodeKind::Div;
        case TokenKind::Percent:  return NodeKind::Mod;
        case TokenKind::Amp:      return NodeKind::And;
        case TokenKind::Pipe:     return NodeKind::Or;
        case TokenKind::Caret:    return NodeKind::Xor;
        case TokenKind::Shl:      return NodeKind::Shl;
        case TokenKind::Shr:      return NodeKind::Shr;
        case TokenKind::EqEq:     return NodeKind::CmpEq;
        case TokenKind::BangEq:   return NodeKind::CmpNe;
        case TokenKind::Lt:       return NodeKind::CmpLt;
        case TokenKind::LtEq:     return NodeKind::CmpLe;
        case TokenKind::Gt:       return NodeKind::CmpGt;
        case TokenKind::GtEq:     return NodeKind::CmpGe;
        case TokenKind::AndAnd:   return NodeKind::And;
        case TokenKind::OrOr:     return NodeKind::Or;
        // Compound assignment tokens are not valid binary ops for
        // expression lowering — they are handled by lower_stmt's
        // AssignExpr case. Reaching here is a programming error.
        case TokenKind::PlusEq:
        case TokenKind::MinusEq:
        case TokenKind::StarEq:
        case TokenKind::SlashEq:
        case TokenKind::Question:
        case TokenKind::FatArrow:
        case TokenKind::Arrow:
            return NodeKind::Add; // Defensive fallback; verifier catches.
        case TokenKind::Eof:      return NodeKind::Add;
        // The remaining tokens (keywords, punctuation, etc.) are not
        // valid binary operators and should never reach this function.
        default:                  return NodeKind::Add;
    }
}
} // namespace

Expected<bool> Lowerer::lower_module(const ASTModule& mod) {
    for (const auto& it : mod.items) {
        if (!it) continue;
        if (it->kind == ASTKind::FnDecl) {
            auto r = lower_fn(static_cast<const ASTFnDecl&>(*it));
            if (!r.has_value()) return std::unexpected(r.error());
        }
        // Structs / Enums are skipped for now — full layout pass comes later.
    }
    return true;
}

Expected<bool> Lowerer::lower_fn(const ASTFnDecl& fn) {
    reset_function_state();

    // Project the Start node into its Control and Effect outputs.
    NodeId start = kStartNodeId; // id 0 by Graph ctor convention.
    current_ctrl_ = g_.make_proj(start, 0);
    current_eff_  = g_.make_proj(start, 1);

    // Lower parameters into Parameter nodes + bind them by name.
    for (const auto& p : fn.params) {
        const ASTParam* param = static_cast<const ASTParam*>(p.get());
        if (!param) continue;
        NodePayload pp; pp.sym = param->name;
        NodeId param_node = g_.make_node(NodeKind::Parameter, {}, kInvalidTypeId, pp);
        params_.insert(param->name, param_node);
        bindings_.insert(param->name, param_node);
    }

    // Lower the body block statement-by-statement.
    if (fn.body) {
        const auto& blk = static_cast<const ASTBlock&>(*fn.body);
        for (const auto& s : blk.stmts) {
            if (!s) continue;
            auto r = lower_stmt(*s);
            if (!r.has_value()) return std::unexpected(r.error());
        }
    }

    // If the body didn't explicitly return, synthesize a void return.
    if (current_ctrl_ != kInvalidNodeId && current_eff_ != kInvalidNodeId) {
        NodeId zero = g_.make_constant_i64(0, kInvalidTypeId);
        g_.make_return(current_ctrl_, current_eff_, zero);
    }
    return true;
}

namespace {
// Snapshot of bindings for branch-merge phi materialization. We collect
// the live (SymbolId, NodeId) pairs into a SmallVector so we can compare
// then-branch and else-branch bindings and emit Phis for differences.
struct BindingSnapshot {
    struct Pair { SymbolId sym; NodeId node; };
    SmallVector<Pair, 8> entries;
};

BindingSnapshot snapshot(const SwissTable<SymbolId, NodeId>& m) {
    BindingSnapshot s;
    m.for_each([&](SymbolId k, NodeId v) {
        s.entries.push_back({k, v});
    });
    return s;
}
} // namespace

Expected<bool> Lowerer::lower_stmt(const ASTNode& n) {
    switch (n.kind) {
        case ASTKind::LetStmt: {
            const auto& s = static_cast<const ASTLetStmt&>(n);
            if (s.init) {
                auto r = lower_expr(*s.init);
                if (!r.has_value()) return std::unexpected(r.error());
                bindings_.insert(s.name, *r);
            } else {
                NodeId zero = g_.make_constant_i64(0, kInvalidTypeId);
                bindings_.insert(s.name, zero);
            }
            return true;
        }
        case ASTKind::ExprStmt: {
            const auto& s = static_cast<const ASTExprStmt&>(n);
            if (s.expr) {
                auto r = lower_expr(*s.expr);
                if (!r.has_value()) return std::unexpected(r.error());
            }
            return true;
        }
        case ASTKind::ReturnStmt: {
            const auto& s = static_cast<const ASTReturnStmt&>(n);
            NodeId val = kInvalidNodeId;
            if (s.value) {
                auto r = lower_expr(*s.value);
                if (!r.has_value()) return std::unexpected(r.error());
                val = *r;
            } else {
                val = g_.make_constant_i64(0, kInvalidTypeId);
            }
            g_.make_return(current_ctrl_, current_eff_, val);
            current_ctrl_ = kInvalidNodeId;
            current_eff_  = kInvalidNodeId;
            return true;
        }
        case ASTKind::IfStmt: {
            const auto& s = static_cast<const ASTIfStmt&>(n);
            auto cond = lower_expr(*s.cond);
            if (!cond.has_value()) return std::unexpected(cond.error());
            NodeId if_node = g_.make_if(current_ctrl_, *cond);
            NodeId true_proj  = g_.make_proj(if_node, 0);
            NodeId false_proj = g_.make_proj(if_node, 1);

            NodeId saved_eff = current_eff_;

            // Lower then-branch.
            current_ctrl_ = true_proj;
            BindingSnapshot bindings_before_then = snapshot(bindings_);
            auto then_blk = lower_stmt(*s.then_branch);
            if (!then_blk.has_value()) return std::unexpected(then_blk.error());
            NodeId ctrl_after_then = current_ctrl_;
            NodeId eff_after_then  = current_eff_;
            BindingSnapshot bindings_after_then = snapshot(bindings_);

            // Lower else-branch (or fall through).
            current_ctrl_ = false_proj;
            current_eff_  = saved_eff;
            // Restore the pre-then bindings for the else branch.
            bindings_.clear();
            for (const auto& [sym, node] : bindings_before_then.entries) {
                bindings_.insert(sym, node);
            }
            if (s.else_branch) {
                auto else_blk = lower_stmt(*s.else_branch);
                if (!else_blk.has_value()) return std::unexpected(else_blk.error());
            }
            NodeId ctrl_after_else = current_ctrl_;
            NodeId eff_after_else  = current_eff_;
            BindingSnapshot bindings_after_else = snapshot(bindings_);

            // Merge via Region.
            NodeId merge = g_.make_region({ctrl_after_then, ctrl_after_else});
            current_ctrl_ = merge;
            current_eff_ = eff_after_then;
            (void)eff_after_else;

            // Merge let-bindings that were modified in either branch via
            // Phi nodes. For each binding that exists in both branches
            // with *different* NodeIds, emit a Phi at the merge region.
            for (const auto& [name, then_val] : bindings_after_then.entries) {
                // Find the corresponding else-branch binding (if any).
                const NodeId* else_p = nullptr;
                for (const auto& [ename, enode] : bindings_after_else.entries) {
                    if (ename == name) {
                        else_p = &enode;
                        break;
                    }
                }
                if (else_p != nullptr && *else_p != then_val) {
                    NodeId phi = g_.make_phi(merge, {then_val, *else_p}, kInvalidTypeId);
                    bindings_.insert(name, phi);
                } else {
                    bindings_.insert(name, then_val);
                }
            }
            return true;
        }
        case ASTKind::Block: {
            const auto& b = static_cast<const ASTBlock&>(n);
            for (const auto& s : b.stmts) {
                if (!s) continue;
                auto r = lower_stmt(*s);
                if (!r.has_value()) return std::unexpected(r.error());
            }
            return true;
        }
        case ASTKind::AssignExpr: {
            const auto& a = static_cast<const ASTAssignExpr&>(n);
            auto val = lower_expr(*a.value);
            if (!val.has_value()) return std::unexpected(val.error());
            if (a.target && a.target->kind == ASTKind::Ident) {
                const auto& ident = static_cast<const ASTIdent&>(*a.target);
                SymbolId name = ident.name;
                if (a.op == TokenKind::Eq) {
                    bindings_.insert(name, *val);
                } else {
                    const NodeId* cur = bindings_.get(name);
                    NodeId lhs = (cur != nullptr) ? *cur
                        : g_.make_constant_i64(0, kInvalidTypeId);
                    NodeKind k = binop_to_node_kind(a.op);
                    if (a.op == TokenKind::PlusEq)   k = NodeKind::Add;
                    else if (a.op == TokenKind::MinusEq) k = NodeKind::Sub;
                    else if (a.op == TokenKind::StarEq)  k = NodeKind::Mul;
                    else if (a.op == TokenKind::SlashEq) k = NodeKind::Div;
                    NodeId folded = hc_.lookup_or_insert(k, {lhs, *val}, kInvalidTypeId, NodePayload{});
                    bindings_.insert(name, folded);
                }
            }
            return true;
        }
        default:
            auto r = lower_expr(n);
            return r.has_value();
    }
}

Expected<NodeId> Lowerer::lower_expr(const ASTNode& n) {
    switch (n.kind) {
        case ASTKind::IntLit: {
            const auto& l = static_cast<const ASTIntLit&>(n);
            return g_.make_constant_i64(static_cast<int64_t>(l.value), kInvalidTypeId);
        }
        case ASTKind::FloatLit: {
            const auto& l = static_cast<const ASTFloatLit&>(n);
            return g_.make_constant_f64(l.value, kInvalidTypeId);
        }
        case ASTKind::BoolLit: {
            const auto& l = static_cast<const ASTBoolLit&>(n);
            return g_.make_constant_u64(l.value ? 1u : 0u, kInvalidTypeId);
        }
        case ASTKind::StrLit: {
            const auto& l = static_cast<const ASTStrLit&>(n);
            NodePayload p; p.sym = l.value;
            return g_.make_node(NodeKind::Constant, {}, kInvalidTypeId, p);
        }
        case ASTKind::Ident: {
            const auto& i = static_cast<const ASTIdent&>(n);
            if (const NodeId* p = bindings_.get(i.name); p != nullptr) {
                return *p;
            }
            NodePayload p; p.sym = i.name;
            return g_.make_node(NodeKind::Parameter, {}, kInvalidTypeId, p);
        }
        case ASTKind::BinaryExpr: {
            const auto& b = static_cast<const ASTBinaryExpr&>(n);
            auto l = lower_expr(*b.lhs);
            if (!l.has_value()) return std::unexpected(l.error());
            auto r = lower_expr(*b.rhs);
            if (!r.has_value()) return std::unexpected(r.error());
            NodeKind k = binop_to_node_kind(b.op);
            return hc_.lookup_or_insert(k, {*l, *r}, kInvalidTypeId, NodePayload{});
        }
        case ASTKind::UnaryExpr: {
            const auto& u = static_cast<const ASTUnaryExpr&>(n);
            auto x = lower_expr(*u.operand);
            if (!x.has_value()) return std::unexpected(x.error());
            NodeKind k = (u.op == TokenKind::Minus) ? NodeKind::Neg : NodeKind::Not;
            return hc_.lookup_or_insert(k, {*x}, kInvalidTypeId, NodePayload{});
        }
        case ASTKind::CallExpr: {
            const auto& c = static_cast<const ASTCallExpr&>(n);
            SymbolId callee = kInvalidSymbolId;
            if (c.callee && c.callee->kind == ASTKind::PathExpr) {
                const auto& p = static_cast<const ASTPathExpr&>(*c.callee);
                if (!p.segments.empty()) callee = p.segments.back();
            } else if (c.callee && c.callee->kind == ASTKind::Ident) {
                const auto& i = static_cast<const ASTIdent&>(*c.callee);
                callee = i.name;
            }
            std::vector<NodeId> args;
            args.reserve(c.args.size());
            for (const auto& a : c.args) {
                if (!a) continue;
                auto r = lower_expr(*a);
                if (!r.has_value()) return std::unexpected(r.error());
                args.push_back(*r);
            }
            EffectClass callee_eff = EffectClass::Altered;
            NodeId call = g_.make_call(current_ctrl_, current_eff_, callee,
                                       std::span<const NodeId>{args.data(), args.size()},
                                       kInvalidTypeId, callee_eff);
            if (g_[call].effect != EffectClass::Pure) {
                current_eff_ = call;
            }
            return g_.make_proj(call, 0);
        }
        case ASTKind::Block: {
            const auto& b = static_cast<const ASTBlock&>(n);
            NodeId last = kInvalidNodeId;
            for (const auto& s : b.stmts) {
                if (!s) continue;
                if (s->kind == ASTKind::ExprStmt) {
                    const auto& es = static_cast<const ASTExprStmt&>(*s);
                    if (es.expr) {
                        auto r = lower_expr(*es.expr);
                        if (!r.has_value()) return std::unexpected(r.error());
                        last = *r;
                    }
                } else {
                    auto r = lower_stmt(*s);
                    if (!r.has_value()) return std::unexpected(r.error());
                }
            }
            return last;
        }
        default:
            return g_.make_constant_i64(0, kInvalidTypeId);
    }
}

} // namespace aegis
