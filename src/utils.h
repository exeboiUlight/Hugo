#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define VERSION "0.1.0"
#define MAX_PATH 4096
#define MAX_TOKENS 1048576
#define MAX_STR_LIT 65536
#define MAX_INCLUDE_DEPTH 64
#define MAX_MACRO_ARGS 128
#define MAX_ENUM_MEMBERS 65536
#define MAX_STRUCT_MEMBERS 65536
#define MAX_FUNC_PARAMS 128
#define MAX_LOCAL_VARS 65536
#define MAX_LINE_LEN 131072
#define HASH_SIZE 8192

typedef enum {
    TARGET_LINUX,
    TARGET_WINDOWS
} TargetPlatform;

typedef enum {
    ARCH_X86_64
} Architecture;

typedef struct {
    TargetPlatform platform;
    Architecture arch;
    int optimize_level;
    int emit_debug;
    char output_path[MAX_PATH];
    char asm_path[MAX_PATH];
} CompilerOptions;

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE
} DiagLevel;

void diag(DiagLevel level, const char *file, int line, int col, const char *fmt, ...);
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *read_file(const char *path, size_t *len);

#endif
