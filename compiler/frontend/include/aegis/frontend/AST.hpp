// ============================================================
// frontend/AST.h — Aegis Abstract Syntax Tree.
// ============================================================
// AST node types live in a tagged union for cache-friendly traversal.
// All AST nodes carry a source Span for diagnostics. The AST itself is
// NOT on the hot path — it lives in the frontend only.
// ============================================================
#pragma once

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "aegis/support/Diagnostics.hpp"  // for Span
#include "aegis/support/Primitives.hpp"
#include "aegis/support/StringIntern.hpp"
#include "aegis/frontend/Lexer.hpp"

namespace aegis {

enum class ASTKind : uint8_t {
    Module,
    FnDecl,
    Param,
    StructDecl,
    StructField,
    EnumDecl,
    EnumVariant,
    LetStmt,
    VarStmt,
    ExprStmt,
    ReturnStmt,
    IfStmt,
    Block,
    AssignExpr,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    FieldExpr,
    IndexExpr,
    Ident,
    IntLit,
    FloatLit,
    StrLit,
    BoolLit,
    MatchStmt,
    MatchArm,
    TuplePat,
    IdentPat,
    ForStmt,
    RangeExpr,
    PathPat,
    PathExpr,
    MemberPat,
};

struct ASTNode;
using ASTPtr = std::unique_ptr<ASTNode>;
struct ASTModule;
struct ASTFnDecl;
struct ASTExpr;
struct ASTStmt;

// Reuse common::Span (defined in Diagnostics.h).
// struct Span is in common/Diagnostics.h.

struct ASTNode {
    ASTKind  kind;
    Span     span{};
    virtual ~ASTNode() = default;
protected:
    ASTNode(ASTKind k) : kind(k) {}
};

// ---- Module ----
struct ASTModule : ASTNode {
    std::vector<ASTPtr> items;
    ASTModule() : ASTNode(ASTKind::Module) {}
};

// ---- Declarations ----
struct ASTParam : ASTNode {
    SymbolId name;
    ASTPtr   type_ann; // optional ASTExpr (type)
    bool     mutable_{false};
    ASTParam() : ASTNode(ASTKind::Param) {}
};

struct ASTFnDecl : ASTNode {
    SymbolId   name;
    std::vector<ASTPtr> params;
    ASTPtr     return_type; // optional
    ASTPtr     body;         // ASTBlock
    bool       is_pub{false};
    ASTFnDecl() : ASTNode(ASTKind::FnDecl) {}
};

struct ASTStructDecl : ASTNode {
    SymbolId   name;
    std::vector<std::pair<SymbolId, ASTPtr>> fields; // name + type
    ASTStructDecl() : ASTNode(ASTKind::StructDecl) {}
};

struct ASTEnumDecl : ASTNode {
    SymbolId name;
    struct Variant { SymbolId name; std::vector<ASTPtr> payload_types; };
    std::vector<Variant> variants;
    ASTEnumDecl() : ASTNode(ASTKind::EnumDecl) {}
};

// ---- Statements ----
struct ASTStmt : ASTNode {
    ASTStmt(ASTKind k) : ASTNode(k) {}
};

struct ASTBlock : ASTStmt {
    std::vector<ASTPtr> stmts;
    ASTBlock() : ASTStmt(ASTKind::Block) {}
};

struct ASTLetStmt : ASTStmt {
    SymbolId name;
    ASTPtr   type_ann; // optional
    ASTPtr   init;      // optional ASTExpr
    bool     is_var{false};
    ASTLetStmt() : ASTStmt(ASTKind::LetStmt) {}
};

struct ASTExprStmt : ASTStmt {
    ASTPtr expr;
    ASTExprStmt() : ASTStmt(ASTKind::ExprStmt) {}
};

struct ASTReturnStmt : ASTStmt {
    ASTPtr value; // optional
    ASTReturnStmt() : ASTStmt(ASTKind::ReturnStmt) {}
};

struct ASTIfStmt : ASTStmt {
    ASTPtr cond;
    ASTPtr then_branch; // ASTBlock
    ASTPtr else_branch; // optional ASTBlock
    ASTIfStmt() : ASTStmt(ASTKind::IfStmt) {}
};

struct ASTForStmt : ASTStmt {
    ASTPtr iter;         // ASTRangeExpr for the supported `lo..hi` form
    SymbolId var_name;
    ASTPtr   body; // ASTBlock
    ASTForStmt() : ASTStmt(ASTKind::ForStmt) {}
};

struct ASTMatchStmt : ASTStmt {
    ASTPtr scrutinee;
    struct Arm {
        ASTPtr pattern;     // ASTPat
        ASTPtr body;        // ASTExpr or ASTBlock
        Span   span;
    };
    std::vector<Arm> arms;
    ASTMatchStmt() : ASTStmt(ASTKind::MatchStmt) {}
};

// ---- Expressions ----
struct ASTExpr : ASTNode {
    ASTExpr(ASTKind k) : ASTNode(k) {}
};

struct ASTBinaryExpr : ASTExpr {
    TokenKind op;
    ASTPtr lhs, rhs;
    ASTBinaryExpr(TokenKind o) : ASTExpr(ASTKind::BinaryExpr), op(o) {}
};

struct ASTUnaryExpr : ASTExpr {
    TokenKind op;
    ASTPtr operand;
    ASTUnaryExpr(TokenKind o) : ASTExpr(ASTKind::UnaryExpr), op(o) {}
};

struct ASTCallExpr : ASTExpr {
    ASTPtr callee;     // typically ASTIdent or ASTPathExpr
    std::vector<ASTPtr> args;
    ASTCallExpr() : ASTExpr(ASTKind::CallExpr) {}
};

struct ASTFieldExpr : ASTExpr {
    ASTPtr base;
    SymbolId field;
    ASTFieldExpr() : ASTExpr(ASTKind::FieldExpr) {}
};

struct ASTIndexExpr : ASTExpr {
    ASTPtr base, index;
    ASTIndexExpr() : ASTExpr(ASTKind::IndexExpr) {}
};

struct ASTAssignExpr : ASTExpr {
    ASTPtr target;
    ASTPtr value;
    TokenKind op{TokenKind::Eq};
    ASTAssignExpr() : ASTExpr(ASTKind::AssignExpr) {}
};

struct ASTIdent : ASTExpr {
    SymbolId name;
    ASTIdent() : ASTExpr(ASTKind::Ident) {}
};

struct ASTIntLit : ASTExpr {
    uint64_t value;
    bool     is_signed{false};
    ASTIntLit() : ASTExpr(ASTKind::IntLit) {}
};

struct ASTFloatLit : ASTExpr {
    double value;
    ASTFloatLit() : ASTExpr(ASTKind::FloatLit) {}
};

struct ASTStrLit : ASTExpr {
    SymbolId value;
    ASTStrLit() : ASTExpr(ASTKind::StrLit) {}
};

struct ASTBoolLit : ASTExpr {
    bool value;
    ASTBoolLit() : ASTExpr(ASTKind::BoolLit) {}
};

struct ASTPathExpr : ASTExpr {
    std::vector<SymbolId> segments; // e.g. ["std","io","write_stderr"]
    ASTPathExpr() : ASTExpr(ASTKind::PathExpr) {}
};

// Inclusive-lower / exclusive-upper integer range: `lo..hi`
// (iteration space [lo, hi), step 1). Currently produced by the
// `for var in lo..hi { ... }` statement parser.
struct ASTRangeExpr : ASTExpr {
    ASTPtr lo;
    ASTPtr hi;
    ASTRangeExpr() : ASTExpr(ASTKind::RangeExpr) {}
};

// ---- Patterns ----
struct ASTPat : ASTNode { ASTPat(ASTKind k) : ASTNode(k) {} };
struct ASTIdentPat : ASTPat {
    SymbolId name;
    ASTIdentPat() : ASTPat(ASTKind::IdentPat) {}
};
struct ASTPathPat : ASTPat {
    std::vector<SymbolId> segments;
    ASTPtr sub_pattern; // optional, the inner pattern of a variant
    ASTPathPat() : ASTPat(ASTKind::PathPat) {}
};
struct ASTMemberPat : ASTPat {
    SymbolId member;            // e.g. ".Connected"
    ASTPtr   sub_pattern;       // optional inner pattern
    ASTMemberPat() : ASTPat(ASTKind::MemberPat) {}
};

} // namespace aegis
