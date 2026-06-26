#include "preprocessor.h"
#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int pp_is_ident_start(int c) { return c == '_' || isalpha(c); }
static int pp_is_ident_cont(int c) { return c == '_' || isalnum(c); }
static int pp_hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* simple lightweight preprocessor */
static Macro *find_macro(Preprocessor *pp, const char *name) {
    for (int i = 0; i < pp->nmacros; i++)
        if (strcmp(pp->macros[i]->name, name) == 0)
            return pp->macros[i];
    return NULL;
}

static void pp_out_char(Preprocessor *pp, int c) {
    if (pp->output_len + 2 > pp->output_cap) {
        pp->output_cap = pp->output_cap ? pp->output_cap * 2 : 65536;
        pp->output = (char*)xrealloc(pp->output, pp->output_cap);
    }
    pp->output[pp->output_len++] = (char)c;
    pp->output[pp->output_len] = '\0';
}

static void pp_out_str(Preprocessor *pp, const char *s) {
    while (*s) pp_out_char(pp, *s++);
}

static int is_ws(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void pp_init(Preprocessor *pp, TargetPlatform target) {
    memset(pp, 0, sizeof(*pp));
    pp->target = target;
    pp_add_include_path(pp, ".");
    pp_add_include_path(pp, "libc/include");

    /* built-in macros */
    pp_define(pp, "__HUGO__", "1");
    pp_define(pp, "__STDC__", "1");
    pp_define(pp, "__STDC_VERSION__", "202311L");
    pp_define(pp, "__STDC_HOSTED__", "1");
    pp_define(pp, "__cplusplus", NULL); /* not C++ */

    if (target == TARGET_LINUX) {
        pp_define(pp, "__linux__", "1");
        pp_define(pp, "linux", "1");
        pp_define(pp, "__unix__", "1");
        pp_define(pp, "__ELF__", "1");
    } else {
        pp_define(pp, "_WIN32", "1");
        pp_define(pp, "_WIN64", "1");
        pp_define(pp, "__PE__", "1");
        pp_define(pp, "WIN32", "1");
        pp_define(pp, "WIN64", "1");
    }

    pp_define(pp, "__x86_64__", "1");
    pp_define(pp, "__x86_64", "1");
    pp_define(pp, "__amd64__", "1");

    /* C23 feature test macros */
    pp_define(pp, "__bool_true_false_are_defined", "1");
    pp_define(pp, "__STDC_NO_COMPLEX__", "1");
    pp_define(pp, "__STDC_NO_VLA__", "1");
}

void pp_add_include_path(Preprocessor *pp, const char *path) {
    pp->include_paths = (char**)xrealloc(pp->include_paths, (pp->npaths + 1) * sizeof(char*));
    pp->include_paths[pp->npaths++] = xstrdup(path);
}

void pp_define(Preprocessor *pp, const char *name, const char *body) {
    Macro *m = find_macro(pp, name);
    if (m) {
        free(m->body);
        m->body = body ? xstrdup(body) : NULL;
        return;
    }
    m = (Macro*)xcalloc(1, sizeof(Macro));
    m->name = xstrdup(name);
    m->body = body ? xstrdup(body) : NULL;
    m->is_builtin = 1;
    if (pp->nmacros >= pp->capacity) {
        pp->capacity = pp->capacity ? pp->capacity * 2 : 64;
        pp->macros = (Macro**)xrealloc(pp->macros, pp->capacity * sizeof(Macro*));
    }
    pp->macros[pp->nmacros++] = m;
}

static long long eval_pp_expr(Preprocessor *pp, const char *expr, int *ok);

static char *expand_macro_in_expr(Preprocessor *pp, const char *input) {
    /* simple macro expansion for conditionals */
    char *result = xstrdup(input);
    char temp[4096];
    int changed;
    do {
        changed = 0;
        int ri = 0, wi = 0;
        while (result[ri]) {
            if (result[ri] == '_' || isalpha((unsigned char)result[ri])) {
                char name[256]; int ni = 0;
                while ((result[ri] == '_' || isalnum((unsigned char)result[ri])) && ni < 255) name[ni++] = result[ri++];
                name[ni] = '\0';
                Macro *m = find_macro(pp, name);
                if (m && m->body) {
                    const char *body = m->body;
                    while (*body) temp[wi++] = *body++;
                    changed = 1;
                } else if (!m) {
                    temp[wi++] = '0';
                    changed = 1;
                } else {
                    memcpy(temp + wi, name, ni);
                    wi += ni;
                }
            } else {
                temp[wi++] = result[ri++];
            }
            if (wi > 4000) break;
        }
        temp[wi] = '\0';
        free(result);
        result = xstrdup(temp);
    } while (changed);
    return result;
}

static int pp_file_exists(Preprocessor *pp, const char *fname) {
    char full_path[MAX_PATH + MAX_PATH];
    for (int i = 0; i < pp->npaths; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", pp->include_paths[i], fname);
        FILE *f = fopen(full_path, "rb");
        if (f) { fclose(f); return 1; }
    }
    FILE *f = fopen(fname, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* parse header name from string like "file" or <file> or file */
static int pp_parse_header(const char **p, char *fname, int maxlen) {
    const char *s = *p;
    while (is_ws(*s)) s++;
    int flen = 0;
    if (*s == '<') {
        s++;
        while (*s && *s != '>' && flen < maxlen - 1) fname[flen++] = *s++;
        if (*s == '>') s++;
    } else if (*s == '\"') {
        s++;
        while (*s && *s != '\"' && flen < maxlen - 1) fname[flen++] = *s++;
        if (*s == '\"') s++;
    } else {
        while (*s && !is_ws(*s) && *s != ')' && flen < maxlen - 1) fname[flen++] = *s++;
    }
    fname[flen] = '\0';
    *p = s;
    return flen > 0;
}

static int pp_has_include(Preprocessor *pp, const char **p) {
    while (is_ws(**p)) (*p)++;
    if (**p != '(') return 0;
    (*p)++;
    while (is_ws(**p)) (*p)++;
    char fname[MAX_PATH];
    int found = pp_parse_header(p, fname, MAX_PATH) ? pp_file_exists(pp, fname) : 0;
    while (is_ws(**p)) (*p)++;
    if (**p == ')') (*p)++;
    return found;
}

static int pp_has_c_attribute(Preprocessor *pp, const char **p) {
    (void)pp;
    while (is_ws(**p)) (*p)++;
    if (**p != '(') return 0;
    (*p)++;
    while (is_ws(**p)) (*p)++;
    char attr[MAX_PATH];
    int ai = 0;
    while (**p && **p != ')' && ai < MAX_PATH - 1) attr[ai++] = *(*p)++;
    attr[ai] = '\0';
    static const char *known_attrs[] = {
        "nodiscard", "maybe_unused", "deprecated", "noreturn", "fallthrough",
        "gnu::format", "gnu::packed",
        NULL
    };
    int found = 0;
    for (int i = 0; known_attrs[i]; i++)
        if (strcmp(attr, known_attrs[i]) == 0) { found = 1; break; }
    while (is_ws(**p)) (*p)++;
    if (**p == ')') (*p)++;
    return found;
}

static long long eval_pp_primary(Preprocessor *pp, const char **p, int *ok) {
    while (is_ws(**p)) (*p)++;
    if (**p == '(') {
        (*p)++;
        long long v = eval_pp_expr(pp, *p, ok);
        if (!*ok) return 0;
        while (is_ws(**p)) (*p)++;
        if (**p == ')') { (*p)++; return v; }
        *ok = 0; return 0;
    }
    if (**p == '!') { (*p)++; return !eval_pp_primary(pp, p, ok); }
    if (**p == '~') { (*p)++; return ~eval_pp_primary(pp, p, ok); }
    if (**p == '-') { (*p)++; return -eval_pp_primary(pp, p, ok); }
    if (**p == '+') { (*p)++; return eval_pp_primary(pp, p, ok); }

    if (isdigit(**p)) {
        long long v = 0;
        if (**p == '0' && (*p)[1] == 'x') {
            *p += 2;
            while (pp_hex_val(**p) >= 0) { v = v * 16 + pp_hex_val(**p); (*p)++; }
        } else {
            while (isdigit(**p)) { v = v * 10 + (**p - '0'); (*p)++; }
        }
        return v;
    }

    if (**p == '\'') {
        (*p)++;
        long long v = **p;
        if (**p == '\\') { (*p)++; v = **p; }
        (*p)++;
        if (**p == '\'') (*p)++;
        return v;
    }

    /* handle __has_include and __has_c_attribute */
    if (strncmp(*p, "__has_include", 13) == 0 && !pp_is_ident_cont((*p)[13])) {
        *p += 13;
        return pp_has_include(pp, p);
    }
    if (strncmp(*p, "__has_c_attribute", 17) == 0 && !pp_is_ident_cont((*p)[17])) {
        *p += 17;
        return pp_has_c_attribute(pp, p);
    }

    if (pp_is_ident_start(**p)) {
        while (pp_is_ident_cont(**p)) (*p)++;
        /* undefined identifier = 0 in preprocessor */
        return 0;
    }

    *ok = 0;
    return 0;
}

static int pp_op_prec(Preprocessor *pp, const char **p) {
    while (is_ws(**p)) (*p)++;
    if (**p == 'd' && (*p)[1] == 'e' && (*p)[2] == 'f' && (*p)[3] == 'i' && (*p)[4] == 'n' && (*p)[5] == 'e' && (*p)[6] == 'd') {
        *p += 7;
        while (is_ws(**p)) (*p)++;
        if (**p == '(') (*p)++;
        while (is_ws(**p)) (*p)++;
        char name[256]; int ni = 0;
        while (pp_is_ident_cont(**p) && ni < 255) name[ni++] = *(*p)++;
        name[ni] = '\0';
        while (is_ws(**p)) (*p)++;
        if (**p == ')') (*p)++;
        return find_macro(pp, name) ? 1 : 0;
    }

    struct { const char *op; int prec; int tok; } ops[] = {
        {"||", 1, 0}, {"&&", 2, 0}, {"|", 3, 0}, {"^", 4, 0}, {"&", 5, 0},
        {"==", 6, 0}, {"!=", 6, 0},
        {"<=", 7, 0}, {">=", 7, 0}, {"<", 7, 0}, {">", 7, 0},
        {"<<", 8, 0}, {">>", 8, 0},
        {"+", 9, 0}, {"-", 9, 0},
        {"*", 10, 0}, {"/", 10, 0}, {"%", 10, 0},
        {NULL, 0, 0}
    };
    for (int i = 0; ops[i].op; i++) {
        int l = (int)strlen(ops[i].op);
        if (strncmp(*p, ops[i].op, l) == 0) {
            *p += l;
            return ops[i].prec;
        }
    }
    return -1;
}

static long long eval_pp_expr(Preprocessor *pp, const char *expr, int *ok) {
    const char *p = expr;
    long long val = 0;

    val = eval_pp_primary(pp, &p, ok);
    if (!*ok) return 0;

    while (1) {
        const char *save = p;
        int new_prec = pp_op_prec(pp, &p);
        if (new_prec < 0) { p = save; break; }

        long long rhs = eval_pp_primary(pp, &p, ok);
        if (!*ok) return 0;

        const char *op_str = save;
        int l = (int)(p - save);
        (void)l;

        if (strncmp(op_str, "||", 2) == 0) val = val || rhs;
        else if (strncmp(op_str, "&&", 2) == 0) val = val && rhs;
        else if (strncmp(op_str, "|", 1) == 0 && strncmp(op_str, "||", 2) != 0) val = val | rhs;
        else if (strncmp(op_str, "^", 1) == 0) val = val ^ rhs;
        else if (strncmp(op_str, "&", 1) == 0 && strncmp(op_str, "&&", 2) != 0) val = val & rhs;
        else if (strncmp(op_str, "==", 2) == 0) val = val == rhs;
        else if (strncmp(op_str, "!=", 2) == 0) val = val != rhs;
        else if (strncmp(op_str, "<=", 2) == 0) val = val <= rhs;
        else if (strncmp(op_str, ">=", 2) == 0) val = val >= rhs;
        else if (strncmp(op_str, "<", 1) == 0) val = val < rhs;
        else if (strncmp(op_str, ">", 1) == 0) val = val > rhs;
        else if (strncmp(op_str, "<<", 2) == 0) val = val << rhs;
        else if (strncmp(op_str, ">>", 2) == 0) val = val >> rhs;
        else if (strncmp(op_str, "+", 1) == 0) val = val + rhs;
        else if (strncmp(op_str, "-", 1) == 0) val = val - rhs;
        else if (strncmp(op_str, "*", 1) == 0) val = val * rhs;
        else if (strncmp(op_str, "/", 1) == 0) val = rhs ? val / rhs : 0;
        else if (strncmp(op_str, "%", 1) == 0) val = rhs ? val % rhs : 0;
    }
    return val;
}

static void pp_include_file(Preprocessor *pp, const char *filename, const char *caller_file, int caller_line) {
    /* strip quotes/angles */
    char fname[MAX_PATH];
    int flen = 0;
    const char *s = filename;
    if (*s == '<') {
        s++;
        while (*s && *s != '>') fname[flen++] = *s++;
    } else if (*s == '\"') {
        s++;
        while (*s && *s != '\"') fname[flen++] = *s++;
    } else {
        while (*s && !is_ws(*s)) fname[flen++] = *s++;
    }
    fname[flen] = '\0';

    /* search in include paths */
    char full_path[MAX_PATH + MAX_PATH];
    for (int i = 0; i < pp->npaths; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", pp->include_paths[i], fname);
        size_t len;
        char *content = read_file(full_path, &len);
        if (content) {
            pp_preprocess(pp, full_path, content);
            free(content);
            return;
        }
    }
    /* try the filename directly */
    size_t len;
    char *content = read_file(fname, &len);
    if (content) {
        pp_preprocess(pp, fname, content);
        free(content);
        return;
    }
    diag(DIAG_ERROR, caller_file, caller_line, 1, "cannot open include file '%s'", fname);
}

static void pp_handle_directive(Preprocessor *pp, const char *dir_line, const char *file, int linenum) {
    const char *p = dir_line;
    while (is_ws(*p)) p++;

    if (*p == '#') p++;
    while (is_ws(*p)) p++;

    if (strncmp(p, "include", 6) == 0 && is_ws(p[6])) {
        p += 6;
        while (is_ws(*p)) p++;
        pp_include_file(pp, p, file, linenum);
        return;
    }

    if (strncmp(p, "define", 6) == 0 && is_ws(p[6])) {
        p += 6;
        while (is_ws(*p)) p++;
        char name[256]; int ni = 0;
        while (pp_is_ident_cont(*p) && ni < 255) name[ni++] = *p++;
        name[ni] = '\0';
        if (*p == '(') {
            /* skip for now - simple object-like only */
        }
        while (is_ws(*p)) p++;
        size_t body_len = strlen(p);
        while (body_len > 0 && is_ws(p[body_len - 1])) body_len--;
        char *body = (char*)xmalloc(body_len + 1);
        memcpy(body, p, body_len);
        body[body_len] = '\0';
        pp_define(pp, name, body);
        free(body);
        return;
    }

    if (strncmp(p, "undef", 5) == 0) {
        p += 5;
        while (is_ws(*p)) p++;
        char name[256]; int ni = 0;
        while (pp_is_ident_cont(*p) && ni < 255) name[ni++] = *p++;
        name[ni] = '\0';
        for (int i = 0; i < pp->nmacros; i++) {
            if (strcmp(pp->macros[i]->name, name) == 0) {
                free(pp->macros[i]->name);
                free(pp->macros[i]->body);
                free(pp->macros[i]);
                pp->macros[i] = pp->macros[--pp->nmacros];
                break;
            }
        }
        return;
    }

    /* helper: evaluate if condition */
    if (strncmp(p, "ifdef", 5) == 0) {
        return;
    }

    if (strncmp(p, "ifndef", 6) == 0) {
        return;
    }

    if (strncmp(p, "elifdef", 7) == 0) {
        /* handled at directive level */
        return;
    }

    if (strncmp(p, "elifndef", 8) == 0) {
        return;
    }

    if (strncmp(p, "if", 2) == 0 && (is_ws(p[2]) || p[2] == '(')) {
        p += 2;
        while (is_ws(*p)) p++;
        char *expanded = expand_macro_in_expr(pp, p);
        free(expanded);
        return;
    }

    if (strncmp(p, "elif", 4) == 0) {
        return;
    }

    if (strncmp(p, "else", 4) == 0) {
        return;
    }

    if (strncmp(p, "endif", 5) == 0) {
        return;
    }

    if (strncmp(p, "line", 4) == 0) {
        return;
    }

    if (strncmp(p, "error", 5) == 0) {
        diag(DIAG_ERROR, file, linenum, 1, "%s", p + 5);
        return;
    }

    if (strncmp(p, "warning", 7) == 0) {
        diag(DIAG_WARNING, file, linenum, 1, "%s", p + 7);
        return;
    }

    if (strncmp(p, "embed", 5) == 0 && (is_ws(p[5]) || p[5] == '<' || p[5] == '\"')) {
        p += 5;
        while (is_ws(*p)) p++;
        char fname[MAX_PATH];
        pp_parse_header(&p, fname, MAX_PATH);
        while (is_ws(*p)) p++;
        /* parse optional parameters: limit(N), prefix(...), suffix(...), if_empty(...) */
        int limit = -1;
        /* skip any params for now - just read the whole file */
        size_t flen;
        char *content = NULL;
        char full_path[MAX_PATH + MAX_PATH];
        for (int i = 0; i < pp->npaths; i++) {
            snprintf(full_path, sizeof(full_path), "%s/%s", pp->include_paths[i], fname);
            content = read_file(full_path, &flen);
            if (content) break;
        }
        if (!content) content = read_file(fname, &flen);
        if (content) {
            int nbytes = (limit > 0 && (int)flen > limit) ? limit : (int)flen;
            for (int i = 0; i < nbytes; i++) {
                char hex[8];
                snprintf(hex, sizeof(hex), "%s0x%02X", i > 0 ? ", " : "", (unsigned char)content[i]);
                pp_out_str(pp, hex);
            }
            free(content);
        } else {
            /* if_empty - for now produce nothing if file missing */
        }
        return;
    }

    if (strncmp(p, "pragma", 6) == 0) {
        return; /* ignore pragmas */
    }

    /* unknown directive - pass through */
    pp_out_str(pp, dir_line);
    pp_out_char(pp, '\n');
}

void pp_preprocess(Preprocessor *pp, const char *file, const char *source) {
    if (pp->stack_depth >= MAX_INCLUDE_DEPTH) {
        diag(DIAG_ERROR, file, 0, 0, "include nesting depth exceeded");
        return;
    }

    if (pp->stack_depth == 0) {
        pp->output_len = 0;
        pp->output_cap = 0;
        pp->output = NULL;
    }

    if (!pp->include_stack) {
        pp->include_stack = (const char**)xcalloc(MAX_INCLUDE_DEPTH, sizeof(const char*));
        pp->include_line_stack = (size_t*)xcalloc(MAX_INCLUDE_DEPTH, sizeof(size_t));
    }
    pp->include_stack[pp->stack_depth] = file;
    pp->stack_depth++;

    /* line-by-line preprocessing */
    const char *p = source;
    int linenum = 1;
    char line_buf[MAX_LINE_LEN];
    int li = 0;
    int skipping = 0; /* 0=normal, 1=skipping, 2=looking for endif */
    int skip_count = 0;

    while (*p) {
        if (*p == '\n') {
            line_buf[li] = '\0';

            int is_dir = 0;
            /* check if line is a directive */
            const char *tp = line_buf;
            while (*tp == ' ' || *tp == '\t') tp++;
            if (*tp == '#') is_dir = 1;

            if (is_dir) {
                const char *dp = line_buf;
                while (*dp == ' ' || *dp == '\t') dp++;
                dp++; /* skip # */
                while (*dp == ' ' || *dp == '\t') dp++;

                if (strncmp(dp, "if", 2) == 0 && (dp[2] == ' ' || dp[2] == '\t' || dp[2] == '(')) {
                    if (skipping) {
                        skip_count++;
                    } else {
                        /* evaluate */
                        char *expanded = expand_macro_in_expr(pp, dp + 2);
                        int ok = 1;
                        long long val = eval_pp_expr(pp, expanded, &ok);
                        free(expanded);
                        if (!val) { skipping = 1; skip_count = 0; }
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "ifdef", 5) == 0 && (dp[5] == ' ' || dp[5] == '\t')) {
                    if (skipping) {
                        skip_count++;
                    } else {
                        const char *np = dp + 5;
                        while (*np == ' ' || *np == '\t') np++;
                        char name[256]; int ni = 0;
                        while (pp_is_ident_cont(*np) && ni < 255) name[ni++] = *np++;
                        name[ni] = '\0';
                        if (!find_macro(pp, name)) { skipping = 1; skip_count = 0; }
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "ifndef", 6) == 0 && (dp[6] == ' ' || dp[6] == '\t')) {
                    if (skipping) {
                        skip_count++;
                    } else {
                        const char *np = dp + 6;
                        while (*np == ' ' || *np == '\t') np++;
                        char name[256]; int ni = 0;
                        while (pp_is_ident_cont(*np) && ni < 255) name[ni++] = *np++;
                        name[ni] = '\0';
                        if (find_macro(pp, name)) { skipping = 1; skip_count = 0; }
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "elifdef", 7) == 0) {
                    if (!skipping && skip_count == 0) {
                        skipping = 1;
                        skip_count = 0;
                    } else if (skipping && skip_count == 0) {
                        const char *np = dp + 7;
                        while (*np == ' ' || *np == '\t') np++;
                        char name[256]; int ni = 0;
                        while (pp_is_ident_cont(*np) && ni < 255) name[ni++] = *np++;
                        name[ni] = '\0';
                        if (find_macro(pp, name)) skipping = 0;
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "elifndef", 8) == 0) {
                    if (!skipping && skip_count == 0) {
                        skipping = 1;
                        skip_count = 0;
                    } else if (skipping && skip_count == 0) {
                        const char *np = dp + 8;
                        while (*np == ' ' || *np == '\t') np++;
                        char name[256]; int ni = 0;
                        while (pp_is_ident_cont(*np) && ni < 255) name[ni++] = *np++;
                        name[ni] = '\0';
                        if (!find_macro(pp, name)) skipping = 0;
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "elif", 4) == 0 && (dp[4] == ' ' || dp[4] == '\t')) {
                    if (!skipping && skip_count == 0) {
                        skipping = 1;
                    } else if (skipping && skip_count == 0) {
                        char *expanded = expand_macro_in_expr(pp, dp + 4);
                        int ok = 1;
                        long long val = eval_pp_expr(pp, expanded, &ok);
                        free(expanded);
                        if (val) skipping = 0;
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "else", 4) == 0 && (dp[4] == '\0' || is_ws(dp[4]))) {
                    if (skip_count > 0) {
                        /* nothing */
                    } else if (skipping) {
                        skipping = 0;
                    } else {
                        skipping = 1;
                    }
                    p++; linenum++; li = 0;
                    continue;
                }

                if (strncmp(dp, "endif", 5) == 0 && (dp[5] == '\0' || is_ws(dp[5]))) {
                    if (skip_count > 0) skip_count--;
                    else skipping = 0;
                    p++; linenum++; li = 0;
                    continue;
                }

                if (!skipping) {
                    pp_handle_directive(pp, line_buf, file, linenum);
                }
            } else if (!skipping) {
                /* normal line - output with macro expansion */
                const char *cp = line_buf;
                while (*cp) {
                    if (*cp == '/' && *(cp+1) == '/') break;
                    if (*cp == '/' && *(cp+1) == '*') {
                        cp += 2;
                        while (*cp && !(*cp == '*' && *(cp+1) == '/')) cp++;
                        if (*cp) cp += 2;
                        continue;
                    }
                    if (pp_is_ident_start(*cp)) {
                        char name[256]; int ni = 0;
                        while (pp_is_ident_cont(*cp) && ni < 255) name[ni++] = *cp++;
                        name[ni] = '\0';
                        Macro *m = find_macro(pp, name);
                        if (m && m->body) {
                            pp_out_str(pp, m->body);
                        } else {
                            pp_out_str(pp, name);
                        }
                    } else if (*cp == '\"') {
                        pp_out_char(pp, *cp++);
                        while (*cp && *cp != '\"') { if (*cp == '\\') pp_out_char(pp, *cp++); pp_out_char(pp, *cp++); }
                        if (*cp) pp_out_char(pp, *cp++);
                    } else if (*cp == '\'') {
                        pp_out_char(pp, *cp++);
                        while (*cp && *cp != '\'') { if (*cp == '\\') pp_out_char(pp, *cp++); pp_out_char(pp, *cp++); }
                        if (*cp) pp_out_char(pp, *cp++);
                    } else {
                        pp_out_char(pp, *cp++);
                    }
                }
                pp_out_char(pp, '\n');
            }

            p++; linenum++; li = 0;
        } else {
            if (li < MAX_LINE_LEN - 1) line_buf[li++] = *p;
            p++;
        }
    }

    pp->stack_depth--;
}

void pp_free(Preprocessor *pp) {
    for (int i = 0; i < pp->nmacros; i++) {
        free(pp->macros[i]->name);
        free(pp->macros[i]->body);
        free(pp->macros[i]->params);
        free(pp->macros[i]);
    }
    free(pp->macros);
    for (int i = 0; i < pp->npaths; i++) free(pp->include_paths[i]);
    free(pp->include_paths);
    free(pp->output);
    free(pp->include_stack);
    free(pp->include_line_stack);
}
