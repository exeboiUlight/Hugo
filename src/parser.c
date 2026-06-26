#include "parser.h"
#include "typecheck.h"

/* forward declarations */
static AstNode *parse_declaration(Parser *p, int is_global);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_expr(Parser *p);
static AstNode *parse_assignment(Parser *p);
static AstNode *parse_conditional(Parser *p);
static AstNode *parse_logical_or(Parser *p);
static AstNode *parse_logical_and(Parser *p);
static AstNode *parse_inclusive_or(Parser *p);
static AstNode *parse_exclusive_or(Parser *p);
static AstNode *parse_and(Parser *p);
static AstNode *parse_equality(Parser *p);
static AstNode *parse_relational(Parser *p);
static AstNode *parse_shift(Parser *p);
static AstNode *parse_additive(Parser *p);
static AstNode *parse_multiplicative(Parser *p);
static AstNode *parse_cast(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);
static Type *parse_type(Parser *p, int allow_typedef);
static Type *parse_declarator(Parser *p, Type *base, const char **name_out);
static Type *parse_abstract_declarator(Parser *p, Type *base);

static Token *peek_tok(Parser *p) {
    if (p->pos < p->ntokens) return &p->tokens[p->pos];
    return NULL;
}

static Token *expect(Parser *p, TokenKind kind) {
    if (p->pos < p->ntokens && p->tokens[p->pos].kind == kind) {
        return &p->tokens[p->pos++];
    }
    Token *t = peek_tok(p);
    diag(DIAG_ERROR, p->file, t ? t->line : 0, t ? t->col : 0,
         "expected %s, got %s", token_name(kind), t ? token_name(t->kind) : "EOF");
    return NULL;
}

static int peek_type_spec(Parser *p) {
    TokenKind k = peek_tok(p) ? peek_tok(p)->kind : TOK_EOF;
    switch (k) {
        case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT: case TOK_LONG:
        case TOK_FLOAT: case TOK_DOUBLE: case TOK_SIGNED: case TOK_UNSIGNED:
        case TOK__BOOL: case TOK_BOOL: case TOK__COMPLEX:
        case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
        case TOK_TYPEDEF: case TOK_CONST: case TOK_VOLATILE: case TOK_RESTRICT:
        case TOK_EXTERN: case TOK_STATIC: case TOK_REGISTER: case TOK_AUTO:
        case TOK_INLINE: case TOK__NORETURN: case TOK__THREAD_LOCAL:
        case TOK_CONSTEXPR: case TOK_TYPEOF: case TOK_TYPEOF_UNQUAL:
        case TOK_CHAR8_T: case TOK_CHAR16_T: case TOK_CHAR32_T: case TOK_WCHAR_T:
        case TOK__ATOMIC: case TOK_ALIGNAS: case TOK__ALIGNAS:
        case TOK__STATIC_ASSERT: case TOK_STATIC_ASSERT:
        case TOK_THREAD_LOCAL: case TOK_NORETURN:
        case TOK_ATOMIC: case TOK_BITINT:
            return 1;
        case TOK_IDENT: {
            /* could be typedef name */
            Symbol *sym = sym_lookup(p->sym, peek_tok(p)->sval);
            return sym && sym->is_typedef;
        }
        default:
            return 0;
    }
}

static void skip_to_semicolon(Parser *p) {
    while (p->pos < p->ntokens && p->tokens[p->pos].kind != TOK_SEMICOLON && p->tokens[p->pos].kind != TOK_EOF)
        p->pos++;
    if (p->pos < p->ntokens) p->pos++;
}

static void add_global(Parser *p, AstNode *node) {
    if (p->nglobal_decls >= p->cap_global_decls) {
        p->cap_global_decls = p->cap_global_decls ? p->cap_global_decls * 2 : 64;
        p->global_decls = (AstNode**)xrealloc(p->global_decls, p->cap_global_decls * sizeof(AstNode*));
    }
    p->global_decls[p->nglobal_decls++] = node;
}

