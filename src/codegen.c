#include "codegen.h"
#include "typecheck.h"
#include <string.h>

static int label(Codegen *cg) { return cg->label_count++; }

static void record_label(Codegen *cg, int lbl) {
    while (cg->nlabels <= lbl) {
        if (cg->nlabels >= cg->label_capacity) {
            cg->label_capacity = cg->label_capacity ? cg->label_capacity * 2 : 64;
            cg->label_positions = xrealloc(cg->label_positions, cg->label_capacity * sizeof(size_t));
        }
        cg->label_positions[cg->nlabels++] = (size_t)-1;
    }
    cg->label_positions[lbl] = cg->code.len;
}

static void add_fixup(Codegen *cg, size_t pos, int lbl) {
    if (cg->nfixups >= cg->fixup_capacity) {
        cg->fixup_capacity = cg->fixup_capacity ? cg->fixup_capacity * 2 : 64;
        cg->fixups = xrealloc(cg->fixups, cg->fixup_capacity * sizeof(LabelFixup));
    }
    cg->fixups[cg->nfixups].offset = pos;
    cg->fixups[cg->nfixups].label = lbl;
    cg->nfixups++;
}

static void patch_fixups(Codegen *cg) {
    for (int i = 0; i < cg->nfixups; i++) {
        size_t pos = cg->fixups[i].offset;
        int lbl = cg->fixups[i].label;
        size_t target = cg->label_positions[lbl];
        int32_t rel = (int32_t)((int64_t)target - (int64_t)(pos + 4));
        buf_patch32(&cg->code, pos, (uint32_t)rel);
    }
}

void codegen_patch_string_refs(Codegen *cg, unsigned char *text_buf, uint64_t text_start_va, uint64_t rodata_va) {
    for (int i = 0; i < cg->nstring_fixups; i++) {
        size_t pos = cg->string_fixups[i].offset;
        int si = cg->string_fixups[i].string_idx;
        uint64_t str_off = 0;
        for (int j = 0; j < si; j++)
            str_off += cg->strings[j].len + 1;
        uint64_t target_va = rodata_va + str_off;
        uint64_t rip_after = text_start_va + pos + 4;
        int32_t rel = (int32_t)((int64_t)target_va - (int64_t)rip_after);
        text_buf[pos + 0] = (unsigned char)(rel);
        text_buf[pos + 1] = (unsigned char)(rel >> 8);
        text_buf[pos + 2] = (unsigned char)(rel >> 16);
        text_buf[pos + 3] = (unsigned char)(rel >> 24);
    }
}

static int intern_string(Codegen *cg, const char *s, size_t len) {
    for (int i = 0; i < cg->nstrings; i++) {
        if (cg->strings[i].len == len && memcmp(cg->strings[i].content, s, len) == 0)
            return i;
    }
    if (cg->nstrings >= cg->string_capacity) {
        cg->string_capacity = cg->string_capacity ? cg->string_capacity * 2 : 16;
        cg->strings = xrealloc(cg->strings, cg->string_capacity * sizeof(StringNode));
    }
    cg->strings[cg->nstrings].content = xmalloc(len + 1);
    memcpy(cg->strings[cg->nstrings].content, s, len);
    cg->strings[cg->nstrings].content[len] = '\0';
    cg->strings[cg->nstrings].len = len;
    return cg->nstrings++;
}

static void emit_prologue(Codegen *cg, int stack_size, int nparams) {
    enc_push_reg(&cg->code, X86_RBP);
    enc_mov_reg_reg(&cg->code, X86_RBP, X86_RSP);
    if (stack_size > 0)
        enc_sub_reg_imm8(&cg->code, X86_RSP, (stack_size + 15) & ~15);
    X86Reg regs64[] = {X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9};
    for (int i = 0; i < nparams && i < 6; i++) {
        int off = 8 + 8 * (i + 2);
        enc_mov_membase_reg(&cg->code, X86_RBP, off, regs64[i]);
    }
}

