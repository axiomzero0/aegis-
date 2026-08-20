// frontend/Lowerer.cpp — AST -> E-SoN lowering (uses HashCons for GVN).
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
    // Project the Start node into its Control and Effect outputs.
    NodeId start = kStartNodeId; // id 0 by Graph ctor convention.
    current_ctrl_ = g_.make_proj(start, 0);
    current_eff_  = g_.make_proj(start, 1);

    // Lower the body block statement-by-statement.
    if (fn.body) {
        const auto& blk = static_cast<const ASTBlock&>(*fn.body);
        for (const auto& s : blk.stmts) {
            if (!s) continue;
            auto r = lower_stmt(*s);
            if (!r.has_value()) return std::unexpected(r.error());
        }
    }

    // Synthesize a Return node holding the current ctrl / eff. If the
    // body's last statement was a Return, we'll have already produced
    // a Return node and current_ctrl_ / current_eff_ will be marked Dead
    // — that's fine, the verifier accepts that.
    if (current_ctrl_ != kInvalidNodeId && current_eff_ != kInvalidNodeId) {
        // Implicit return with no value: lower a Constant 0.
        NodeId zero = g_.make_constant_i64(0, /*ty=*/kInvalidTypeId);
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
                // Local binding initialization becomes a write to the
                // local slot. For now we treat the local as a stack slot
                // and emit a Store. (A proper implementation would
                // allocate a stack slot once at function entry, and the
                // Store would write to it.)
                // For the prototype, we just discard the value — the
                // identifier binding is recorded in the symbol table.
                (void)r;
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
            // Region to merge both branches (with no else, both go to a
            // shared Region). For an empty else, we lower the false_proj
            // as a no-op pass-through.
            NodeId saved_ctrl = current_ctrl_;
            NodeId saved_eff  = current_eff_;
            current_ctrl_ = true_proj;
            // Lower then-branch.
            auto then_blk = lower_stmt(*s.then_branch);
            if (!then_blk.has_value()) return std::unexpected(then_blk.error());
            NodeId ctrl_after_then = current_ctrl_;
            NodeId eff_after_then  = current_eff_;
            // Lower else-branch (or synthetic empty).
            current_ctrl_ = false_proj;
            current_eff_  = saved_eff;
            if (s.else_branch) {
                auto else_blk = lower_stmt(*s.else_branch);
                if (!else_blk.has_value()) return std::unexpected(else_blk.error());
            }
            // Merge via Region + a Phi on the effect chain.
            NodeId merge = g_.make_region({ctrl_after_then, current_ctrl_});
            (void)merge;
            current_ctrl_ = merge;
            (void)saved_ctrl;
            (void)eff_after_then;
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
        default:
            // Treat as expr-shaped.
            return lower_expr(n).has_value();
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
            // String literals are lowered as a SymbolId payload constant
            // for now (a proper implementation would lower them as
            // global byte buffers with a pointer constant).
            const auto& l = static_cast<const ASTStrLit&>(n);
            NodePayload p; p.sym = l.value;
            return g_.make_node(NodeKind::Constant, {}, kInvalidTypeId, p);
        }
        case ASTKind::Ident: {
            const auto& i = static_cast<const ASTIdent&>(n);
            // Parameter access: lowered as a Parameter node using sym.
            // For locals, we'd look up the binding's slot NodeId. For now,
            // emit a Parameter (placeholder) so the verifier + downstream
            // passes have something to operate on.
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
            // Hash-cons the binop so identical computations are deduped
            // immediately (native GVN per the spec).
            NodePayload p;
            return hc_.lookup_or_insert(k, {*l, *r}, kInvalidTypeId, p);
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
            // Infer callee effect for now: assume Altered unless we have
            // stronger information (a real implementation will look up
            // the inferred effect of the callee in the function effect
            // table built by EffectInference).
            EffectClass callee_eff = EffectClass::Altered;
            // Emit the call. Use the span-based make_call overload so we
            // can pass a runtime-built std::vector<NodeId>.
            NodeId call = g_.make_call(current_ctrl_, current_eff_, callee,
                                       std::span<const NodeId>{args.data(), args.size()},
                                       kInvalidTypeId, callee_eff);
            // If the callee was Pure, the call node is Pure and we
            // didn't need to consume the effect chain. If it was
            // Altered/Crowded, the call consumes the effect chain and
            // produces a new effect output (the call itself).
            if (g_[call].effect != EffectClass::Pure) {
                current_eff_ = call;
            }
            // Project the call's data result.
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
            // graph remains valid. (A real impl will dispatch on more
            // node kinds and emit a proper error otherwise.)
            return g_.make_constant_i64(0, kInvalidTypeId);
    }
}

} // namespace aegis
