#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "ast.h"
#include "symbol_table.h"

void annotate_ast(ASTNode *root);
int get_semantic_errors(void);

#endif