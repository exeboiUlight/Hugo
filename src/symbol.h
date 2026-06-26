#ifndef SYMBOL_H
#define SYMBOL_H

#include "ast.h"

typedef struct Symbol {
    char *name;
    Type *type;
    int is_function;
    int is_typedef;
    int is_static;
    int is_extern;
    int is_constexpr;
    int alignment;
    int defined;
    /* for codegen */
    int stack_offset;
    int global_index;
    struct Symbol *next; /* hash chain */
} Symbol;

typedef struct Scope {
    Symbol **hash;
    struct Scope *parent;
    struct Scope *next_child;
    struct Scope *first_child;
    int level;
} Scope;

typedef struct {
    Scope *current;
    int stack_size;
    int global_count;
} SymbolTable;

SymbolTable *sym_table_new(void);
void sym_table_free(SymbolTable *st);
void sym_enter_scope(SymbolTable *st);
void sym_leave_scope(SymbolTable *st);
Symbol *sym_lookup(SymbolTable *st, const char *name);
Symbol *sym_lookup_current(Scope *s, const char *name);
Symbol *sym_put(SymbolTable *st, const char *name, Type *type, int is_func, int is_typedef);
Symbol *sym_global(SymbolTable *st, const char *name, Type *type, int is_func);
int sym_is_defined(SymbolTable *st, const char *name);

#endif
