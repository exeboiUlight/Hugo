#include "lexer.h"
#include <ctype.h>

static int is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int is_ident_cont(int c) {
    return isalnum(c) || c == '_';
}

static int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void lexer_init(Lexer *l, const char *file, const char *source, TargetPlatform target) {
    l->buf = (char*)source;
    l->len = strlen(source);
    l->pos = 0;
    l->file = file;
    l->line = 1;
    l->col = 1;
    l->bol = 0;
    l->tokens = NULL;
    l->ntokens = 0;
    l->maxtokens = 0;
    l->in_preprocessing = 0;
    l->target = target;
}

static void add_token(Lexer *l, TokenKind kind) {
    if (l->ntokens >= l->maxtokens) {
        l->maxtokens = l->maxtokens ? l->maxtokens * 2 : 4096;
        l->tokens = (Token*)xrealloc(l->tokens, l->maxtokens * sizeof(Token));
    }
    Token *t = &l->tokens[l->ntokens++];
    memset(t, 0, sizeof(Token));
    t->kind = kind;
    t->file = l->file;
    t->line = l->line;
    t->col = l->col;
}

static int peek(Lexer *l, int skip) {
    size_t p = l->pos + skip;
    if (p >= l->len) return EOF;
    return (unsigned char)l->buf[p];
}

static int advance(Lexer *l) {
    if (l->pos >= l->len) return EOF;
    int c = (unsigned char)l->buf[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; l->bol = (int)l->pos; }
    else l->col++;
    return c;
}

static void skip_whitespace_and_comments(Lexer *l) {
    for (;;) {
        int c = peek(l, 0);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            advance(l);
            continue;
        }
        if (c == '/') {
            int c2 = peek(l, 1);
            if (c2 == '/') {
                while (peek(l, 0) != EOF && peek(l, 0) != '\n') advance(l);
                continue;
            }
            if (c2 == '*') {
                advance(l); advance(l);
                while (1) {
                    if (peek(l, 0) == EOF) { diag(DIAG_ERROR, l->file, l->line, l->col, "unterminated comment"); return; }
                    if (peek(l, 0) == '*' && peek(l, 1) == '/') { advance(l); advance(l); break; }
                    advance(l);
                }
                continue;
            }
        }
        break;
    }
}

static int lex_string(Lexer *l, int delimiter) {
    add_token(l, TOK_STRING);
    Token *t = &l->tokens[l->ntokens - 1];
    char buf[MAX_STR_LIT];
    int bufpos = 0;
    while (1) {
        int c = advance(l);
        if (c == EOF) { diag(DIAG_ERROR, l->file, l->line, l->col, "unterminated string"); break; }
        if (c == delimiter) break;
        if (c == '\\') {
            int c2 = advance(l);
            if (c2 == EOF) break;
            switch (c2) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '0': c = '\0'; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                case '\"': c = '\"'; break;
                case 'x': {
                    int h1 = hex_val(peek(l,0)), h2 = hex_val(peek(l,1));
                    if (h1 >= 0) { c = h1; advance(l); if (h2 >= 0) { c = c * 16 + h2; advance(l); } }
                    break;
                }
                default: c = c2; break;
            }
        }
        if (bufpos < MAX_STR_LIT - 1) buf[bufpos++] = (char)c;
    }
    buf[bufpos] = '\0';
    t->sval = xstrdup(buf);
    t->slen = bufpos;
    return 1;
}

static int lex_char(Lexer *l) {
    add_token(l, TOK_CHAR_LIT);
    Token *t = &l->tokens[l->ntokens - 1];
    int c = advance(l);
    if (c == EOF) { diag(DIAG_ERROR, l->file, l->line, l->col, "unterminated char constant"); return 1; }
    if (c == '\\') {
        int c2 = advance(l);
        if (c2 == EOF) return 1;
        switch (c2) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            case '\"': c = '\"'; break;
            default: c = c2; break;
        }
    }
    t->ival = c;
    if (advance(l) != '\'') diag(DIAG_ERROR, l->file, l->line, l->col, "expected closing quote in char constant");
    return 1;
}

