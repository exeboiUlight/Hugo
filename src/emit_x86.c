#include "emit_x86.h"
#include <stdlib.h>
#include <string.h>

void buf_init(ByteBuffer *buf) {
    memset(buf, 0, sizeof(*buf));
}

void buf_free(ByteBuffer *buf) {
    free(buf->bytes);
    buf->bytes = NULL;
    buf->len = buf->cap = 0;
}

void buf_write(ByteBuffer *buf, unsigned char byte) {
    if (buf->len >= buf->cap) {
        buf->cap = buf->cap ? buf->cap * 2 : 4096;
        buf->bytes = realloc(buf->bytes, buf->cap);
    }
    buf->bytes[buf->len++] = byte;
}

void buf_write32(ByteBuffer *buf, uint32_t val) {
    buf_write(buf, val & 0xFF);
    buf_write(buf, (val >> 8) & 0xFF);
    buf_write(buf, (val >> 16) & 0xFF);
    buf_write(buf, (val >> 24) & 0xFF);
}

void buf_write64(ByteBuffer *buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf_write(buf, val & 0xFF);
        val >>= 8;
    }
}

void buf_write_buf(ByteBuffer *buf, const unsigned char *src, size_t n) {
    for (size_t i = 0; i < n; i++) buf_write(buf, src[i]);
}

void buf_patch32(ByteBuffer *buf, size_t offset, uint32_t val) {
    if (offset + 4 > buf->len) return;
    buf->bytes[offset]     = val & 0xFF;
    buf->bytes[offset + 1] = (val >> 8) & 0xFF;
    buf->bytes[offset + 2] = (val >> 16) & 0xFF;
    buf->bytes[offset + 3] = (val >> 24) & 0xFF;
}

/* -- helpers -- */

static int reg4(X86Reg r) { return r & 7; }
static int rex_b(X86Reg r) { return (r >> 3) & 1; }
static int rex_r(X86Reg r) { return (r >> 3) & 1; }

static void rex_w(ByteBuffer *buf, X86Reg r, X86Reg rm) {
    unsigned char rex = 0x48; /* W=1 */
    if (rex_b(rm)) rex |= 1;
    if (rex_r(r))  rex |= 4;
    if (rex != 0x48) buf_write(buf, rex);
    else buf_write(buf, 0x48);
}

static void rex_wb(ByteBuffer *buf, X86Reg rm) {
    unsigned char rex = 0x48;
    if (rex_b(rm)) rex |= 1;
    if (rex != 0x48) buf_write(buf, rex);
    else buf_write(buf, 0x48);
}

static void modrm_reg(ByteBuffer *buf, int reg, int rm) {
    buf_write(buf, 0xC0 | (reg << 3) | rm);
}

static void modrm_mem(ByteBuffer *buf, int reg, int base_reg, int offset) {
    if (offset == 0 && base_reg != 5) {
        buf_write(buf, (reg << 3) | base_reg);
    } else if (offset >= -128 && offset <= 127) {
        buf_write(buf, 0x40 | (reg << 3) | base_reg);
        buf_write(buf, (unsigned char)(offset & 0xFF));
    } else {
        buf_write(buf, 0x80 | (reg << 3) | base_reg);
        buf_write32(buf, (uint32_t)offset);
    }
}

/* -- instructions -- */

void enc_push_reg(ByteBuffer *buf, X86Reg r) {
    if (r < X86_R8) {
        buf_write(buf, 0x50 + reg4(r));
    } else {
        buf_write(buf, 0x41);
        buf_write(buf, 0x50 + reg4(r));
    }
}

void enc_pop_reg(ByteBuffer *buf, X86Reg r) {
    if (r < X86_R8) {
        buf_write(buf, 0x58 + reg4(r));
    } else {
        buf_write(buf, 0x41);
        buf_write(buf, 0x58 + reg4(r));
    }
}

void enc_ret(ByteBuffer *buf) {
    buf_write(buf, 0xC3);
}

void enc_leave(ByteBuffer *buf) {
    buf_write(buf, 0xC9);
}