/* type parsing */
static Type *parse_type_spec(Parser *p, int allow_typedef) {
    Token *t = peek_tok(p);
    if (!t) return NULL;

    Type *type = NULL;
    int is_unsigned = 0;
    int is_long = 0;
    int is_long_long = 0;
    int is_short = 0;
    int is_const = 0;
    int is_volatile = 0;
    int is_restrict = 0;
    int is_atomic = 0;

    while (1) {
        t = peek_tok(p);
        if (!t) break;

        switch (t->kind) {
            case TOK_CONST: is_const = 1; p->pos++; continue;
            case TOK_VOLATILE: is_volatile = 1; p->pos++; continue;
            case TOK_RESTRICT: is_restrict = 1; p->pos++; continue;
            case TOK__ATOMIC: case TOK_ATOMIC: is_atomic = 1; p->pos++; continue;
            case TOK_SIGNED: is_unsigned = 0; p->pos++; continue;
            case TOK_UNSIGNED: is_unsigned = 1; p->pos++; continue;
            case TOK_SHORT: is_short = 1; p->pos++; continue;
            case TOK_LONG:
                if (is_long) is_long_long = 1;
                else is_long = 1;
                p->pos++;
                continue;
            default: break;
        }
        break;
    }

    t = peek_tok(p);
    if (!t) return type_primitive(TYPE_INT, 1);

    switch (t->kind) {
        case TOK_VOID: p->pos++; type = type_primitive(TYPE_VOID, 1); break;
        case TOK_CHAR: p->pos++; type = type_primitive(is_unsigned ? TYPE_UCHAR : TYPE_SCHAR, !is_unsigned); break;
        case TOK_INT: p->pos++;
            if (is_long_long) type = type_primitive(is_unsigned ? TYPE_ULONG_LONG : TYPE_LONG_LONG, !is_unsigned);
            else if (is_long) type = type_primitive(is_unsigned ? TYPE_ULONG : TYPE_LONG, !is_unsigned);
            else if (is_short) type = type_primitive(is_unsigned ? TYPE_USHORT : TYPE_SHORT, !is_unsigned);
            else type = type_primitive(is_unsigned ? TYPE_UINT : TYPE_INT, !is_unsigned);
            break;
        case TOK_FLOAT: p->pos++;
            if (is_long) type = type_primitive(TYPE_LONG_DOUBLE, 1);
            else type = type_primitive(TYPE_FLOAT, 1);
            break;
        case TOK_DOUBLE: p->pos++;
            if (is_long) type = type_primitive(TYPE_LONG_DOUBLE, 1);
            else type = type_primitive(TYPE_DOUBLE, 1);
            break;
        case TOK_BOOL: case TOK__BOOL: p->pos++; type = type_primitive(TYPE_BOOL, 0); break;
        case TOK_CHAR8_T: p->pos++; type = type_primitive(TYPE_CHAR8_T, 0); break;
        case TOK_CHAR16_T: p->pos++; type = type_primitive(TYPE_CHAR16_T, 0); break;
        case TOK_CHAR32_T: p->pos++; type = type_primitive(TYPE_CHAR32_T, 0); break;
        case TOK_WCHAR_T: p->pos++; type = type_primitive(TYPE_WCHAR_T, 0); break;
        case TOK__COMPLEX:
            diag(DIAG_ERROR, p->file, t->line, t->col, "_Complex not supported");
            p->pos++; type = type_primitive(TYPE_DOUBLE, 1); break;
        case TOK_STRUCT: {
            p->pos++;
            const char *name = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                name = xstrdup(peek_tok(p)->sval);
                p->pos++;
            }
            AstNode **members = NULL;
            int nmembers = 0;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACE) {
                p->pos++;
                while (peek_tok(p) && peek_tok(p)->kind != TOK_RBRACE && peek_tok(p)->kind != TOK_EOF) {
                    Type *mt = parse_type(p, 1);
                    while (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                        const char *mname = xstrdup(peek_tok(p)->sval);
                        p->pos++;
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACKET) {
                            p->pos++;
                            int len = 1;
                            if (peek_tok(p) && peek_tok(p)->kind == TOK_INTEGER) { len = (int)peek_tok(p)->ival; p->pos++; }
                            mt = type_array(mt, len);
                            expect(p, TOK_RBRACKET);
                        }
                        /* store member info - simplified, just use dummy */
                        AstNode *mn = ast_alloc(NOD_VAR_DECL);
                        mn->data.var.name = mname;
                        mn->data.var.var_type = mt;
                        members = (AstNode**)xrealloc(members, (nmembers + 1) * sizeof(AstNode*));
                        members[nmembers++] = mn;
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) p->pos++;
                    }
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_SEMICOLON) p->pos++;
                }
                if (peek_tok(p) && peek_tok(p)->kind == TOK_RBRACE) p->pos++;
            }
            type = type_alloc(TYPE_STRUCT);
            type->struct_name = name;
            if (nmembers > 0) {
                type->members = (Type**)xcalloc(nmembers, sizeof(Type*));
                type->member_names = (char**)xcalloc(nmembers, sizeof(char*));
                for (int i = 0; i < nmembers; i++) {
                    type->members[i] = members[i]->data.var.var_type;
                    type->member_names[i] = (char*)members[i]->data.var.name;
                }
            }
            type->nmembers = nmembers;
            /* compute offsets */
            int offset = 0;
            for (int i = 0; i < nmembers; i++) {
                int a = type_align(type->members[i]);
                offset = (offset + a - 1) & ~(a - 1);
                type->member_offsets = (int*)xrealloc(type->member_offsets, (i + 1) * sizeof(int));
                type->member_offsets[i] = offset;
                offset += type_size(type->members[i]);
            }
            type->total_size = offset;
            free(members);
            break;
        }
        case TOK_UNION: {
            p->pos++;
            const char *name = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                name = xstrdup(peek_tok(p)->sval);
                p->pos++;
            }
            AstNode **members_u = NULL;
            int nmembers_u = 0;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACE) {
                p->pos++;
                int max_size = 0;
                while (peek_tok(p) && peek_tok(p)->kind != TOK_RBRACE && peek_tok(p)->kind != TOK_EOF) {
                    Type *mt = parse_type(p, 1);
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                        const char *mname = xstrdup(peek_tok(p)->sval);
                        p->pos++;
                        AstNode *mn = ast_alloc(NOD_VAR_DECL);
                        mn->data.var.name = mname;
                        mn->data.var.var_type = mt;
                        members_u = (AstNode**)xrealloc(members_u, (nmembers_u + 1) * sizeof(AstNode*));
                        members_u[nmembers_u++] = mn;
                        if (type_size(mt) > max_size) max_size = type_size(mt);
                    }
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_SEMICOLON) p->pos++;
                }
                if (peek_tok(p) && peek_tok(p)->kind == TOK_RBRACE) p->pos++;
                type = type_alloc(TYPE_UNION);
                type->struct_name = name;
                type->members = (Type**)xcalloc(nmembers_u, sizeof(Type*));
                type->member_names = (char**)xcalloc(nmembers_u, sizeof(char*));
                for (int i = 0; i < nmembers_u; i++) {
                    type->members[i] = members_u[i]->data.var.var_type;
                    type->member_names[i] = (char*)members_u[i]->data.var.name;
                }
                type->nmembers = nmembers_u;
                type->total_size = max_size;
                free(members_u);
            } else {
                type = type_alloc(TYPE_UNION);
                type->struct_name = name;
                type->nmembers = 0;
                type->total_size = 0;
            }
            break;
        }
        case TOK_ENUM: {
            p->pos++;
            const char *name = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                name = xstrdup(peek_tok(p)->sval);
                p->pos++;
            }
            type = type_alloc(TYPE_ENUM);
            type->enum_name = name;
            type->size = 4;
            long long cur_val = 0;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACE) {
                p->pos++;
                int nm = 0;
                while (peek_tok(p) && peek_tok(p)->kind != TOK_RBRACE && peek_tok(p)->kind != TOK_EOF) {
                    Token *et = peek_tok(p);
                    if (et->kind != TOK_IDENT) { p->pos++; continue; }
                    const char *ename = xstrdup(et->sval); p->pos++;
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_EQ) {
                        p->pos++;
                        AstNode *val_expr = parse_assignment(p);
                        if (val_expr && val_expr->kind == NOD_INT_CONST) cur_val = val_expr->data.int_val;
                    }
                    /* add enum member to table */
                    Type *etp = type_primitive(TYPE_INT, 1);
                    Symbol *esym = sym_put(p->sym, ename, etp, 0, 0);
                    esym->defined = 1;
                    type->enum_members = xrealloc(
                        type->enum_members, (nm + 1) * sizeof(*type->enum_members));
                    type->enum_members[nm].name = ename;
                    type->enum_members[nm].val = cur_val;
                    nm++;
                    cur_val++;
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) p->pos++;
                }
                type->nenums = nm;
                if (peek_tok(p) && peek_tok(p)->kind == TOK_RBRACE) p->pos++;
            }
            break;
        }
        case TOK_TYPEOF: case TOK_TYPEOF_UNQUAL: {
            int is_unqual = (t->kind == TOK_TYPEOF_UNQUAL);
            p->pos++;
            expect(p, TOK_LPAREN);
            Type *te = NULL;
            int saved = p->pos;
            Type *maybe_type = parse_type(p, 1);
            if (maybe_type && peek_tok(p) && peek_tok(p)->kind == TOK_RPAREN) {
                te = maybe_type;
            } else {
                p->pos = saved;
                AstNode *e = parse_expr(p);
                if (e) te = e->type;
            }
            expect(p, TOK_RPAREN);
            if (is_unqual && te)
                type = type_unqual(te);
            else
                type = te ? type_copy(te) : type_primitive(TYPE_INT, 1);
            break;
        }
        case TOK_BITINT: {
            p->pos++;
            expect(p, TOK_LPAREN);
            int width = 32;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_INTEGER) {
                width = (int)peek_tok(p)->ival;
                p->pos++;
            }
            expect(p, TOK_RPAREN);
            type = type_alloc(TYPE_BITINT);
            type->bitint_width = width;
            type->is_signed = !is_unsigned;
            type->size = (width + 7) / 8;
            break;
        }
        default: {
            if (t->kind == TOK_IDENT && allow_typedef) {
                Symbol *sym = sym_lookup(p->sym, t->sval);
                if (sym && sym->is_typedef) {
                    p->pos++;
                    type = type_copy(sym->type);
                    break;
                }
            }
            /* default to int */
            if (is_long_long) type = type_primitive(is_unsigned ? TYPE_ULONG_LONG : TYPE_LONG_LONG, !is_unsigned);
            else if (is_long) type = type_primitive(is_unsigned ? TYPE_ULONG : TYPE_LONG, !is_unsigned);
            else if (is_short) type = type_primitive(is_unsigned ? TYPE_USHORT : TYPE_SHORT, !is_unsigned);
            else type = type_primitive(is_unsigned ? TYPE_UINT : TYPE_INT, !is_unsigned);
            break;
        }
    }

    if (!type) type = type_primitive(TYPE_INT, 1);
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    type->is_restrict = is_restrict;
    type->is_atomic = is_atomic;
    return type;
}

