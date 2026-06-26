#include "ast.h"

AstNode *ast_alloc(NodeKind kind) {
    AstNode *n = (AstNode*)xcalloc(1, sizeof(AstNode));
    n->kind = kind;
    return n;
}

void ast_free(AstNode *node) {
    if (!node) return;
    /* free string data */
    switch (node->kind) {
        case NOD_IDENT:
        case NOD_LABEL:
        case NOD_GOTO:
            free((void*)node->data.ident.name);
            break;
        case NOD_STRING_CONST:
            free((void*)node->data.string_val);
            break;
        case NOD_FUNC_DECL:
        case NOD_FUNC_DEF:
            free((void*)node->data.func.name);
            if (node->data.func.params) {
                for (int i = 0; i < node->data.func.nparams; i++)
                    ast_free(node->data.func.params[i]);
                free(node->data.func.params);
            }
            break;
        case NOD_VAR_DECL:
        case NOD_VAR_DEF:
            free((void*)node->data.var.name);
            break;
        case NOD_STATIC_ASSERT:
            free((void*)node->data.static_assertion.msg);
            break;
        default:
            break;
    }
    free(node);
}

void ast_free_deep(AstNode *node) {
    if (!node) return;
    switch (node->kind) {
        case NOD_COMPOUND:
            for (int i = 0; i < node->data.compound.nstmts; i++)
                ast_free_deep(node->data.compound.stmts[i]);
            free(node->data.compound.stmts);
            break;
        case NOD_EXPR_STMT: ast_free_deep(node->data.expr_stmt.expr); break;
        case NOD_BINARY: ast_free_deep(node->data.binary.left); ast_free_deep(node->data.binary.right); break;
        case NOD_UNARY: ast_free_deep(node->data.unary.operand); break;
        case NOD_ASSIGN: ast_free_deep(node->data.assign.left); ast_free_deep(node->data.assign.right); break;
        case NOD_CALL: ast_free_deep(node->data.call.func); for (int i = 0; i < node->data.call.nargs; i++) ast_free_deep(node->data.call.args[i]); free(node->data.call.args); break;
        case NOD_IF: ast_free_deep(node->data.if_stmt.cond); ast_free_deep(node->data.if_stmt.then); ast_free_deep(node->data.if_stmt.els); break;
        case NOD_WHILE: ast_free_deep(node->data.while_stmt.cond); ast_free_deep(node->data.while_stmt.body); break;
        case NOD_FOR: ast_free_deep(node->data.for_stmt.init); ast_free_deep(node->data.for_stmt.cond); ast_free_deep(node->data.for_stmt.incr); ast_free_deep(node->data.for_stmt.body); break;
        case NOD_RETURN: ast_free_deep(node->data.return_stmt.expr); break;
        case NOD_CAST: ast_free_deep(node->data.cast.operand); break;
        default: break;
    }
    ast_free(node);
}
