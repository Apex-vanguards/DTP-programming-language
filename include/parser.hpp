#pragma once
#include "dtp.hpp"
#include "lexer.hpp"
#include <stdexcept>

namespace dtp {

class Parser {
public:
    std::vector<Diagnostic> diagnostics;

    Parser(std::vector<Token> tokens, std::string filename = "<input>")
        : tokens_(std::move(tokens)), pos_(0), filename_(std::move(filename)) {}

    Module parse_module(const std::string& name = "main") {
        Module mod;
        mod.name = name;
        while (!at_end()) {
            try {
                mod.items.push_back(parse_top_level());
            } catch (std::exception& e) {
                diagnostics.push_back({ErrLevel::Error, e.what(),
                    cur().line, cur().col, filename_});
                sync(); // error recovery
            }
        }
        return mod;
    }

private:
    std::vector<Token> tokens_;
    size_t pos_;
    std::string filename_;

    Token& cur()  { return tokens_[pos_]; }
    Token& peek(int n=1) {
        size_t p = pos_+n;
        return p < tokens_.size() ? tokens_[p] : tokens_.back();
    }
    bool at_end() { return cur().type == TK::EOF_TOK; }

    Token consume() { return tokens_[pos_++]; }

    Token expect(TK t, const std::string& msg="") {
        if (cur().type != t) {
            throw std::runtime_error(
                "Parse error at line " + std::to_string(cur().line) +
                ":" + std::to_string(cur().col) +
                " — expected " + (msg.empty() ? "token" : msg) +
                " but got '" + cur().value + "'");
        }
        return consume();
    }

    bool check(TK t) { return cur().type == t; }
    bool match(TK t) { if(check(t)){consume();return true;} return false; }
    bool match(std::initializer_list<TK> ts) {
        for(auto t:ts) if(check(t)){consume();return true;}
        return false;
    }

    void sync() {
        // Skip to next statement boundary
        while (!at_end()) {
            if (cur().type == TK::SEMICOLON) { consume(); return; }
            if (cur().type == TK::RBRACE) return;
            if (cur().type == TK::KW_fn || cur().type == TK::KW_struct ||
                cur().type == TK::KW_let) return;
            consume();
        }
    }

    // ── Top Level ──────────────────────────────────────────
    NodePtr parse_top_level() {
        bool is_pub    = match(TK::KW_pub);
        bool is_ext    = match(TK::KW_ext);
        bool is_inline = match(TK::KW_inline);
        bool is_comptime = match(TK::KW_comptime);

        if (check(TK::KW_fn))     return parse_fn(is_pub, is_ext, is_inline, is_comptime);
        if (check(TK::KW_struct)) return parse_struct(is_pub);
        if (check(TK::KW_enum))   return parse_enum(is_pub);
        if (check(TK::KW_impl))   return parse_impl();
        if (check(TK::KW_use))    return parse_use();

        throw std::runtime_error(
            std::string("Unexpected token at top level: '") + cur().value + "'");
    }

    // ── Function ───────────────────────────────────────────
    NodePtr parse_fn(bool pub, bool ext, bool inl, bool ctime) {
        auto node = std::make_unique<FnDecl>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_fn, "fn");
        node->name = expect(TK::IDENT, "function name").value;
        node->is_pub = pub; node->is_ext = ext;
        node->is_inline = inl; node->is_comptime = ctime;

        expect(TK::LPAREN, "(");
        while (!check(TK::RPAREN) && !at_end()) {
            Param p;
            p.is_mut = match(TK::KW_mut);
            p.name = expect(TK::IDENT, "param name").value;
            expect(TK::COLON, ":");
            p.type = parse_type();
            if (match(TK::ASSIGN)) p.default_val = parse_expr();
            node->params.push_back(std::move(p));
            if (!match(TK::COMMA)) break;
        }
        expect(TK::RPAREN, ")");

        if (match(TK::ARROW)) {
            node->ret_type = parse_type();
        } else {
            node->ret_type = std::make_shared<TypeInfo>();
            node->ret_type->base = BaseType::Void;
        }

        if (ext || match(TK::SEMICOLON)) {
            // extern / forward declaration
        } else {
            node->body = parse_block();
        }
        return node;
    }

