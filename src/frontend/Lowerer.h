// frontend/Lowerer.h — Lowers an ASTModule into an IR Graph.
// ============================================================
// Law (Section §1 of spec): lowering is the boundary where the IR is
// constructed. All node creation goes through a HashCons so Pure nodes
// are canonicalized as they are built (Rule: "Hash-Consing ... Native
// to SoN"; "GVN: Identifies redundant calculations. Native to SoN.").
// ============================================================
#pragma once
#include <expected>
#include "common/Expected.h"
#include "core/SymbolTable.h"
#include "frontend/AST.h"
#include "frontend/Lexer.h"
#include "ir/Graph.h"
#include "ir/HashConsing.h"

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

    // Translate a single statement. Updates current_ctrl_ / current_eff_.
    Expected<bool> lower_stmt(const ASTNode& n);

    // Translate an expression and return its data NodeId (or an error).
    Expected<NodeId> lower_expr(const ASTNode& n);

    // Translate a function. Sets up Start node outputs (control + effect)
    // and the body.
    Expected<bool> lower_fn(const ASTFnDecl& fn);
};

} // namespace aegis