static Type *parse_declarator(Parser *p, Type *base, const char **name_out) {
    Type *t = base;
    *name_out = NULL;

    if (peek_tok(p) && peek_tok(p)->kind == TOK_STAR) {
        p->pos++;
        while (peek_tok(p) && (peek_tok(p)->kind == TOK_CONST || peek_tok(p)->kind == TOK_VOLATILE || peek_tok(p)->kind == TOK_RESTRICT)) {
            /* consume qualifiers */
            p->pos++;
        }
        t = type_pointer(t);
        /* continue for more pointers */
        if (peek_tok(p) && peek_tok(p)->kind == TOK_STAR) {
            return parse_declarator(p, t, name_out);
        }
    }

    if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
        *name_out = xstrdup(peek_tok(p)->sval);
        p->pos++;
    }

    while (1) {
        if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACKET) {
            p->pos++;
            int len = 0;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_INTEGER) {
                len = (int)peek_tok(p)->ival;
                p->pos++;
            }
            expect(p, TOK_RBRACKET);
            t = type_array(t, len);
        } else if (peek_tok(p) && peek_tok(p)->kind == TOK_LPAREN) {
            /* function */
            p->pos++;
            Type **params = NULL;
            char **pnames = NULL;
            int nparams = 0;
            int is_variadic = 0;
            if (peek_tok(p) && peek_tok(p)->kind != TOK_RPAREN) {
                if (peek_tok(p) && peek_tok(p)->kind == TOK_VOID && (p->pos + 1 < p->ntokens) && p->tokens[p->pos + 1].kind == TOK_RPAREN) {
                    p->pos++; /* consume void */
                } else {
                    while (1) {
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_ELLIPSIS) {
                            is_variadic = 1;
                            p->pos++;
                            break;
                        }
                        Type *pt = parse_type(p, 1);
                        const char *pname = NULL;
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_IDENT) {
                            pname = xstrdup(peek_tok(p)->sval);
                            p->pos++;
                        }
                        params = (Type**)xrealloc(params, (nparams + 1) * sizeof(Type*));
                        pnames = (char**)xrealloc(pnames, (nparams + 1) * sizeof(char*));
                        params[nparams] = pt;
                        pnames[nparams] = (char*)pname;
                        nparams++;
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) p->pos++;
                        else break;
                    }
                }
            }
            expect(p, TOK_RPAREN);
            t = type_func(t, params, pnames, nparams, is_variadic);
            break;
        } else {
            break;
        }
    }
    return t;
}

static Type *parse_abstract_declarator(Parser *p, Type *base) {
    Type *t = base;

    if (peek_tok(p) && peek_tok(p)->kind == TOK_STAR) {
        p->pos++;
        t = type_pointer(t);
        return parse_abstract_declarator(p, t);
    }

    if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACKET) {
        p->pos++;
        int len = 0;
        if (peek_tok(p) && peek_tok(p)->kind == TOK_INTEGER) { len = (int)peek_tok(p)->ival; p->pos++; }
        expect(p, TOK_RBRACKET);
        t = type_array(t, len);
        return parse_abstract_declarator(p, t);
    }

    if (peek_tok(p) && peek_tok(p)->kind == TOK_LPAREN) {
        p->pos++;
        Type **params = NULL;
        int nparams = 0;
        int is_variadic = 0;
        while (peek_tok(p) && peek_tok(p)->kind != TOK_RPAREN && peek_tok(p)->kind != TOK_EOF) {
            if (peek_tok(p) && peek_tok(p)->kind == TOK_ELLIPSIS) { is_variadic = 1; p->pos++; break; }
            Type *pt = parse_type(p, 1);
            params = (Type**)xrealloc(params, (nparams + 1) * sizeof(Type*));
            params[nparams++] = pt;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) p->pos++;
        }
        expect(p, TOK_RPAREN);
        t = type_func(t, params, NULL, nparams, is_variadic);
    }

    return t;
}

static Type *parse_type(Parser *p, int allow_typedef) {
    return parse_type_spec(p, allow_typedef);
}

