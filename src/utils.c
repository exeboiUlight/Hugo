#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void diag(DiagLevel level, const char *file, int line, int col, const char *fmt, ...) {
    const char *prefix;
    switch (level) {
        case DIAG_ERROR:   prefix = "error";   break;
        case DIAG_WARNING: prefix = "warning"; break;
        case DIAG_NOTE:    prefix = "note";    break;
        default:           prefix = "unknown"; break;
    }
    fprintf(stderr, "%s:%d:%d: %s: ", file ? file : "<unknown>", line, col, prefix);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    if (level == DIAG_ERROR) exit(1);
}

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p && size) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p && nmemb && size) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

char *xstrdup(const char *s) {
    size_t len = strlen(s);
    char *p = (char*)xmalloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { diag(DIAG_ERROR, path, 0, 0, "cannot open file"); return NULL; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) { fclose(f); diag(DIAG_ERROR, path, 0, 0, "cannot determine file size"); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)xmalloc((size_t)flen + 2);
    size_t nread = fread(buf, 1, (size_t)flen, f);
    fclose(f);
    buf[nread] = '\n';
    buf[nread + 1] = '\0';
    if (len) *len = nread;
    return buf;
}
