#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <optional>
#include <functional>
#include <cstdint>

// ============================================================
//  DTP - Direct Transfer Protocol Language
//  Design goals: zero-cost abstractions, manual memory model,
//  compile-time computation, minimal runtime overhead.
// ============================================================

namespace dtp {

// ─── TOKEN TYPES ────────────────────────────────────────────
enum class TK : uint8_t {
    // Literals
    INT, FLOAT, STRING, CHAR, BOOL_TRUE, BOOL_FALSE,

    // Identifiers & keywords
    IDENT,
    KW_fn, KW_ret, KW_let, KW_mut, KW_if, KW_elif, KW_else,
    KW_loop, KW_while, KW_for, KW_in, KW_break, KW_next,
    KW_struct, KW_enum, KW_impl, KW_trait, KW_use,
    KW_pub, KW_ext, KW_inline, KW_comptime,
    KW_new, KW_drop, KW_sizeof, KW_alignof,
    KW_null, KW_as, KW_and, KW_or, KW_not,
    KW_defer, KW_unsafe, KW_asm,

    // Primitive types
    TY_i8, TY_i16, TY_i32, TY_i64,
    TY_u8, TY_u16, TY_u32, TY_u64,
    TY_f32, TY_f64, TY_bool, TY_char,
    TY_void, TY_str, TY_ptr, TY_raw,

    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    AMP, PIPE, CARET, TILDE, LSHIFT, RSHIFT,
    EQ, NEQ, LT, GT, LE, GE,
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN,
    SLASH_ASSIGN, AMP_ASSIGN, PIPE_ASSIGN, CARET_ASSIGN,
    ARROW, FAT_ARROW, DOTS, DOTDOT, DOT,
    QUESTION, BANG, DCOLON,

    // Delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMICOLON, COLON, COMMA, AT, HASH, DOLLAR,

    // Special
    EOF_TOK, NEWLINE, COMMENT
};

struct Token {
    TK type;
    std::string value;
    uint32_t line;
    uint32_t col;
    uint32_t file_id;
};

// ─── TYPES ──────────────────────────────────────────────────
enum class BaseType : uint8_t {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64, Bool, Char,
    Void, Str,
    Ptr, RawPtr,
    Array, Slice,
    Struct, Enum, Fn,
    Unknown
};

struct TypeInfo {
    BaseType base;
    bool is_mut      = false;
    bool is_ref      = false;
    bool is_ptr      = false;
    bool is_nullable = false;
    uint32_t ptr_depth = 0;
    std::string name;                        // for named types
    std::shared_ptr<TypeInfo> inner;         // for ptr/array/slice
    std::vector<std::shared_ptr<TypeInfo>> params; // for fn types
    std::shared_ptr<TypeInfo> ret_type;
    int64_t array_len = -1;                  // -1 = slice