/* Expression parsing */
static AstNode *parse_primary(Parser *p) {
    Token *t = peek_tok(p);
    if (!t) return NULL;

    AstNode *node = NULL;

    switch (t->kind) {
        case TOK_INTEGER: {
            node = ast_alloc(NOD_INT_CONST);
            node->data.int_val = t->ival;
            node->type = type_primitive(TYPE_INT, 1);
            p->pos++;
            break;
        }
        case TOK_FLOAT_LIT: {
            node = ast_alloc(NOD_FLOAT_CONST);
            node->data.float_val = t->fval;
            node->type = type_primitive(TYPE_DOUBLE, 1);
            p->pos++;
            break;
        }
        case TOK_STRING: {
            node = ast_alloc(NOD_STRING_CONST);
            node->data.string_val = xstrdup(t->sval);
            node->type = type_pointer(type_primitive(TYPE_CHAR, 0));
            p->pos++;
            break;
        }
        case TOK_CHAR_LIT: {
            node = ast_alloc(NOD_CHAR_CONST);
            node->data.char_val = (int)t->ival;
            node->type = type_primitive(TYPE_INT, 1);
            p->pos++;
            break;
        }
        case TOK_TRUE: {
            node = ast_alloc(NOD_BOOL_CONST);
            node->data.bool_val = 1;
            node->type = type_primitive(TYPE_BOOL, 0);
            p->pos++;
            break;
        }
        case TOK_FALSE: {
            node = ast_alloc(NOD_BOOL_CONST);
            node->data.bool_val = 0;
            node->type = type_primitive(TYPE_BOOL, 0);
            p->pos++;
            break;
        }
        case TOK_NULLPTR: {
            node = ast_alloc(NOD_NULLPTR_CONST);
            node->type = type_pointer(type_primitive(TYPE_VOID, 1));
            p->pos++;
            break;
        }
        case TOK_IDENT: {
            Symbol *sym = sym_lookup(p->sym, t->sval);
            node = ast_alloc(NOD_IDENT);
            node->data.ident.name = xstrdup(t->sval);
            node->type = sym ? sym->type : type_primitive(TYPE_INT, 1);
            p->pos++;
            break;
        }
        case TOK_LPAREN: {
            p->pos++;
            if (peek_type_spec(p)) {
                /* cast or compound literal - handle as parenthesized type */
                Type *type = parse_type(p, 1);
                if (type) {
                    type = parse_abstract_declarator(p, type);
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_RPAREN) {
                        p->pos++;
                        if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACE) {
                            /* compound literal - skip for now */
                            node = ast_alloc(NOD_INT_CONST);
                            node->data.int_val = 0;
                            node->type = type;
                        } else {
                            node = ast_alloc(NOD_CAST);
                            node->data.cast.to_type = type;
                            node->data.cast.operand = parse_cast(p);
                            if (node->data.cast.operand)
                                node->type = type;
                            else
                                node->type = type;
                        }
                    }
                }
            } else {
                node = parse_expr(p);
                expect(p, TOK_RPAREN);
            }
            break;
        }
        case TOK_SIZEOF: {
            p->pos++;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_LPAREN) {
                p->pos++;
                if (peek_type_spec(p)) {
                    Type *st = parse_type(p, 1);
                    st = parse_abstract_declarator(p, st);
                    expect(p, TOK_RPAREN);
                    node = ast_alloc(NOD_INT_CONST);
                    node->data.int_val = type_size(st);
                    node->type = type_primitive(TYPE_ULONG, 0);
                } else {
                    AstNode *e = parse_expr(p);
                    expect(p, TOK_RPAREN);
                    node = ast_alloc(NOD_INT_CONST);
                    node->data.int_val = type_size(e ? e->type : NULL);
                    node->type = type_primitive(TYPE_ULONG, 0);
                }
            } else {
                /* sizeof expression */
                AstNode *e = parse_unary(p);
                node = ast_alloc(NOD_INT_CONST);
                node->data.int_val = type_size(e ? e->type : NULL);
                node->type = type_primitive(TYPE_ULONG, 0);
            }
            break;
        }
        case TOK_ALIGNOF: case TOK__ALIGNOF: {
            p->pos++;
            expect(p, TOK_LPAREN);
            Type *at = parse_type(p, 1);
            at = parse_abstract_declarator(p, at);
            expect(p, TOK_RPAREN);
            node = ast_alloc(NOD_INT_CONST);
            node->data.int_val = type_align(at);
            node->type = type_primitive(TYPE_ULONG, 0);
            break;
        }
        default:
            break;
    }

    return node;
}

static AstNode *parse_postfix(Parser *p) {
    AstNode *node = parse_primary(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;

        if (t->kind == TOK_LBRACKET) {
            p->pos++;
            AstNode *index = parse_expr(p);
            expect(p, TOK_RBRACKET);
            AstNode *arr = ast_alloc(NOD_ARRAY_INDEX);
            arr->data.array_index.arr = node;
            arr->data.array_index.index = index;
            arr->type = node->type && node->type->kind == TYPE_ARRAY ? node->type->elem_of :
                        node->type && node->type->kind == TYPE_PTR ? node->type->ptr_to :
                        type_primitive(TYPE_INT, 1);
            node = arr;
        } else if (t->kind == TOK_LPAREN) {
            p->pos++;
            AstNode **args = NULL;
            int nargs = 0;
            if (peek_tok(p) && peek_tok(p)->kind != TOK_RPAREN) {
                while (1) {
                    AstNode *arg = parse_assignment(p);
                    args = (AstNode**)xrealloc(args, (nargs + 1) * sizeof(AstNode*));
                    args[nargs++] = arg;
                    if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) p->pos++;
                    else break;
                }
            }
            expect(p, TOK_RPAREN);
            AstNode *call = ast_alloc(NOD_CALL);
            call->data.call.func = node;
            call->data.call.args = args;
            call->data.call.nargs = nargs;
            call->type = type_primitive(TYPE_INT, 1);
            if (node && node->type && node->type->kind == TYPE_PTR && node->type->ptr_to && node->type->ptr_to->kind == TYPE_FUNC)
                call->type = node->type->ptr_to->ret_of ? node->type->ptr_to->ret_of : type_primitive(TYPE_INT, 1);
            else if (node && node->type && node->type->kind == TYPE_FUNC)
                call->type = node->type->ret_of ? node->type->ret_of : type_primitive(TYPE_INT, 1);
            node = call;
        } else if (t->kind == TOK_DOT) {
            p->pos++;
            Token *name = expect(p, TOK_IDENT);
            AstNode *ma = ast_alloc(NOD_MEMBER_ACCESS);
            ma->data.member.struct_ptr = node;
            ma->data.member.member_name = name ? xstrdup(name->sval) : NULL;
            ma->type = type_primitive(TYPE_INT, 1);
            /* look up member type */
            if (node->type && (node->type->kind == TYPE_STRUCT || node->type->kind == TYPE_UNION)) {
                for (int i = 0; i < node->type->nmembers; i++) {
                    if (name && node->type->member_names[i] && strcmp(node->type->member_names[i], name->sval) == 0) {
                        ma->type = node->type->members[i];
                        break;
                    }
                }
            }
            node = ma;
        } else if (t->kind == TOK_ARROW) {
            p->pos++;
            Token *name = expect(p, TOK_IDENT);
            AstNode *ma = ast_alloc(NOD_MEMBER_DEREF);
            ma->data.member.struct_ptr = node;
            ma->data.member.member_name = name ? xstrdup(name->sval) : NULL;
            ma->type = type_primitive(TYPE_INT, 1);
            if (node->type && node->type->kind == TYPE_PTR && node->type->ptr_to &&
                (node->type->ptr_to->kind == TYPE_STRUCT || node->type->ptr_to->kind == TYPE_UNION)) {
                for (int i = 0; i < node->type->ptr_to->nmembers; i++) {
                    if (name && node->type->ptr_to->member_names[i] && strcmp(node->type->ptr_to->member_names[i], name->sval) == 0) {
                        ma->type = node->type->ptr_to->members[i];
                        break;
                    }
                }
            }
            node = ma;
        } else if (t->kind == TOK_PLUS_PLUS) {
            p->pos++;
            AstNode *post = ast_alloc(NOD_UNARY);
            post->data.unary.op = 8; /* post++ */
            post->data.unary.operand = node;
            post->type = node->type;
            node = post;
        } else if (t->kind == TOK_MINUS_MINUS) {
            p->pos++;
            AstNode *post = ast_alloc(NOD_UNARY);
            post->data.unary.op = 9; /* post-- */
            post->data.unary.operand = node;
            post->type = node->type;
            node = post;
        } else {
            break;
        }
    }

    return node;
}