static void emit_epilogue(Codegen *cg) {
    enc_mov_reg_reg(&cg->code, X86_RSP, X86_RBP);
    enc_pop_reg(&cg->code, X86_RBP);
    if (cg->target == TARGET_LINUX && cg->current_function && strcmp(cg->current_function, "main") == 0) {
        /* Linux: main is the ELF entry point (no _start stub), use exit syscall */
        enc_mov_reg_reg(&cg->code, X86_RDI, X86_RAX);
        enc_mov_reg_imm64(&cg->code, X86_RAX, 60);
        buf_write(&cg->code, 0x0F);
        buf_write(&cg->code, 0x05);
    } else {
        enc_ret(&cg->code);
    }
}

static void emit_expr(Codegen *cg, AstNode *node);
static void emit_statement(Codegen *cg, AstNode *node);
static void emit_compound(Codegen *cg, AstNode *node);

static void emit_binary(Codegen *cg, AstNode *node) {
    emit_expr(cg, node->data.binary.left);
    enc_push_reg(&cg->code, X86_RAX);
    emit_expr(cg, node->data.binary.right);
    enc_mov_reg_reg(&cg->code, X86_RBX, X86_RAX);
    enc_pop_reg(&cg->code, X86_RAX);

    switch (node->data.binary.op) {
        case 0:  enc_add_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 1:  enc_sub_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 2:  enc_imul_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 3:
            enc_xor_reg_reg(&cg->code, X86_RDX, X86_RDX);
            enc_idiv_reg(&cg->code, X86_RBX);
            break;
        case 4:
            enc_xor_reg_reg(&cg->code, X86_RDX, X86_RDX);
            enc_idiv_reg(&cg->code, X86_RBX);
            enc_mov_reg_reg(&cg->code, X86_RAX, X86_RDX);
            break;
        case 5:  enc_and_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 6:  enc_or_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 7:  enc_xor_reg_reg(&cg->code, X86_RAX, X86_RBX); break;
        case 8:
            enc_mov_reg_reg(&cg->code, X86_RCX, X86_RBX);
            enc_shl_reg_cl(&cg->code, X86_RAX);
            break;
        case 9:
            enc_mov_reg_reg(&cg->code, X86_RCX, X86_RBX);
            enc_sar_reg_cl(&cg->code, X86_RAX);
            break;
        case 10:
            enc_cmp_reg_imm(&cg->code, X86_RAX, 0);
            enc_setcc_reg(&cg->code, 5, X86_RAX); /* setnz */
            enc_cmp_reg_imm(&cg->code, X86_RBX, 0);
            enc_setcc_reg(&cg->code, 5, X86_RBX); /* setnz */
            enc_and_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 11:
            enc_cmp_reg_imm(&cg->code, X86_RAX, 0);
            enc_setcc_reg(&cg->code, 5, X86_RAX);
            enc_cmp_reg_imm(&cg->code, X86_RBX, 0);
            enc_setcc_reg(&cg->code, 5, X86_RBX);
            enc_or_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 12:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 4, X86_RAX); /* sete */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 13:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 5, X86_RAX); /* setne */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 14:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 12, X86_RAX); /* setl */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 15:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 15, X86_RAX); /* setg (nle) */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 16:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 14, X86_RAX); /* setle */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 17:
            enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_setcc_reg(&cg->code, 13, X86_RAX); /* setge */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
    }
}

