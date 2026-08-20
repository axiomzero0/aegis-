// ============================================================
// frontend/Lexer.h — Lexer for Aegis source.
// ============================================================
// The lexer reads Aegis source bytes and produces a stream of Tokens.
// Aegis syntax (Section 1 of the spec):
//
//   let immutable_value = 10;
//   var mutable_value = 20;
//   let heap_buf = alloc System [u8; 1024];
//   let arena_buf = alloc my_arena [Vertex; 100];
//   let stack_buf = stack [u8; 64];
//   fn add(a: i32, b: i32) -> i32 { return a + b; }
//   fn read_file(path: str) -> Result<File, IoError> { ... }
//   enum NetworkEvent { Connected(Socket), ... }
//   match event { .Connected(sock) => start_ping(sock), ... }
//   struct Vec3 { x: f32, y: f32, z: f32, }
//
// The lexer runs on the cold frontend path (parser) and may use
// std::string / std::vector. It does not appear in the hot path.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aegis/support/Primitives.hpp"

namespace aegis {

enum class TokenKind : uint8_t {
    Eof,

    // ---- Literals ----
    Ident,
    IntLit,        // 123
    FloatLit,      // 1.5
    StrLit,        // "..."
    TrueKw,        // true
    FalseKw,       // false

    // ---- Keywords ----
    LetKw, VarKw, FnKw, ReturnKw,
    IfKw, ElseKw, MatchKw,
    StructKw, EnumKw,
    AllocKw, StackKw,   // alloc / stack
    ForKw, InKw,
    PubKw, MutKw,
    // Type keywords (used as primitives in type position)
    I8Kw, I16Kw, I32Kw, I64Kw, U8Kw, U16Kw, U32Kw, U64Kw,
    F32Kw, F64Kw, BoolKw, StrKw,

    // ---- Operators / punctuation ----
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Tilde, Bang,
    Shl, Shr,
    EqEq, BangEq, LtEq, GtEq, Lt, Gt,
    AndAnd, OrOr,
    Eq, PlusEq, MinusEq, StarEq, SlashEq,
    Arrow, FatArrow,           // -> =>
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Colon, DoubleColon, Dot, At,
    Question,                  // ? error-propagation

    // ---- Reference operators ----
    RefMut,                    // &mut
    Ref,                       // &
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    SymbolId  file_id{kInvalidSymbolId};
    uint32_t  line{0};
    uint32_t  col{0};
    uint32_t  len{0};

    // For literals/idents: interned SymbolId into the lexer's SymbolTable.
    // For IntLit/FloatLit, the raw bytes are also kept as a string_view
    // into the source buffer.
    SymbolId  text_id{kInvalidSymbolId};
    std::string_view text{};
};

class Lexer {
public:
    Lexer(std::string_view src, SymbolId file_id, class SymbolTable* syms)
        : src_(src), file_id_(file_id), syms_(syms) {}

    // Tokenize the whole source. Returns false on a lexical error.
    bool tokenize(std::vector<Token>& out_tokens);

    [[nodiscard]] uint32_t error_line()   const noexcept { return err_line_; }
    [[nodiscard]] uint32_t error_col()    const noexcept { return err_col_; }
    [[nodiscard]] std::string_view error_message() const noexcept { return err_msg_; }

private:
    std::string_view src_;
    SymbolId file_id_;
    SymbolTable* syms_;

    size_t pos_{0};
    size_t line_{1};
    size_t col_{1};

    uint32_t err_line_{0};
    uint32_t err_col_{0};
    std::string err_msg_;

    [[nodiscard]] char peek(size_t off = 0) const noexcept {
        return (pos_ + off < src_.size()) ? src_[pos_ + off] : '\0';
    }
    void advance(size_t n = 1) noexcept;
    void skip_ws_and_comments() noexcept;

    Token make_token(TokenKind k, size_t start, size_t start_line, size_t start_col) noexcept;
    bool lex_number(std::vector<Token>& out);
    bool lex_ident_or_kw(std::vector<Token>& out);
    bool lex_string_lit(std::vector<Token>& out);
    bool error(uint32_t l, uint32_t c, std::string_view msg);
};

} // namespace aegis
