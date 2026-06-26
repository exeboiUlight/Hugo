#include "typecheck.h"

Type *type_alloc(TypeKind kind) {
    Type *t = (Type*)xcalloc(1, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_primitive(TypeKind kind, int is_signed) {
    Type *t = type_alloc(kind);
    t->is_signed = is_signed;
    switch (kind) {
        case TYPE_VOID:       t->size = 0; break;
        case TYPE_BOOL:       t->size = 1; break;
        case TYPE_CHAR:       t->size = 1; break;
        case TYPE_SCHAR:      t->size = 1; break;
        case TYPE_UCHAR:      t->size = 1; break;
        case TYPE_SHORT:      t->size = 2; break;
        case TYPE_USHORT:     t->size = 2; break;
        case TYPE_INT:        t->size = 4; break;
        case TYPE_UINT:       t->size = 4; break;
        case TYPE_LONG:       t->size = 8; break;
        case TYPE_ULONG:      t->size = 8; break;
        case TYPE_LONG_LONG:  t->size = 8; break;
        case TYPE_ULONG_LONG: t->size = 8; break;
        case TYPE_FLOAT:      t->size = 4; break;
        case TYPE_DOUBLE:     t->size = 8; break;
        case TYPE_LONG_DOUBLE: t->size = 16; break;
        case TYPE_CHAR8_T:    t->size = 1; break;
        case TYPE_CHAR16_T:   t->size = 2; break;
        case TYPE_CHAR32_T:   t->size = 4; break;
        case TYPE_WCHAR_T:    t->size = 4; break;
        default:              t->size = 0; break;
    }
    return t;
}

Type *type_pointer(Type *to) {
    Type *t = type_alloc(TYPE_PTR);
    t->ptr_to = to;
    t->size = 8; /* x86-64 */
    return t;
}

Type *type_array(Type *elem, int len) {
    Type *t = type_alloc(TYPE_ARRAY);
    t->elem_of = elem;
    t->array_len = len;
    if (len > 0) t->size = elem->size * len;
    else t->size = 0;
    return t;
}

Type *type_func(Type *ret, Type **params, char **param_names, int nparams, int is_variadic) {
    Type *t = type_alloc(TYPE_FUNC);
    t->ret_of = ret;
    t->params_of = params;
    t->param_names = param_names;
    t->nparams = nparams;
    t->is_variadic = is_variadic;
    t->size = 0;
    return t;
}

Type *type_copy(Type *src) {
    if (!src) return NULL;
    Type *t = (Type*)xmalloc(sizeof(Type));
    memcpy(t, src, sizeof(Type));
    t->params_of = NULL;
    t->member_names = NULL;
    t->members = NULL;
    t->member_offsets = NULL;
    t->enum_members = NULL;
    return t;
}

int type_size(Type *t) {
    if (!t) return 0;
    if (t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) return t->total_size;
    if (t->kind == TYPE_BITINT) return (t->bitint_width + 7) / 8;
    return t->size;
}

int type_align(Type *t) {
    if (!t) return 1;
    switch (t->kind) {
        case TYPE_CHAR: case TYPE_SCHAR: case TYPE_UCHAR: case TYPE_BOOL: case TYPE_CHAR8_T:
            return 1;
        case TYPE_SHORT: case TYPE_USHORT: case TYPE_CHAR16_T:
            return 2;
        case TYPE_INT: case TYPE_UINT: case TYPE_FLOAT: case TYPE_CHAR32_T: case TYPE_WCHAR_T:
            return 4;
        case TYPE_LONG: case TYPE_ULONG: case TYPE_LONG_LONG: case TYPE_ULONG_LONG:
        case TYPE_DOUBLE: case TYPE_PTR:
            return 8;
        case TYPE_LONG_DOUBLE:
            return 16;
        case TYPE_BITINT: {
            int sz = type_size(t);
            if (sz >= 8) return 8;
            if (sz >= 4) return 4;
            if (sz >= 2) return 2;
            return 1;
        }
        case TYPE_ARRAY:
            return type_align(t->elem_of);
        case TYPE_STRUCT:
        case TYPE_UNION:
            return t->total_size > 0 ? 8 : 1; /* simplified */
        default:
            return 8;
    }
}

int type_is_integer(Type *t) {
    if (!t) return 0;
    switch (t->kind) {
        case TYPE_BOOL: case TYPE_CHAR: case TYPE_SCHAR: case TYPE_UCHAR:
        case TYPE_SHORT: case TYPE_USHORT: case TYPE_INT: case TYPE_UINT:
        case TYPE_LONG: case TYPE_ULONG: case TYPE_LONG_LONG: case TYPE_ULONG_LONG:
        case TYPE_CHAR8_T: case TYPE_CHAR16_T: case TYPE_CHAR32_T: case TYPE_WCHAR_T:
            return 1;
        default: return 0;
    }
}

int type_is_arithmetic(Type *t) {
    return type_is_integer(t) || (t && (t->kind == TYPE_FLOAT || t->kind == TYPE_DOUBLE || t->kind == TYPE_LONG_DOUBLE));
}

int type_is_scalar(Type *t) {
    return type_is_arithmetic(t) || (t && t->kind == TYPE_PTR);
}

int type_is_complete(Type *t) {
    if (!t) return 0;
    switch (t->kind) {
        case TYPE_VOID: return 0;
        case TYPE_ARRAY: return t->array_len > 0 && type_is_complete(t->elem_of);
        case TYPE_STRUCT: case TYPE_UNION: return t->total_size > 0;
        default: return t->size > 0;
    }
}

int type_equal(Type *a, Type *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    if (a->is_const != b->is_const) return 0;
    if (a->kind == TYPE_PTR) return type_equal(a->ptr_to, b->ptr_to);
    if (a->kind == TYPE_ARRAY) return a->array_len == b->array_len && type_equal(a->elem_of, b->elem_of);
    if (a->kind == TYPE_FUNC) {
        if (!type_equal(a->ret_of, b->ret_of)) return 0;
        if (a->nparams != b->nparams) return 0;
        for (int i = 0; i < a->nparams; i++)
            if (!type_equal(a->params_of[i], b->params_of[i])) return 0;
        return 1;
    }
    return 1; /* same kind for primitives */
}

Type *type_unqual(Type *t) {
    if (!t) return NULL;
    Type *c = type_copy(t);
    c->is_const = 0;
    c->is_volatile = 0;
    c->is_restrict = 0;
    c->is_atomic = 0;
    if (c->kind == TYPE_ARRAY && c->elem_of)
        c->elem_of = type_unqual(c->elem_of);
    if (c->kind == TYPE_PTR && c->ptr_to)
        c->ptr_to = type_unqual(c->ptr_to);
    return c;
}
