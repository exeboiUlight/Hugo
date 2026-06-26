#include <stdio.h>
#include <string.h>
#include "lexer.h"
#include "preprocessor.h"
#include "parser.h"
#include "codegen.h"
#include "platform.h"
#include "emit_elf.h"
#include "emit_pe.h"

static void usage(void) {
    fprintf(stderr, "Hugo C23 Compiler v%s\n", VERSION);
    fprintf(stderr, "Usage: hugo [options] file.c\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>    Output file\n");
    fprintf(stderr, "  -target      Target platform (linux, windows)\n");
    fprintf(stderr, "  -O<level>    Optimization level (default 0)\n");
    fprintf(stderr, "  -I<dir>      Add include directory\n");
    fprintf(stderr, "  -D<def>      Define macro\n");
    fprintf(stderr, "  -h           Show help\n");
}

static void replace_ext(char *buf, size_t sz, const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    size_t base_len = dot ? (size_t)(dot - path) : strlen(path);
    if (base_len >= sz) base_len = sz - 1;
    memcpy(buf, path, base_len);
    buf[base_len] = '\0';
    strncat(buf, ext, sz - strlen(buf) - 1);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }

    CompilerOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.platform = DEFAULT_TARGET;
    opts.arch = ARCH_X86_64;
    opts.optimize_level = 0;
    const char *input_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            strncpy(opts.output_path, argv[++i], MAX_PATH - 1);
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opts.optimize_level = atoi(argv[i] + 2);
        } else if (strncmp(argv[i], "-I", 2) == 0) {
        } else if (strncmp(argv[i], "-D", 2) == 0) {
        } else if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "linux") == 0) opts.platform = TARGET_LINUX;
            else if (strcmp(argv[i], "windows") == 0) opts.platform = TARGET_WINDOWS;
            else { fprintf(stderr, "Unknown target: %s\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(); return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_file) { usage(); return 1; }

    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) return 1;

    Preprocessor pp;
    pp_init(&pp, opts.platform);
    pp_preprocess(&pp, input_file, source);
    free(source);

    if (!pp.output) {
        fprintf(stderr, "Preprocessing produced no output\n");
        pp_free(&pp);
        return 1;
    }

    Lexer lex;
    lexer_init(&lex, input_file, pp.output, opts.platform);
    lexer_lex(&lex);

    Parser parser;
    parser_init(&parser, lex.tokens, lex.ntokens, input_file, opts.platform);
    parser_parse(&parser);

    Codegen cg;
    codegen_init(&cg, opts.platform, parser.sym);
    codegen_emit(&cg, parser.global_decls, parser.nglobal_decls);

    /* Determine output path */
    char out_path[MAX_PATH];
    if (opts.output_path[0]) {
        snprintf(out_path, MAX_PATH, "%s", opts.output_path);
    } else {
        replace_ext(out_path, sizeof(out_path), input_file, target_output_ext(opts.platform));
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot open output file: %s\n", out_path);
        return 1;
    }

    int ret;
    if (opts.platform == TARGET_LINUX) {
        ret = elf_write(out, &cg, "main");
    } else {
        ret = pe_write(out, &cg);
    }

    fclose(out);

    if (ret != 0) {
        fprintf(stderr, "Failed to write executable\n");
        remove(out_path);
        return 1;
    }

    printf("Output: %s\n", out_path);

    codegen_free(&cg);
    lexer_free(&lex);
    parser_free(&parser);
    pp_free(&pp);

    return 0;
}