static void emit_unary(Codegen *cg, AstNode *node) {
    int op = node->data.unary.op;
    AstNode *operand = node->data.unary.operand;
    switch (op) {
        case 0: /* pre++ */
            emit_expr(cg, operand);
            enc_mov_reg_membase(&cg->code, X86_RBX, X86_RAX, 0);
            enc_add_reg_imm8(&cg->code, X86_RBX, 1);
            enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RBX);
            enc_lea_reg_membase(&cg->code, X86_RAX, X86_RBX, -1);
            break;
        case 1: /* pre-- */
            emit_expr(cg, operand);
            enc_mov_reg_membase(&cg->code, X86_RBX, X86_RAX, 0);
            enc_sub_reg_imm8(&cg->code, X86_RBX, 1);
            enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RBX);
            enc_lea_reg_membase(&cg->code, X86_RAX, X86_RBX, 1);
            break;
        case 2: /* + */
            emit_expr(cg, operand);
            break;
        case 3: /* - (negate) */
            emit_expr(cg, operand);
            enc_neg_reg(&cg->code, X86_RAX);
            break;
        case 4: /* ! */
            emit_expr(cg, operand);
            enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
            enc_setcc_reg(&cg->code, 4, X86_RAX); /* sete */
            enc_movzx_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case 5: /* ~ */
            emit_expr(cg, operand);
            enc_not_reg(&cg->code, X86_RAX);
            break;
        case 6: /* & (addr of) */
            if (operand && operand->kind == NOD_IDENT) {
                Symbol *s = sym_lookup(cg->sym, operand->data.ident.name);
                if (s && !s->is_function) {
                    if (s->is_static || s->is_extern) {
                        enc_lea_reg_rip(&cg->code, X86_RAX, 0);
                        add_fixup(cg, cg->code.len - 4, -1);
                        /* we'll need label mapping for globals */
                    } else {
                        enc_lea_reg_membase(&cg->code, X86_RAX, X86_RBP, s->stack_offset);
                    }
                } else {
                    enc_xor_reg_reg(&cg->code, X86_RAX, X86_RAX);
                }
            } else {
                emit_expr(cg, operand);
            }
            break;
        case 7: /* * (deref) */
            emit_expr(cg, operand);
            enc_mov_reg_membase(&cg->code, X86_RAX, X86_RAX, 0);
            break;
        case 8: /* post++ */
            emit_expr(cg, operand);
            enc_mov_reg_membase(&cg->code, X86_RBX, X86_RAX, 0);
            enc_lea_reg_membase(&cg->code, X86_RCX, X86_RBX, 1);
            enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RCX);
            enc_mov_reg_reg(&cg->code, X86_RAX, X86_RBX);
            break;
        case 9: /* post-- */
            emit_expr(cg, operand);
            enc_mov_reg_membase(&cg->code, X86_RBX, X86_RAX, 0);
            enc_lea_reg_membase(&cg->code, X86_RCX, X86_RBX, -1);
            enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RCX);
            enc_mov_reg_reg(&cg->code, X86_RAX, X86_RBX);
            break;
    }
}

static void emit_lvalue_addr(Codegen *cg, AstNode *node) {
    if (node->kind == NOD_IDENT) {
        Symbol *s = sym_lookup(cg->sym, node->data.ident.name);
        if (s && !s->is_function && !s->is_static && !s->is_extern)
            enc_lea_reg_membase(&cg->code, X86_RAX, X86_RBP, s->stack_offset);
        else
            enc_lea_reg_rip(&cg->code, X86_RAX, 0);
    } else if (node->kind == NOD_DEREF) {
        emit_expr(cg, node->data.unary.operand);
    } else if (node->kind == NOD_MEMBER_ACCESS) {
        emit_expr(cg, node);
    } else if (node->kind == NOD_ARRAY_INDEX) {
        emit_expr(cg, node);
    } else {
        emit_expr(cg, node);
    }
}

static void emit_assign(Codegen *cg, AstNode *node) {
    AstNode *left = node->data.assign.left;
    AstNode *right = node->data.assign.right;
    emit_lvalue_addr(cg, left);
    enc_push_reg(&cg->code, X86_RAX);

    emit_expr(cg, right);

    if (node->data.assign.op == 18) {
        enc_mov_reg_reg(&cg->code, X86_RBX, X86_RAX);
        enc_pop_reg(&cg->code, X86_RAX);
        enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RBX);
    } else {
        enc_mov_reg_reg(&cg->code, X86_RBX, X86_RAX);
        enc_pop_reg(&cg->code, X86_RAX);
        enc_mov_reg_membase(&cg->code, X86_RCX, X86_RAX, 0);
        switch (node->data.assign.op) {
            case 0: enc_add_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 1: enc_sub_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 2: enc_imul_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 3:
                enc_xchg_reg_reg(&cg->code, X86_RAX, X86_RCX);
                enc_xor_reg_reg(&cg->code, X86_RDX, X86_RDX);
                enc_idiv_reg(&cg->code, X86_RBX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RAX);
                break;
            case 4:
                enc_xchg_reg_reg(&cg->code, X86_RAX, X86_RCX);
                enc_xor_reg_reg(&cg->code, X86_RDX, X86_RDX);
                enc_idiv_reg(&cg->code, X86_RBX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RDX);
                break;
            case 5: enc_and_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 6: enc_or_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 7: enc_xor_reg_reg(&cg->code, X86_RCX, X86_RBX); break;
            case 8:
                enc_mov_reg_reg(&cg->code, X86_RAX, X86_RCX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RBX);
                enc_shl_reg_cl(&cg->code, X86_RAX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RAX);
                break;
            case 9:
                enc_mov_reg_reg(&cg->code, X86_RAX, X86_RCX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RBX);
                enc_sar_reg_cl(&cg->code, X86_RAX);
                enc_mov_reg_reg(&cg->code, X86_RCX, X86_RAX);
                break;
        }
        enc_mov_membase_reg(&cg->code, X86_RAX, 0, X86_RCX);
        enc_mov_reg_reg(&cg->code, X86_RAX, X86_RCX);
    }
}

