#pragma once
#include "dtp.hpp"
#include <stdexcept>
#include <sstream>

namespace dtp {

// ── TypeInfo methods ──────────────────────────────────────────
inline uint32_t TypeInfo::size() const {
    switch(base) {
        case BaseType::I8:  case BaseType::U8:  case BaseType::Char: case BaseType::Bool: return 1;
        case BaseType::I16: case BaseType::U16: return 2;
        case BaseType::I32: case BaseType::U32: case BaseType::F32:  return 4;
        case BaseType::I64: case BaseType::U64: case BaseType::F64:  return 8;
        case BaseType::Ptr: case BaseType::RawPtr: case BaseType::Str: return 8; // 64-bit ptr
        case BaseType::Slice: return 16; // ptr + len
        case BaseType::Array:
            if (inner && array_len >= 0)
                return inner->size() * (uint32_t)array_len;
            return 0;
        case BaseType::Void: return 0;
        default: return 8; // struct/unknown: placeholder
    }
}

inline uint32_t TypeInfo::align() const {
    switch(base) {
        case BaseType::I8:  case BaseType::U8:  case BaseType::Char: case BaseType::Bool: return 1;
        case BaseType::I16: case BaseType::U16: return 2;
        case BaseType::I32: case BaseType::U32: case BaseType::F32:  return 4;
        default: return 8;
    }
}

inline bool TypeInfo::is_integer() const {
    return base>=BaseType::I8 && base<=BaseType::U64;
}
inline bool TypeInfo::is_float() const {
    return base==BaseType::F32 || base==BaseType::F64;
}
inline bool TypeInfo::is_numeric() const { return is_integer()||is_float(); }
inline bool TypeInfo::is_primitive() const {
    return is_numeric()||base==BaseType::Bool||base==BaseType::Char;
}
inline bool TypeInfo::operator==(const TypeInfo& o) const {
    if (base != o.base) return false;
    if (name != o.name) return false;
    if (is_ptr != o.is_ptr || ptr_depth != o.ptr_depth) return false;
    if (inner && o.inner) return *inner == *o.inner;
    return inner == o.inner;
}

// ── Type Checker ──────────────────────────────────────────────
class TypeChecker {
public:
    std::vector<Diagnostic> diagnostics;

    void check(Module& mod) {
        // First pass: collect type declarations
        for (auto& item : mod.items) {
            if (auto* s = dynamic_cast<StructDecl*>(item.get())) {
                struct_defs_[s->name] = s;
            }
            if (auto* e = dynamic_cast<EnumDecl*>(item.get())) {
                enum_defs_[e->name] = e;
            }
        }
        // Second pass: collect function signatures
        for (auto& item : mod.items) {
            if (auto* fn = dynamic_cast<FnDecl*>(item.get())) {
                fn_sigs_[fn->name] = fn;
            }
        }
        // Third pass: check bodies
        push_scope();
        for (auto& item : mod.items) {
            check_top(item.get());
        }
        pop_scope();
    }

private:
    std::vector<Scope*> scopes_;
    std::unordered_map<std::string, StructDecl*> struct_defs_;
    std::unordered_map<std::string, EnumDecl*>   enum_defs_;
    std::unordered_map<std::string, FnDecl*>     fn_sigs_;
    TypeRef current_fn_ret_;

    Scope* cur_scope() { return scopes_.back(); }

    void push_scope() {
        auto* s = new Scope();
        if (!scopes_.empty()) s->parent = scopes_.back();
        scopes_.push_back(s);
    }

    void pop_scope() {
        delete scopes_.back();
        scopes_.pop_back();
    }

    void err(const std::string& msg, uint32_t line, uint32_t col) {
        diagnostics.push_back({ErrLevel::Error, msg, line, col, ""});
    }

    void check_top(ASTNode* node) {
        if (auto* fn = dynamic_cast<FnDecl*>(node)) check_fn(fn);
        if (auto* st = dynamic_cast<StructDecl*>(node)) check_struct(st);
        if (auto* im = dynamic_cast<ImplBlock*>(node)) check_impl(im);
    }

