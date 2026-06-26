#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include "symbol.h"

typedef struct {
    Token *tokens;
    int pos;
    int ntokens;
    const char *file;
    SymbolTable *sym;
    TargetPlatform target;
    int current_scope;
    int label_count;
    AstNode **global_decls;
    int nglobal_decls;
    int cap_global_decls;
} Parser;

void parser_init(Parser *p, Token *tokens, int ntokens, const char *file, TargetPlatform target);
void parser_parse(Parser *p);
void parser_free(Parser *p);

#endif
