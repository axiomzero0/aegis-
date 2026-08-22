// frontend/TypeChecker.hpp — Name resolution + semantic checking (Pass 1).
//
// What this pass ENFORCES today (all failures are loud diagnostics —
// Rule D.3, no silent fallbacks):
//   1. Duplicate function declarations at module scope.
//   2. Duplicate parameter names in a function signature.
//   3. Duplicate `let`/`var` bindings along one lexical path (bindings
//      introduced in sibling if-branches are legal and merge, exactly
//      like the Lowerer's Phi merge).
//   4. Use of an undefined identifier (the Lowerer would otherwise
//      silently materialize a bogus Parameter node).
//   5. Assignment to an immutable `let` binding (only `var` bindings
//      may be assigned).
//   6. Call arity mismatch for locally-declared callees.
//   7. Parseable-but-not-yet-lowerable constructs (`for`, `match`,
//      field access, indexing) are REJECTED with an explicit
//      "not yet lowered" error. The Lowerer's fallback for these is a
//      constant 0 — accepting them would silently compute a wrong
//      result (Rule D.3 forbids silent wrong answers).
//
// What is DEFERRED (documented limitation, not a silent gap — see
// docs/laws.md Rule 74 for the escalation path):
//   * Full affine ownership / borrow / lexical-region lifetime
//     checking (requires the TypeTable wiring in ir/Types).
//   * Evaluation of type annotations (annotations are parsed but not
//     yet checked against inferred types).
//   * Return-type inference and checking.
//
// (Rule 70/71: the enforced set and the deferred set are both part of
// the contract; changing either requires updating this comment.)
#pragma once
#include "aegis/support/Expected.hpp"
#include "aegis/support/StringIntern.hpp"
#include "aegis/frontend/AST.hpp"

namespace aegis {
class TypeChecker {
public:
    TypeChecker(SymbolTable* syms, class DiagnosticSink* sink)
        : syms_(syms), sink_(sink) {}

    // Runs all enforced checks over the module. Returns false (via
    // std::unexpected) iff any error was reported to the sink.
    Expected<bool> check_module(const ASTModule& mod);

private:
    SymbolTable*     syms_;
    DiagnosticSink*  sink_;
};
} // namespace aegis
