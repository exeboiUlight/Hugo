#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include "utils.h"

typedef struct Macro {
    char *name;
    char **params;
    int nparams;
    int is_variadic;
    int is_function_like;
    char *body;
    int is_builtin;
} Macro;

typedef struct {
    Macro **macros;
    int nmacros;
    int capacity;
    char **include_paths;
    int npaths;
    char *output;
    size_t output_len;
    size_t output_cap;
    const char *current_file;
    const char **include_stack;
    size_t *include_line_stack;
    int stack_depth;
    TargetPlatform target;
} Preprocessor;

void pp_init(Preprocessor *pp, TargetPlatform target);
void pp_add_include_path(Preprocessor *pp, const char *path);
void pp_define(Preprocessor *pp, const char *name, const char *body);
void pp_preprocess(Preprocessor *pp, const char *file, const char *source);
void pp_free(Preprocessor *pp);

#endif
