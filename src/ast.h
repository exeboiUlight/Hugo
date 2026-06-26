#ifndef AST_H
#define AST_H

#include "utils.h"

/* Types */
typedef enum {
    TYPE_VOID, TYPE_BOOL, TYPE_CHAR, TYPE_SCHAR, TYPE_UCHAR,
    TYPE_SHORT, TYPE_USHORT, TYPE_INT, TYPE_UINT,
    TYPE_LONG, TYPE_ULONG, TYPE_LONG_LONG, TYPE_ULONG_LONG,
    TYPE_FLOAT, TYPE_DOUBLE, TYPE_LONG_DOUBLE,
    TYPE_CHAR8_T, TYPE_CHAR16_T, TYPE_CHAR32_T, TYPE_WCHAR_T,
    TYPE_PTR, TYPE_ARRAY, TYPE_FUNC, TYPE_STRUCT, TYPE_UNION,
    TYPE_ENUM, TYPE_TYPEDEF_NAME, TYPE_VOID_PTR, TYPE_BITINT,
} TypeKind;

typedef struct Type {
    TypeKind kind;
    int is_const;
    int is_volatile;
    int is_restrict;
    int is_atomic;
    int is_signed;
    int bitint_width; /* for _BitInt(N) */
    int size; /* 0 = unknown */
    struct Type *ptr_to;
    struct Type *elem_of;
    int array_len;
    struct Type *ret_of;
    struct Type **params_of;
    char **param_names;
    int nparams;
    int is_variadic;
    const char *struct_name;
    struct Type **members;
    char **member_names;
    int nmembers;
    int *member_offsets;
    int total_size;
    const char *enum_name;
    struct { const char *name; long long val; } *enum_members;
    int nenums;
    const char *typedef_name;
} Type;

typedef struct AstNode AstNode;

typedef enum {
    /* declarations */
    NOD_FUNC_DECL, NOD_FUNC_DEF, NOD_VAR_DECL, NOD_VAR_DEF,
    NOD_STRUCT_DECL, NOD_UNION_DECL, NOD_ENUM_DECL,
    NOD_TYPEDEF, NOD_STATIC_ASSERT, NOD_ATTRIBUTE,

    /* statements */
    NOD_COMPOUND, NOD_IF, NOD_WHILE, NOD_DO, NOD_FOR,
    NOD_SWITCH, NOD_CASE, NOD_DEFAULT, NOD_BREAK, NOD_CONTINUE,
    NOD_RETURN, NOD_GOTO, NOD_LABEL, NOD_EXPR_STMT, NOD_NULL_STMT,

    /* expressions */
    NOD_INT_CONST, NOD_FLOAT_CONST, NOD_STRING_CONST, NOD_CHAR_CONST,
    NOD_BOOL_CONST, NOD_NULLPTR_CONST,
    NOD_IDENT, NOD_BINARY, NOD_UNARY, NOD_POSTFIX,
    NOD_CONDITIONAL,
    NOD_ASSIGN, NOD_CALL, NOD_CAST, NOD_IMPLICIT_CAST,
    NOD_MEMBER_ACCESS, NOD_MEMBER_DEREF,
    NOD_ARRAY_INDEX, NOD_ADDR_OF, NOD_DEREF,
    NOD_SIZEOF, NOD_ALIGNOF, NOD_TYPEOF,
    NOD_COMMA,
    NOD_INIT_LIST,
} NodeKind;

typedef struct AstNode {
    NodeKind kind;
    Type *type;
    const char *file;
    int line, col;

    union {
        /* declarations */
        struct {
            const char *name;
            Type *func_type;
            AstNode *body;         /* compound statement for definition */
            AstNode **params;
            int nparams;
            int is_definition;
            int is_static;
            int is_extern;
            int is_inline;
            int is_thread_local;
            int storage_class; /* 0=none, 1=typedef, 2=extern, 3=static, 4=register, 5=auto */
        } func;

        struct {
            const char *name;
            Type *var_type;
            AstNode *init;
            int is_static;
            int is_extern;
            int is_thread_local;
            int is_constexpr;
            int alignment;
        } var;

        struct {
            const char *name;
            AstNode **members;
            int nmembers;
            int is_complete;
        } struct_decl;

        struct {
            const char *name;
            AstNode **members;
            int nmembers;
            int is_complete;
        } union_decl;

        struct {
            const char *name;
            struct { const char *name; AstNode *val; } *members;
            int nmembers;
            int is_complete;
        } enum_decl;

        struct {
            const char *name;
            Type *type;
        } typedef_decl;

        struct {
            AstNode *expr;
            const char *msg;
        } static_assertion;

        /* statements */
        struct {
            AstNode **stmts;
            int nstmts;
        } compound;

        struct {
            AstNode *cond;
            AstNode *then;
            AstNode *els;
        } if_stmt;

        struct {
            AstNode *cond;
            AstNode *body;
        } while_stmt;

        struct {
            AstNode *body;
            AstNode *cond;
        } do_stmt;

        struct {
            AstNode *init;
            AstNode *cond;
            AstNode *incr;
            AstNode *body;
        } for_stmt;

        struct {
            AstNode *expr;
            AstNode *body;
        } switch_stmt;

        struct {
            AstNode *expr;
            AstNode *stmt;
        } case_stmt;

        struct {
            AstNode *stmt;
        } default_stmt;

        struct {
            AstNode *expr;
        } return_stmt;

        struct {
            const char *label;
        } goto_stmt;

        struct {
            const char *name;
            AstNode *stmt;
        } label;

        struct {
            AstNode *expr;
        } expr_stmt;

        /* expressions */
        long long int_val;
        double float_val;
        const char *string_val;
        size_t string_len;
        int char_val;
        int bool_val;

        struct {
            const char *name;
        } ident;

        struct {
            int op; /* 0=+, 1=-, 2=*, 3=/, 4=%, 5=&, 6=|, 7=^, 8=<<, 9=>>,
                       10=&&, 11=||, 12==, 13!=, 14<, 15>, 16<=, 17>=, 18=, */
            AstNode *left, *right;
        } binary;

        struct {
            int op; /* 0=pre++, 1=pre--, 2=+, 3=-, 4=!, 5=~, 6=&, 7=*,
                       8=post++, 9=post-- */
            AstNode *operand;
        } unary;

        struct {
            int op; /* 0=+=, 1=-=, 2=*=, 3=/=, 4=%=, 5=&=, 6=|=, 7=^=,
                       8=<<=, 9=>>= */
            AstNode *left, *right;
        } assign;

        struct {
            AstNode *func;
            AstNode **args;
            int nargs;
        } call;

        struct {
            AstNode *operand;
            Type *to_type;
        } cast;

        struct {
            AstNode *struct_ptr;
            const char *member_name;
        } member;

        struct {
            AstNode *arr;
            AstNode *index;
        } array_index;

        struct {
            AstNode *cond;
            AstNode *then;
            AstNode *els;
        } conditional;

        struct {
            AstNode **exprs;
            int nexprs;
        } comma;

        struct {
            AstNode **values;
            int nvalues;
        } init_list;

        struct {
            AstNode *operand;
        } sizeof_expr;

        struct {
            Type *type;
        } typeof_expr;
    } data;
} AstNode;

AstNode *ast_alloc(NodeKind kind);
void ast_free(AstNode *node);
void ast_free_deep(AstNode *node);

#endif
