// frontend/Lowering.hpp — Lowers an ASTModule into an IR Graph.
// ============================================================
// Law (Section §1 of spec): lowering is the boundary where the IR is
// constructed. All node creation goes through a HashCons so Pure nodes
// are canonicalized as they are built (Rule: "Hash-Consing ... Native
// to SoN"; "GVN: Identifies redundant calculations. Native to SoN.").
// ============================================================
#pragma once
#include <expected>
#include <unordered_map>

#include "aegis/support/Expected.hpp"
#include "aegis/support/Primitives.hpp"
#include "aegis/support/StringIntern.hpp"
#include "aegis/frontend/AST.hpp"
#include "aegis/frontend/Lexer.hpp"
#include "aegis/ir/Graph.hpp"
#include "aegis/ir/HashConsing.hpp"

namespace aegis {

class Lowerer {
public:
    Lowerer(Graph& g, SymbolTable* syms) : g_(g), hc_(g), syms_(syms) {}

    // Lower an entire module into the graph. Returns true on success.
    Expected<bool> lower_module(const ASTModule& mod);

private:
    Graph&       g_;
    HashCons     hc_;
    SymbolTable* syms_;

    // Per-function lowering state.
    NodeId       current_ctrl_{kInvalidNodeId};
    NodeId       current_eff_{kInvalidNodeId};

    // Variable bindings: maps an interned identifier (SymbolId) to the
    // NodeId that produces its value. Updated by `let` / `var` statements
    // and read by `ASTIdent` expressions. This is what was missing in
    // the initial prototype — the Lowerer was discarding the let-bound
    // value, so reads of the binding were lowered as fresh Parameter
    // placeholders instead of pointing at the value's producer node.
    std::unordered_map<SymbolId, NodeId> bindings_;

    // Function parameters: maps a parameter name SymbolId to the NodeId
    // of its Parameter node. Set by lower_fn.
    std::unordered_map<SymbolId, NodeId> params_;

    // Translate a single statement. Updates current_ctrl_ / current_eff_.
    Expected<bool> lower_stmt(const ASTNode& n);

    // Translate an expression and return its data NodeId (or an error).
    Expected<NodeId> lower_expr(const ASTNode& n);

    // Translate a function. Sets up Start node outputs (control + effect)
    // and the body.
    Expected<bool> lower_fn(const ASTFnDecl& fn);

    // Reset per-function state.
    void reset_function_state() {
        current_ctrl_ = kInvalidNodeId;
        current_eff_  = kInvalidNodeId;
        bindings_.clear();
        params_.clear();
    }
};

} // namespace aegis
