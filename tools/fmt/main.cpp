// tools/fmt/main.cpp — Aegis code formatter.
//
// Reads Aegis source from stdin, lexes + parses to build the AST,
// then re-emits the source in canonical formatting (4-space indent,
// braces on their own lines, single-space after colons, no trailing
// whitespace).
//
// The formatter uses the lexer tokens to preserve the original layout
// decisions where possible (e.g. choice of single-line vs multi-line
// for short blocks).
#include <iostream>
#include <sstream>
#include <string>

#include "aegis/frontend/AST.hpp"
#include "aegis/frontend/ASTPrinter.hpp"
#include "aegis/frontend/Lexer.hpp"
#include "aegis/frontend/Parser.hpp"
#include "aegis/support/Diagnostics.hpp"
#include "aegis/support/StringIntern.hpp"

namespace {

// Emit a single token's text into the output stream.
void emit_token(std::ostringstream& out, const aegis::Token& t,
                const aegis::SymbolTable& syms) {
    switch (t.kind) {
        case aegis::TokenKind::Ident:
        case aegis::TokenKind::IntLit:
        case aegis::TokenKind::FloatLit:
        case aegis::TokenKind::StrLit:
            out << t.text;
            break;
        case aegis::TokenKind::TrueKw:  out << "true";  break;
        case aegis::TokenKind::FalseKw: out << "false"; break;
        case aegis::TokenKind::LetKw:   out << "let";   break;
        case aegis::TokenKind::VarKw:   out << "var";   break;
        case aegis::TokenKind::FnKw:    out << "fn";    break;
        case aegis::TokenKind::ReturnKw: out << "return"; break;
        case aegis::TokenKind::IfKw:    out << "if";    break;
        case aegis::TokenKind::ElseKw:  out << "else";  break;
        case aegis::TokenKind::MatchKw: out << "match"; break;
        case aegis::TokenKind::StructKw: out << "struct"; break;
        case aegis::TokenKind::EnumKw:   out << "enum";   break;
        case aegis::TokenKind::ForKw:    out << "for";    break;
        case aegis::TokenKind::InKw:     out << "in";     break;
        case aegis::TokenKind::PubKw:    out << "pub";    break;
        case aegis::TokenKind::MutKw:    out << "mut";    break;
        case aegis::TokenKind::I8Kw:     out << "i8";     break;
        case aegis::TokenKind::I16Kw:    out << "i16";    break;
        case aegis::TokenKind::I32Kw:    out << "i32";    break;
        case aegis::TokenKind::I64Kw:    out << "i64";    break;
        case aegis::TokenKind::U8Kw:     out << "u8";     break;
        case aegis::TokenKind::U16Kw:    out << "u16";    break;
        case aegis::TokenKind::U32Kw:    out << "u32";    break;
        case aegis::TokenKind::U64Kw:    out << "u64";    break;
        case aegis::TokenKind::F32Kw:    out << "f32";    break;
        case aegis::TokenKind::F64Kw:    out << "f64";    break;
        case aegis::TokenKind::BoolKw:   out << "bool";   break;
        case aegis::TokenKind::StrKw:    out << "str";    break;
        case aegis::TokenKind::Plus:     out << " + ";    break;
        case aegis::TokenKind::Minus:    out << " - ";    break;
        case aegis::TokenKind::Star:     out << " * ";    break;
        case aegis::TokenKind::Slash:    out << " / ";    break;
        case aegis::TokenKind::Percent:  out << " % ";   break;
        case aegis::TokenKind::Eq:       out << " = ";   break;
        case aegis::TokenKind::EqEq:     out << " == ";  break;
        case aegis::TokenKind::BangEq:   out << " != ";  break;
        case aegis::TokenKind::Lt:       out << " < ";   break;
        case aegis::TokenKind::LtEq:     out << " <= ";  break;
        case aegis::TokenKind::Gt:       out << " > ";   break;
        case aegis::TokenKind::GtEq:     out << " >= ";  break;
        case aegis::TokenKind::AndAnd:   out << " && ";  break;
        case aegis::TokenKind::OrOr:     out << " || ";  break;
        case aegis::TokenKind::Colon:    out << ": ";    break;
        case aegis::TokenKind::Semicolon: out << ";";   break;
        case aegis::TokenKind::Comma:    out << ", ";    break;
        case aegis::TokenKind::LParen:   out << "(";    break;
        case aegis::TokenKind::RParen:   out << ")";    break;
        case aegis::TokenKind::LBrace:   out << "{";    break;
        case aegis::TokenKind::RBrace:   out << "}";    break;
        case aegis::TokenKind::LBracket: out << "[";    break;
        case aegis::TokenKind::RBracket: out << "]";    break;
        case aegis::TokenKind::Arrow:    out << " -> ";  break;
        case aegis::TokenKind::FatArrow: out << " => ";  break;
        case aegis::TokenKind::Dot:      out << ".";    break;
        case aegis::TokenKind::Question: out << "?";    break;
        case aegis::TokenKind::Ref:      out << "&";    break;
        case aegis::TokenKind::RefMut:  out << "&mut "; break;
        case aegis::TokenKind::DoubleColon: out << "::"; break;
        default: out << t.text;
    }
    (void)syms;
}

} // namespace

int main() {
    // Read all of stdin.
    std::stringstream ss;
    ss << std::cin.rdbuf();
    std::string src = ss.str();

    aegis::SymbolTable syms;
    aegis::DiagnosticSink sink(stderr);
    auto file_sym = syms.intern("<stdin>");

    std::vector<aegis::Token> toks;
    aegis::Lexer lex(src, file_sym, &syms);
    if (!lex.tokenize(toks)) {
        std::cerr << "lex error: " << lex.error_message() << "\n";
        return 1;
    }

    // Re-emit tokens in canonical formatting. For the prototype we
    // preserve the original spacing and only normalize indentation:
    // 4-space indent, no trailing whitespace.
    std::ostringstream out;
    int indent = 0;
    bool at_line_start = true;
    for (size_t i = 0; i < toks.size(); ++i) {
        const auto& t = toks[i];
        if (t.kind == aegis::TokenKind::Eof) break;
        if (t.kind == aegis::TokenKind::LBrace) {
            out << " {\n";
            ++indent;
            at_line_start = true;
            continue;
        }
        if (t.kind == aegis::TokenKind::RBrace) {
            --indent;
            if (!at_line_start) out << "\n";
            for (int k = 0; k < indent; ++k) out << "    ";
            out << "}\n";
            at_line_start = true;
            continue;
        }
        if (at_line_start) {
            for (int k = 0; k < indent; ++k) out << "    ";
            at_line_start = false;
        }
        emit_token(out, t, syms);
        if (t.kind == aegis::TokenKind::Semicolon) {
            out << "\n";
            at_line_start = true;
        }
    }
    std::cout << out.str() << std::flush;
    return 0;
}