    void check_struct(StructDecl* st) {
        for (auto& f : st->fields) {
            if (f.type->base == BaseType::Struct && f.type->name == st->name) {
                err("Recursive struct without pointer: " + st->name, 0, 0);
            }
        }
    }

    void check_impl(ImplBlock* im) {
        for (auto& m : im->methods) {
            if (auto* fn = dynamic_cast<FnDecl*>(m.get())) {
                fn_sigs_[im->type_name + "::" + fn->name] = fn;
                check_fn(fn);
            }
        }
    }

    void check_fn(FnDecl* fn) {
        push_scope();
        current_fn_ret_ = fn->ret_type;

        // Register params
        for (auto& p : fn->params) {
            Symbol sym;
            sym.name   = p.name;
            sym.type   = p.type;
            sym.kind   = SymKind::Param;
            sym.is_mut = p.is_mut;
            cur_scope()->define(std::move(sym));
        }

        if (fn->body) {
            check_block(dynamic_cast<BlockExpr*>(fn->body.get()));
        }
        pop_scope();
    }

    void check_block(BlockExpr* blk) {
        if (!blk) return;
        push_scope();
        for (auto& s : blk->stmts) check_stmt(s.get());
        if (blk->tail_expr) check_expr(blk->tail_expr.get());
        pop_scope();
    }

    void check_stmt(ASTNode* node) {
        if (auto* s = dynamic_cast<LetStmt*>(node)) {
            TypeRef init_ty;
            if (s->init) init_ty = check_expr(s->init.get());
            TypeRef ty = s->type_ann ? s->type_ann : init_ty;
            if (!ty) {
                err("Cannot infer type of '" + s->name + "'", s->line, s->col);
                ty = std::make_shared<TypeInfo>(); ty->base = BaseType::Unknown;
            }
            if (s->type_ann && init_ty && !(*ty == *init_ty)) {
                bool both_num    = ty->is_numeric() && init_ty->is_numeric();
                bool struct_ok   = ty->base==BaseType::Struct && init_ty->base==BaseType::Struct
                                   && ty->name==init_ty->name;
                bool ptr_ok      = ty->base==BaseType::Ptr;
                bool unknown_ok  = init_ty->base==BaseType::Unknown || ty->base==BaseType::Unknown;
                if (!both_num && !struct_ok && !ptr_ok && !unknown_ok) {
                    err("Type mismatch in let binding '" + s->name + "'", s->line, s->col);
                }
            }
            Symbol sym;
            sym.name = s->name; sym.type = ty;
            sym.kind = SymKind::Var; sym.is_mut = s->is_mut;
            cur_scope()->define(std::move(sym));
        }
        else if (auto* s = dynamic_cast<AssignStmt*>(node)) {
            auto lty = check_expr(s->lhs.get());
            auto rty = check_expr(s->rhs.get());
            // Check mutability
            if (auto* id = dynamic_cast<IdentExpr*>(s->lhs.get())) {
                auto* sym = cur_scope()->lookup(id->name);
                if (sym && !sym->is_mut) {
                    err("Cannot assign to immutable variable '" + id->name + "'", s->line, s->col);
                }
            }
        }
        else if (auto* s = dynamic_cast<RetStmt*>(node)) {
            TypeRef ty;
            if (s->value) ty = check_expr(s->value.get());
            else {
                ty = std::make_shared<TypeInfo>(); ty->base = BaseType::Void;
            }
            if (current_fn_ret_ && ty && !(*current_fn_ret_ == *ty)) {
                bool both_numeric = current_fn_ret_->is_numeric() && ty->is_numeric();
                bool both_struct  = current_fn_ret_->base==BaseType::Struct &&
                                    ty->base==BaseType::Struct &&
                                    current_fn_ret_->name == ty->name;
                bool ret_void = current_fn_ret_->base==BaseType::Void;
                bool ty_unknown = ty->base==BaseType::Unknown;
                if (!both_numeric && !both_struct && !ret_void && !ty_unknown) {
                    err("Return type mismatch", s->line, s->col);
                }
            }
        }
        else if (auto* s = dynamic_cast<WhileStmt*>(node)) {
            check_expr(s->cond.get());
            if (auto* blk = dynamic_cast<BlockExpr*>(s->body.get())) check_block(blk);
        }
        else if (auto* s = dynamic_cast<ForStmt*>(node)) {
            check_expr(s->iter.get());
            push_scope();
            Symbol sym; sym.name=s->var; sym.kind=SymKind::Var;
            sym.type = std::make_shared<TypeInfo>(); sym.type->base=BaseType::I64;
            cur_scope()->define(sym);
            if (auto* blk = dynamic_cast<BlockExpr*>(s->body.get())) check_block(blk);
            pop_scope();
        }
        else if (auto* s = dynamic_cast<IfExpr*>(node)) {
            check_expr(s->cond.get());
            if (auto* blk = dynamic_cast<BlockExpr*>(s->then_block.get())) check_block(blk);
            for (auto& [c,b] : s->elif_branches) {
                check_expr(c.get());
                if (auto* blk = dynamic_cast<BlockExpr*>(b.get())) check_block(blk);
            }
            if (s->else_block)
                if (auto* blk = dynamic_cast<BlockExpr*>(s->else_block.get())) check_block(blk);
        }
        else if (auto* s = dynamic_cast<ExprStmt*>(node)) {
            check_expr(s->expr.get());
        }
        else if (auto* s = dynamic_cast<LoopStmt*>(node)) {
            if (auto* blk = dynamic_cast<BlockExpr*>(s->body.get())) check_block(blk);
        }
        else if (auto* s = dynamic_cast<DeferStmt*>(node)) {
            check_expr(s->expr.get());
        }
        else if (auto* s = dynamic_cast<BlockExpr*>(node)) {
            check_block(s);
        }
    }