static int lex_number(Lexer *l) {
    char buf[MAX_LINE_LEN];
    int bip = 0;

    if (peek(l, 0) == '0' && (peek(l, 1) == 'x' || peek(l, 1) == 'X')) {
        buf[bip++] = advance(l); buf[bip++] = advance(l);
        while (hex_val(peek(l, 0)) >= 0 || peek(l, 0) == '\'') {
            if (peek(l, 0) != '\'') buf[bip++] = (char)advance(l); else advance(l);
        }
        buf[bip] = '\0';
        add_token(l, TOK_INTEGER);
        Token *t = &l->tokens[l->ntokens - 1];
        t->uval = strtoull(buf, NULL, 16);
        t->ival = (long long)t->uval;
        t->is_unsigned = (t->uval > 0x7FFFFFFFFFFFFFFFULL);
        return 1;
    }
    if (peek(l, 0) == '0' && (peek(l, 1) == 'b' || peek(l, 1) == 'B')) {
        buf[bip++] = advance(l); buf[bip++] = advance(l);
        while (peek(l, 0) == '0' || peek(l, 0) == '1' || peek(l, 0) == '\'') {
            if (peek(l, 0) != '\'') buf[bip++] = (char)advance(l); else advance(l);
        }
        buf[bip] = '\0';
        add_token(l, TOK_INTEGER);
        Token *t = &l->tokens[l->ntokens - 1];
        t->uval = strtoull(buf + 2, NULL, 2);
        t->ival = (long long)t->uval;
        t->is_unsigned = (t->uval > 0x7FFFFFFFFFFFFFFFULL);
        return 1;
    }

    int is_float = 0;
    if (peek(l, 0) == '0') {
        buf[bip++] = (char)advance(l);
        while (peek(l, 0) >= '0' && peek(l, 0) <= '7') { buf[bip++] = (char)advance(l); }
    } else {
        while (isdigit(peek(l, 0)) || peek(l, 0) == '\'') {
            if (peek(l, 0) != '\'') buf[bip++] = (char)advance(l); else advance(l);
        }
    }

    if (peek(l, 0) == '.' && peek(l, 1) != '.') {
        is_float = 1;
        buf[bip++] = (char)advance(l);
        while (isdigit(peek(l, 0)) || peek(l, 0) == '\'') {
            if (peek(l, 0) != '\'') buf[bip++] = (char)advance(l); else advance(l);
        }
    }

    if (peek(l, 0) == 'e' || peek(l, 0) == 'E' || peek(l, 0) == 'p' || peek(l, 0) == 'P') {
        is_float = 1;
        buf[bip++] = (char)advance(l);
        if (peek(l, 0) == '+' || peek(l, 0) == '-') buf[bip++] = (char)advance(l);
        while (isdigit(peek(l, 0)) || peek(l, 0) == '\'') {
            if (peek(l, 0) != '\'') buf[bip++] = (char)advance(l); else advance(l);
        }
    }

    if (is_float) {
        if (peek(l, 0) == 'f' || peek(l, 0) == 'F' || peek(l, 0) == 'l' || peek(l, 0) == 'L') {
            buf[bip++] = (char)advance(l);
        }
        buf[bip] = '\0';
        add_token(l, TOK_FLOAT_LIT);
        Token *t = &l->tokens[l->ntokens - 1];
        t->fval = strtod(buf, NULL);
        return 1;
    }

    int is_long = 0, is_long_long = 0, is_unsigned = 0;
    while (peek(l, 0) == 'u' || peek(l, 0) == 'U' || peek(l, 0) == 'l' || peek(l, 0) == 'L') {
        int c = advance(l);
        if (c == 'u' || c == 'U') is_unsigned = 1;
        else if (is_long) is_long_long = 1;
        else is_long = 1;
    }
    buf[bip] = '\0';
    add_token(l, TOK_INTEGER);
    Token *t = &l->tokens[l->ntokens - 1];
    if (bip > 1 && buf[0] == '0' && buf[1] != 'x' && buf[1] != 'b' && buf[1] != 'B' && buf[1] != '\0') {
        t->uval = strtoull(buf, NULL, 8);
    } else {
        t->uval = strtoull(buf, NULL, 10);
    }
    t->ival = (long long)t->uval;
    t->is_unsigned = is_unsigned || (t->uval > 0x7FFFFFFFFFFFFFFFULL);
    t->is_long = is_long;
    t->is_long_long = is_long_long;
    return 1;
}