static AstNode *parse_unary(Parser *p) {
    Token *t = peek_tok(p);
    if (!t) return NULL;

    if (t->kind == TOK_PLUS_PLUS) {
        p->pos++;
        AstNode *op = parse_unary(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 0; /* pre++ */
        node->data.unary.operand = op;
        node->type = op ? op->type : type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_MINUS_MINUS) {
        p->pos++;
        AstNode *op = parse_unary(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 1; /* pre-- */
        node->data.unary.operand = op;
        node->type = op ? op->type : type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_AMPERSAND) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_ADDR_OF);
        node->data.unary.operand = op;
        node->data.unary.op = 6;
        node->type = type_pointer(op ? op->type : type_primitive(TYPE_INT, 1));
        return node;
    }
    if (t->kind == TOK_STAR) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_DEREF);
        node->data.unary.operand = op;
        node->data.unary.op = 7;
        Type *deref_type = (op && op->type && op->type->kind == TYPE_PTR) ? op->type->ptr_to : type_primitive(TYPE_INT, 1);
        node->type = deref_type;
        return node;
    }
    if (t->kind == TOK_PLUS) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 2;
        node->data.unary.operand = op;
        node->type = op ? op->type : type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_MINUS) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 3;
        node->data.unary.operand = op;
        node->type = op ? op->type : type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_EXCLAIM) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 4;
        node->data.unary.operand = op;
        node->type = type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_TILDE) {
        p->pos++;
        AstNode *op = parse_cast(p);
        AstNode *node = ast_alloc(NOD_UNARY);
        node->data.unary.op = 5;
        node->data.unary.operand = op;
        node->type = op ? op->type : type_primitive(TYPE_INT, 1);
        return node;
    }
    if (t->kind == TOK_SIZEOF) {
        p->pos++;
        if (peek_tok(p) && peek_tok(p)->kind == TOK_LPAREN) {
            p->pos++;
            if (peek_type_spec(p)) {
                Type *st = parse_type(p, 1);
                st = parse_abstract_declarator(p, st);
                expect(p, TOK_RPAREN);
                AstNode *node = ast_alloc(NOD_INT_CONST);
                node->data.int_val = type_size(st);
                node->type = type_primitive(TYPE_ULONG, 0);
                return node;
            } else {
                AstNode *e = parse_expr(p);
                expect(p, TOK_RPAREN);
                AstNode *node = ast_alloc(NOD_INT_CONST);
                node->data.int_val = type_size(e ? e->type : NULL);
                node->type = type_primitive(TYPE_ULONG, 0);
                return node;
            }
        }
        AstNode *e = parse_unary(p);
        AstNode *node = ast_alloc(NOD_INT_CONST);
        node->data.int_val = type_size(e ? e->type : NULL);
        node->type = type_primitive(TYPE_ULONG, 0);
        return node;
    }
    if (t->kind == TOK_ALIGNOF || t->kind == TOK__ALIGNOF) {
        p->pos++;
        expect(p, TOK_LPAREN);
        Type *at = parse_type(p, 1);
        at = parse_abstract_declarator(p, at);
        expect(p, TOK_RPAREN);
        AstNode *node = ast_alloc(NOD_INT_CONST);
        node->data.int_val = type_align(at);
        node->type = type_primitive(TYPE_ULONG, 0);
        return node;
    }

    return parse_postfix(p);
}

static AstNode *parse_cast(Parser *p) {
    if (peek_tok(p) && peek_tok(p)->kind == TOK_LPAREN) {
        /* look-ahead for cast */
        int saved = p->pos;
        p->pos++;
        if (peek_type_spec(p)) {
            Type *ctype = parse_type(p, 1);
            if (ctype) ctype = parse_abstract_declarator(p, ctype);
            if (peek_tok(p) && peek_tok(p)->kind == TOK_RPAREN) {
                p->pos++;
                AstNode *op = parse_cast(p);
                AstNode *node = ast_alloc(NOD_CAST);
                node->data.cast.to_type = ctype;
                node->data.cast.operand = op;
                node->type = ctype;
                return node;
            }
        }
        p->pos = saved;
    }
    return parse_unary(p);
}

