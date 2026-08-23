// frontend/Lowering.hpp — Lowers an ASTModule into an IR Graph.
// ============================================================
// Law (Section §1 of spec): lowering is the boundary where the IR is
// constructed. All node creation goes through a HashCons so Pure nodes
// are canonicalized as they are built (Rule: "Hash-Consing ... Native
// to SoN"; "GVN: Identifies redundant calculations. Native to SoN.").
//
// Law: Rule D.4 — No Lazy Data Structures. Uses SwissTable (not
// std::unordered_map) for bindings / params lookups — these are hot
// paths in the frontend.
// ============================================================
#pragma once
#include <expected>

#include "aegis/support/Expected.hpp"
#include "aegis/support/Primitives.hpp"
#include "aegis/support/StringIntern.hpp"
#include "aegis/support/SwissTable.hpp"
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

    // Lower ONE function into this (fresh) graph. Public because
    // separate machine emission compiles each function into its own
    // graph (own vreg space) while sharing one SymbolTable, so call
    // sites and callees agree on symbol ids. The graph must contain
    // only this function (create a fresh Graph per call).
    Expected<bool> lower_fn(const ASTFnDecl& fn);

private:
    Graph&       g_;
    HashCons     hc_;
    SymbolTable* syms_;

    // Per-function lowering state.
    NodeId       current_ctrl_{kInvalidNodeId};
    NodeId       current_eff_{kInvalidNodeId};

    // Variable bindings: maps an interned identifier (SymbolId) to the
    // NodeId that produces its value. Updated by `let` / `var` statements
    // and read by `ASTIdent` expressions.
    //
    // Law: Rule D.4 — use SwissTable (flat, open-addressing, cache-
    // friendly) instead of std::unordered_map (which allocates per
    // insertion and is forbidden in the hot path).
    SwissTable<SymbolId, NodeId> bindings_{};

    // Function parameters: maps a parameter name SymbolId to the NodeId
    // of its Parameter node.
    SwissTable<SymbolId, NodeId> params_{};

    // Translate a single statement. Updates current_ctrl_ / current_eff_.
    Expected<bool> lower_stmt(const ASTNode& n);

    // Translate an expression and return its data NodeId (or an error).
    Expected<NodeId> lower_expr(const ASTNode& n);

    // Reset per-function state.
    void reset_function_state() {
        current_ctrl_ = kInvalidNodeId;
        current_eff_  = kInvalidNodeId;
        bindings_.clear();
        params_.clear();
    }
};

} // namespace aegis