static int try_keyword_or_ident(Lexer *l) {
    char word[256];
    int wi = 0;
    int c = peek(l, 0);
    if (!is_ident_start(c)) return 0;
    while (is_ident_cont(peek(l, 0)) && wi < 255) {
        word[wi++] = (char)advance(l);
    }
    word[wi] = '\0';

    struct { const char *name; TokenKind kind; } keywords[] = {
        {"auto", TOK_AUTO}, {"break", TOK_BREAK}, {"case", TOK_CASE},
        {"char", TOK_CHAR}, {"const", TOK_CONST}, {"continue", TOK_CONTINUE},
        {"default", TOK_DEFAULT}, {"do", TOK_DO}, {"double", TOK_DOUBLE},
        {"else", TOK_ELSE}, {"enum", TOK_ENUM}, {"extern", TOK_EXTERN},
        {"float", TOK_FLOAT}, {"for", TOK_FOR}, {"goto", TOK_GOTO},
        {"if", TOK_IF}, {"inline", TOK_INLINE}, {"int", TOK_INT},
        {"long", TOK_LONG}, {"register", TOK_REGISTER}, {"restrict", TOK_RESTRICT},
        {"return", TOK_RETURN}, {"short", TOK_SHORT}, {"signed", TOK_SIGNED},
        {"sizeof", TOK_SIZEOF}, {"static", TOK_STATIC}, {"struct", TOK_STRUCT},
        {"switch", TOK_SWITCH}, {"typedef", TOK_TYPEDEF}, {"union", TOK_UNION},
        {"unsigned", TOK_UNSIGNED}, {"void", TOK_VOID}, {"volatile", TOK_VOLATILE},
        {"while", TOK_WHILE},
        {"_Alignas", TOK__ALIGNAS}, {"_Alignof", TOK__ALIGNOF},
        {"_Atomic", TOK__ATOMIC}, {"_Bool", TOK__BOOL},
        {"_Complex", TOK__COMPLEX}, {"_Generic", TOK__GENERIC},
        {"_Imaginary", TOK__IMAGINARY}, {"_Noreturn", TOK__NORETURN},
        {"_Static_assert", TOK__STATIC_ASSERT}, {"_Thread_local", TOK__THREAD_LOCAL},
        /* C23 */
        {"bool", TOK_BOOL}, {"constexpr", TOK_CONSTEXPR},
        {"false", TOK_FALSE}, {"nullptr", TOK_NULLPTR},
        {"static_assert", TOK_STATIC_ASSERT}, {"thread_local", TOK_THREAD_LOCAL},
        {"true", TOK_TRUE}, {"typeof", TOK_TYPEOF},
        {"typeof_unqual", TOK_TYPEOF_UNQUAL},
        {"char8_t", TOK_CHAR8_T}, {"char16_t", TOK_CHAR16_T},
        {"char32_t", TOK_CHAR32_T}, {"wchar_t", TOK_WCHAR_T},
        {"_Noreturn", TOK_NORETURN},
        {"alignas", TOK_ALIGNAS}, {"alignof", TOK_ALIGNOF},
        {"atomic", TOK_ATOMIC}, {"_BitInt", TOK_BITINT},
        {NULL, TOK_EOF}
    };

    for (int i = 0; keywords[i].name; i++) {
        if (strcmp(word, keywords[i].name) == 0) {
            add_token(l, keywords[i].kind);
            return 1;
        }
    }

    add_token(l, TOK_IDENT);
    Token *t = &l->tokens[l->ntokens - 1];
    t->sval = xstrdup(word);
    return 1;
}

