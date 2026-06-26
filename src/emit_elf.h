#ifndef EMIT_ELF_H
#define EMIT_ELF_H

#include "codegen.h"
#include <stdio.h>

int elf_write(FILE *out, Codegen *cg, const char *entry_name);

#endif
