// frontend/Parser.cpp — recursive-descent + Pratt parser for Aegis.
#include "aegis/frontend/Parser.hpp"

#include <utility>

namespace aegis {

namespace {
TokenKind binary_op_kind(TokenKind k) noexcept {
    switch (k) {
        case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star:
        case TokenKind::Slash: case TokenKind::Percent:
        case TokenKind::Amp: case TokenKind::Pipe: case TokenKind::Caret:
        case TokenKind::Shl: case TokenKind::Shr:
        case TokenKind::EqEq: case TokenKind::BangEq:
        case TokenKind::Lt: case TokenKind::LtEq:
        case TokenKind::Gt: case TokenKind::GtEq:
        case TokenKind::AndAnd: case TokenKind::OrOr:
        // Assignment operators MUST be recognized here so the Pratt
        // loop can dispatch them to ASTAssignExpr below. Omitting them
        // made every assignment statement (`x = 5;`) a parse error —
        // the ASTAssignExpr / Lowering support was unreachable dead
        // code (root-cause fix; Rule 68).
        case TokenKind::Eq: case TokenKind::PlusEq:
        case TokenKind::MinusEq: case TokenKind::StarEq:
        case TokenKind::SlashEq:
            return k;
        default: return TokenKind::Eof;
    }
}
}

int Parser::precedence(TokenKind k) noexcept {
    switch (k) {
        case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent:
            return 13;
        case TokenKind::Plus: case TokenKind::Minus:
            return 12;
        case TokenKind::Shl: case TokenKind::Shr:
            return 11;
        case TokenKind::Amp: case TokenKind::Caret: case TokenKind::Pipe:
            return 10;
        case TokenKind::EqEq: case TokenKind::BangEq:
        case TokenKind::Lt: case TokenKind::LtEq:
        case TokenKind::Gt: case TokenKind::GtEq:
            return 9;
        case TokenKind::AndAnd:
            return 5;
        case TokenKind::OrOr:
            return 4;
        case TokenKind::Eq: case TokenKind::PlusEq: case TokenKind::MinusEq:
        case TokenKind::StarEq: case TokenKind::SlashEq:
            return 2;
        default:
            return -1;
    }
}

Error Parser::make_err(const Token& t, std::string_view what) {
    // Note: we can't intern message text into a 32-bit message_id without a
    // proper message table; for now we encode the error category and use
    // the message_id slot as a small constant id.
    (void)what;
    Error e = Error::parse(0x100 /* generic parse error */,
                          Span{t.file_id, t.line, t.col, t.len});
    sink_->report(e);
    return e;
}

const Token& Parser::expect(TokenKind k, const char* what) {
    if (at(k)) return toks_[pos_++];
    // Treat as a parse error and report.
    Error e = make_err(peek(), what);
    (void)e;
    return toks_[std::min(pos_, toks_.size() - 1)];
}

Expected<std::unique_ptr<ASTModule>> Parser::parse_module() {
    auto mod = std::make_unique<ASTModule>();
    while (!at(TokenKind::Eof)) {
        auto r = parse_top_level_item();
        if (!r.has_value()) [[unlikely]] {
            return std::unexpected(r.error());
        }
        mod->items.push_back(std::move(*r));
    }
    return mod;
}

Expected<ASTPtr> Parser::parse_top_level_item() {
    bool is_pub = consume(TokenKind::PubKw);
    if (at(TokenKind::FnKw))   return parse_fn_decl(is_pub);
    if (is_pub) return std::unexpected(make_err(peek(), "expected fn after pub"));
    if (at(TokenKind::StructKw)) return parse_struct_decl();
    if (at(TokenKind::EnumKw))   return parse_enum_decl();
    return std::unexpected(make_err(peek(), "expected top-level item"));
}

Expected<ASTPtr> Parser::parse_fn_decl(bool is_pub) {
    expect(TokenKind::FnKw, "fn");
    const Token& name_tok = expect(TokenKind::Ident, "function name");
    auto fn = std::make_unique<ASTFnDecl>();
    fn->name = syms_->intern(name_tok.text);
    fn->is_pub = is_pub;
    expect(TokenKind::LParen, "(");
    while (!at(TokenKind::RParen) && !at(TokenKind::Eof)) {
        auto param = std::make_unique<ASTParam>();
        if (consume(TokenKind::MutKw)) param->mutable_ = true;
        const Token& pname = expect(TokenKind::Ident, "param name");
        param->name = syms_->intern(pname.text);
        expect(TokenKind::Colon, ":");
        auto t = parse_type_annotation();
        if (!t.has_value()) return std::unexpected(t.error());
        param->type_ann = std::move(*t);
        fn->params.push_back(std::move(param));
        if (!consume(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, ")");
    if (consume(TokenKind::Arrow)) {
        auto t = parse_type_annotation();
        if (!t.has_value()) return std::unexpected(t.error());
        fn->return_type = std::move(*t);
    }
    auto body = parse_block();
    if (!body.has_value()) return std::unexpected(body.error());
    fn->body = std::move(*body);
    return fn;
}

Expected<ASTPtr> Parser::parse_struct_decl() {
    expect(TokenKind::StructKw, "struct");
    const Token& name = expect(TokenKind::Ident, "struct name");
    auto s = std::make_unique<ASTStructDecl>();
    s->name = syms_->intern(name.text);
    expect(TokenKind::LBrace, "{");
    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
        const Token& field = expect(TokenKind::Ident, "field name");
        expect(TokenKind::Colon, ":");
        auto ty = parse_type_annotation();
        if (!ty.has_value()) return std::unexpected(ty.error());
        s->fields.emplace_back(syms_->intern(field.text), std::move(*ty));
        if (!consume(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return s;
}

Expected<ASTPtr> Parser::parse_enum_decl() {
    expect(TokenKind::EnumKw, "enum");
    const Token& name = expect(TokenKind::Ident, "enum name");
    auto e = std::make_unique<ASTEnumDecl>();
    e->name = syms_->intern(name.text);
    expect(TokenKind::LBrace, "{");
    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
        const Token& v = expect(TokenKind::Ident, "variant name");
        ASTEnumDecl::Variant var;
        var.name = syms_->intern(v.text);
        if (consume(TokenKind::LParen)) {
            while (!at(TokenKind::RParen) && !at(TokenKind::Eof)) {
                auto ty = parse_type_annotation();
                if (!ty.has_value()) return std::unexpected(ty.error());
                var.payload_types.push_back(std::move(*ty));
                if (!consume(TokenKind::Comma)) break;
            }
            expect(TokenKind::RParen, ")");
        }
        e->variants.push_back(std::move(var));
        if (!consume(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return e;
}

Expected<ASTPtr> Parser::parse_block() {
    expect(TokenKind::LBrace, "{");
    auto blk = std::make_unique<ASTBlock>();
    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
        auto s = parse_stmt();
        if (!s.has_value()) return std::unexpected(s.error());
        blk->stmts.push_back(std::move(*s));
    }
    expect(TokenKind::RBrace, "}");
    return blk;
}

Expected<ASTPtr> Parser::parse_stmt() {
    if (at(TokenKind::LetKw)) return parse_let_or_var_stmt(false);
    if (at(TokenKind::VarKw)) return parse_let_or_var_stmt(true);
    if (at(TokenKind::ReturnKw)) return parse_return_stmt();
    if (at(TokenKind::IfKw)) return parse_if_stmt();
    if (at(TokenKind::ForKw)) return parse_for_stmt();
    if (at(TokenKind::MatchKw)) return parse_match_stmt();
    if (at(TokenKind::LBrace)) return parse_block();

    // Expression statement (optional trailing semicolon).
    auto e = parse_expr();
    if (!e.has_value()) return std::unexpected(e.error());
    // An assignment IS a statement (the Lowerer's lower_stmt handles
    // ASTAssignExpr directly). Wrapping it in an ExprStmt would route
    // it through lower_expr's fallback and silently drop the store —
    // return it un-wrapped so the binding takes effect.
    if ((*e)->kind == ASTKind::AssignExpr) {
        consume(TokenKind::Semicolon);
        return e;
    }
    if (consume(TokenKind::Semicolon)) {
        auto es = std::make_unique<ASTExprStmt>();
        es->expr = std::move(*e);
        return es;
    }
    return Expected<ASTPtr>(std::move(*e));
}

Expected<ASTPtr> Parser::parse_let_or_var_stmt(bool is_var) {
    expect(is_var ? TokenKind::VarKw : TokenKind::LetKw, is_var ? "var" : "let");
    const Token& name = expect(TokenKind::Ident, "binding name");
    auto stmt = std::make_unique<ASTLetStmt>();
    stmt->is_var = is_var;
    stmt->name = syms_->intern(name.text);
    if (consume(TokenKind::Colon)) {
        auto ty = parse_type_annotation();
        if (!ty.has_value()) return std::unexpected(ty.error());
        stmt->type_ann = std::move(*ty);
    }
    if (consume(TokenKind::Eq)) {
        auto e = parse_expr();
        if (!e.has_value()) return std::unexpected(e.error());
        stmt->init = std::move(*e);
    }
    consume(TokenKind::Semicolon);
    return stmt;
}

Expected<ASTPtr> Parser::parse_return_stmt() {
    expect(TokenKind::ReturnKw, "return");
    auto s = std::make_unique<ASTReturnStmt>();
    if (!at(TokenKind::Semicolon) && !at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
        auto e = parse_expr();
        if (!e.has_value()) return std::unexpected(e.error());
        s->value = std::move(*e);
    }
    consume(TokenKind::Semicolon);
    return s;
}

Expected<ASTPtr> Parser::parse_if_stmt() {
    expect(TokenKind::IfKw, "if");
    auto cond = parse_expr();
    if (!cond.has_value()) return std::unexpected(cond.error());
    auto then_blk = parse_block();
    if (!then_blk.has_value()) return std::unexpected(then_blk.error());
    auto s = std::make_unique<ASTIfStmt>();
    s->cond = std::move(*cond);
    s->then_branch = std::move(*then_blk);
    if (consume(TokenKind::ElseKw)) {
        auto else_blk = parse_block();
        if (!else_blk.has_value()) return std::unexpected(else_blk.error());
        s->else_branch = std::move(*else_blk);
    }
    return s;
}

Expected<ASTPtr> Parser::parse_for_stmt() {
    expect(TokenKind::ForKw, "for");
    const Token& var = expect(TokenKind::Ident, "for variable");
    expect(TokenKind::InKw, "in");
    // The supported iteration space is `lo..hi` (integral, step 1).
    // Parse the lower bound, require the range operator, parse the
    // upper bound — anything else is a loud parse error, never a
    // silently-misinterpreted expression (Rule D.3).
    auto lo = parse_expr();
    if (!lo.has_value()) return std::unexpected(lo.error());
    expect(TokenKind::DotDot, "lo..hi range");
    auto hi = parse_expr();
    if (!hi.has_value()) return std::unexpected(hi.error());
    auto range = std::make_unique<ASTRangeExpr>();
    range->lo = std::move(*lo);
    range->hi = std::move(*hi);
    auto body = parse_block();
    if (!body.has_value()) return std::unexpected(body.error());
    auto s = std::make_unique<ASTForStmt>();
    s->var_name = syms_->intern(var.text);
    s->iter = std::move(range);
    s->body = std::move(*body);
    return s;
}

Expected<ASTPtr> Parser::parse_match_stmt() {
    expect(TokenKind::MatchKw, "match");
    auto scrut = parse_expr();
    if (!scrut.has_value()) return std::unexpected(scrut.error());
    expect(TokenKind::LBrace, "{");
    auto s = std::make_unique<ASTMatchStmt>();
    s->scrutinee = std::move(*scrut);
    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
        ASTMatchStmt::Arm arm;
        auto p = parse_pattern();
        if (!p.has_value()) return std::unexpected(p.error());
        arm.pattern = std::move(*p);
        expect(TokenKind::FatArrow, "=>");
        auto body = parse_expr();
        if (!body.has_value()) return std::unexpected(body.error());
        arm.body = std::move(*body);
        consume(TokenKind::Comma);
        s->arms.push_back(std::move(arm));
    }
    expect(TokenKind::RBrace, "}");
    return s;
}

Expected<ASTPtr> Parser::parse_pattern() {
    // .Member(sub) | Path(...) | ident | _
    if (consume(TokenKind::Dot)) {
        const Token& m = expect(TokenKind::Ident, "member pattern name");
        auto p = std::make_unique<ASTMemberPat>();
        p->member = syms_->intern(m.text);
        if (consume(TokenKind::LParen)) {
            auto sub = parse_pattern();
            if (!sub.has_value()) return std::unexpected(sub.error());
            p->sub_pattern = std::move(*sub);
            expect(TokenKind::RParen, ")");
        }
        return p;
    }
    if (at(TokenKind::Ident)) {
        const Token& t = peek();
        ++pos_;
        // Could be ident or path.
        if (consume(TokenKind::DoubleColon)) {
            auto p = std::make_unique<ASTPathPat>();
            p->segments.push_back(syms_->intern(t.text));
            while (true) {
                const Token& seg = expect(TokenKind::Ident, "path segment");
                p->segments.push_back(syms_->intern(seg.text));
                if (!consume(TokenKind::DoubleColon)) break;
            }
            if (consume(TokenKind::LParen)) {
                auto sub = parse_pattern();
                if (!sub.has_value()) return std::unexpected(sub.error());
                p->sub_pattern = std::move(*sub);
                expect(TokenKind::RParen, ")");
            }
            return p;
        }
        auto p = std::make_unique<ASTIdentPat>();
        p->name = syms_->intern(t.text);
        return p;
    }
    return std::unexpected(make_err(peek(), "expected pattern"));
}

Expected<ASTPtr> Parser::parse_type_annotation() {
    // For now, type annotations use the same expression grammar.
    return parse_expr();
}

// Pratt expression parser.
Expected<ASTPtr> Parser::parse_expr(int min_prec) {
    auto lhs = parse_unary();
    if (!lhs.has_value()) return std::unexpected(lhs.error());
    while (true) {
        TokenKind op = binary_op_kind(peek().kind);
        if (op == TokenKind::Eof) break;
        int prec = precedence(op);
        if (prec < min_prec) break;
        ++pos_;
        // Assignments are right-associative: parse the RHS at the SAME
        // precedence (not prec+1) so `a = b = c` nests as a = (b = c).
        int rhs_prec = (op == TokenKind::Eq || op == TokenKind::PlusEq ||
                        op == TokenKind::MinusEq || op == TokenKind::StarEq ||
                        op == TokenKind::SlashEq) ? prec : prec + 1;
        auto rhs = parse_expr(rhs_prec);
        if (!rhs.has_value()) return std::unexpected(rhs.error());

        if (op == TokenKind::Eq || op == TokenKind::PlusEq || op == TokenKind::MinusEq ||
            op == TokenKind::StarEq || op == TokenKind::SlashEq) {
            auto a = std::make_unique<ASTAssignExpr>();
            a->op = op;
            a->target = std::move(*lhs);
            a->value  = std::move(*rhs);
            lhs = std::move(a);
        } else {
            auto b = std::make_unique<ASTBinaryExpr>(op);
            b->lhs = std::move(*lhs);
            b->rhs = std::move(*rhs);
            lhs = std::move(b);
        }
    }
    return lhs;
}

Expected<ASTPtr> Parser::parse_unary() {
    TokenKind op = peek().kind;
    if (op == TokenKind::Minus || op == TokenKind::Bang || op == TokenKind::Tilde) {
        ++pos_;
        auto sub = parse_unary();
        if (!sub.has_value()) return std::unexpected(sub.error());
        auto u = std::make_unique<ASTUnaryExpr>(op);
        u->operand = std::move(*sub);
        return u;
    }
    auto prim = parse_primary();
    if (!prim.has_value()) return std::unexpected(prim.error());
    return parse_postfix(std::move(*prim));
}

Expected<ASTPtr> Parser::parse_primary() {
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::IntLit: {
            ++pos_;
            auto lit = std::make_unique<ASTIntLit>();
            // Naive parse: just decimal digits.
            uint64_t v = 0;
            for (char c : t.text) { if (c >= '0' && c <= '9') v = v * 10 + (c - '0'); }
            lit->value = v;
            return lit;
        }
        case TokenKind::FloatLit: {
            ++pos_;
            auto lit = std::make_unique<ASTFloatLit>();
            double v = 0;
            double div = 1;
            bool after_dot = false;
            for (char c : t.text) {
                if (c == '.') { after_dot = true; continue; }
                if (c >= '0' && c <= '9') {
                    if (after_dot) { div *= 10; v += (c - '0') / div; }
                    else            { v = v * 10 + (c - '0'); }
                }
            }
            lit->value = v;
            return lit;
        }
        case TokenKind::StrLit: {
            ++pos_;
            auto lit = std::make_unique<ASTStrLit>();
            lit->value = syms_->intern(t.text);
            return lit;
        }
        case TokenKind::TrueKw:  { ++pos_; auto lit = std::make_unique<ASTBoolLit>(); lit->value = true;  return lit; }
        case TokenKind::FalseKw: { ++pos_; auto lit = std::make_unique<ASTBoolLit>(); lit->value = false; return lit; }
        case TokenKind::Ident: {
            ++pos_;
            if (consume(TokenKind::DoubleColon)) {
                auto p = std::make_unique<ASTPathExpr>();
                p->segments.push_back(syms_->intern(t.text));
                while (true) {
                    const Token& seg = expect(TokenKind::Ident, "path segment");
                    p->segments.push_back(syms_->intern(seg.text));
                    if (!consume(TokenKind::DoubleColon)) break;
                }
                return p;
            }
            auto i = std::make_unique<ASTIdent>();
            i->name = syms_->intern(t.text);
            return i;
        }
        // Type keywords in expression position (used as type annotations).
        // Treat them as plain identifiers.
        case TokenKind::I8Kw:  case TokenKind::I16Kw:
        case TokenKind::I32Kw: case TokenKind::I64Kw:
        case TokenKind::U8Kw:  case TokenKind::U16Kw:
        case TokenKind::U32Kw: case TokenKind::U64Kw:
        case TokenKind::F32Kw: case TokenKind::F64Kw:
        case TokenKind::BoolKw: case TokenKind::StrKw: {
            ++pos_;
            auto i = std::make_unique<ASTIdent>();
            i->name = syms_->intern(t.text);
            return i;
        }
        case TokenKind::LParen: {
            ++pos_;
            auto e = parse_expr();
            if (!e.has_value()) return std::unexpected(e.error());
            (void)expect(TokenKind::RParen, ")");
            return Expected<ASTPtr>(std::move(*e));
        }
        case TokenKind::LBrace: return parse_block();
        default:
            return std::unexpected(make_err(t, "unexpected token in expression"));
    }
}

Expected<ASTPtr> Parser::parse_postfix(ASTPtr base) {
    while (true) {
        const Token& t = peek();
        if (t.kind == TokenKind::LParen) {
            ++pos_;
            auto call = std::make_unique<ASTCallExpr>();
            call->callee = std::move(base);
            while (!at(TokenKind::RParen) && !at(TokenKind::Eof)) {
                auto arg = parse_expr();
                if (!arg.has_value()) return std::unexpected(arg.error());
                call->args.push_back(std::move(*arg));
                if (!consume(TokenKind::Comma)) break;
            }
            expect(TokenKind::RParen, ")");
            base = std::move(call);
        } else if (t.kind == TokenKind::Dot) {
            ++pos_;
            const Token& field = expect(TokenKind::Ident, "field name");
            auto f = std::make_unique<ASTFieldExpr>();
            f->base = std::move(base);
            f->field = syms_->intern(field.text);
            base = std::move(f);
        } else if (t.kind == TokenKind::LBracket) {
            ++pos_;
            auto idx = parse_expr();
            if (!idx.has_value()) return std::unexpected(idx.error());
            expect(TokenKind::RBracket, "]");
            auto i = std::make_unique<ASTIndexExpr>();
            i->base = std::move(base);
            i->index = std::move(*idx);
            base = std::move(i);
        } else {
            break;
        }
    }
    return base;
}

} // namespace aegis