void lexer_lex(Lexer *l) {
    while (1) {
        skip_whitespace_and_comments(l);
        int c = peek(l, 0);
        if (c == EOF) break;

        l->col = (int)(l->pos - l->bol + 1);

        if (c == '\'') { advance(l); lex_char(l); continue; }
        if (c == '\"') { advance(l); lex_string(l, '\"'); continue; }

        if (c == 'L' || c == 'u' || c == 'U') {
            int n = peek(l, 1);
            if (n == '\"') {
                if (c == 'L') { add_token(l, TOK_INTEGER); /* prefix for wide */ }
                advance(l); advance(l); lex_string(l, '\"');
                continue;
            }
            if (n == '\'') {
                advance(l); advance(l); lex_char(l);
                continue;
            }
        }
        if (c == 'u' && peek(l, 1) == '8' && peek(l, 2) == '\"') {
            advance(l); advance(l); advance(l); lex_string(l, '\"');
            continue;
        }

        if (is_ident_start(c)) { try_keyword_or_ident(l); continue; }
        if (isdigit(c)) { lex_number(l); continue; }

        if (c == '[' && peek(l, 1) == '[') {
            advance(l); advance(l);
            skip_whitespace_and_comments(l);
            char attr[64]; int ai = 0;
            while (is_ident_cont(peek(l, 0)) && ai < 63) attr[ai++] = (char)advance(l);
            attr[ai] = '\0';
            skip_whitespace_and_comments(l);
            if (peek(l, 0) == ':' && peek(l, 1) == ':') { advance(l); advance(l); skip_whitespace_and_comments(l); ai = 0; while (is_ident_cont(peek(l, 0)) && ai < 63) attr[ai++] = (char)advance(l); attr[ai] = '\0'; skip_whitespace_and_comments(l); }
            if (peek(l, 0) == '(') {
                while (peek(l, 0) != EOF && peek(l, 0) != ']') advance(l);
            }
            if (peek(l, 0) == ']' && peek(l, 1) == ']') { advance(l); advance(l); }
            struct { const char *name; TokenKind kind; } attrs[] = {
                {"nodiscard", TOK_ATTR_NODISCARD}, {"maybe_unused", TOK_ATTR_MAYBE_UNUSED},
                {"deprecated", TOK_ATTR_DEPRECATED}, {"noreturn", TOK_ATTR_NORETURN},
                {"fallthrough", TOK_ATTR_FALLTHROUGH}, {NULL, TOK_EOF}
            };
            int found = 0;
            for (int i = 0; attrs[i].name; i++) {
                if (strcmp(attr, attrs[i].name) == 0) { add_token(l, attrs[i].kind); found = 1; break; }
            }
            if (!found) add_token(l, TOK_IDENT);
            continue;
        }

        /* punctuators */
        struct { const char *str; TokenKind kind; } punct[] = {
            {"...", TOK_ELLIPSIS}, {">>=", TOK_GT_GT_EQ}, {"<<=", TOK_LT_LT_EQ},
            {">>", TOK_GT_GT}, {"<<", TOK_LT_LT},
            {"+=", TOK_PLUS_EQ}, {"-=", TOK_MINUS_EQ}, {"*=", TOK_STAR_EQ},
            {"/=", TOK_SLASH_EQ}, {"%=", TOK_PERCENT_EQ}, {"&=", TOK_AMPERSAND_EQ},
            {"|=", TOK_PIPE_EQ}, {"^=", TOK_CARET_EQ},
            {"++", TOK_PLUS_PLUS}, {"--", TOK_MINUS_MINUS},
            {"&&", TOK_AMPERSAND_AMPERSAND}, {"||", TOK_PIPE_PIPE},
            {"==", TOK_EQ_EQ}, {"!=", TOK_EXCLAIM_EQ},
            {"<=", TOK_LT_EQ}, {">=", TOK_GT_EQ},
            {"->", TOK_ARROW},
            {"##", TOK_HASH_HASH},
            {"#", TOK_HASH},
            {"+", TOK_PLUS}, {"-", TOK_MINUS}, {"*", TOK_STAR},
            {"/", TOK_SLASH}, {"%", TOK_PERCENT},
            {"&", TOK_AMPERSAND}, {"|", TOK_PIPE}, {"^", TOK_CARET},
            {"~", TOK_TILDE}, {"!", TOK_EXCLAIM},
            {"=", TOK_EQ}, {"<", TOK_LT}, {">", TOK_GT},
            {"?", TOK_QUESTION}, {":", TOK_COLON},
            {";", TOK_SEMICOLON}, {",", TOK_COMMA},
            {".", TOK_DOT},
            {"[", TOK_LBRACKET}, {"]", TOK_RBRACKET},
            {"(", TOK_LPAREN}, {")", TOK_RPAREN},
            {"{", TOK_LBRACE}, {"}", TOK_RBRACE},
            {NULL, TOK_EOF}
        };

        int found = 0;
        for (int i = 0; punct[i].str; i++) {
            int len = (int)strlen(punct[i].str);
            int match = 1;
            for (int j = 0; j < len; j++) {
                if (peek(l, j) != punct[i].str[j]) { match = 0; break; }
            }
            if (match) {
                for (int j = 0; j < len; j++) advance(l);
                add_token(l, punct[i].kind);
                found = 1;
                break;
            }
        }
        if (!found) {
            diag(DIAG_ERROR, l->file, l->line, l->col, "unexpected character '%c' (0x%02x)", c, c);
            advance(l);
        }
    }
    add_token(l, TOK_EOF);
}

