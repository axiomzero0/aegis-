// frontend/Lexer.cpp — Aegis lexer.
#include "aegis/frontend/Lexer.hpp"

#include "aegis/support/StringIntern.hpp"

#include <cctype>
#include <cstring>

namespace aegis {

namespace {

struct KwEntry { std::string_view text; TokenKind kind; };

// Note: keep this sorted by length so multi-char keywords win on prefix match.
constexpr KwEntry kKeywords[] = {
    {"let",     TokenKind::LetKw},
    {"var",     TokenKind::VarKw},
    {"fn",      TokenKind::FnKw},
    {"return",  TokenKind::ReturnKw},
    {"if",      TokenKind::IfKw},
    {"else",    TokenKind::ElseKw},
    {"match",   TokenKind::MatchKw},
    {"struct",  TokenKind::StructKw},
    {"enum",    TokenKind::EnumKw},
    {"alloc",   TokenKind::AllocKw},
    {"stack",   TokenKind::StackKw},
    {"for",     TokenKind::ForKw},
    {"in",      TokenKind::InKw},
    {"pub",     TokenKind::PubKw},
    {"mut",     TokenKind::MutKw},
    {"true",    TokenKind::TrueKw},
    {"false",   TokenKind::FalseKw},
    {"i8",      TokenKind::I8Kw},
    {"i16",     TokenKind::I16Kw},
    {"i32",     TokenKind::I32Kw},
    {"i64",     TokenKind::I64Kw},
    {"u8",      TokenKind::U8Kw},
    {"u16",     TokenKind::U16Kw},
    {"u32",     TokenKind::U32Kw},
    {"u64",     TokenKind::U64Kw},
    {"f32",     TokenKind::F32Kw},
    {"f64",     TokenKind::F64Kw},
    {"bool",    TokenKind::BoolKw},
    {"str",     TokenKind::StrKw},
};

[[nodiscard]] TokenKind match_keyword(std::string_view s) noexcept {
    for (const auto& kw : kKeywords) {
        if (s == kw.text) return kw.kind;
    }
    return TokenKind::Ident;
}

} // namespace

void Lexer::advance(size_t n) noexcept {
    for (size_t i = 0; i < n; ++i) {
        if (pos_ < src_.size()) {
            if (src_[pos_] == '\n') {
                ++line_;
                col_ = 1;
            } else {
                ++col_;
            }
            ++pos_;
        }
    }
}

void Lexer::skip_ws_and_comments() noexcept {
    while (pos_ < src_.size()) {
        char c = src_[pos_];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
            // line comment: consume to end-of-line or EOF
            while (pos_ < src_.size() && src_[pos_] != '\n') advance();
            continue;
        }
        if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '*') {
            // block comment: consume until */  (no nesting for simplicity)
            advance(2);
            while (pos_ + 1 < src_.size() &&
                   !(src_[pos_] == '*' && src_[pos_ + 1] == '/')) {
                advance();
            }
            if (pos_ + 1 < src_.size()) advance(2);
            continue;
        }
        break;
    }
}

Token Lexer::make_token(TokenKind k, size_t start, size_t start_line, size_t start_col) noexcept {
    Token t;
    t.kind    = k;
    t.file_id = file_id_;
    t.line    = static_cast<uint32_t>(start_line);
    t.col     = static_cast<uint32_t>(start_col);
    t.len     = static_cast<uint32_t>(pos_ - start);
    t.text    = src_.substr(start, pos_ - start);
    return t;
}

bool Lexer::lex_ident_or_kw(std::vector<Token>& out) {
    size_t start = pos_;
    size_t sl = line_, sc = col_;
    while (pos_ < src_.size() &&
           (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
        advance();
    }
    std::string_view text = src_.substr(start, pos_ - start);
    TokenKind k = match_keyword(text);
    Token t = make_token(k, start, sl, sc);
    t.text = text;
    out.push_back(t);
    return true;
}

bool Lexer::lex_number(std::vector<Token>& out) {
    size_t start = pos_;
    size_t sl = line_, sc = col_;
    bool is_float = false;
    bool is_hex = false;
    if (src_[pos_] == '0' && pos_ + 1 < src_.size() &&
        (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
        advance(2);
        is_hex = true;
        while (pos_ < src_.size() && std::isxdigit(static_cast<unsigned char>(src_[pos_]))) advance();
    } else {
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) advance();
        if (pos_ < src_.size() && src_[pos_] == '.' &&
            pos_ + 1 < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_ + 1]))) {
            is_float = true;
            advance(); // dot
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) advance();
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            advance();
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) advance();
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) advance();
        }
    }
    // optional suffix: f32/f64/u32/u64/i32/i64
    // (only consume 3-4 letter suffixes that match a known type suffix)
    (void)is_hex;
    TokenKind k = is_float ? TokenKind::FloatLit : TokenKind::IntLit;
    Token t = make_token(k, start, sl, sc);
    out.push_back(t);
    return true;
}

