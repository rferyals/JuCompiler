#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "ast.h"

/* lista de tipos de parâmetros, usada nas assinaturas dos métodos */
typedef struct param_type {
    char *type;
    struct param_type *next;
} ParamType;

/* símbolo individual */
typedef struct symbol {
    char *name;          /* nome do símbolo: factorial, n, argument, return, ... */
    char *type;          /* tipo: int, double, boolean, void, String [] */
    ParamType *params;   /* só usado quando o símbolo representa um método */
    int is_param;       /* 1 se for parâmetro formal, 0 caso contrário */
    int is_method;      /* 1 se for método, 0 caso contrário */
    int line;
    int col;
    struct symbol *next;
} Symbol;

/* tabela de símbolos */
typedef struct symbol_table {
    char *name;                 /* nome da classe ou do método */
    char *return_type;          /* só usado em tabelas de método */
    ParamType *method_params;   /* assinatura do método, para impressão do header */
    Symbol *symbols;            /* símbolos por ordem de inserção */
    struct symbol_table *next;  /* próxima tabela de método */
    int valid;
} SymbolTable;

/* tabela global da classe */
extern SymbolTable *class_table;

/* lista de tabelas dos métodos */
extern SymbolTable *method_tables;


/* =========================
   Criação e inserção
   ========================= */

SymbolTable *create_symbol_table(char *name);
Symbol *create_symbol(char *name, char *type, int is_param, int line, int col);
ParamType *create_param_type(char *type);

int append_symbol(SymbolTable *table, Symbol *symbol);
void append_method_table(SymbolTable *table);
void append_param_type(ParamType **list, char *type);


//builder
void build_symbol_tables(ASTNode *root);


//leitura ast
ASTNode *first_child(ASTNode *node);
ASTNode *second_child(ASTNode *node);
ASTNode *third_child(ASTNode *node);


//helper de tipos
const char *ast_type_to_string(ASTNode *type_node);
ParamType *collect_param_types(ASTNode *method_params_node);
ParamType *copy_param_types(ParamType *params);


//prints
void print_symbol_tables(void);
void print_class_symbol_table(SymbolTable *table);
void print_method_symbol_table(SymbolTable *table);
void print_param_signature(ParamType *params);


//free
void free_param_types(ParamType *params);
void free_symbols(Symbol *symbols);
void free_symbol_table(SymbolTable *table);
void free_symbol_tables(void);

#endif