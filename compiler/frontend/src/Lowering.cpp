// frontend/Lowering.cpp — AST -> E-SoN lowering (uses HashCons for GVN).
//
// Tracks let-bindings so that `let c = a + b; return c;` correctly
// returns the Add node rather than a Parameter placeholder.
//
// Includes the HashCons machinery for native GVN: Pure nodes are
// canonicalized at construction time so the IR never contains two
// structurally identical Pure nodes.
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
        default:                  return NodeKind::Add; // fallback
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
        params_[param->name] = param_node;
        bindings_[param->name] = param_node;
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

Expected<bool> Lowerer::lower_stmt(const ASTNode& n) {
    switch (n.kind) {
        case ASTKind::LetStmt: {
            const auto& s = static_cast<const ASTLetStmt&>(n);
            if (s.init) {
                auto r = lower_expr(*s.init);
                if (!r.has_value()) return std::unexpected(r.error());
                // Record the binding so subsequent Ident reads pick up the
                // value's producer node id rather than synthesizing a
                // Parameter placeholder.
                bindings_[s.name] = *r;
            } else {
                // Uninitialized `var` — produce a zero Constant placeholder.
                NodeId zero = g_.make_constant_i64(0, kInvalidTypeId);
                bindings_[s.name] = zero;
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
            // Snapshot bindings for the then-branch (we'll need them for
            // phi-merging at the join).
            auto bindings_before_then = bindings_;
            auto then_blk = lower_stmt(*s.then_branch);
            if (!then_blk.has_value()) return std::unexpected(then_blk.error());
            NodeId ctrl_after_then = current_ctrl_;
            NodeId eff_after_then  = current_eff_;
            auto bindings_after_then = bindings_;

            // Lower else-branch (or fall through).
            current_ctrl_ = false_proj;
            current_eff_  = saved_eff;
            bindings_ = bindings_before_then;
            if (s.else_branch) {
                auto else_blk = lower_stmt(*s.else_branch);
                if (!else_blk.has_value()) return std::unexpected(else_blk.error());
            }
            NodeId ctrl_after_else = current_ctrl_;
            NodeId eff_after_else  = current_eff_;
            auto bindings_after_else = bindings_;

            // Merge via Region.
            NodeId merge = g_.make_region({ctrl_after_then, ctrl_after_else});
            current_ctrl_ = merge;
            // For the effect chain, prefer the then-branch's effect for
            // simplicity (a real impl would emit a Phi for the effect too).
            current_eff_ = eff_after_then;
            (void)eff_after_else;

            // Merge let-bindings that were modified in either branch via
            // Phi nodes. For each binding that exists in both branches
            // with *different* NodeIds, emit a Phi at the merge region.
            // For simplicity, we currently just take the then-branch's
            // binding; full phi materialization comes with a real SSA
            // construction pass.
            for (const auto& [name, then_val] : bindings_after_then) {
                auto it = bindings_after_else.find(name);
                if (it != bindings_after_else.end() && it->second != then_val) {
                    // Both branches assigned different values. Emit a Phi.
                    NodeId phi = g_.make_phi(merge, {then_val, it->second}, kInvalidTypeId);
                    bindings_[name] = phi;
                } else {
                    bindings_[name] = then_val;
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
            // Expression-level assignment: target <- value. We support the
            // simple `x = expr` case (where x is an Ident bound by `let`
            // or `var`): update the binding's NodeId. Compound assignment
            // (=, +=, etc.) is desugared into a BinaryExpr + assignment.
            const auto& a = static_cast<const ASTAssignExpr&>(n);
            // Lower the value (right-hand side).
            auto val = lower_expr(*a.value);
            if (!val.has_value()) return std::unexpected(val.error());
            // If target is an Ident, look up its binding and update it.
            if (a.target && a.target->kind == ASTKind::Ident) {
                const auto& ident = static_cast<const ASTIdent&>(*a.target);
                SymbolId name = ident.name;
                if (a.op == TokenKind::Eq) {
                    bindings_[name] = *val;
                } else {
                    // Compound assignment (a += b -> a = a + b).
                    // Look up current value, fold with new value via binop.
                    auto cur = bindings_.find(name);
                    NodeId lhs = (cur != bindings_.end()) ? cur->second
                        : g_.make_constant_i64(0, kInvalidTypeId);
                    NodeKind k = binop_to_node_kind(a.op);
                    // For PlusEq -> Add, MinusEq -> Sub, etc.
                    if (a.op == TokenKind::PlusEq)   k = NodeKind::Add;
                    else if (a.op == TokenKind::MinusEq) k = NodeKind::Sub;
                    else if (a.op == TokenKind::StarEq)  k = NodeKind::Mul;
                    else if (a.op == TokenKind::SlashEq) k = NodeKind::Div;
                    NodeId folded = hc_.lookup_or_insert(k, {lhs, *val}, kInvalidTypeId, NodePayload{});
                    bindings_[name] = folded;
                }
            }
            return true;
        }
        default:
            // Treat as expr-shaped.
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
            // Look up the binding first; if it exists, return its NodeId.
            // This is the key fix: `let c = a + b; return c;` will now
            // return the Add node, not a fresh Parameter.
            auto it = bindings_.find(i.name);
            if (it != bindings_.end()) {
                return it->second;
            }
            // Unknown identifier — synthesize a Parameter placeholder
            // (the type checker would normally catch this as an error).
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
            // Resolve callee SymbolId.
            SymbolId callee = kInvalidSymbolId;
            if (c.callee && c.callee->kind == ASTKind::PathExpr) {
                const auto& p = static_cast<const ASTPathExpr&>(*c.callee);
                if (!p.segments.empty()) callee = p.segments.back();
            } else if (c.callee && c.callee->kind == ASTKind::Ident) {
                const auto& i = static_cast<const ASTIdent&>(*c.callee);
                callee = i.name;
            }
            // Lower arguments.
            std::vector<NodeId> args;
            args.reserve(c.args.size());
            for (const auto& a : c.args) {
                if (!a) continue;
                auto r = lower_expr(*a);
                if (!r.has_value()) return std::unexpected(r.error());
                args.push_back(*r);
            }
            // Conservative: assume Altered unless we know better. A real
            // impl looks up the callee's inferred effect from a function
            // effect table built by EffectInference.
            EffectClass callee_eff = EffectClass::Altered;
            // Emit the call.
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
            // Unhandled expression: emit a placeholder Constant 0 so the
            // graph remains valid.
            return g_.make_constant_i64(0, kInvalidTypeId);
    }
}

} // namespace aegis
