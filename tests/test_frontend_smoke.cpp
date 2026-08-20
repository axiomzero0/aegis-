// tests/test_frontend_smoke.cpp — Smoke test: lex+parse a tiny Aegis file.
#include <cassert>
#include <iostream>

#include "common/Diagnostics.h"
#include "core/SymbolTable.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/EffectInference.h"

namespace {

using namespace aegis;

int test_lexer_basic_keywords() {
    SymbolTable syms;
    std::vector<Token> tokens;
    Lexer lex("let x = 42;", kInvalidSymbolId, &syms);
    if (!lex.tokenize(tokens)) return 1;
    assert(tokens[0].kind == TokenKind::LetKw);
    assert(tokens[1].kind == TokenKind::Ident);
    assert(tokens[2].kind == TokenKind::Eq);
    assert(tokens[3].kind == TokenKind::IntLit);
    assert(tokens[4].kind == TokenKind::Semicolon);
    return 0;
}

int test_parse_simple_fn() {
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    std::string src = "fn add(a: i32, b: i32) -> i32 { return a + b; }";
    Lexer lex(src, kInvalidSymbolId, &syms);
    std::vector<Token> tokens;
    if (!lex.tokenize(tokens)) return 1;
    Parser parser(std::move(tokens), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    assert(!mod.value()->items.empty());
    return 0;
}

int test_effect_inference() {
    SymbolTable syms;
    DiagnosticSink sink(stderr);
    std::string src = "fn add(a: i32, b: i32) -> i32 { return a + b; }";
    Lexer lex(src, kInvalidSymbolId, &syms);
    std::vector<Token> tokens;
    if (!lex.tokenize(tokens)) return 1;
    Parser parser(std::move(tokens), &syms, &sink);
    auto mod = parser.parse_module();
    if (!mod.has_value()) return 1;
    const auto& fn = static_cast<const ASTFnDecl&>(*mod.value()->items[0]);
    InferredEffect e = infer_function_effect(fn, &syms);
    assert(e == InferredEffect::Pure);
    return 0;
}

} // namespace

int main() {
    test_lexer_basic_keywords();
    test_parse_simple_fn();
    test_effect_inference();
    std::cout << "frontend_smoke tests passed (assertions OK)\n";
    return 0;
}