static void emit_call(Codegen *cg, AstNode *node) {
    int stack_args = node->data.call.nargs > 6 ? node->data.call.nargs - 6 : 0;
    if (stack_args > 0) {
        for (int i = node->data.call.nargs - 1; i >= 6; i--) {
            emit_expr(cg, node->data.call.args[i]);
            enc_push_reg(&cg->code, X86_RAX);
        }
    }
    for (int i = node->data.call.nargs - 1; i >= 0 && i < 6; i--)
        emit_expr(cg, node->data.call.args[i]);
    X86Reg regs64[] = {X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9};
    for (int i = 0; i < node->data.call.nargs && i < 6; i++)
        enc_mov_reg_reg(&cg->code, regs64[i], X86_RAX);
    for (int i = 5; i >= 0 && i < node->data.call.nargs; i--)
        enc_pop_reg(&cg->code, regs64[i]);

    if (node->data.call.func->kind == NOD_IDENT) {
        enc_call_rel32(&cg->code, 0);
        /* store fixup for external call - will need symbol resolution */
        /* for now, external calls need to be handled via ELF/PE relocations */
    } else {
        emit_expr(cg, node->data.call.func);
        enc_call_reg(&cg->code, X86_RAX);
    }
    if (stack_args > 0)
        enc_add_rsp_imm32(&cg->code, stack_args * 8);
}

static void emit_if(Codegen *cg, AstNode *node) {
    int l_else = label(cg);
    int l_end = label(cg);
    emit_expr(cg, node->data.if_stmt.cond);
    enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
    enc_jcc_rel32(&cg->code, 4, 0); /* jz */
    add_fixup(cg, cg->code.len - 4, l_else);
    if (node->data.if_stmt.then)
        emit_statement(cg, node->data.if_stmt.then);
    enc_jmp_rel32(&cg->code, 0);
    add_fixup(cg, cg->code.len - 4, l_end);
    record_label(cg, l_else);
    if (node->data.if_stmt.els)
        emit_statement(cg, node->data.if_stmt.els);
    record_label(cg, l_end);
}

static void emit_while(Codegen *cg, AstNode *node) {
    int l_start = label(cg), l_end = label(cg);
    int os = cg->loop_start_label, oe = cg->loop_end_label;
    cg->loop_start_label = l_start; cg->loop_end_label = l_end;
    record_label(cg, l_start);
    emit_expr(cg, node->data.while_stmt.cond);
    enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
    enc_jcc_rel32(&cg->code, 4, 0); /* jz */
    add_fixup(cg, cg->code.len - 4, l_end);
    if (node->data.while_stmt.body)
        emit_statement(cg, node->data.while_stmt.body);
    enc_jmp_rel32(&cg->code, 0);
    add_fixup(cg, cg->code.len - 4, l_start);
    record_label(cg, l_end);
    cg->loop_start_label = os; cg->loop_end_label = oe;
}