    TypeRef check_expr(ASTNode* node) {
        if (!node) return nullptr;

        if (auto* n = dynamic_cast<LitIntExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::I64;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<LitFloatExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::F64;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<LitBoolExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Bool;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<LitStrExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Str;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<LitCharExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Char;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<NullExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Ptr;
            ty->is_nullable=true; node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<IdentExpr*>(node)) {
            // Check if it's a function
            if (fn_sigs_.count(n->name)) {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Fn;
                ty->name=n->name; node->type=ty; return ty;
            }
            auto* sym = cur_scope()->lookup(n->name);
            if (!sym) {
                err("Undefined variable: '" + n->name + "'", n->line, n->col);
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
                return ty;
            }
            node->type = sym->type;
            return sym->type;
        }
        if (auto* n = dynamic_cast<BinopExpr*>(node)) {
            auto lty = check_expr(n->lhs.get());
            auto rty = check_expr(n->rhs.get());
            // Comparison → bool
            if (n->op=="=="||n->op=="!="||n->op=="<"||n->op==">"||n->op=="<="||n->op==">="||
                n->op=="and"||n->op=="or") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Bool;
                node->type=ty; return ty;
            }
            // Arithmetic → promote
            auto ty = lty ? lty : rty;
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<UnopExpr*>(node)) {
            auto ty = check_expr(n->operand.get());
            if (n->op == "not" || n->op == "!") {
                auto bty = std::make_shared<TypeInfo>(); bty->base=BaseType::Bool;
                node->type=bty; return bty;
            }
            node->type = ty; return ty;
        }
        if (auto* n = dynamic_cast<CallExpr*>(node)) {
            // Get callee name
            std::string fn_name;
            if (auto* id = dynamic_cast<IdentExpr*>(n->callee.get())) fn_name = id->name;
            if (auto* fe = dynamic_cast<FieldExpr*>(n->callee.get())) {
                // method call
                check_expr(fe->obj.get());
            }

            for (auto& a : n->args) check_expr(a.get());

            // Built-ins
            if (fn_name=="print"||fn_name=="println"||fn_name=="eprint") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Void;
                node->type=ty; return ty;
            }
            if (fn_name=="len"||fn_name=="cap") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::U64;
                node->type=ty; return ty;
            }
            if (fn_name=="read_i64"||fn_name=="input_i64") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::I64;
                node->type=ty; return ty;
            }
            if (fn_name=="read_f64"||fn_name=="input_f64") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::F64;
                node->type=ty; return ty;
            }
            if (fn_name=="read_str"||fn_name=="input_str"||fn_name=="input") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Str;
                node->type=ty; return ty;
            }
            if (fn_name=="read_bool"||fn_name=="input_bool") {
                auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Bool;
                node->type=ty; return ty;
            }

            auto it = fn_sigs_.find(fn_name);
            if (it != fn_sigs_.end()) {
                node->type = it->second->ret_type;
                return it->second->ret_type;
            }
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<CastExpr*>(node)) {
            check_expr(n->expr.get());
            node->type = n->target; return n->target;
        }
        if (auto* n = dynamic_cast<AddrOfExpr*>(node)) {
            auto inner = check_expr(n->expr.get());
            auto ty = std::make_shared<TypeInfo>();
            ty->base=BaseType::Ptr; ty->is_ptr=true; ty->inner=inner; ty->ptr_depth=1;
            ty->is_mut=n->is_mut;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<DerefExpr*>(node)) {
            auto pty = check_expr(n->expr.get());
            if (pty && pty->inner) { node->type=pty->inner; return pty->inner; }
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<IndexExpr*>(node)) {
            auto oty = check_expr(n->obj.get());
            check_expr(n->index.get());
            if (oty && oty->inner) { node->type=oty->inner; return oty->inner; }
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<FieldExpr*>(node)) {
            auto oty = check_expr(n->obj.get());
            if (oty && oty->base == BaseType::Struct) {
                auto it = struct_defs_.find(oty->name);
                if (it != struct_defs_.end()) {
                    for (auto& f : it->second->fields) {
                        if (f.name == n->field) { node->type=f.type; return f.type; }
                    }
                    err("No field '" + n->field + "' in struct '" + oty->name + "'", n->line, n->col);
                }
            }
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<SizeofExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::U64;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<NewExpr*>(node)) {
            auto ty = std::make_shared<TypeInfo>();
            ty->base=BaseType::Ptr; ty->is_ptr=true; ty->inner=n->target; ty->ptr_depth=1;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<IfExpr*>(node)) {
            check_expr(n->cond.get());
            TypeRef ty;
            if (auto* blk = dynamic_cast<BlockExpr*>(n->then_block.get())) {
                check_block(blk);
                if (blk->tail_expr) ty = blk->tail_expr->type;
            }
            node->type = ty ? ty : (std::make_shared<TypeInfo>());
            return node->type;
        }
        if (auto* n = dynamic_cast<BlockExpr*>(node)) {
            check_block(n);
            if (n->tail_expr) { node->type=n->tail_expr->type; return node->type; }
            auto ty=std::make_shared<TypeInfo>(); ty->base=BaseType::Void;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<ArrayExpr*>(node)) {
            TypeRef ety;
            for (auto& e : n->elements) ety = check_expr(e.get());
            auto ty = std::make_shared<TypeInfo>();
            ty->base=BaseType::Array; ty->inner=ety;
            ty->array_len=(int64_t)n->elements.size();
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<RangeExpr*>(node)) {
            check_expr(n->start.get()); check_expr(n->end.get());
            auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Slice;
            ty->inner = std::make_shared<TypeInfo>(); ty->inner->base=BaseType::I64;
            node->type=ty; return ty;
        }
        if (auto* n = dynamic_cast<StructLitExpr*>(node)) {
            for (auto& [fname, fval] : n->fields) check_expr(fval.get());
            auto ty = std::make_shared<TypeInfo>();
            ty->base = BaseType::Struct; ty->name = n->name;
            node->type = ty; return ty;
        }
        // Fallthrough
        auto ty = std::make_shared<TypeInfo>(); ty->base=BaseType::Unknown;
        node->type = ty; return ty;
    }
};

} // namespace dtp
