#ifndef LEXER_H
#define LEXER_H

#include "utils.h"

typedef enum {
    TOK_EOF,

    /* identifiers & keywords */
    TOK_IDENT, TOK_INTEGER, TOK_FLOAT_LIT, TOK_STRING, TOK_CHAR_LIT,

    /* C23 keywords */
    TOK_AUTO, TOK_BREAK, TOK_CASE, TOK_CHAR, TOK_CONST, TOK_CONTINUE,
    TOK_DEFAULT, TOK_DO, TOK_DOUBLE, TOK_ELSE, TOK_ENUM, TOK_EXTERN,
    TOK_FLOAT, TOK_FOR, TOK_GOTO, TOK_IF, TOK_INLINE, TOK_INT,
    TOK_LONG, TOK_REGISTER, TOK_RESTRICT, TOK_RETURN, TOK_SHORT,
    TOK_SIGNED, TOK_SIZEOF, TOK_STATIC, TOK_STRUCT, TOK_SWITCH,
    TOK_TYPEDEF, TOK_UNION, TOK_UNSIGNED, TOK_VOID, TOK_VOLATILE,
    TOK_WHILE, TOK__ALIGNAS, TOK__ALIGNOF, TOK__ATOMIC, TOK__BOOL,
    TOK__COMPLEX, TOK__GENERIC, TOK__IMAGINARY, TOK__NORETURN,
    TOK__STATIC_ASSERT, TOK__THREAD_LOCAL,
    /* C23 additions */
    TOK_BOOL, TOK_CONSTEXPR, TOK_FALSE, TOK_NULLPTR, TOK_STATIC_ASSERT,
    TOK_THREAD_LOCAL, TOK_TRUE, TOK_TYPEOF, TOK_TYPEOF_UNQUAL,
    TOK_CHAR8_T, TOK_CHAR16_T, TOK_CHAR32_T, TOK_WCHAR_T,
    TOK_NORETURN, TOK_ALIGNAS, TOK_ALIGNOF, TOK_ATOMIC,
    TOK_BITINT,

    /* attributes */
    TOK_ATTR_NODISCARD, TOK_ATTR_MAYBE_UNUSED, TOK_ATTR_DEPRECATED,
    TOK_ATTR_NORETURN, TOK_ATTR_FALLTHROUGH,

    /* operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMPERSAND, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_EXCLAIM,
    TOK_EQ, TOK_LT, TOK_GT, TOK_QUESTION, TOK_COLON,
    TOK_SEMICOLON, TOK_COMMA, TOK_DOT, TOK_ARROW, TOK_LBRACKET,
    TOK_RBRACKET, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_PLUS_PLUS, TOK_MINUS_MINUS, TOK_AMPERSAND_AMPERSAND,
    TOK_PIPE_PIPE, TOK_EQ_EQ, TOK_EXCLAIM_EQ, TOK_LT_EQ, TOK_GT_EQ,
    TOK_LT_LT, TOK_GT_GT, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ,
    TOK_SLASH_EQ, TOK_PERCENT_EQ, TOK_AMPERSAND_EQ, TOK_PIPE_EQ,
    TOK_CARET_EQ, TOK_LT_LT_EQ, TOK_GT_GT_EQ,
    TOK_ELLIPSIS, TOK_HASH, TOK_HASH_HASH,
    TOK_HASH_AT,  /* #@ for MS extension? no - skip */
    TOK_ATTR_START, /* [[ */

    /* preprocessor tokens (only used internally) */
    TOK_PP_NUMBER, TOK_PP_HEADER_NAME,
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *file;
    int line;
    int col;
    union {
        char *sval;     /* for identifiers and string literals */
        long long ival; /* for integer constants */
        double fval;    /* for float constants */
        unsigned long long uval;
    };
    size_t slen; /* string literal length */
    int is_unsigned;
    int is_long;
    int is_long_long;
} Token;

typedef struct {
    char *buf;
    size_t len;
    size_t pos;
    const char *file;
    int line;
    int col;
    int bol; /* beginning of line position */
    Token *tokens;
    int ntokens;
    int maxtokens;
    int in_preprocessing;
    TargetPlatform target;
} Lexer;

void lexer_init(Lexer *l, const char *file, const char *source, TargetPlatform target);
void lexer_lex(Lexer *l);
void lexer_free(Lexer *l);
const char *token_name(TokenKind kind);
const char *token_spelling(Token *t);

#endif