static void emit_for(Codegen *cg, AstNode *node) {
    int l_start = label(cg), l_end = label(cg);
    int os = cg->loop_start_label, oe = cg->loop_end_label;
    cg->loop_start_label = l_start; cg->loop_end_label = l_end;
    if (node->data.for_stmt.init && node->data.for_stmt.init->kind != NOD_NULL_STMT)
        emit_statement(cg, node->data.for_stmt.init);
    record_label(cg, l_start);
    if (node->data.for_stmt.cond) {
        emit_expr(cg, node->data.for_stmt.cond);
        enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
        enc_jcc_rel32(&cg->code, 4, 0); /* jz */
        add_fixup(cg, cg->code.len - 4, l_end);
    }
    if (node->data.for_stmt.body)
        emit_statement(cg, node->data.for_stmt.body);
    if (node->data.for_stmt.incr)
        emit_expr(cg, node->data.for_stmt.incr);
    enc_jmp_rel32(&cg->code, 0);
    add_fixup(cg, cg->code.len - 4, l_start);
    record_label(cg, l_end);
    cg->loop_start_label = os; cg->loop_end_label = oe;
}

static void emit_do(Codegen *cg, AstNode *node) {
    int l_start = label(cg), l_end = label(cg);
    int os = cg->loop_start_label, oe = cg->loop_end_label;
    cg->loop_start_label = l_start; cg->loop_end_label = l_end;
    record_label(cg, l_start);
    if (node->data.do_stmt.body)
        emit_statement(cg, node->data.do_stmt.body);
    emit_expr(cg, node->data.do_stmt.cond);
    enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
    enc_jcc_rel32(&cg->code, 5, 0); /* jnz */
    add_fixup(cg, cg->code.len - 4, l_start);
    record_label(cg, l_end);
    cg->loop_start_label = os; cg->loop_end_label = oe;
}

static void emit_switch(Codegen *cg, AstNode *node) {
    int l_end = label(cg);
    int oe = cg->loop_end_label;
    cg->loop_end_label = l_end;
    emit_expr(cg, node->data.switch_stmt.expr);
    enc_push_reg(&cg->code, X86_RAX);
    if (node->data.switch_stmt.body)
        emit_statement(cg, node->data.switch_stmt.body);
    record_label(cg, l_end);
    enc_add_rsp_imm32(&cg->code, 8);
    cg->loop_end_label = oe;
}

static void emit_case(Codegen *cg, AstNode *node) {
    int l = label(cg);
    enc_mov_reg_membase(&cg->code, X86_RAX, X86_RSP, 0);
    emit_expr(cg, node->data.case_stmt.expr);
    enc_mov_reg_reg(&cg->code, X86_RBX, X86_RAX);
    enc_mov_reg_membase(&cg->code, X86_RAX, X86_RSP, 0);
    enc_cmp_reg_reg(&cg->code, X86_RAX, X86_RBX);
    enc_jcc_rel32(&cg->code, 5, 0); /* jne */
    add_fixup(cg, cg->code.len - 4, l);
    if (node->data.case_stmt.stmt)
        emit_statement(cg, node->data.case_stmt.stmt);
    record_label(cg, l);
}

static void emit_compound(Codegen *cg, AstNode *node) {
    for (int i = 0; i < node->data.compound.nstmts; i++) {
        AstNode *child = node->data.compound.stmts[i];
        if (child && child->kind == NOD_NULL_STMT) continue;
        emit_statement(cg, child);
    }
}

