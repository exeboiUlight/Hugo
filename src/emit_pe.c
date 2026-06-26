#include "emit_pe.h"
#include <string.h>
#include <stdint.h>

#define PE_ALIGN 0x200
#define VA_BASE  0x140000000ULL

static void wr16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void wr32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void wr64(FILE *f, uint64_t v) { wr32(f, (uint32_t)(v)); wr32(f, (uint32_t)(v >> 32)); }

static uint32_t alignv(uint32_t v, uint32_t a) {
    return (v + a - 1) & ~(a - 1);
}

int pe_write(FILE *out, Codegen *cg) {
    /* Stub: call main; mov ecx, eax; jmp [rip+iat_off] → 13 bytes */
    size_t text_code_size = cg->code.len;
    size_t stub_size = 13;
    size_t text_total = text_code_size + stub_size;
    size_t rodata_strings = codegen_rodata_size(cg);
    size_t data_size = cg->data.len;

    /* Import structures that go into .rdata after strings */
    size_t import_idt_size  = 40;   /* 2 × 20 = one entry + null terminator */
    size_t import_ilt_size  = 16;   /* 2 × 8 */
    size_t import_hn_size   = 16;   /* ExitProcess hint/name (padded) */
    size_t import_dll_size  = 16;   /* "kernel32.dll\0" (padded) */
    size_t rodata_total = rodata_strings + import_idt_size + import_ilt_size
                          + import_hn_size + import_dll_size;

    /* IAT in .data (2 × 8 = 16 bytes) */
    size_t iat_size = 16;
    size_t data_total = data_size + iat_size;

    /* Virtual addresses */
    uint32_t text_rva = 0x1000;
    uint32_t rodata_rva = alignv(text_rva + (uint32_t)text_total, 0x1000);
    uint32_t data_rva = alignv(rodata_rva + (uint32_t)rodata_total, 0x1000);
    uint32_t image_size = alignv(data_rva + (uint32_t)data_total, 0x1000);

    /* File offsets */
    uint32_t text_raw_off  = 0x200;
    uint32_t rodata_raw_off = alignv(text_raw_off + (uint32_t)text_total, PE_ALIGN);
    uint32_t data_raw_off   = alignv(rodata_raw_off + (uint32_t)rodata_total, PE_ALIGN);
    uint32_t total_file     = alignv(data_raw_off + (uint32_t)data_total, PE_ALIGN);

    /* RVAs of import sub-structures */
    uint32_t idt_rva   = rodata_rva + (uint32_t)rodata_strings;
    uint32_t ilt_rva   = idt_rva + 40;
    uint32_t hn_rva    = ilt_rva + 16;
    uint32_t dll_rva   = hn_rva + 16;
    uint32_t iat_rva   = data_rva + (uint32_t)data_size;

    uint32_t entry_rva = text_rva;

    /* Build stub */
    unsigned char stub[13];
    int32_t call_rel = (int32_t)((text_rva + stub_size) - (text_rva + 5));
    stub[0] = 0xE8;
    stub[1] = call_rel & 0xFF;
    stub[2] = (call_rel >> 8) & 0xFF;
    stub[3] = (call_rel >> 16) & 0xFF;
    stub[4] = (call_rel >> 24) & 0xFF;
    stub[5] = 0x89; stub[6] = 0xC1;       /* mov ecx, eax */
    int32_t jmp_disp = (int32_t)(iat_rva - (entry_rva + stub_size - 6 + 6));
    stub[7] = 0xFF; stub[8] = 0x25;        /* jmp [rip+disp] */
    stub[9]  = jmp_disp & 0xFF;
    stub[10] = (jmp_disp >> 8) & 0xFF;
    stub[11] = (jmp_disp >> 16) & 0xFF;
    stub[12] = (jmp_disp >> 24) & 0xFF;

    /* DOS header */
    unsigned char dos[0x40];
    memset(dos, 0, sizeof(dos));
    dos[0] = 'M'; dos[1] = 'Z';
    dos[0x3C] = 0x40; /* e_lfanew */
    fwrite(dos, 1, sizeof(dos), out);

    /* PE signature */
    fwrite("PE\0\0", 4, 1, out);

    /* COFF header */
    wr16(out, 0x8664);           /* Machine: AMD64 */
    wr16(out, 3);                /* NumberOfSections */
    wr32(out, 0);                /* TimeDateStamp */
    wr32(out, 0);                /* PointerToSymbolTable */
    wr32(out, 0);                /* NumberOfSymbols */
    wr16(out, 0xF0);             /* SizeOfOptionalHeader = 240 */
    wr16(out, 0x2E);             /* Characteristics */

    /* Optional header PE32+ (240 bytes) */
    wr16(out, 0x020B);           /* magic */
    fputc(0, out); fputc(0, out);/* linker version */
    wr32(out, (uint32_t)text_total);     /* SizeOfCode */
    wr32(out, (uint32_t)rodata_total    /* SizeOfInitData */
                        + (uint32_t)data_total);
    wr32(out, 0);                /* SizeOfUninitData */
    wr32(out, entry_rva);        /* AddressOfEntryPoint */
    wr32(out, text_rva);         /* BaseOfCode */
    wr64(out, VA_BASE);          /* ImageBase */
    wr32(out, 0x1000);           /* SectionAlignment */
    wr32(out, PE_ALIGN);         /* FileAlignment */
    wr16(out, 6); wr16(out, 0);  /* OSVersion */
    wr16(out, 0); wr16(out, 0);  /* ImageVersion */
    wr16(out, 6); wr16(out, 0);  /* SubsystemVersion */
    wr32(out, 0);                /* Win32VersionValue */
    wr32(out, image_size);       /* SizeOfImage */
    wr32(out, 0x200);            /* SizeOfHeaders */
    wr32(out, 0);                /* CheckSum */
    wr16(out, 3);                /* Subsystem: CONSOLE */
    wr16(out, 0x140);            /* DllCharacteristics: NX | DYNAMIC_BASE | HIGH_ENTROPY_VA */
    wr64(out, 0x100000);         /* SizeOfStackReserve */
    wr64(out, 0x1000);           /* SizeOfStackCommit */
    wr64(out, 0x100000);         /* SizeOfHeapReserve */
    wr64(out, 0x1000);           /* SizeOfHeapCommit */
    wr32(out, 0);                /* LoaderFlags */
    wr32(out, 16);               /* NumberOfRvaAndSizes */

    /* Data directories: only import (index 1) */
    for (int i = 0; i < 16; i++) {
        if (i == 1) {
            wr32(out, idt_rva);    /* Import RVA */
            wr32(out, 40);         /* Import Size */
        } else {
            wr32(out, 0);
            wr32(out, 0);
        }
    }

    /* === Section headers === */

    /* .text */
    fwrite(".text\0\0\0", 8, 1, out);
    wr32(out, (uint32_t)text_total);
    wr32(out, text_rva);
    wr32(out, alignv((uint32_t)text_total, PE_ALIGN));
    wr32(out, text_raw_off);
    wr32(out, 0); wr32(out, 0); wr16(out, 0); wr16(out, 0);
    wr32(out, 0x60000020);       /* CODE | EXECUTE | READ */

    /* .rdata */
    fwrite(".rdata\0\0", 8, 1, out);
    wr32(out, (uint32_t)rodata_total);
    wr32(out, rodata_rva);
    wr32(out, alignv((uint32_t)rodata_total, PE_ALIGN));
    wr32(out, rodata_raw_off);
    wr32(out, 0); wr32(out, 0); wr16(out, 0); wr16(out, 0);
    wr32(out, 0x40000040);       /* INIT_DATA | READ */

    /* .data */
    fwrite(".data\0\0\0", 8, 1, out);
    wr32(out, (uint32_t)data_total);
    wr32(out, data_rva);
    wr32(out, alignv((uint32_t)data_total, PE_ALIGN));
    wr32(out, data_raw_off);
    wr32(out, 0); wr32(out, 0); wr16(out, 0); wr16(out, 0);
    wr32(out, 0xC0000040);       /* INIT_DATA | READ | WRITE */

    /* === Write section payloads === */

    /* .text: stub + user code */
    fseek(out, text_raw_off, SEEK_SET);
    fwrite(stub, 1, 13, out);
    {
        unsigned char *t = xmalloc(text_code_size ? text_code_size : 1);
        codegen_write_text(cg, t);
        uint64_t text_start_va = VA_BASE + text_rva + 13;
        uint64_t rodata_va = VA_BASE + rodata_rva;
        codegen_patch_string_refs(cg, t, text_start_va, rodata_va);
        fwrite(t, 1, text_code_size, out);
        free(t);
    }

    /* pad to rodata_raw_off */
    { long p = ftell(out);
      while ((unsigned long)p < rodata_raw_off) { fputc(0, out); p++; } }

    /* .rdata: user strings */
    if (rodata_strings > 0) {
        unsigned char *r = xmalloc(rodata_strings);
        codegen_write_rodata(cg, r);
        fwrite(r, 1, rodata_strings, out);
        free(r);
    }

    /* .rdata: Import Directory Table (2 entries × 20) */
    /* Entry 0: kernel32.dll */
    wr32(out, ilt_rva);            /* OriginalFirstThunk */
    wr32(out, 0);                  /* TimeDateStamp */
    wr32(out, 0);                  /* ForwarderChain */
    wr32(out, dll_rva);            /* Name */
    wr32(out, iat_rva);            /* FirstThunk */
    /* Entry 1: null terminator */
    wr32(out, 0); wr32(out, 0); wr32(out, 0); wr32(out, 0); wr32(out, 0);

    /* Import Lookup Table (2 × 8) */
    uint64_t ilt_entry = hn_rva;     /* bit63=0 → name import */
    wr64(out, ilt_entry);          /* ExitProcess (by name) */
    wr64(out, 0);                  /* null terminator */

    /* Hint/Name entry: ExitProcess */
    wr16(out, 0);                  /* hint = 0 */
    fwrite("ExitProcess\0", 1, 12, out);
    wr16(out, 0);                  /* padding */

    /* DLL name */
    fwrite("kernel32.dll\0", 1, 13, out);
    fputc(0, out); fputc(0, out); fputc(0, out); /* padding */

    /* pad to data_raw_off */
    { long p = ftell(out);
      while ((unsigned long)p < data_raw_off) { fputc(0, out); p++; } }

    /* .data: user data + IAT */
    if (data_size > 0)
        fwrite(cg->data.bytes, 1, data_size, out);

    /* IAT (same as ILT initially) */
    wr64(out, ilt_entry);
    wr64(out, 0);

    /* pad to total_file */
    { long p = ftell(out);
      while ((unsigned long)p < total_file) { fputc(0, out); p++; } }

    return 0;
}