bool Lexer::lex_string_lit(std::vector<Token>& out) {
    size_t start = pos_;
    size_t sl = line_, sc = col_;
    advance(); // consume opening "
    while (pos_ < src_.size() && src_[pos_] != '"') {
        if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
            advance(2);
        } else {
            advance();
        }
    }
    if (pos_ >= src_.size()) {
        return error(static_cast<uint32_t>(sl), static_cast<uint32_t>(sc), "unterminated string literal");
    }
    advance(); // consume closing "
    Token t = make_token(TokenKind::StrLit, start, sl, sc);
    out.push_back(t);
    return true;
}

bool Lexer::error(uint32_t l, uint32_t c, std::string_view msg) {
    err_line_ = l;
    err_col_ = c;
    err_msg_ = std::string(msg);
    return false;
}

bool Lexer::tokenize(std::vector<Token>& out) {
    while (true) {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            Token eof;
            eof.kind = TokenKind::Eof;
            eof.line = static_cast<uint32_t>(line_);
            eof.col = static_cast<uint32_t>(col_);
            out.push_back(eof);
            return true;
        }
        char c = src_[pos_];
        size_t sl = line_, sc = col_, start = pos_;

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            if (!lex_ident_or_kw(out)) return false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!lex_number(out)) return false;
            continue;
        }
        if (c == '"') {
            if (!lex_string_lit(out)) return false;
            continue;
        }

        // ---- Operators / punctuation ----
        TokenKind k = TokenKind::Eof;
        switch (c) {
            case '+':
                if (peek(1) == '=') { advance(2); k = TokenKind::PlusEq; }
                else { advance(); k = TokenKind::Plus; }
                break;
            case '-':
                if (peek(1) == '>') { advance(2); k = TokenKind::Arrow; }
                else if (peek(1) == '=') { advance(2); k = TokenKind::MinusEq; }
                else { advance(); k = TokenKind::Minus; }
                break;
            case '*':
                if (peek(1) == '=') { advance(2); k = TokenKind::StarEq; }
                else { advance(); k = TokenKind::Star; }
                break;
            case '/':
                advance(); k = TokenKind::Slash; break;
            case '%': advance(); k = TokenKind::Percent; break;
            case '~': advance(); k = TokenKind::Tilde; break;
            case '!':
                if (peek(1) == '=') { advance(2); k = TokenKind::BangEq; }
                else { advance(); k = TokenKind::Bang; }
                break;
            case '=':
                if (peek(1) == '=') { advance(2); k = TokenKind::EqEq; }
                else if (peek(1) == '>') { advance(2); k = TokenKind::FatArrow; }
                else { advance(); k = TokenKind::Eq; }
                break;
            case '<':
                if (peek(1) == '<') { advance(2); k = TokenKind::Shl; }
                else if (peek(1) == '=') { advance(2); k = TokenKind::LtEq; }
                else { advance(); k = TokenKind::Lt; }
                break;
            case '>':
                if (peek(1) == '>') { advance(2); k = TokenKind::Shr; }
                else if (peek(1) == '=') { advance(2); k = TokenKind::GtEq; }
                else { advance(); k = TokenKind::Gt; }
                break;
            case '&':
                if (peek(1) == '&') { advance(2); k = TokenKind::AndAnd; }
                else { advance(); k = TokenKind::Amp; }
                // NOTE: a future borrow syntax (`&expr`, `&mut expr`)
                // will need distinct lexing (e.g. contextual keywords)
                // — mapping single `&` to Ref made the documented
                // bitwise-AND operator unreachable (root-cause fix per
                // Rule 68; Ref/RefMut stay reserved in the enum).
                break;
            case '|':
                if (peek(1) == '|') { advance(2); k = TokenKind::OrOr; }
                else { advance(); k = TokenKind::Pipe; }
                break;
            case '^': advance(); k = TokenKind::Caret; break;
            case '(': advance(); k = TokenKind::LParen; break;
            case ')': advance(); k = TokenKind::RParen; break;
            case '{': advance(); k = TokenKind::LBrace; break;
            case '}': advance(); k = TokenKind::RBrace; break;
            case '[': advance(); k = TokenKind::LBracket; break;
            case ']': advance(); k = TokenKind::RBracket; break;
            case ',': advance(); k = TokenKind::Comma; break;
            case ';': advance(); k = TokenKind::Semicolon; break;
            case ':':
                if (peek(1) == ':') { advance(2); k = TokenKind::DoubleColon; }
                else { advance(); k = TokenKind::Colon; }
                break;
            case '.':
                if (peek(1) == '.') { advance(2); k = TokenKind::DotDot; }
                else { advance(); k = TokenKind::Dot; }
                break;
            case '@': advance(); k = TokenKind::At; break;
            case '?': advance(); k = TokenKind::Question; break;
            default:
                return error(static_cast<uint32_t>(sl), static_cast<uint32_t>(sc), "unexpected character");
        }
        if (k != TokenKind::Eof) {
            Token t = make_token(k, start, sl, sc);
            out.push_back(t);
        }
    }
}

} // namespace aegis