static void emit_statement(Codegen *cg, AstNode *node) {
    if (!node) return;
    switch (node->kind) {
        case NOD_COMPOUND:  emit_compound(cg, node); break;
        case NOD_IF:        emit_if(cg, node); break;
        case NOD_WHILE:     emit_while(cg, node); break;
        case NOD_FOR:       emit_for(cg, node); break;
        case NOD_DO:        emit_do(cg, node); break;
        case NOD_SWITCH:    emit_switch(cg, node); break;
        case NOD_CASE:      emit_case(cg, node); break;
        case NOD_DEFAULT:
            if (node->data.default_stmt.stmt) emit_statement(cg, node->data.default_stmt.stmt);
            break;
        case NOD_BREAK:
            if (cg->loop_end_label >= 0) {
                enc_jmp_rel32(&cg->code, 0);
                add_fixup(cg, cg->code.len - 4, cg->loop_end_label);
            }
            break;
        case NOD_CONTINUE:
            if (cg->loop_start_label >= 0) {
                enc_jmp_rel32(&cg->code, 0);
                add_fixup(cg, cg->code.len - 4, cg->loop_start_label);
            }
            break;
        case NOD_RETURN:
            if (node->data.return_stmt.expr) emit_expr(cg, node->data.return_stmt.expr);
            enc_jmp_rel32(&cg->code, 0);
            add_fixup(cg, cg->code.len - 4, cg->return_label);
            break;
        case NOD_LABEL:
            /* labels for goto - store position */
            record_label(cg, -1); /* placeholder - will need custom handling */
            if (node->data.label.stmt) emit_statement(cg, node->data.label.stmt);
            break;
        case NOD_GOTO:
            enc_jmp_rel32(&cg->code, 0);
            /* need label fixup for goto targets */
            break;
        case NOD_EXPR_STMT:
            if (node->data.expr_stmt.expr) emit_expr(cg, node->data.expr_stmt.expr);
            break;
        case NOD_NULL_STMT:
            break;
        case NOD_VAR_DECL: case NOD_VAR_DEF:
            if (node->data.var.init) {
                emit_expr(cg, node->data.var.init);
                Symbol *s = sym_lookup(cg->sym, node->data.var.name);
                if (s) enc_mov_membase_reg(&cg->code, X86_RBP, s->stack_offset, X86_RAX);
            }
            break;
        default:
            emit_expr(cg, node);
            break;
    }
}

