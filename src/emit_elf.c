#include "emit_elf.h"
#include <string.h>
#include <stdint.h>

#define ELF_BASE 0x400000
#define PGSIZE 4096

/* ELF64 types */
typedef struct { unsigned char e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version; uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags; uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } __attribute__((packed)) Ehdr;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } __attribute__((packed)) Phdr;
typedef struct { uint32_t sh_name, sh_type; uint64_t sh_flags, sh_addr, sh_offset, sh_size; uint32_t sh_link, sh_info; uint64_t sh_addralign, sh_entsize; } __attribute__((packed)) Shdr;

static void write_ehdr(FILE *f, uint64_t entry, uint64_t phoff, uint64_t shoff, uint16_t phnum, uint16_t shnum, uint16_t shstrndx) {
    Ehdr h;
    memset(&h, 0, sizeof(h));
    h.e_ident[0] = 0x7F; h.e_ident[1] = 'E'; h.e_ident[2] = 'L'; h.e_ident[3] = 'F';
    h.e_ident[4] = 2; /* 64-bit */
    h.e_ident[5] = 1; /* little-endian */
    h.e_ident[6] = 1; /* version */
    h.e_type = 2; /* ET_EXEC */
    h.e_machine = 62; /* EM_X86_64 */
    h.e_version = 1;
    h.e_entry = entry;
    h.e_phoff = phoff;
    h.e_shoff = shoff;
    h.e_flags = 0;
    h.e_ehsize = 64;
    h.e_phentsize = 56;
    h.e_phnum = phnum;
    h.e_shentsize = 64;
    h.e_shnum = shnum;
    h.e_shstrndx = shstrndx;
    fwrite(&h, sizeof(h), 1, f);
}

static void write_phdr(FILE *f, uint32_t type, uint32_t flags, uint64_t off, uint64_t vaddr, uint64_t filesz, uint64_t memsz) {
    Phdr p;
    memset(&p, 0, sizeof(p));
    p.p_type = type;
    p.p_flags = flags;
    p.p_offset = off;
    p.p_vaddr = vaddr;
    p.p_paddr = vaddr;
    p.p_filesz = filesz;
    p.p_memsz = memsz;
    p.p_align = PGSIZE;
    fwrite(&p, sizeof(p), 1, f);
}

static void write_shdr(FILE *f, uint32_t name, uint32_t type, uint64_t flags, uint64_t addr, uint64_t offset, uint64_t size, uint64_t align) {
    Shdr s;
    memset(&s, 0, sizeof(s));
    s.sh_name = name;
    s.sh_type = type;
    s.sh_flags = flags;
    s.sh_addr = addr;
    s.sh_offset = offset;
    s.sh_size = size;
    s.sh_addralign = align;
    fwrite(&s, sizeof(s), 1, f);
}

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

int elf_write(FILE *out, Codegen *cg, const char *entry_name) {
    (void)entry_name;

    size_t text_size = cg->code.len;
    size_t rodata_size = codegen_rodata_size(cg);
    size_t data_size = cg->data.len;

    uint64_t rodata_file_off = align_up(0x200 + text_size, 16);
    uint64_t data_file_off = align_up(rodata_file_off + rodata_size, 16);

    uint64_t text_vaddr = ELF_BASE + 0x200;
    uint64_t rodata_vaddr = ELF_BASE + rodata_file_off;
    uint64_t data_vaddr = ELF_BASE + data_file_off;

    uint64_t entry = text_vaddr;

    const char *shstr = "\0.text\0.rodata\0.data\0.shstrtab";
    size_t shstr_len = strlen(shstr) + 1;
    uint64_t shstr_off = 64 + 2 * 56 + 4 * 64;
    uint64_t shstr_vaddr = ELF_BASE + shstr_off;

    write_ehdr(out, entry, 64, 64 + 2 * 56, 2, 4, 3);

    uint64_t rx_size = rodata_file_off - 0x200;
    write_phdr(out, 1, 5, 0x200, text_vaddr, rx_size, rx_size);
    write_phdr(out, 1, 6, data_file_off, data_vaddr, data_size, data_size);

    write_shdr(out, 1, 1, 2 | 4, text_vaddr, 0x200, text_size, 16);
    write_shdr(out, 8, 1, 2, rodata_vaddr, rodata_file_off, rodata_size, 16);
    write_shdr(out, 14, 1, 2 | 1, data_vaddr, data_file_off, data_size, 16);
    write_shdr(out, 20, 3, 0, shstr_vaddr, shstr_off, shstr_len, 1);

    fwrite(shstr, 1, shstr_len, out);

    /* pad to 0x200 */
    { long pos = ftell(out);
      while (pos < 0x200) { fputc(0, out); pos++; } }

    /* .text */
    { unsigned char *buf = xmalloc(text_size ? text_size : 1);
      codegen_write_text(cg, buf);
      codegen_patch_string_refs(cg, buf, text_vaddr, rodata_vaddr);
      fwrite(buf, 1, text_size, out); free(buf); }

    /* pad to rodata offset */
    { long pos = ftell(out);
      while ((unsigned long)pos < rodata_file_off) { fputc(0, out); pos++; } }

    /* .rodata */
    { unsigned char *buf = xmalloc(rodata_size ? rodata_size : 1);
      codegen_write_rodata(cg, buf);
      fwrite(buf, 1, rodata_size, out); free(buf); }

    /* pad to data offset */
    { long pos = ftell(out);
      while ((unsigned long)pos < data_file_off) { fputc(0, out); pos++; } }

    /* .data */
    if (data_size > 0)
        fwrite(cg->data.bytes, 1, data_size, out);

    return 0;
}
