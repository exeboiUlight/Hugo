#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include "symbol.h"
#include "utils.h"
#include "emit_x86.h"

typedef struct StringNode {
    char *content;
    size_t len;
} StringNode;

typedef struct {
    size_t offset;
    int label;
} LabelFixup;

typedef struct {
    size_t offset;
    int string_idx;
} StringRefFixup;

typedef struct {
    ByteBuffer code;
    TargetPlatform target;
    SymbolTable *sym;
    int label_count;
    size_t *label_positions;
    int nlabels;
    int label_capacity;
    LabelFixup *fixups;
    int nfixups;
    int fixup_capacity;
    StringRefFixup *string_fixups;
    int nstring_fixups;
    int string_fixup_capacity;
    int return_label;
    const char *current_function;
    int stack_size;
    int loop_start_label;
    int loop_end_label;
    StringNode *strings;
    int nstrings;
    int string_capacity;
    ByteBuffer data;
    int has_global_data;
} Codegen;

void codegen_init(Codegen *cg, TargetPlatform target, SymbolTable *sym);
void codegen_emit(Codegen *cg, AstNode **decls, int ndecls);
void codegen_free(Codegen *cg);
size_t codegen_text_size(Codegen *cg);
size_t codegen_rodata_size(Codegen *cg);
void codegen_write_text(Codegen *cg, unsigned char *dst);
void codegen_write_rodata(Codegen *cg, unsigned char *dst);
void codegen_patch_string_refs(Codegen *cg, unsigned char *text_buf, uint64_t text_start_va, uint64_t rodata_va);

#endif
