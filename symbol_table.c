#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"

SymbolTable *class_table = NULL;
SymbolTable *method_tables = NULL;

static char *sym_strdup(const char *s) {
    char *copy;

    if (s == NULL) return NULL;

    copy = (char *)malloc(strlen(s) + 1);
    if (copy == NULL) return NULL;

    strcpy(copy, s);
    return copy;
}

/* =========================================================
   Criação e inserção
   ========================================================= */

SymbolTable *create_symbol_table(char *name) {
    SymbolTable *table = (SymbolTable *)malloc(sizeof(SymbolTable));
    if (table == NULL) return NULL;

    table->name = sym_strdup(name);
    table->return_type = NULL;
    table->method_params = NULL;
    table->symbols = NULL;
    table->next = NULL;
    table->valid = 1;

    return table;
}

Symbol *create_symbol(char *name, char *type, int is_param, int line, int col) {
    Symbol *symbol = (Symbol *)malloc(sizeof(Symbol));
    if (symbol == NULL) return NULL;

    symbol->name = sym_strdup(name);
    symbol->type = sym_strdup(type);
    symbol->params = NULL;
    symbol->is_param = is_param;
    symbol->is_method = 0;
    symbol->line = line;
    symbol->col = col;
    symbol->next = NULL;

    return symbol;
}

ParamType *create_param_type(char *type) {
    ParamType *param = (ParamType *)malloc(sizeof(ParamType));
    if (param == NULL) return NULL;

    param->type = sym_strdup(type);
    param->next = NULL;

    return param;
}

int symbol_exists(SymbolTable *table, char *name) {
    Symbol *cur;

    if (table == NULL || name == NULL) return 0;

    cur = table->symbols;
    while (cur != NULL) {
        if (strcmp(cur->name, name) == 0)
            return 1;
        cur = cur->next;
    }

    return 0;
}

int is_reserved_symbol(char *name) {
    return name != NULL && strcmp(name, "_") == 0;
}

int same_params(ParamType *a, ParamType *b) {
    while (a != NULL && b != NULL) {
        if (strcmp(a->type, b->type) != 0)
            return 0;

        a = a->next;
        b = b->next;
    }

    return a == NULL && b == NULL;
}

static char *symbol_param_signature(ParamType *params) {
    char buffer[1024];
    ParamType *p;
    int first;

    buffer[0] = '\0';
    strcat(buffer, "(");

    p = params;
    first = 1;

    while (p != NULL) {
        if (!first)
            strcat(buffer, ",");

        strcat(buffer, p->type);

        first = 0;
        p = p->next;
    }

    strcat(buffer, ")");

    return sym_strdup(buffer);
}

int append_symbol(SymbolTable *table, Symbol *symbol) {
    Symbol *cur;
    char *signature;

    if (table == NULL || symbol == NULL)
        return 0;

    if (is_reserved_symbol(symbol->name)) {
        fprintf(stderr, "Line %d, col %d: Symbol %s is reserved\n",
               symbol->line, symbol->col, symbol->name);
        return 0;
    }

    cur = table->symbols;
    while (cur != NULL) {
        if (strcmp(cur->name, symbol->name) == 0) {
            if (cur->is_method || symbol->is_method) {
                if (cur->is_method && symbol->is_method &&
                    same_params(cur->params, symbol->params)) {
                    if (symbol->is_method) {
                        signature = symbol_param_signature(symbol->params);

                        fprintf(stderr, "Line %d, col %d: Symbol %s%s already defined\n",
                               symbol->line, symbol->col, symbol->name, signature);

                        free(signature);
                    } else {
                        fprintf(stderr, "Line %d, col %d: Symbol %s already defined\n",
                               symbol->line, symbol->col, symbol->name);
                    }

                    return 0;
                }
            } else {
                fprintf(stderr, "Line %d, col %d: Symbol %s already defined\n",
                       symbol->line, symbol->col, symbol->name);
                return 0;
            }
        }

        cur = cur->next;
    }

    if (table->symbols == NULL) {
        table->symbols = symbol;
        return 1;
    }

    cur = table->symbols;
    while (cur->next != NULL)
        cur = cur->next;

    cur->next = symbol;
    return 1;
}


void append_method_table(SymbolTable *table) {
    SymbolTable *cur;

    if (table == NULL) return;

    if (method_tables == NULL) {
        method_tables = table;
        return;
    }

    cur = method_tables;
    while (cur->next != NULL)
        cur = cur->next;

    cur->next = table;
}

void append_param_type(ParamType **list, char *type) {
    ParamType *new_param;
    ParamType *cur;

    if (list == NULL || type == NULL) return;

    new_param = create_param_type(type);
    if (new_param == NULL) return;

    if (*list == NULL) {
        *list = new_param;
        return;
    }

    cur = *list;
    while (cur->next != NULL)
        cur = cur->next;

    cur->next = new_param;
}