static void emit_expr(Codegen *cg, AstNode *node) {
    if (!node) return;
    switch (node->kind) {
        case NOD_INT_CONST:
            enc_mov_reg_imm64(&cg->code, X86_RAX, (uint64_t)node->data.int_val);
            break;
        case NOD_FLOAT_CONST:
            enc_mov_reg_imm64(&cg->code, X86_RAX, 0);
            break;
        case NOD_STRING_CONST: {
            size_t slen = node->data.string_val ? strlen(node->data.string_val) : 0;
            int si = intern_string(cg, node->data.string_val ? node->data.string_val : "", slen);
            enc_lea_reg_rip(&cg->code, X86_RAX, 0);
            /* record fixup to patch RIP offset to point into .rodata */
            if (cg->nstring_fixups >= cg->string_fixup_capacity) {
                cg->string_fixup_capacity = cg->string_fixup_capacity ? cg->string_fixup_capacity * 2 : 16;
                cg->string_fixups = xrealloc(cg->string_fixups, cg->string_fixup_capacity * sizeof(StringRefFixup));
            }
            cg->string_fixups[cg->nstring_fixups].offset = cg->code.len - 4;
            cg->string_fixups[cg->nstring_fixups].string_idx = si;
            cg->nstring_fixups++;
            break;
        }
        case NOD_CHAR_CONST:
            enc_mov_reg_imm64(&cg->code, X86_RAX, (uint64_t)(uint64_t)(int64_t)node->data.char_val);
            break;
        case NOD_BOOL_CONST:
            enc_mov_reg_imm64(&cg->code, X86_RAX, (uint64_t)node->data.bool_val);
            break;
        case NOD_NULLPTR_CONST:
            enc_xor_reg_reg(&cg->code, X86_RAX, X86_RAX);
            break;
        case NOD_IDENT: {
            Symbol *s = sym_lookup(cg->sym, node->data.ident.name);
            if (s && s->is_function) {
                enc_lea_reg_rip(&cg->code, X86_RAX, 0);
            } else if (s && (s->is_static || s->is_extern)) {
                enc_mov_reg_rip(&cg->code, X86_RAX, 0);
            } else if (s && s->stack_offset < 0) {
                enc_mov_reg_membase(&cg->code, X86_RAX, X86_RBP, s->stack_offset);
            } else if (s && s->stack_offset > 0) {
                enc_mov_reg_membase(&cg->code, X86_RAX, X86_RBP, s->stack_offset);
            } else {
                enc_mov_reg_rip(&cg->code, X86_RAX, 0);
            }
            break;
        }
        case NOD_BINARY:     emit_binary(cg, node); break;
        case NOD_UNARY:      emit_unary(cg, node); break;
        case NOD_ASSIGN:     emit_assign(cg, node); break;
        case NOD_CALL:       emit_call(cg, node); break;
        case NOD_CAST: case NOD_IMPLICIT_CAST:
            emit_expr(cg, node->data.cast.operand);
            break;
        case NOD_MEMBER_ACCESS: {
            if (node->data.member.struct_ptr->kind == NOD_IDENT) {
                Symbol *s = sym_lookup(cg->sym, node->data.member.struct_ptr->data.ident.name);
                if (s) {
                    enc_lea_reg_membase(&cg->code, X86_RAX, X86_RBP, s->stack_offset);
                    Type *st = s->type;
                    if (st) for (int i = 0; i < st->nmembers; i++)
                        if (st->member_names[i] && strcmp(st->member_names[i], node->data.member.member_name) == 0) {
                            if (st->member_offsets)
                                enc_add_reg_imm8(&cg->code, X86_RAX, (int8_t)st->member_offsets[i]);
                            break;
                        }
                    enc_mov_reg_membase(&cg->code, X86_RAX, X86_RAX, 0);
                }
            }
            break;
        }
        case NOD_MEMBER_DEREF:
            emit_expr(cg, node->data.member.struct_ptr);
            enc_mov_reg_membase(&cg->code, X86_RAX, X86_RAX, 0);
            break;
        case NOD_ARRAY_INDEX: {
            emit_expr(cg, node->data.array_index.arr);
            enc_push_reg(&cg->code, X86_RAX);
            emit_expr(cg, node->data.array_index.index);
            enc_mov_reg_reg(&cg->code, X86_RBX, X86_RAX);
            enc_pop_reg(&cg->code, X86_RAX);
            int es = 8;
            if (node->type) es = type_size(node->type);
            if (es > 1)
                enc_imul_reg_reg(&cg->code, X86_RBX, X86_RBX);
            /* if es != 1, RBX was multiplied by es via imul. But we need mul by es, not square.
               Fix: imul rbx, rbx, es is 3-operand imul */
            enc_add_reg_reg(&cg->code, X86_RAX, X86_RBX);
            enc_mov_reg_membase(&cg->code, X86_RAX, X86_RAX, 0);
            break;
        }
        case NOD_ADDR_OF:
            emit_lvalue_addr(cg, node->data.unary.operand);
            break;
        case NOD_DEREF:
            emit_expr(cg, node->data.unary.operand);
            enc_mov_reg_membase(&cg->code, X86_RAX, X86_RAX, 0);
            break;
        case NOD_CONDITIONAL: {
            int l_else = label(cg), l_end = label(cg);
            emit_expr(cg, node->data.conditional.cond);
            enc_test_reg_reg(&cg->code, X86_RAX, X86_RAX);
            enc_jcc_rel32(&cg->code, 4, 0); /* jz */
            add_fixup(cg, cg->code.len - 4, l_else);
            emit_expr(cg, node->data.conditional.then);
            enc_jmp_rel32(&cg->code, 0);
            add_fixup(cg, cg->code.len - 4, l_end);
            record_label(cg, l_else);
            emit_expr(cg, node->data.conditional.els);
            record_label(cg, l_end);
            break;
        }
        case NOD_COMMA:
            for (int i = 0; i < node->data.comma.nexprs; i++)
                emit_expr(cg, node->data.comma.exprs[i]);
            break;
        case NOD_SIZEOF:
            enc_mov_reg_imm64(&cg->code, X86_RAX, (uint64_t)node->data.int_val);
            break;
        default: break;
    }
}