void enc_mov_reg_imm64(ByteBuffer *buf, X86Reg r, uint64_t imm) {
    if (r < X86_R8) {
        buf_write(buf, 0x48);
        buf_write(buf, 0xB8 + reg4(r));
    } else {
        buf_write(buf, 0x49);
        buf_write(buf, 0xB8 + reg4(r));
    }
    buf_write64(buf, imm);
}

void enc_mov_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    /* mov dst, src (dst = src) */
    rex_w(buf, dst, src);
    buf_write(buf, 0x8B);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_mov_reg_membase(ByteBuffer *buf, X86Reg dst, X86Reg base, int offset) {
    /* mov dst, [base+offset] */
    rex_w(buf, dst, base);
    buf_write(buf, 0x8B);
    modrm_mem(buf, reg4(dst), reg4(base), offset);
}

void enc_mov_membase_reg(ByteBuffer *buf, X86Reg base, int offset, X86Reg src) {
    /* mov [base+offset], src */
    rex_w(buf, src, base);
    buf_write(buf, 0x89);
    modrm_mem(buf, reg4(src), reg4(base), offset);
}

void enc_mov_reg_rip(ByteBuffer *buf, X86Reg dst, int32_t disp) {
    /* mov dst, [rip + disp] */
    rex_w(buf, dst, X86_RBP);
    buf_write(buf, 0x8B);
    /* mod=00, reg=dst, r/m=5 (RIP-relative) */
    buf_write(buf, (reg4(dst) << 3) | 5);
    buf_write32(buf, (uint32_t)disp);
}

void enc_mov_rip_reg(ByteBuffer *buf, X86Reg src, int32_t disp) {
    /* mov [rip + disp], src */
    rex_w(buf, src, X86_RBP);
    buf_write(buf, 0x89);
    buf_write(buf, (reg4(src) << 3) | 5);
    buf_write32(buf, (uint32_t)disp);
}

void enc_mov_reg_reg_sx(ByteBuffer *buf, X86Reg dst, X86Reg src, int sign_extend) {
    /* movsx/movzx: sign_extend=1 for movsx, 0 for movzx */
    rex_w(buf, dst, src);
    buf_write(buf, 0x0F);
    buf_write(buf, sign_extend ? 0xBE : 0xB6);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_lea_reg_membase(ByteBuffer *buf, X86Reg dst, X86Reg base, int offset) {
    rex_w(buf, dst, base);
    buf_write(buf, 0x8D);
    modrm_mem(buf, reg4(dst), reg4(base), offset);
}

void enc_lea_reg_rip(ByteBuffer *buf, X86Reg dst, int32_t disp) {
    rex_w(buf, dst, X86_RBP);
    buf_write(buf, 0x8D);
    buf_write(buf, (reg4(dst) << 3) | 5);
    buf_write32(buf, (uint32_t)disp);
}

void enc_add_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x03);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_sub_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x2B);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_add_reg_imm8(ByteBuffer *buf, X86Reg r, int8_t imm) {
    rex_wb(buf, r);
    buf_write(buf, 0x83);
    modrm_reg(buf, 0, reg4(r));
    buf_write(buf, (unsigned char)imm);
}

void enc_sub_reg_imm8(ByteBuffer *buf, X86Reg r, int8_t imm) {
    rex_wb(buf, r);
    buf_write(buf, 0x83);
    modrm_reg(buf, 5, reg4(r));
    buf_write(buf, (unsigned char)imm);
}

void enc_imul_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x0F);
    buf_write(buf, 0xAF);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_and_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x23);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_or_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x0B);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_xor_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    rex_w(buf, dst, src);
    buf_write(buf, 0x33);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_neg_reg(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xF7);
    modrm_reg(buf, 3, reg4(r));
}

void enc_not_reg(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xF7);
    modrm_reg(buf, 2, reg4(r));
}

void enc_shl_reg_cl(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xD3);
    modrm_reg(buf, 4, reg4(r));
}

void enc_sar_reg_cl(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xD3);
    modrm_reg(buf, 7, reg4(r));
}

