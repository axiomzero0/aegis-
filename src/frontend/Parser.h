// ============================================================
// frontend/Parser.h — Pratt parser for Aegis.
// ============================================================
// Recursive-descent + Pratt-style precedence for binary operators.
// Produces an ASTModule holding the top-level declarations.
//
// The parser is on the cold frontend path. Errors are reported via the
// DiagnosticSink and the parser uses std::expected<ASTPtr, Error> as
// required by Rule B.1.
// ============================================================
#pragma once

#include <expected>
#include <vector>

#include "common/Diagnostics.h"
#include "common/Expected.h"
#include "core/SymbolTable.h"
#include "frontend/AST.h"
#include "frontend/Lexer.h"

namespace aegis {

class Parser {
public:
    Parser(std::vector<Token> toks, SymbolTable* syms, DiagnosticSink* sink)
        : toks_(std::move(toks)), syms_(syms), sink_(sink) {}

    // Parses an entire module. On error, returns the first error.
    Expected<std::unique_ptr<ASTModule>> parse_module();

private:
    std::vector<Token> toks_;
    SymbolTable*       syms_;
    DiagnosticSink*    sink_;
    size_t             pos_{0};

    [[nodiscard]] const Token& peek(size_t off = 0) const noexcept {
        return toks_[std::min(pos_ + off, toks_.size() - 1)];
    }
    [[nodiscard]] bool at(TokenKind k) const noexcept { return peek().kind == k; }
    bool consume(TokenKind k) noexcept {
        if (at(k)) { ++pos_; return true; }
        return false;
    }
    const Token& expect(TokenKind k, const char* what);
    [[nodiscard]] Error make_err(const Token& t, std::string_view what);

    // Top-level decls.
    Expected<ASTPtr> parse_top_level_item();
    Expected<ASTPtr> parse_fn_decl(bool is_pub);
    Expected<ASTPtr> parse_struct_decl();
    Expected<ASTPtr> parse_enum_decl();

    // Statements.
    Expected<ASTPtr> parse_block();
    Expected<ASTPtr> parse_stmt();
    Expected<ASTPtr> parse_let_or_var_stmt(bool is_var);
    Expected<ASTPtr> parse_return_stmt();
    Expected<ASTPtr> parse_if_stmt();
    Expected<ASTPtr> parse_for_stmt();
    Expected<ASTPtr> parse_match_stmt();

    // Expressions (Pratt).
    Expected<ASTPtr> parse_expr(int min_prec = 0);
    Expected<ASTPtr> parse_primary();
    Expected<ASTPtr> parse_unary();
    Expected<ASTPtr> parse_postfix(ASTPtr base);

    // Patterns.
    Expected<ASTPtr> parse_pattern();

    // Type annotations (reuses expression grammar for simplicity).
    Expected<ASTPtr> parse_type_annotation();

    static int precedence(TokenKind k) noexcept;
};

} // namespace aegis