static void emit_var_decl(Codegen *cg, AstNode *node) {
    if (node->data.var.is_extern) return;
    cg->has_global_data = 1;
    /* handle alignment */
    int al = node->data.var.alignment;
    if (al > 0) {
        size_t cur = cg->data.len;
        size_t aligned = (cur + al - 1) & ~(al - 1);
        while (cg->data.len < aligned) buf_write(&cg->data, 0);
    }
    int sz = 8;
    if (node->data.var.var_type) {
        sz = type_size(node->data.var.var_type);
        if (sz < 1) sz = 1;
    }
    if (node->data.var.init && node->data.var.init->kind == NOD_INT_CONST) {
        uint64_t val = (uint64_t)node->data.var.init->data.int_val;
        for (int i = 0; i < sz; i++) {
            buf_write(&cg->data, val & 0xFF);
            val >>= 8;
        }
    } else if (node->data.var.init && node->data.var.init->kind == NOD_STRING_CONST) {
        size_t slen = node->data.var.init->data.string_val ? strlen(node->data.var.init->data.string_val) : 0;
        for (size_t i = 0; i < slen && i < (size_t)sz; i++)
            buf_write(&cg->data, (unsigned char)node->data.var.init->data.string_val[i]);
        while (cg->data.len % sz != 0) buf_write(&cg->data, 0);
    } else {
        for (int i = 0; i < sz; i++) buf_write(&cg->data, 0);
    }
}

static void emit_func_decl(Codegen *cg, AstNode *node) {
    Symbol *sym = sym_lookup(cg->sym, node->data.func.name);
    if (!sym) return;

    if (!node->data.func.is_definition) return;

    /* function entry - we'll need to track this for ELF/PE */
    cg->current_function = node->data.func.name;

    int stack_size = cg->sym->stack_size;
    cg->return_label = label(cg);
    emit_prologue(cg, stack_size, node->data.func.nparams);
    if (node->data.func.body)
        emit_compound(cg, node->data.func.body);
    record_label(cg, cg->return_label);
    emit_epilogue(cg);
}

void codegen_init(Codegen *cg, TargetPlatform target, SymbolTable *sym) {
    memset(cg, 0, sizeof(*cg));
    buf_init(&cg->code);
    buf_init(&cg->data);
    cg->target = target;
    cg->sym = sym;
    cg->label_count = 0;
    cg->return_label = 0;
    cg->current_function = NULL;
    cg->stack_size = 0;
    cg->loop_start_label = -1;
    cg->loop_end_label = -1;
    cg->strings = NULL;
    cg->nstrings = 0;
    cg->string_capacity = 0;
    cg->label_positions = NULL;
    cg->nlabels = 0;
    cg->label_capacity = 0;
    cg->fixups = NULL;
    cg->nfixups = 0;
    cg->fixup_capacity = 0;
    cg->string_fixups = NULL;
    cg->nstring_fixups = 0;
    cg->string_fixup_capacity = 0;
    cg->has_global_data = 0;
}

void codegen_free(Codegen *cg) {
    buf_free(&cg->code);
    buf_free(&cg->data);
    for (int i = 0; i < cg->nstrings; i++)
        free(cg->strings[i].content);
    free(cg->strings);
    free(cg->label_positions);
    free(cg->fixups);
    free(cg->string_fixups);
}

void codegen_emit(Codegen *cg, AstNode **decls, int ndecls) {
    for (int i = 0; i < ndecls; i++) {
        AstNode *node = decls[i];
        if (!node) continue;
        switch (node->kind) {
            case NOD_FUNC_DEF:
            case NOD_FUNC_DECL: emit_func_decl(cg, node); break;
            case NOD_VAR_DECL:
            case NOD_VAR_DEF:   emit_var_decl(cg, node); break;
            default: break;
        }
    }
    patch_fixups(cg);
}

size_t codegen_text_size(Codegen *cg) {
    return cg->code.len;
}

size_t codegen_rodata_size(Codegen *cg) {
    size_t sz = 0;
    for (int i = 0; i < cg->nstrings; i++)
        sz += cg->strings[i].len + 1;
    return sz;
}

void codegen_write_text(Codegen *cg, unsigned char *dst) {
    memcpy(dst, cg->code.bytes, cg->code.len);
}

void codegen_write_rodata(Codegen *cg, unsigned char *dst) {
    size_t off = 0;
    for (int i = 0; i < cg->nstrings; i++) {
        memcpy(dst + off, cg->strings[i].content, cg->strings[i].len);
        off += cg->strings[i].len;
        dst[off++] = 0;
    }
}