void enc_shl_reg_imm8(ByteBuffer *buf, X86Reg r, uint8_t imm) {
    rex_wb(buf, r);
    buf_write(buf, 0xC1);
    modrm_reg(buf, 4, reg4(r));
    buf_write(buf, imm);
}

void enc_cmp_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b) {
    /* cmp a, b */
    rex_w(buf, a, b);
    buf_write(buf, 0x3B);
    modrm_reg(buf, reg4(a), reg4(b));
}

void enc_cmp_reg_imm(ByteBuffer *buf, X86Reg r, int32_t imm) {
    if (imm >= -128 && imm <= 127) {
        rex_wb(buf, r);
        buf_write(buf, 0x83);
        modrm_reg(buf, 7, reg4(r));
        buf_write(buf, (unsigned char)(imm & 0xFF));
    } else {
        rex_wb(buf, r);
        buf_write(buf, 0x81);
        modrm_reg(buf, 7, reg4(r));
        buf_write32(buf, (uint32_t)imm);
    }
}

void enc_test_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b) {
    rex_w(buf, a, b);
    buf_write(buf, 0x85);
    modrm_reg(buf, reg4(a), reg4(b));
}

void enc_setcc_reg(ByteBuffer *buf, int cc, X86Reg r) {
    /* cc: 0=o,1=no,2=b/nae,3=nb/ae,4=e/z,5=ne/nz,6=be/na,7=nbe/a,
           8=s,9=ns,10=p,11=np,12=l/nge,13=nl/ge,14=le/ng,15=nle/g */
    unsigned char rex = 0x40;
    if (rex_b(r)) rex |= 1;
    if (rex != 0x40) buf_write(buf, rex);
    buf_write(buf, 0x0F);
    buf_write(buf, 0x90 | (cc & 0xF));
    modrm_reg(buf, 0, reg4(r));
}

void enc_movzx_reg_reg(ByteBuffer *buf, X86Reg dst, X86Reg src) {
    /* movzx with 64-bit destination */
    rex_w(buf, dst, src);
    buf_write(buf, 0x0F);
    buf_write(buf, 0xB6);
    modrm_reg(buf, reg4(dst), reg4(src));
}

void enc_jmp_rel32(ByteBuffer *buf, int32_t offset) {
    buf_write(buf, 0xE9);
    buf_write32(buf, (uint32_t)offset);
}

void enc_jcc_rel32(ByteBuffer *buf, int cc, int32_t offset) {
    buf_write(buf, 0x0F);
    buf_write(buf, 0x80 | (cc & 0xF));
    buf_write32(buf, (uint32_t)offset);
}

void enc_call_rel32(ByteBuffer *buf, int32_t offset) {
    buf_write(buf, 0xE8);
    buf_write32(buf, (uint32_t)offset);
}

void enc_call_reg(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xFF);
    modrm_reg(buf, 2, reg4(r));
}

void enc_push_imm32(ByteBuffer *buf, int32_t imm) {
    buf_write(buf, 0x68);
    buf_write32(buf, (uint32_t)imm);
}

void enc_add_rsp_imm32(ByteBuffer *buf, int32_t imm) {
    rex_wb(buf, X86_RSP);
    buf_write(buf, 0x81);
    modrm_reg(buf, 0, 4);
    buf_write32(buf, (uint32_t)imm);
}

void enc_xchg_reg_reg(ByteBuffer *buf, X86Reg a, X86Reg b) {
    rex_w(buf, a, b);
    buf_write(buf, 0x87);
    modrm_reg(buf, reg4(a), reg4(b));
}

void enc_idiv_reg(ByteBuffer *buf, X86Reg r) {
    rex_wb(buf, r);
    buf_write(buf, 0xF7);
    modrm_reg(buf, 7, reg4(r));
}

void enc_mov_membase_imm32(ByteBuffer *buf, X86Reg base, int offset, int32_t imm) {
    rex_wb(buf, base);
    buf_write(buf, 0xC7);
    modrm_mem(buf, 0, reg4(base), offset);
    buf_write32(buf, (uint32_t)imm);
}
