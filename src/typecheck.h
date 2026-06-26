#ifndef TYPECHECK_H
#define TYPECHECK_H

#include "ast.h"

Type *type_alloc(TypeKind kind);
Type *type_primitive(TypeKind kind, int is_signed);
Type *type_pointer(Type *to);
Type *type_array(Type *elem, int len);
Type *type_func(Type *ret, Type **params, char **param_names, int nparams, int is_variadic);
Type *type_copy(Type *src);
int type_size(Type *t);
int type_align(Type *t);
int type_is_integer(Type *t);
int type_is_arithmetic(Type *t);
int type_is_scalar(Type *t);
int type_is_complete(Type *t);
int type_equal(Type *a, Type *b);
Type *type_unqual(Type *t);

#endif