    // ── Struct ─────────────────────────────────────────────
    NodePtr parse_struct(bool pub) {
        auto node = std::make_unique<StructDecl>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_struct, "struct");
        node->name = expect(TK::IDENT, "struct name").value;
        node->is_pub = pub;
        expect(TK::LBRACE, "{");
        while (!check(TK::RBRACE) && !at_end()) {
            StructField f;
            f.is_pub = match(TK::KW_pub);
            f.name = expect(TK::IDENT, "field name").value;
            expect(TK::COLON, ":");
            f.type = parse_type();
            if (match(TK::ASSIGN)) f.default_val = parse_expr();
            match(TK::COMMA); // optional trailing comma
            node->fields.push_back(std::move(f));
        }
        expect(TK::RBRACE, "}");
        return node;
    }

    // ── Enum ───────────────────────────────────────────────
    NodePtr parse_enum(bool pub) {
        auto node = std::make_unique<EnumDecl>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_enum, "enum");
        node->name = expect(TK::IDENT, "enum name").value;
        node->is_pub = pub;
        expect(TK::LBRACE, "{");
        int64_t disc = 0;
        while (!check(TK::RBRACE) && !at_end()) {
            EnumVariant v;
            v.name = expect(TK::IDENT, "variant name").value;
            if (match(TK::LPAREN)) {
                while (!check(TK::RPAREN) && !at_end()) {
                    v.payload.push_back(parse_type());
                    if (!match(TK::COMMA)) break;
                }
                expect(TK::RPAREN, ")");
            }
            if (match(TK::ASSIGN)) {
                auto e = parse_expr();
                if (auto* li = dynamic_cast<LitIntExpr*>(e.get()))
                    disc = li->value;
            }
            v.discriminant = disc++;
            node->variants.push_back(std::move(v));
            if (!match(TK::COMMA)) break;
        }
        expect(TK::RBRACE, "}");
        return node;
    }

    // ── Impl ───────────────────────────────────────────────
    NodePtr parse_impl() {
        auto node = std::make_unique<ImplBlock>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_impl, "impl");
        node->type_name = expect(TK::IDENT, "type name").value;
        if (match(TK::COLON)) {
            node->trait_name = expect(TK::IDENT, "trait name").value;
        }
        expect(TK::LBRACE, "{");
        while (!check(TK::RBRACE) && !at_end()) {
            bool pub = match(TK::KW_pub);
            bool inl = match(TK::KW_inline);
            bool ctime = match(TK::KW_comptime);
            node->methods.push_back(parse_fn(pub,false,inl,ctime));
        }
        expect(TK::RBRACE, "}");
        return node;
    }

    // ── Use ────────────────────────────────────────────────
    NodePtr parse_use() {
        auto node = std::make_unique<UseDecl>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_use, "use");
        node->path.push_back(expect(TK::IDENT, "module path").value);
        while (match(TK::DCOLON)) {
            node->path.push_back(expect(TK::IDENT, "path segment").value);
        }
        if (match(TK::KW_as)) {
            node->alias = expect(TK::IDENT, "alias").value;
        }
        match(TK::SEMICOLON);
        return node;
    }

    // ── Type parsing ───────────────────────────────────────
    TypeRef parse_type() {
        auto ty = std::make_shared<TypeInfo>();

        // Pointer: *T or *mut T
        if (match(TK::STAR)) {
            ty->base = BaseType::Ptr;
            ty->is_ptr = true;
            ty->ptr_depth = 1;
            if (match(TK::KW_mut)) ty->is_mut = true;
            ty->inner = parse_type();
            return ty;
        }

        // Slice/Array [T] or [T:N]
        if (match(TK::LBRACKET)) {
            ty->inner = parse_type();
            if (match(TK::COLON)) {
                auto sz = parse_expr();
                if (auto* li = dynamic_cast<LitIntExpr*>(sz.get()))
                    ty->array_len = li->value;
                ty->base = BaseType::Array;
            } else {
                ty->base = BaseType::Slice;
                ty->array_len = -1;
            }
            expect(TK::RBRACKET, "]");
            return ty;
        }

        // Optional: ?T
        if (match(TK::QUESTION)) {
            ty->is_nullable = true;
            auto inner = parse_type();
            *ty = *inner;
            ty->is_nullable = true;
            return ty;
        }

        // Primitive or named
        switch(cur().type) {
            case TK::TY_i8:   ty->base=BaseType::I8;   consume(); break;
            case TK::TY_i16:  ty->base=BaseType::I16;  consume(); break;
            case TK::TY_i32:  ty->base=BaseType::I32;  consume(); break;
            case TK::TY_i64:  ty->base=BaseType::I64;  consume(); break;
            case TK::TY_u8:   ty->base=BaseType::U8;   consume(); break;
            case TK::TY_u16:  ty->base=BaseType::U16;  consume(); break;
            case TK::TY_u32:  ty->base=BaseType::U32;  consume(); break;
            case TK::TY_u64:  ty->base=BaseType::U64;  consume(); break;
            case TK::TY_f32:  ty->base=BaseType::F32;  consume(); break;
            case TK::TY_f64:  ty->base=BaseType::F64;  consume(); break;
            case TK::TY_bool: ty->base=BaseType::Bool; consume(); break;
            case TK::TY_char: ty->base=BaseType::Char; consume(); break;
            case TK::TY_void: ty->base=BaseType::Void; consume(); break;
            case TK::TY_str:  ty->base=BaseType::Str;  consume(); break;
            case TK::IDENT:
                ty->base = BaseType::Struct;
                ty->name = consume().value;
                break;
            default:
                throw std::runtime_error(
                    "Expected type, got '" + cur().value + "' at " +
                    std::to_string(cur().line)+":"+std::to_string(cur().col));
        }
        return ty;
    }

    // ── Block ──────────────────────────────────────────────
    NodePtr parse_block() {
        auto node = std::make_unique<BlockExpr>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::LBRACE, "{");
        while (!check(TK::RBRACE) && !at_end()) {
            node->stmts.push_back(parse_stmt());
        }
        expect(TK::RBRACE, "}");
        return node;
    }

    // ── Statement ──────────────────────────────────────────
    NodePtr parse_stmt() {
        uint32_t sl = cur().line, sc = cur().col;

        if (check(TK::KW_let))    return parse_let();
        if (check(TK::KW_ret))    return parse_ret();
        if (check(TK::KW_break))  return parse_break();
        if (check(TK::KW_next))   { consume(); match(TK::SEMICOLON); return std::make_unique<NextStmt>(); }
        if (check(TK::KW_while))  return parse_while();
        if (check(TK::KW_loop))   return parse_loop();
        if (check(TK::KW_for))    return parse_for();
        if (check(TK::KW_defer))  return parse_defer();
        if (check(TK::KW_drop))   return parse_drop();
        if (check(TK::KW_if))     return parse_if_stmt();
        if (check(TK::LBRACE))    return parse_block();

        // Expression statement (or assignment)
        // For assignment, we need lhs to be a simple postfix expr, not full binop
        // Try to detect assignment by checking token after a postfix expr
        auto expr = parse_postfix_stmt();

        // Assignment?
        static const std::vector<TK> assign_ops = {
            TK::ASSIGN, TK::PLUS_ASSIGN, TK::MINUS_ASSIGN,
            TK::STAR_ASSIGN, TK::SLASH_ASSIGN,
            TK::AMP_ASSIGN, TK::PIPE_ASSIGN, TK::CARET_ASSIGN
        };
        for (auto op : assign_ops) {
            if (check(op)) {
                auto node = std::make_unique<AssignStmt>();
                node->line = sl; node->col = sc;
                node->op = consume().value;
                node->lhs = std::move(expr);
                node->rhs = parse_expr();
                match(TK::SEMICOLON);
                return node;
            }
        }
        // Not an assignment — treat expr as full expression (re-parse as binop if needed)
        // Since we already parsed postfix, continue with binop
        expr = parse_expr_from(std::move(expr), 0);
        match(TK::SEMICOLON);
        auto es = std::make_unique<ExprStmt>();
        es->line = sl; es->col = sc;
        es->expr = std::move(expr);
        return es;
    }

    NodePtr parse_let() {
        auto node = std::make_unique<LetStmt>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_let, "let");
        node->is_mut = match(TK::KW_mut);
        node->name = expect(TK::IDENT, "variable name").value;
        if (match(TK::COLON)) node->type_ann = parse_type();
        if (match(TK::ASSIGN)) node->init = parse_expr();
        match(TK::SEMICOLON);
        return node;
    }

    NodePtr parse_ret() {
        auto node = std::make_unique<RetStmt>();
        node->line = cur().line; node->col = cur().col;
        expect(TK::KW_ret, "ret");
        if (!check(TK::SEMICOLON) && !check(TK::RBRACE))
            node->value = parse_expr();
        match(TK::SEMICOLON);
        return node;
    }

    NodePtr parse_break() {
        auto node = std::make_unique<BreakStmt>();
        node->line = cur().line;
        expect(TK::KW_break, "break");
        if (!check(TK::SEMICOLON) && !check(TK::RBRACE))
            node->value = parse_expr();
        match(TK::SEMICOLON);
        return node;
    }

    NodePtr parse_while() {
        auto node = std::make_unique<WhileStmt>();
        node->line = cur().line;
        expect(TK::KW_while, "while");
        node->cond = parse_expr();
        node->body = parse_block();
        return node;
    }

    NodePtr parse_loop() {
        auto node = std::make_unique<LoopStmt>();
        node->line = cur().line;
        expect(TK::KW_loop, "loop");
        node->body = parse_block();
        return node;
    }

    NodePtr parse_for() {
        auto node = std::make_unique<ForStmt>();
        node->line = cur().line;
        expect(TK::KW_for, "for");
        node->var = expect(TK::IDENT, "loop variable").value;
        expect(TK::KW_in, "in");
        node->iter = parse_expr();
        node->body = parse_block();
        return node;
    }

    NodePtr parse_defer() {
        auto node = std::make_unique<DeferStmt>();
        node->line = cur().line;
        expect(TK::KW_defer, "defer");
        node->expr = parse_expr();
        match(TK::SEMICOLON);
        return node;
    }

    NodePtr parse_drop() {
        auto node = std::make_unique<DropStmt>();
        node->line = cur().line;
        expect(TK::KW_drop, "drop");
        node->expr = parse_expr();
        match(TK::SEMICOLON);
        return node;
    }

    NodePtr parse_if_stmt() {
        return parse_if_expr();
    }

    NodePtr parse_if_expr() {
        auto node = std::make_unique<IfExpr>();
        node->line = cur().line;
        expect(TK::KW_if, "if");
        node->cond = parse_expr();
        node->then_block = parse_block();
        while (check(TK::KW_elif)) {
            consume();
            auto c = parse_expr();
            auto b = parse_block();
            node->elif_branches.emplace_back(std::move(c), std::move(b));
        }
        if (match(TK::KW_else)) node->else_block = parse_block();
        return node;
    }

    // Helper: parse only postfix, no binop — for LHS detection
    NodePtr parse_postfix_stmt() { return parse_postfix(); }

    // Helper: continue Pratt from already-parsed LHS
    NodePtr parse_expr_from(NodePtr lhs, int min_prec) {
        while (true) {
            int prec = binop_prec();
            if (prec <= min_prec) break;
            auto op = consume().value;
            auto rhs = parse_expr(prec);
            auto node = std::make_unique<BinopExpr>();
            node->op = op; node->lhs = std::move(lhs); node->rhs = std::move(rhs);
            lhs = std::move(node);
        }
        if (check(TK::KW_as)) {
            consume();
            auto node = std::make_unique<CastExpr>();
            node->expr = std::move(lhs); node->target = parse_type(); return node;
        }
        return lhs;
    }

    // Expressions (Pratt parser)
    NodePtr parse_expr(int min_prec = 0) {
        auto lhs = parse_unary();
        while (true) {
            int prec = binop_prec();
            if (prec <= min_prec) break;
            auto op = consume().value;
            auto rhs = parse_expr(prec);
            auto node = std::make_unique<BinopExpr>();
            node->op = op;
            node->lhs = std::move(lhs);
            node->rhs = std::move(rhs);
            lhs = std::move(node);
        }
        // 'as' cast
        if (check(TK::KW_as)) {
            consume();
            auto node = std::make_unique<CastExpr>();
            node->expr = std::move(lhs);
            node->target = parse_type();
            return node;
        }
        // Range
        if (check(TK::DOTDOT) || check(TK::DOTS)) {
            bool incl = cur().type == TK::DOTS;
            consume();
            auto node = std::make_unique<RangeExpr>();
            node->start = std::move(lhs);
            node->end = parse_expr();
            node->inclusive = incl;
            return node;
        }
        return lhs;
    }

    int binop_prec() {
        switch(cur().type) {
            case TK::KW_or:                   return 1;
            case TK::KW_and:                  return 2;
            case TK::PIPE:                    return 3;
            case TK::CARET:                   return 4;
            case TK::AMP:                     return 5;
            case TK::EQ: case TK::NEQ:        return 6;
            case TK::LT: case TK::GT:
            case TK::LE: case TK::GE:         return 7;
            case TK::LSHIFT: case TK::RSHIFT: return 8;
            case TK::PLUS: case TK::MINUS:    return 9;
            case TK::STAR: case TK::SLASH:
            case TK::PERCENT:                 return 10;
            default: return 0;
        }
    }

    NodePtr parse_unary() {
        uint32_t sl=cur().line, sc=cur().col;
        if (check(TK::MINUS)||check(TK::BANG)||check(TK::KW_not)||check(TK::TILDE)) {
            auto node = std::make_unique<UnopExpr>();
            node->line=sl; node->col=sc;
            node->op = consume().value;
            node->operand = parse_unary();
            return node;
        }
        if (check(TK::STAR)) {
            consume();
            auto node = std::make_unique<DerefExpr>();
            node->line=sl; node->col=sc;
            node->expr = parse_unary();
            return node;
        }
        if (check(TK::AMP)) {
            consume();
            auto node = std::make_unique<AddrOfExpr>();
            node->line=sl; node->col=sc;
            node->is_mut = match(TK::KW_mut);
            node->expr = parse_unary();
            return node;
        }
        return parse_postfix();
    }

    NodePtr parse_postfix() {
        auto expr = parse_primary();
        while (true) {
            if (check(TK::LPAREN)) {
                // Function call
                auto node = std::make_unique<CallExpr>();
                node->line = expr->line;
                node->callee = std::move(expr);
                consume(); // (
                while (!check(TK::RPAREN) && !at_end()) {
                    // named arg: name: expr
                    if (check(TK::IDENT) && peek().type == TK::COLON) {
                        node->named_args.push_back(consume().value);
                        consume(); // :
                    } else {
                        node->named_args.push_back("");
                    }
                    node->args.push_back(parse_expr());
                    if (!match(TK::COMMA)) break;
                }
                expect(TK::RPAREN, ")");
                expr = std::move(node);
            } else if (check(TK::LBRACKET)) {
                // Index
                auto node = std::make_unique<IndexExpr>();
                node->line = expr->line;
                node->obj = std::move(expr);
                consume();
                node->index = parse_expr();
                expect(TK::RBRACKET, "]");
                expr = std::move(node);
            } else if (check(TK::DOT) || check(TK::ARROW)) {
                bool deref = cur().type == TK::ARROW;
                consume();
                auto node = std::make_unique<FieldExpr>();
                node->obj = std::move(expr);
                node->field = expect(TK::IDENT, "field name").value;
                node->deref = deref;
                expr = std::move(node);
            } else break;
        }
        return expr;
    }

    NodePtr parse_primary() {
        uint32_t sl=cur().line, sc=cur().col;

        if (check(TK::INT)) {
            auto node = std::make_unique<LitIntExpr>();
            node->line=sl; node->col=sc;
            std::string v = consume().value;
            // strip suffix
            size_t p = v.find_first_not_of("0123456789xXbBoO");
            if (p != std::string::npos) v = v.substr(0,p);
            try {
                if (v.size()>2 && v[0]=='0' && v[1]=='x')
                    node->value = std::stoll(v,nullptr,16);
                else if (v.size()>2 && v[0]=='0' && v[1]=='b')
                    node->value = std::stoll(v.substr(2),nullptr,2);
                else
                    node->value = std::stoll(v);
            } catch(...) { node->value = 0; }
            return node;
        }
        if (check(TK::FLOAT)) {
            auto node = std::make_unique<LitFloatExpr>();
            node->line=sl; node->col=sc;
            try { node->value = std::stod(consume().value); } catch(...){node->value=0;}
            return node;
        }
        if (check(TK::STRING)) {
            auto node = std::make_unique<LitStrExpr>();
            node->line=sl; node->col=sc;
            node->value = consume().value;
            return node;
        }
        if (check(TK::CHAR)) {
            auto node = std::make_unique<LitCharExpr>();
            node->line=sl; node->col=sc;
            node->value = consume().value[0];
            return node;
        }
        if (check(TK::BOOL_TRUE)) {
            consume();
            auto node = std::make_unique<LitBoolExpr>();
            node->line=sl; node->col=sc; node->value=true; return node;
        }
        if (check(TK::BOOL_FALSE)) {
            consume();
            auto node = std::make_unique<LitBoolExpr>();
            node->line=sl; node->col=sc; node->value=false; return node;
        }
        if (check(TK::KW_null)) {
            consume();
            auto node = std::make_unique<NullExpr>();
            node->line=sl; node->col=sc; return node;
        }
        if (check(TK::KW_sizeof)) {
            consume();
            expect(TK::LPAREN,"(");
            auto node = std::make_unique<SizeofExpr>();
            node->line=sl; node->col=sc;
            node->target = parse_type();
            expect(TK::RPAREN,")");
            return node;
        }
        if (check(TK::KW_new)) {
            consume();
            auto node = std::make_unique<NewExpr>();
            node->line=sl; node->col=sc;
            node->target = parse_type();
            if (match(TK::LPAREN)) {
                while(!check(TK::RPAREN)&&!at_end()){
                    node->args.push_back(parse_expr());
                    if(!match(TK::COMMA))break;
                }
                expect(TK::RPAREN,")");
            }
            return node;
        }
        if (check(TK::KW_if)) return parse_if_expr();
        if (check(TK::LBRACE)) return parse_block();

        // Array literal [a,b,c]
        if (check(TK::LBRACKET)) {
            consume();
            auto node = std::make_unique<ArrayExpr>();
            node->line=sl; node->col=sc;
            while(!check(TK::RBRACKET)&&!at_end()){
                node->elements.push_back(parse_expr());
                if(!match(TK::COMMA))break;
            }
            expect(TK::RBRACKET,"]");
            return node;
        }

        if (check(TK::LPAREN)) {
            consume();
            auto e = parse_expr();
            expect(TK::RPAREN,")");
            return e;
        }

        // Ident or struct literal
        if (check(TK::IDENT)) {
            std::string name = consume().value;
            // Struct literal: Name { field: val, ... }
            if (check(TK::LBRACE)) {
                // Look ahead to see if this is struct literal
                // Heuristic: if next token after { is ident followed by :
                if (pos_+1 < tokens_.size() &&
                    tokens_[pos_+1].type == TK::IDENT &&
                    pos_+2 < tokens_.size() &&
                    tokens_[pos_+2].type == TK::COLON) {
                    consume(); // {
                    auto node = std::make_unique<StructLitExpr>();
                    node->line=sl; node->col=sc;
                    node->name = name;
                    while(!check(TK::RBRACE)&&!at_end()){
                        std::string fn = expect(TK::IDENT,"field name").value;
                        expect(TK::COLON,":");
                        auto fv = parse_expr();
                        node->fields.emplace_back(fn, std::move(fv));
                        if(!match(TK::COMMA))break;
                    }
                    expect(TK::RBRACE,"}");
                    return node;
                }
            }
            auto node = std::make_unique<IdentExpr>();
            node->line=sl; node->col=sc;
            node->name = name;
            return node;
        }

        throw std::runtime_error(
            "Unexpected token in expression: '" + cur().value + "' at " +
            std::to_string(cur().line)+":"+std::to_string(cur().col));
    }
};

} // namespace dtp