void lexer_free(Lexer *l) {
    for (int i = 0; i < l->ntokens; i++) {
        TokenKind k = l->tokens[i].kind;
        if (k == TOK_IDENT || k == TOK_STRING) {
            free(l->tokens[i].sval);
        }
    }
    free(l->tokens);
}

const char *token_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_IDENT: return "identifier";
        case TOK_INTEGER: return "integer constant";
        case TOK_FLOAT_LIT: return "float constant";
        case TOK_STRING: return "string literal";
        case TOK_CHAR_LIT: return "char constant";
        case TOK_BOOL: return "bool";
        case TOK_TRUE: return "true";
        case TOK_FALSE: return "false";
        case TOK_NULLPTR: return "nullptr";
        case TOK_TYPEOF: return "typeof";
        case TOK_TYPEOF_UNQUAL: return "typeof_unqual";
        case TOK_STATIC_ASSERT: return "static_assert";
        case TOK_CONSTEXPR: return "constexpr";
        case TOK_CHAR8_T: return "char8_t";
        case TOK_CHAR16_T: return "char16_t";
        case TOK_CHAR32_T: return "char32_t";
        case TOK_WCHAR_T: return "wchar_t";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "token_%d", kind);
            return buf;
        }
    }
}

const char *token_spelling(Token *t) {
    static char buf[256];
    switch (t->kind) {
        case TOK_IDENT: return t->sval;
        case TOK_INTEGER: snprintf(buf, sizeof(buf), "%lld", t->ival); return buf;
        case TOK_FLOAT_LIT: snprintf(buf, sizeof(buf), "%g", t->fval); return buf;
        case TOK_STRING: return t->sval;
        default: return token_name(t->kind);
    }
}
