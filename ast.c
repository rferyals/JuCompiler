#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

ASTNode *ast_root = NULL;

static char *ast_strdup(const char *s) {
    if (s == NULL) return NULL;

    char *copy = (char *)malloc(strlen(s) + 1);
    if (copy == NULL) return NULL;

    strcpy(copy, s);
    return copy;
}

ASTNode *ast_new(char *label, char *lexeme) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (node == NULL) return NULL;

    node->label = ast_strdup(label);
    node->lexeme = ast_strdup(lexeme);
    node->annotation = NULL;
    node->line = 0;
    node->col = 0;
    node->child = NULL;
    node->next = NULL;

    return node;
}

ASTNode *ast_set_pos(ASTNode *node, int line, int col) {
    if (node == NULL) return NULL;

    node->line = line;
    node->col = col;

    return node;
}

ASTNode *ast_attach_child(ASTNode *parent, ASTNode *child) {
    if (parent == NULL) return NULL;

    parent->child = child;
    return parent;
}

ASTNode *ast_attach_sibling(ASTNode *node, ASTNode *sibling) {
    ASTNode *cur;

    if (node == NULL && sibling == NULL) return NULL;
    if (node == NULL) return sibling;
    if (sibling == NULL) return node;

    cur = node;
    while (cur->next != NULL)
        cur = cur->next;

    cur->next = sibling;
    return node;
}

ASTNode *ast_make_if(ASTNode *condition, ASTNode *then_stmt, ASTNode *else_stmt) {
    ASTNode *node = ast_new("If", NULL);

    if (then_stmt == NULL)
        then_stmt = ast_new("Block", NULL);

    if (else_stmt == NULL)
        else_stmt = ast_new("Block", NULL);

    return ast_attach_child(node,
           ast_attach_sibling(condition,
           ast_attach_sibling(then_stmt, else_stmt)));
}

ASTNode *ast_make_while(ASTNode *condition, ASTNode *body) {
    ASTNode *node = ast_new("While", NULL);

    if (body == NULL)
        body = ast_new("Block", NULL);

    return ast_attach_child(node, ast_attach_sibling(condition, body));
}

ASTNode *ast_make_block(ASTNode *first, ASTNode *second) {
    ASTNode *node;

    if (first == NULL && second == NULL) return NULL;
    if (first == NULL) return second;
    if (second == NULL) return first;

    node = ast_new("Block", NULL);
    return ast_attach_child(node, ast_attach_sibling(first, second));
}

void ast_print(ASTNode *node, int depth) {
    int i;

    if (node == NULL) return;

    for (i = 0; i < depth * 2; i++)
        putchar('.');

    if (node->lexeme != NULL) {
        printf("%s(%s)", node->label, node->lexeme);
    } else {
    printf("%s", node->label);
    }

if (node->annotation != NULL)
    printf(" - %s", node->annotation);

printf("\n");

    ast_print(node->child, depth + 1);
    ast_print(node->next, depth);
}

void ast_free(ASTNode *node) {
    if (node == NULL) return;

    ast_free(node->child);
    ast_free(node->next);

    free(node->label);
    free(node->lexeme);
    free(node->annotation);
    free(node);
}