    uint32_t size() const;
    uint32_t align() const;
    bool is_integer() const;
    bool is_float() const;
    bool is_numeric() const;
    bool is_primitive() const;
    bool operator==(const TypeInfo& o) const;
};

using TypeRef = std::shared_ptr<TypeInfo>;

// ─── AST NODES ──────────────────────────────────────────────
struct ASTNode {
    uint32_t line = 0;
    uint32_t col  = 0;
    TypeRef  type;   // filled by type checker
    virtual ~ASTNode() = default;
    virtual std::string kind() const = 0;
};
using NodePtr = std::unique_ptr<ASTNode>;

// --- Expressions ---
struct LitIntExpr    : ASTNode { int64_t  value; std::string kind() const override { return "LitInt"; } };
struct LitFloatExpr  : ASTNode { double   value; std::string kind() const override { return "LitFloat"; } };
struct LitStrExpr    : ASTNode { std::string value; std::string kind() const override { return "LitStr"; } };
struct LitCharExpr   : ASTNode { char     value; std::string kind() const override { return "LitChar"; } };
struct LitBoolExpr   : ASTNode { bool     value; std::string kind() const override { return "LitBool"; } };
struct NullExpr      : ASTNode { std::string kind() const override { return "Null"; } };

struct IdentExpr : ASTNode {
    std::string name;
    std::string kind() const override { return "Ident"; }
};

struct BinopExpr : ASTNode {
    std::string op;
    NodePtr lhs, rhs;
    std::string kind() const override { return "Binop"; }
};

struct UnopExpr : ASTNode {
    std::string op;
    NodePtr operand;
    bool postfix = false;
    std::string kind() const override { return "Unop"; }
};

struct CallExpr : ASTNode {
    NodePtr callee;
    std::vector<NodePtr> args;
    std::vector<std::string> named_args;
    std::string kind() const override { return "Call"; }
};

struct IndexExpr : ASTNode {
    NodePtr obj, index;
    std::string kind() const override { return "Index"; }
};

struct FieldExpr : ASTNode {
    NodePtr obj;
    std::string field;
    bool deref = false; // -> vs .
    std::string kind() const override { return "Field"; }
};

struct CastExpr : ASTNode {
    NodePtr expr;
    TypeRef target;
    std::string kind() const override { return "Cast"; }
};

struct AddrOfExpr : ASTNode {
    NodePtr expr;
    bool is_mut = false;
    std::string kind() const override { return "AddrOf"; }
};

struct DerefExpr : ASTNode {
    NodePtr expr;
    std::string kind() const override { return "Deref"; }
};

struct SizeofExpr : ASTNode {
    TypeRef target;
    std::string kind() const override { return "Sizeof"; }
};

struct NewExpr : ASTNode {
    TypeRef target;
    std::vector<NodePtr> args;
    std::string kind() const override { return "New"; }
};

struct ArrayExpr : ASTNode {
    std::vector<NodePtr> elements;
    std::string kind() const override { return "Array"; }
};

struct StructLitExpr : ASTNode {
    std::string name;
    std::vector<std::pair<std::string, NodePtr>> fields;
    std::string kind() const override { return "StructLit"; }
};

struct RangeExpr : ASTNode {
    NodePtr start, end;
    bool inclusive = false;
    std::string kind() const override { return "Range"; }
};

struct IfExpr : ASTNode {
    NodePtr cond;
    NodePtr then_block;
    std::vector<std::pair<NodePtr,NodePtr>> elif_branches;
    NodePtr else_block;
    std::string kind() const override { return "IfExpr"; }
};

struct BlockExpr : ASTNode {
    std::vector<NodePtr> stmts;
    NodePtr tail_expr; // optional trailing expr (return value)
    std::string kind() const override { return "Block"; }
};

struct InlineAsmExpr : ASTNode {
    std::string code;
    std::vector<std::pair<std::string,std::string>> inputs;
    std::vector<std::pair<std::string,std::string>> outputs;
    std::vector<std::string> clobbers;
    std::string kind() const override { return "InlineAsm"; }
};

// --- Statements ---
struct LetStmt : ASTNode {
    std::string name;
    TypeRef type_ann;
    NodePtr init;
    bool is_mut = false;
    std::string kind() const override { return "Let"; }
};

struct AssignStmt : ASTNode {
    NodePtr lhs, rhs;
    std::string op; // = += -= etc.
    std::string kind() const override { return "Assign"; }
};

struct RetStmt : ASTNode {
    NodePtr value;
    std::string kind() const override { return "Ret"; }
};

struct BreakStmt : ASTNode {
    NodePtr value;
    std::string kind() const override { return "Break"; }
};

struct NextStmt : ASTNode {
    std::string kind() const override { return "Next"; }
};

struct DeferStmt : ASTNode {
    NodePtr expr;
    std::string kind() const override { return "Defer"; }
};

struct DropStmt : ASTNode {
    NodePtr expr;
    std::string kind() const override { return "Drop"; }
};

struct ExprStmt : ASTNode {
    NodePtr expr;
    std::string kind() const override { return "ExprStmt"; }
};

// Loop variants
struct WhileStmt : ASTNode {
    NodePtr cond;
    NodePtr body;
    std::string kind() const override { return "While"; }
};

struct LoopStmt : ASTNode {
    NodePtr body;
    std::string kind() const override { return "Loop"; }
};

struct ForStmt : ASTNode {
    std::string var;
    NodePtr iter;
    NodePtr body;
    std::string kind() const override { return "For"; }
};

// --- Declarations ---
struct Param {
    std::string name;
    TypeRef     type;
    NodePtr     default_val;
    bool        is_mut = false;
};

struct FnDecl : ASTNode {
    std::string name;
    std::vector<Param> params;
    TypeRef ret_type;
    NodePtr body;
    bool is_pub    = false;
    bool is_ext    = false;
    bool is_inline = false;
    bool is_comptime = false;
    std::vector<std::string> attrs;
    std::string kind() const override { return "FnDecl"; }
};

struct StructField {
    std::string name;
    TypeRef     type;
    NodePtr     default_val;
    bool        is_pub = false;
};

struct StructDecl : ASTNode {
    std::string name;
    std::vector<StructField> fields;
    bool is_pub = false;
    std::vector<std::string> attrs;
    std::string kind() const override { return "StructDecl"; }
};

struct EnumVariant {
    std::string name;
    std::vector<TypeRef> payload;
    int64_t discriminant = -1;
};

struct EnumDecl : ASTNode {
    std::string name;
    std::vector<EnumVariant> variants;
    bool is_pub = false;
    std::string kind() const override { return "EnumDecl"; }
};

struct ImplBlock : ASTNode {
    std::string type_name;
    std::string trait_name; // optional
    std::vector<NodePtr> methods;
    std::string kind() const override { return "Impl"; }
};

struct UseDecl : ASTNode {
    std::vector<std::string> path;
    std::string alias;
    std::string kind() const override { return "Use"; }
};

struct Module {
    std::string name;
    std::vector<NodePtr> items;
};

// ─── SYMBOL TABLE ───────────────────────────────────────────
enum class SymKind { Var, Fn, Type, Param };

struct Symbol {
    std::string name;
    TypeRef     type;
    SymKind     kind;
    bool        is_mut    = false;
    int32_t     stack_off = 0;  // offset from rbp
    bool        is_global = false;
    std::string label;          // for globals/fns
};

struct Scope {
    std::unordered_map<std::string, Symbol> syms;
    Scope* parent = nullptr;

    Symbol* lookup(const std::string& n) {
        auto it = syms.find(n);
        if (it != syms.end()) return &it->second;
        if (parent) return parent->lookup(n);
        return nullptr;
    }

    void define(Symbol s) { syms[s.name] = std::move(s); }
};

// ─── ERROR SYSTEM ───────────────────────────────────────────
enum class ErrLevel { Note, Warn, Error, Fatal };

struct Diagnostic {
    ErrLevel    level;
    std::string msg;
    uint32_t    line, col;
    std::string file;
    std::string hint;
};

} // namespace dtp
