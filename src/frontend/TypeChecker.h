// frontend/TypeChecker.h — Stub for the affine type checker.
// Full implementation of affine ownership / borrow / region lifetimes
// is a large module; this stub provides the API surface so the Lowerer
// can drive it. (Rule: Affine Type Checking is Pass #1 of the frontend.)
#pragma once
#include "common/Expected.h"
#include "core/SymbolTable.h"
#include "frontend/AST.h"

namespace aegis {
class TypeChecker {
public:
    TypeChecker(SymbolTable* syms, class DiagnosticSink* sink)
        : syms_(syms), sink_(sink) {}
    Expected<bool> check_module(const ASTModule& mod);
private:
    SymbolTable* syms_;
    DiagnosticSink* sink_;
};
} // namespace aegis