/* =========================================================
   Helpers de leitura da AST
   ========================================================= */

ASTNode *first_child(ASTNode *node) {
    if (node == NULL) return NULL;
    return node->child;
}

ASTNode *second_child(ASTNode *node) {
    if (node == NULL || node->child == NULL) return NULL;
    return node->child->next;
}

ASTNode *third_child(ASTNode *node) {
    if (node == NULL || node->child == NULL || node->child->next == NULL)
        return NULL;
    return node->child->next->next;
}

/* =========================================================
   Helpers internos
   ========================================================= */

static int is_label(ASTNode *node, const char *label) {
    if (node == NULL || label == NULL || node->label == NULL) return 0;
    return strcmp(node->label, label) == 0;
}

static void collect_field_decl(ASTNode *field_decl);
static void collect_method_decl(ASTNode *method_decl);
static void collect_param_decls(SymbolTable *table, ASTNode *method_params_node);
static void collect_local_vars(SymbolTable *table, ASTNode *method_body_node);

/* =========================================================
   Helpers de tipos
   ========================================================= */

const char *ast_type_to_string(ASTNode *type_node) {
    if (type_node == NULL || type_node->label == NULL) return "";

    if (strcmp(type_node->label, "Bool") == 0) return "boolean";
    if (strcmp(type_node->label, "Int") == 0) return "int";
    if (strcmp(type_node->label, "Double") == 0) return "double";
    if (strcmp(type_node->label, "Void") == 0) return "void";
    if (strcmp(type_node->label, "StringArray") == 0) return "String[]";

    return "";
}

ParamType *collect_param_types(ASTNode *method_params_node) {
    ParamType *params = NULL;
    ASTNode *param_decl;
    ASTNode *type_node;

    if (method_params_node == NULL) return NULL;
    if (!is_label(method_params_node, "MethodParams")) return NULL;

    param_decl = first_child(method_params_node);
    while (param_decl != NULL) {
        if (is_label(param_decl, "ParamDecl")) {
            type_node = first_child(param_decl);
            append_param_type(&params, (char *)ast_type_to_string(type_node));
        }
        param_decl = param_decl->next;
    }

    return params;
}

ParamType *copy_param_types(ParamType *params) {
    ParamType *copy = NULL;

    while (params != NULL) {
        append_param_type(&copy, params->type);
        params = params->next;
    }

    return copy;
}

/* =========================================================
   Construção das tabelas
   ========================================================= */

void build_symbol_tables(ASTNode *root) {
    ASTNode *class_id;
    ASTNode *member;

    if (root == NULL) return;
    if (
        !is_label(root, "Program") &&
        !is_label(root, "ProgramNode")
    ) return;

    class_id = first_child(root);
    if (class_id == NULL || !is_label(class_id, "Identifier")) return;

    class_table = create_symbol_table(class_id->lexeme);

    member = class_id->next;
    while (member != NULL) {
        if (is_label(member, "FieldDecl")) {
            collect_field_decl(member);
        }
        else if (is_label(member, "MethodDecl")) {
            collect_method_decl(member);
        }
        member = member->next;
    }
}

static void collect_field_decl(ASTNode *field_decl) {
    ASTNode *type_node;
    ASTNode *id_node;

    type_node = first_child(field_decl);
    id_node = second_child(field_decl);

    if (type_node == NULL || id_node == NULL) return;
    if (!is_label(id_node, "Identifier")) return;


    append_symbol(
        class_table,
        create_symbol(id_node->lexeme, (char *)ast_type_to_string(type_node), 0, id_node->line, id_node->col)
    );
}

static void collect_method_decl(ASTNode *method_decl) {
    ASTNode *header;
    ASTNode *body;
    ASTNode *ret_node;
    ASTNode *name_node;
    ASTNode *params_node;
    Symbol *method_symbol;
    SymbolTable *method_table;
    int method_inserted;

    if (method_decl == NULL) return;

    header = first_child(method_decl);
    body = second_child(method_decl);

    if (header == NULL || body == NULL) return;
    if (!is_label(header, "MethodHeader")) return;
    if (!is_label(body, "MethodBody")) return;

    ret_node = first_child(header);
    name_node = second_child(header);
    params_node = third_child(header);

    if (ret_node == NULL || name_node == NULL) return;
    if (!is_label(name_node, "Identifier")) return;

    method_symbol = create_symbol(
        name_node->lexeme,
        (char *)ast_type_to_string(ret_node),
        0,
        name_node->line,
        name_node->col
    );
    method_symbol->is_method = 1;

    if (params_node != NULL && is_label(params_node, "MethodParams")) {
        method_symbol->params = collect_param_types(params_node);
    } else {
        method_symbol->params = NULL;
    }

    
    method_table = create_symbol_table(name_node->lexeme);
    
    method_table->return_type = sym_strdup(ast_type_to_string(ret_node));
    method_table->method_params = copy_param_types(method_symbol->params);

    append_symbol(
        method_table,
        create_symbol("return", (char *)ast_type_to_string(ret_node), 0, 0, 0)
    );

    if (params_node != NULL && is_label(params_node, "MethodParams")) {
        collect_param_decls(method_table, params_node);
    }
    method_inserted = append_symbol(class_table, method_symbol);
    if (!method_inserted)
        method_table->valid = 0;
    collect_local_vars(method_table, body);

    append_method_table(method_table);
}

