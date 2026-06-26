#ifndef EMIT_X86_H
#define EMIT_X86_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    unsigned char *bytes;
    size_t len;
    size_t cap;
} ByteBuffer;

void buf_init(ByteBuffer *buf);
void buf_free(ByteBuffer *buf);
void buf_write(ByteBuffer *buf, unsigned char byte);
void buf_write32(ByteBuffer *buf, uint32_t val);
void buf_write64(ByteBuffer *buf, uint64_t val);
void buf_write_buf(ByteBuffer *buf, const unsigned char *src, size_t n);
void buf_patch32(ByteBuffer *buf, size_t offset, uint32_t val);

typedef enum {
    X86_RAX, X86_RCX, X86_RDX, X86_RBX,
    X86_RSP, X86_RBP, X86_RSI, X86_RDI,
    X86_R8,  X86_R9,  X86_R10, X86_R11,
    X86_R12, X86_R13, X86_R14, X86_R15
} X86Reg;

void enc_push_reg(ByteBuffer *buf, X86Reg r);
void enc_pop_reg(ByteBuffer *buf, X86Reg r);
void enc_ret(ByteBuffer *buf);
void enc_leave(ByteBuffer *buf);

void enc_mov_reg_imm64(ByteBuffer *buf, X86Reg r, uint64_t imm);
void enc_mov_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_mov_reg_membase(ByteBuffer *buf, X86Reg dst, X86Reg base, int offset);
void enc_mov_membase_reg(ByteBuffer *buf, X86Reg base, int offset, X86Reg src);
void enc_mov_reg_rip(ByteBuffer *buf, X86Reg dst, int32_t disp);
void enc_mov_rip_reg(ByteBuffer *buf, X86Reg src, int32_t disp);
void enc_mov_reg_reg_sx(ByteBuffer *buf, X86Reg dst, X86Reg src, int sign_extend);

void enc_lea_reg_membase(ByteBuffer *buf, X86Reg dst, X86Reg base, int offset);
void enc_lea_reg_rip(ByteBuffer *buf, X86Reg dst, int32_t disp);

void enc_add_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_sub_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_add_reg_imm8(ByteBuffer *buf, X86Reg r, int8_t imm);
void enc_sub_reg_imm8(ByteBuffer *buf, X86Reg r, int8_t imm);
void enc_imul_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_and_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_or_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_xor_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);
void enc_neg_reg(ByteBuffer *buf, X86Reg r);
void enc_not_reg(ByteBuffer *buf, X86Reg r);

void enc_shl_reg_cl(ByteBuffer *buf, X86Reg r);
void enc_sar_reg_cl(ByteBuffer *buf, X86Reg r);
void enc_shl_reg_imm8(ByteBuffer *buf, X86Reg r, uint8_t imm);

void enc_cmp_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b);
void enc_cmp_reg_imm(ByteBuffer *buf, X86Reg r, int32_t imm);
void enc_test_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b);

void enc_setcc_reg(ByteBuffer *buf, int cc, X86Reg r);
void enc_movzx_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src);

void enc_jmp_rel32(ByteBuffer *buf, int32_t offset);
void enc_jcc_rel32(ByteBuffer *buf, int cc, int32_t offset);
void enc_call_rel32(ByteBuffer *buf, int32_t offset);
void enc_call_reg(ByteBuffer *buf, X86Reg r);

void enc_push_imm32(ByteBuffer *buf, int32_t imm);
void enc_add_rsp_imm32(ByteBuffer *buf, int32_t imm);

void enc_xchg_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b);
void enc_idiv_reg(ByteBuffer *buf, X86Reg r);

void enc_mov_membase_imm32(ByteBuffer *buf, X86Reg base, int offset, int32_t imm);

#endif
