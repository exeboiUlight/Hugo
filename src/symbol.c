#include "symbol.h"
#include <string.h>

static unsigned hash_str(const char *s) {
    unsigned h = 0;
    while (*s) h = h * 65599 + (unsigned char)*s++;
    return h % HASH_SIZE;
}

static Scope *scope_new(Scope *parent, int level) {
    Scope *s = (Scope*)xcalloc(1, sizeof(Scope));
    s->hash = (Symbol**)xcalloc(HASH_SIZE, sizeof(Symbol*));
    s->parent = parent;
    s->level = level;
    /* add to parent's child list */
    if (parent) {
        s->next_child = parent->first_child;
        parent->first_child = s;
    }
    return s;
}

static void scope_free(Scope *s) {
    if (!s) return;
    for (int i = 0; i < HASH_SIZE; i++) {
        Symbol *sym = s->hash[i];
        while (sym) {
            Symbol *next = sym->next;
            free(sym->name);
            free(sym);
            sym = next;
        }
    }
    free(s->hash);
    free(s);
}

SymbolTable *sym_table_new(void) {
    SymbolTable *st = (SymbolTable*)xcalloc(1, sizeof(SymbolTable));
    st->current = scope_new(NULL, 0);
    st->stack_size = 0;
    st->global_count = 0;
    return st;
}

void sym_table_free(SymbolTable *st) {
    while (st->current) {
        Scope *p = st->current->parent;
        scope_free(st->current);
        st->current = p;
    }
    free(st);
}

void sym_enter_scope(SymbolTable *st) {
    st->current = scope_new(st->current, st->current->level + 1);
}

void sym_leave_scope(SymbolTable *st) {
    if (st->current->parent) {
        st->current = st->current->parent;
        /* don't free - keep scopes alive for codegen */
    }
}

static Symbol *sym_lookup_recursive(Scope *s, const char *name) {
    if (!s) return NULL;
    Symbol *sym = sym_lookup_current(s, name);
    if (sym) return sym;
    /* search children scopes recursively */
    for (Scope *child = s->first_child; child; child = child->next_child) {
        sym = sym_lookup_recursive(child, name);
        if (sym) return sym;
    }
    return NULL;
}

Symbol *sym_lookup(SymbolTable *st, const char *name) {
    return sym_lookup_recursive(st->current, name);
}

Symbol *sym_lookup_current(Scope *s, const char *name) {
    unsigned h = hash_str(name);
    Symbol *sym = s->hash[h];
    while (sym) {
        if (strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

Symbol *sym_put(SymbolTable *st, const char *name, Type *type, int is_func, int is_typedef) {
    unsigned h = hash_str(name);
    Symbol *sym = (Symbol*)xcalloc(1, sizeof(Symbol));
    sym->name = xstrdup(name);
    sym->type = type;
    sym->is_function = is_func;
    sym->is_typedef = is_typedef;
    sym->stack_offset = 0;
    sym->global_index = 0;
    sym->next = st->current->hash[h];
    st->current->hash[h] = sym;
    return sym;
}

Symbol *sym_global(SymbolTable *st, const char *name, Type *type, int is_func) {
    /* find root scope */
    Scope *s = st->current;
    while (s->parent) s = s->parent;
    unsigned h = hash_str(name);
    Symbol *sym = (Symbol*)xcalloc(1, sizeof(Symbol));
    sym->name = xstrdup(name);
    sym->type = type;
    sym->is_function = is_func;
    sym->global_index = st->global_count++;
    /* put at root scope */
    sym->next = s->hash[h];
    s->hash[h] = sym;
    return sym;
}

int sym_is_defined(SymbolTable *st, const char *name) {
    Symbol *s = sym_lookup(st, name);
    return s && s->defined;
}
