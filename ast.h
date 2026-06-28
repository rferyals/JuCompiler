#ifndef AST_H
#define AST_H

#include <stddef.h>

#define AST_NAME_SIZE 100

typedef struct ast_node {
    char *label;              /* ex: "Program", "Identifier", "If" */
    char *lexeme;             /* ex: "Factorial", "n", "123" ou NULL */
    char *annotation;         /* para a meta 3; pode ficar NULL por agora */
    int line;
    int col;

    struct ast_node *child;   /* primeiro filho */
    struct ast_node *next;    /* irmão seguinte */
} ASTNode;

extern ASTNode *ast_root;

/* criação e ligação básica */
ASTNode *ast_new(char *label, char *lexeme);
ASTNode *ast_set_pos(ASTNode *node, int line, int col);
ASTNode *ast_attach_child(ASTNode *parent, ASTNode *child);
ASTNode *ast_attach_sibling(ASTNode *node, ASTNode *sibling);

/* helpers úteis para o parser */
ASTNode *ast_make_if(ASTNode *condition, ASTNode *then_stmt, ASTNode *else_stmt);
ASTNode *ast_make_while(ASTNode *condition, ASTNode *body);
ASTNode *ast_make_block(ASTNode *first, ASTNode *second);

/* utilitários */
void ast_print(ASTNode *node, int depth);
void ast_free(ASTNode *node);

#endif