static AstNode *parse_multiplicative(Parser *p) {
    AstNode *node = parse_cast(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;
        int op = -1;
        if (t->kind == TOK_STAR) op = 2;
        else if (t->kind == TOK_SLASH) op = 3;
        else if (t->kind == TOK_PERCENT) op = 4;
        else break;
        p->pos++;
        AstNode *right = parse_cast(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = op;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_additive(Parser *p) {
    AstNode *node = parse_multiplicative(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;
        int op = -1;
        if (t->kind == TOK_PLUS) op = 0;
        else if (t->kind == TOK_MINUS) op = 1;
        else break;
        p->pos++;
        AstNode *right = parse_multiplicative(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = op;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_shift(Parser *p) {
    AstNode *node = parse_additive(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;
        int op = -1;
        if (t->kind == TOK_LT_LT) op = 8;
        else if (t->kind == TOK_GT_GT) op = 9;
        else break;
        p->pos++;
        AstNode *right = parse_additive(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = op;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_relational(Parser *p) {
    AstNode *node = parse_shift(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;
        int op = -1;
        if (t->kind == TOK_LT) op = 14;
        else if (t->kind == TOK_GT) op = 15;
        else if (t->kind == TOK_LT_EQ) op = 16;
        else if (t->kind == TOK_GT_EQ) op = 17;
        else break;
        p->pos++;
        AstNode *right = parse_shift(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = op;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = type_primitive(TYPE_INT, 1);
        node = bin;
    }
    return node;
}

static AstNode *parse_equality(Parser *p) {
    AstNode *node = parse_relational(p);
    if (!node) return NULL;

    while (1) {
        Token *t = peek_tok(p);
        if (!t) break;
        int op = -1;
        if (t->kind == TOK_EQ_EQ) op = 12;
        else if (t->kind == TOK_EXCLAIM_EQ) op = 13;
        else break;
        p->pos++;
        AstNode *right = parse_relational(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = op;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = type_primitive(TYPE_INT, 1);
        node = bin;
    }
    return node;
}

static AstNode *parse_and(Parser *p) {
    AstNode *node = parse_equality(p);
    if (!node) return NULL;

    while (peek_tok(p) && peek_tok(p)->kind == TOK_AMPERSAND) {
        p->pos++;
        AstNode *right = parse_equality(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = 5;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_exclusive_or(Parser *p) {
    AstNode *node = parse_and(p);
    if (!node) return NULL;

    while (peek_tok(p) && peek_tok(p)->kind == TOK_CARET) {
        p->pos++;
        AstNode *right = parse_and(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = 7;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_inclusive_or(Parser *p) {
    AstNode *node = parse_exclusive_or(p);
    if (!node) return NULL;

    while (peek_tok(p) && peek_tok(p)->kind == TOK_PIPE) {
        p->pos++;
        AstNode *right = parse_exclusive_or(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = 6;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = node->type;
        node = bin;
    }
    return node;
}

static AstNode *parse_logical_and(Parser *p) {
    AstNode *node = parse_inclusive_or(p);
    if (!node) return NULL;

    while (peek_tok(p) && peek_tok(p)->kind == TOK_AMPERSAND_AMPERSAND) {
        p->pos++;
        AstNode *right = parse_inclusive_or(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = 10;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = type_primitive(TYPE_INT, 1);
        node = bin;
    }
    return node;
}

static AstNode *parse_logical_or(Parser *p) {
    AstNode *node = parse_logical_and(p);
    if (!node) return NULL;

    while (peek_tok(p) && peek_tok(p)->kind == TOK_PIPE_PIPE) {
        p->pos++;
        AstNode *right = parse_logical_and(p);
        AstNode *bin = ast_alloc(NOD_BINARY);
        bin->data.binary.op = 11;
        bin->data.binary.left = node;
        bin->data.binary.right = right;
        bin->type = type_primitive(TYPE_INT, 1);
        node = bin;
    }
    return node;
}

static AstNode *parse_conditional(Parser *p) {
    AstNode *node = parse_logical_or(p);
    if (!node) return NULL;

    if (peek_tok(p) && peek_tok(p)->kind == TOK_QUESTION) {
        p->pos++;
        AstNode *then = parse_expr(p);
        expect(p, TOK_COLON);
        AstNode *els = parse_conditional(p);
        AstNode *cond = ast_alloc(NOD_CONDITIONAL);
        cond->data.conditional.cond = node;
        cond->data.conditional.then = then;
        cond->data.conditional.els = els;
        cond->type = then ? then->type : type_primitive(TYPE_INT, 1);
        node = cond;
    }
    return node;
}

static AstNode *parse_assignment(Parser *p) {
    AstNode *node = parse_conditional(p);
    if (!node) return NULL;

    Token *t = peek_tok(p);
    if (!t) return node;

    int op = -1;
    if (t->kind == TOK_EQ) op = 18;
    else if (t->kind == TOK_PLUS_EQ) op = 0;
    else if (t->kind == TOK_MINUS_EQ) op = 1;
    else if (t->kind == TOK_STAR_EQ) op = 2;
    else if (t->kind == TOK_SLASH_EQ) op = 3;
    else if (t->kind == TOK_PERCENT_EQ) op = 4;
    else if (t->kind == TOK_AMPERSAND_EQ) op = 5;
    else if (t->kind == TOK_PIPE_EQ) op = 6;
    else if (t->kind == TOK_CARET_EQ) op = 7;
    else if (t->kind == TOK_LT_LT_EQ) op = 8;
    else if (t->kind == TOK_GT_GT_EQ) op = 9;

    if (op >= 0) {
        p->pos++;
        AstNode *right = parse_assignment(p);
        AstNode *assign = ast_alloc(NOD_ASSIGN);
        assign->data.assign.op = op;
        assign->data.assign.left = node;
        assign->data.assign.right = right;
        assign->type = node->type;
        node = assign;
    }

    return node;
}

static AstNode *parse_expr(Parser *p) {
    AstNode *node = parse_assignment(p);
    if (!node) return NULL;

    if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) {
        AstNode **exprs = NULL;
        int nexprs = 0;
        exprs = (AstNode**)xrealloc(exprs, (nexprs + 1) * sizeof(AstNode*));
        exprs[nexprs++] = node;
        while (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) {
            p->pos++;
            AstNode *e = parse_assignment(p);
            exprs = (AstNode**)xrealloc(exprs, (nexprs + 1) * sizeof(AstNode*));
            exprs[nexprs++] = e;
        }
        AstNode *comma = ast_alloc(NOD_COMMA);
        comma->data.comma.exprs = exprs;
        comma->data.comma.nexprs = nexprs;
        comma->type = exprs[nexprs - 1] ? exprs[nexprs - 1]->type : type_primitive(TYPE_INT, 1);
        node = comma;
    }

    return node;
}

/* Statement parsing */
static AstNode *parse_compound(Parser *p) {
    AstNode *node = ast_alloc(NOD_COMPOUND);
    node->data.compound.stmts = NULL;
    node->data.compound.nstmts = 0;

    expect(p, TOK_LBRACE);
    sym_enter_scope(p->sym);

    while (peek_tok(p) && peek_tok(p)->kind != TOK_RBRACE && peek_tok(p)->kind != TOK_EOF) {
        AstNode *stmt = NULL;
        if (peek_type_spec(p) || (peek_tok(p) && (peek_tok(p)->kind == TOK__STATIC_ASSERT || peek_tok(p)->kind == TOK_STATIC_ASSERT))) {
            stmt = parse_declaration(p, 0);
        } else {
            stmt = parse_statement(p);
        }
        if (stmt) {
            node->data.compound.stmts = (AstNode**)xrealloc(node->data.compound.stmts,
                (node->data.compound.nstmts + 1) * sizeof(AstNode*));
            node->data.compound.stmts[node->data.compound.nstmts++] = stmt;
        }
    }

    sym_leave_scope(p->sym);
    expect(p, TOK_RBRACE);
    return node;
}

static AstNode *parse_statement(Parser *p) {
    Token *t = peek_tok(p);
    if (!t) return NULL;

    switch (t->kind) {
        case TOK_LBRACE: return parse_compound(p);
        case TOK_SEMICOLON: { p->pos++; AstNode *n = ast_alloc(NOD_NULL_STMT); return n; }
        case TOK_IF: {
            p->pos++;
            expect(p, TOK_LPAREN);
            AstNode *cond = parse_expr(p);
            expect(p, TOK_RPAREN);
            AstNode *then = parse_statement(p);
            AstNode *els = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_ELSE) {
                p->pos++;
                els = parse_statement(p);
            }
            AstNode *n = ast_alloc(NOD_IF);
            n->data.if_stmt.cond = cond;
            n->data.if_stmt.then = then;
            n->data.if_stmt.els = els;
            return n;
        }
        case TOK_WHILE: {
            p->pos++;
            expect(p, TOK_LPAREN);
            AstNode *cond = parse_expr(p);
            expect(p, TOK_RPAREN);
            AstNode *body = parse_statement(p);
            AstNode *n = ast_alloc(NOD_WHILE);
            n->data.while_stmt.cond = cond;
            n->data.while_stmt.body = body;
            return n;
        }
        case TOK_DO: {
            p->pos++;
            AstNode *body = parse_statement(p);
            expect(p, TOK_WHILE);
            expect(p, TOK_LPAREN);
            AstNode *cond = parse_expr(p);
            expect(p, TOK_RPAREN);
            expect(p, TOK_SEMICOLON);
            AstNode *n = ast_alloc(NOD_DO);
            n->data.do_stmt.body = body;
            n->data.do_stmt.cond = cond;
            return n;
        }
        case TOK_FOR: {
            p->pos++;
            expect(p, TOK_LPAREN);
            sym_enter_scope(p->sym);
            AstNode *init = NULL;
            if (peek_type_spec(p)) {
                init = parse_declaration(p, 0);
            } else {
                if (peek_tok(p) && peek_tok(p)->kind != TOK_SEMICOLON)
                    init = parse_expr(p);
                expect(p, TOK_SEMICOLON);
            }
            AstNode *cond = NULL;
            if (peek_tok(p) && peek_tok(p)->kind != TOK_SEMICOLON)
                cond = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            AstNode *incr = NULL;
            if (peek_tok(p) && peek_tok(p)->kind != TOK_RPAREN)
                incr = parse_expr(p);
            expect(p, TOK_RPAREN);
            AstNode *body = parse_statement(p);
            sym_leave_scope(p->sym);
            AstNode *n = ast_alloc(NOD_FOR);
            n->data.for_stmt.init = init;
            n->data.for_stmt.cond = cond;
            n->data.for_stmt.incr = incr;
            n->data.for_stmt.body = body;
            return n;
        }
        case TOK_SWITCH: {
            p->pos++;
            expect(p, TOK_LPAREN);
            AstNode *expr = parse_expr(p);
            expect(p, TOK_RPAREN);
            AstNode *body = parse_statement(p);
            AstNode *n = ast_alloc(NOD_SWITCH);
            n->data.switch_stmt.expr = expr;
            n->data.switch_stmt.body = body;
            return n;
        }
        case TOK_CASE: {
            p->pos++;
            AstNode *expr = parse_expr(p);
            expect(p, TOK_COLON);
            AstNode *stmt = parse_statement(p);
            AstNode *n = ast_alloc(NOD_CASE);
            n->data.case_stmt.expr = expr;
            n->data.case_stmt.stmt = stmt;
            return n;
        }
        case TOK_DEFAULT: {
            p->pos++;
            expect(p, TOK_COLON);
            AstNode *stmt = parse_statement(p);
            AstNode *n = ast_alloc(NOD_DEFAULT);
            n->data.default_stmt.stmt = stmt;
            return n;
        }
        case TOK_BREAK: {
            p->pos++;
            expect(p, TOK_SEMICOLON);
            return ast_alloc(NOD_BREAK);
        }
        case TOK_CONTINUE: {
            p->pos++;
            expect(p, TOK_SEMICOLON);
            return ast_alloc(NOD_CONTINUE);
        }
        case TOK_RETURN: {
            p->pos++;
            AstNode *expr = NULL;
            if (peek_tok(p) && peek_tok(p)->kind != TOK_SEMICOLON)
                expr = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            AstNode *n = ast_alloc(NOD_RETURN);
            n->data.return_stmt.expr = expr;
            return n;
        }
        case TOK_GOTO: {
            p->pos++;
            Token *label = expect(p, TOK_IDENT);
            expect(p, TOK_SEMICOLON);
            AstNode *n = ast_alloc(NOD_GOTO);
            n->data.goto_stmt.label = label ? xstrdup(label->sval) : NULL;
            return n;
        }
        case TOK_IDENT: {
            /* could be label */
            if (p->pos + 2 < p->ntokens && p->tokens[p->pos + 1].kind == TOK_COLON) {
                const char *lname = xstrdup(t->sval);
                p->pos++; p->pos++; /* skip ident and colon */
                AstNode *stmt = parse_statement(p);
                AstNode *n = ast_alloc(NOD_LABEL);
                n->data.label.name = lname;
                n->data.label.stmt = stmt;
                return n;
            }
            /* fall through to expression */
        }
        /* fall through */
        default: {
            AstNode *expr = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            AstNode *n = ast_alloc(NOD_EXPR_STMT);
            n->data.expr_stmt.expr = expr;
            return n;
        }
    }
}

/* Declaration parsing */
static AstNode *parse_declaration(Parser *p, int is_global) {
    Token *t = peek_tok(p);
    if (!t) return NULL;

    /* handle static_assert */
    if (t->kind == TOK_STATIC_ASSERT || t->kind == TOK__STATIC_ASSERT) {
        p->pos++;
        expect(p, TOK_LPAREN);
        AstNode *expr = parse_assignment(p);
        const char *msg = NULL;
        if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) {
            p->pos++;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_STRING) {
                msg = xstrdup(peek_tok(p)->sval);
                p->pos++;
            }
        }
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMICOLON);
        /* evaluate constant */
        if (expr && expr->kind == NOD_INT_CONST && expr->data.int_val == 0) {
            diag(DIAG_ERROR, p->file, t->line, t->col, "static assertion failed: %s", msg ? msg : "");
        }
        AstNode *n = ast_alloc(NOD_STATIC_ASSERT);
        n->data.static_assertion.expr = expr;
        n->data.static_assertion.msg = msg;
        return n;
    }

    /* collect storage class and qualifiers */
    int storage = 0; /* 0=none, 1=typedef, 2=extern, 3=static, 4=register, 5=auto */
    int is_inline = 0;
    int is_constexpr = 0;
    int align_val = 0;

    while (1) {
        t = peek_tok(p);
        if (!t) break;
        switch (t->kind) {
            case TOK_TYPEDEF: storage = 1; p->pos++; continue;
            case TOK_EXTERN: storage = 2; p->pos++; continue;
            case TOK_STATIC: storage = 3; p->pos++; continue;
            case TOK_REGISTER: storage = 4; p->pos++; continue;
            case TOK_AUTO: storage = 5; p->pos++; continue;
            case TOK_INLINE: is_inline = 1; p->pos++; continue;
            case TOK__NORETURN: case TOK_NORETURN: p->pos++; continue;
            case TOK__THREAD_LOCAL: case TOK_THREAD_LOCAL: p->pos++; continue;
            case TOK_CONSTEXPR: is_constexpr = 1; p->pos++; continue;
            case TOK_ALIGNAS: case TOK__ALIGNAS: {
                p->pos++; expect(p, TOK_LPAREN);
                if (peek_tok(p) && peek_tok(p)->kind == TOK_INTEGER) {
                    align_val = (int)peek_tok(p)->ival;
                    p->pos++;
                } else {
                    Type *at = parse_type(p, 1);
                    if (at) align_val = type_align(at);
                }
                expect(p, TOK_RPAREN);
                continue;
            }
            case TOK_ATTR_NODISCARD: case TOK_ATTR_MAYBE_UNUSED: case TOK_ATTR_DEPRECATED: case TOK_ATTR_NORETURN: case TOK_ATTR_FALLTHROUGH: p->pos++; continue;
            default: break;
        }
        break;
    }

    Type *base = parse_type(p, 1);
    if (!base) {
        skip_to_semicolon(p);
        return NULL;
    }

    int is_struct_union_enum = (base->kind == TYPE_STRUCT || base->kind == TYPE_UNION || base->kind == TYPE_ENUM);

    /* parse declarators */
    AstNode *first_decl = NULL;

    while (1) {
        const char *name = NULL;
        Type *decl_type = parse_declarator(p, type_copy(base), &name);

        if (!name && decl_type && !is_struct_union_enum) {
            /* abstract declarator - skip */
            if (peek_tok(p) && peek_tok(p)->kind == TOK_SEMICOLON) break;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) { p->pos++; continue; }
            break;
        }

        if (storage == 1) {
            /* typedef */
            Symbol *sym = sym_put(p->sym, name, decl_type, 0, 1);
            sym->defined = 1;
            AstNode *n = ast_alloc(NOD_TYPEDEF);
            n->data.typedef_decl.name = name ? xstrdup(name) : NULL;
            n->data.typedef_decl.type = decl_type;
            if (!first_decl) first_decl = n;
        } else if (decl_type && decl_type->kind == TYPE_FUNC) {
            /* function */
            int is_def = 0;
            AstNode *body = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_LBRACE) {
                is_def = 1;
                /* enter new scope for parameters */
                sym_enter_scope(p->sym);
                for (int i = 0; i < decl_type->nparams; i++) {
                    if (decl_type->params_of[i]) {
                        const char *pname = (decl_type->param_names && decl_type->param_names[i]) ? decl_type->param_names[i] : "_";
                        Symbol *ps = sym_put(p->sym, pname, decl_type->params_of[i], 0, 0);
                        ps->defined = 1;
                        ps->stack_offset = 8 + 8 * (i + 2);
                    }
                }
                body = parse_compound(p);
                sym_leave_scope(p->sym);
            }

            Symbol *sym = sym_global(p->sym, name, decl_type, 1);
            sym->defined = 1;
            sym->is_extern = (storage == 2);
            sym->is_static = (storage == 3);

            AstNode *n = ast_alloc(is_def ? NOD_FUNC_DEF : NOD_FUNC_DECL);
            n->data.func.name = name ? xstrdup(name) : NULL;
            n->data.func.func_type = decl_type;
            n->data.func.body = body;
            n->data.func.nparams = decl_type ? decl_type->nparams : 0;
            n->data.func.is_definition = is_def;
            n->data.func.is_static = (storage == 3);
            n->data.func.is_extern = (storage == 2);
            n->data.func.is_inline = is_inline;
            n = is_def ? n : n;
            if (!first_decl) first_decl = n;
            if (is_def) { /* only one definition */ }
        } else {
            /* variable declaration */
            AstNode *init = NULL;
            if (peek_tok(p) && peek_tok(p)->kind == TOK_EQ) {
                p->pos++;
                init = parse_assignment(p);
            }

            if (storage == 5) {
                /* C23 auto type inference */
                if (!init) {
                    diag(DIAG_ERROR, p->file, peek_tok(p) ? peek_tok(p)->line : 0, 0, "'auto' variable requires an initializer");
                } else if (init->type) {
                    decl_type = type_unqual(init->type);
                }
            }

            if (is_global || storage == 3 || storage == 2) {
                Symbol *sym = sym_global(p->sym, name, decl_type, 0);
                sym->is_static = (storage == 3 || (!is_global && storage != 2));
                sym->is_extern = (storage == 2);
                sym->is_constexpr = is_constexpr;
                sym->alignment = align_val;
                sym->defined = 1;
            } else {
                Symbol *sym = sym_put(p->sym, name, decl_type, 0, 0);
                sym->defined = 1;
                sym->is_constexpr = is_constexpr;
                sym->alignment = align_val;
                {
                    int sz = type_size(decl_type);
                    if (sz < 8) sz = 8;
                    p->sym->stack_size += sz;
                    sym->stack_offset = -(int)p->sym->stack_size;
                }
            }

            AstNode *n = ast_alloc(init ? NOD_VAR_DEF : NOD_VAR_DECL);
            n->data.var.name = name ? xstrdup(name) : NULL;
            n->data.var.var_type = decl_type;
            n->data.var.init = init;
            n->data.var.is_static = (storage == 3 || (!is_global && storage != 2));
            n->data.var.is_extern = (storage == 2);
            n->data.var.is_constexpr = is_constexpr;
            n->data.var.alignment = align_val;
            if (!first_decl) first_decl = n;
        }

        free((void*)name);
        if (peek_tok(p) && peek_tok(p)->kind == TOK_COMMA) {
            p->pos++;
        } else {
            break;
        }
    }

    int is_func_def = first_decl && (first_decl->kind == NOD_FUNC_DEF);
    if (first_decl && !is_func_def) {
        expect(p, TOK_SEMICOLON);
    } else if (first_decl && is_func_def) {
        /* function definitions don't need a semicolon */
    } else if (!is_struct_union_enum) {
        /* consume stray semicolons */
        if (peek_tok(p) && peek_tok(p)->kind == TOK_SEMICOLON) p->pos++;
    } else {
        /* struct/union/enum definition may not need semicolon if it's followed by a declarator */
        /* but if it's just a type definition, we need semicolon */
        if (peek_tok(p) && peek_tok(p)->kind == TOK_SEMICOLON) p->pos++;
    }

    return first_decl;
}

void parser_init(Parser *p, Token *tokens, int ntokens, const char *file, TargetPlatform target) {
    memset(p, 0, sizeof(*p));
    p->tokens = tokens;
    p->ntokens = ntokens;
    p->file = file;
    p->target = target;
    p->sym = sym_table_new();
    p->current_scope = 0;
    p->label_count = 0;
    p->global_decls = NULL;
    p->nglobal_decls = 0;
    p->cap_global_decls = 0;
}

void parser_parse(Parser *p) {
    while (p->pos < p->ntokens) {
        Token *t = peek_tok(p);
        if (!t || t->kind == TOK_EOF) break;

        /* skip attributes before declarations */
        while (t && (t->kind == TOK_ATTR_NODISCARD || t->kind == TOK_ATTR_MAYBE_UNUSED ||
                     t->kind == TOK_ATTR_DEPRECATED || t->kind == TOK_ATTR_NORETURN ||
                     t->kind == TOK_ATTR_FALLTHROUGH)) {
            p->pos++;
            t = peek_tok(p);
        }

        if (t && (peek_type_spec(p) || t->kind == TOK_STATIC_ASSERT || t->kind == TOK__STATIC_ASSERT ||
                  t->kind == TOK_ATTR_NODISCARD || t->kind == TOK_ATTR_MAYBE_UNUSED ||
                  t->kind == TOK_ATTR_DEPRECATED || t->kind == TOK_ATTR_NORETURN)) {
            AstNode *decl = parse_declaration(p, 1);
            if (decl) add_global(p, decl);
        } else if (t && t->kind == TOK_SEMICOLON) {
            p->pos++; /* stray semicolon */
        } else {
            /* try parsing as function definition with identifier */
            if (t && t->kind == TOK_IDENT) {
                Symbol *sym = sym_lookup(p->sym, t->sval);
                if (sym && sym->is_typedef) {
                    /* typedef name - treat as declaration */
                    AstNode *decl = parse_declaration(p, 1);
                    if (decl) add_global(p, decl);
                    continue;
                }
            }
            diag(DIAG_ERROR, p->file, t ? t->line : 0, t ? t->col : 0, "unexpected token at top level");
            p->pos++;
        }
    }
}

void parser_free(Parser *p) {
    sym_table_free(p->sym);
    /* global decls are freed by caller */
    free(p->global_decls);
}