static void collect_param_decls(SymbolTable *table, ASTNode *method_params_node) {
    ASTNode *param_decl;
    ASTNode *type_node;
    ASTNode *id_node;

    if (table == NULL || method_params_node == NULL) return;
    if (!is_label(method_params_node, "MethodParams")) return;

    param_decl = first_child(method_params_node);
    while (param_decl != NULL) {
        if (is_label(param_decl, "ParamDecl")) {
            type_node = first_child(param_decl);
            id_node = second_child(param_decl);

            if (type_node != NULL && id_node != NULL && is_label(id_node, "Identifier")) {
                append_symbol(
                    table,
                    create_symbol(id_node->lexeme, (char *)ast_type_to_string(type_node), 1,id_node->line, id_node->col)
                );
            }
        }
        param_decl = param_decl->next;
    }
}

static void collect_local_vars(SymbolTable *table, ASTNode *method_body_node) {
    ASTNode *node;
    ASTNode *type_node;
    ASTNode *id_node;

    if (table == NULL || method_body_node == NULL) return;
    if (!is_label(method_body_node, "MethodBody")) return;

    node = first_child(method_body_node);
    while (node != NULL) {
        if (is_label(node, "VarDecl")) {
            type_node = first_child(node);
            id_node = second_child(node);

            if (type_node != NULL && id_node != NULL && is_label(id_node, "Identifier")) {
                append_symbol(
                    table,
                    create_symbol(id_node->lexeme, (char *)ast_type_to_string(type_node), 0, id_node->line, id_node->col)
                );
            }
        }
        node = node->next;
    }
}

/* =========================================================
   Impressão
   ========================================================= */

void print_param_signature(ParamType *params) {
    printf("(");

    while (params != NULL) {
        printf("%s", params->type);
        if (params->next != NULL)
            printf(",");
        params = params->next;
    }

    printf(")");
}

void print_class_symbol_table(SymbolTable *table) {
    Symbol *symbol;

    if (table == NULL) return;

    printf("===== Class %s Symbol Table =====\n", table->name);

    symbol = table->symbols;
    while (symbol != NULL) {
        if (symbol->is_method) {
            printf("%s\t", symbol->name);
            print_param_signature(symbol->params);
            printf("\t%s\n", symbol->type);
        }
        else {
            printf("%s\t\t%s\n", symbol->name, symbol->type);
        }

        symbol = symbol->next;
    }
}

void print_method_symbol_table(SymbolTable *table) {
    Symbol *symbol;

    if (table == NULL) return;

    printf("===== Method %s", table->name);
    print_param_signature(table->method_params);
    printf(" Symbol Table =====\n");

    symbol = table->symbols;
    while (symbol != NULL) {
        if (symbol->is_param) {
            printf("%s\t\t%s\tparam\n", symbol->name, symbol->type);
        }
        else {
            printf("%s\t\t%s\n", symbol->name, symbol->type);
        }
        symbol = symbol->next;
    }
}

void print_symbol_tables(void) {
    SymbolTable *method_table;

    if (class_table != NULL)
        print_class_symbol_table(class_table);

    method_table = method_tables;
    while (method_table != NULL) {
        if (method_table->valid) {
            printf("\n");
            print_method_symbol_table(method_table);
        }
        method_table = method_table->next;
    }
}


/* =========================================================
   Libertação de memória
   ========================================================= */

void free_param_types(ParamType *params) {
    ParamType *next;

    while (params != NULL) {
        next = params->next;
        free(params->type);
        free(params);
        params = next;
    }
}

void free_symbols(Symbol *symbols) {
    Symbol *next;

    while (symbols != NULL) {
        next = symbols->next;
        free(symbols->name);
        free(symbols->type);
        free_param_types(symbols->params);
        free(symbols);
        symbols = next;
    }
}

void free_symbol_table(SymbolTable *table) {
    if (table == NULL) return;

    free(table->name);
    free(table->return_type);
    free_param_types(table->method_params);
    free_symbols(table->symbols);
    free(table);
}

void free_symbol_tables(void) {
    SymbolTable *next;

    if (class_table != NULL) {
        free_symbol_table(class_table);
        class_table = NULL;
    }

    while (method_tables != NULL) {
        next = method_tables->next;
        free_symbol_table(method_tables);
        method_tables = next;
    }
}