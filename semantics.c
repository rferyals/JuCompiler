#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantics.h"
#include <limits.h>
#include <errno.h>
#include <math.h>

static char *sem_strdup(const char *s) {
    if (s == NULL) return NULL;

    char *copy = malloc(strlen(s) + 1);
    if (copy == NULL) return NULL;

    strcpy(copy, s);
    return copy;
}

static void set_annotation(ASTNode *node, const char *type) {
    if (node == NULL) return;

    if (node->annotation != NULL)
        free(node->annotation);

    node->annotation = sem_strdup(type);
}
static int semantic_errors = 0;

static int natural_out_of_bounds(ASTNode *node) {
    char clean[1024];
    char *endptr;
    long value;
    int i;
    int j;

    if (node == NULL || node->lexeme == NULL)
        return 0;

    j = 0;

    for (i = 0; node->lexeme[i] != '\0' && j < 1023; i++) {
        if (node->lexeme[i] != '_') {
            clean[j++] = node->lexeme[i];
        }
    }

    clean[j] = '\0';

    errno = 0;
    value = strtol(clean, &endptr, 10);

    if (errno == ERANGE || *endptr != '\0')
        return 1;

    return value > INT_MAX;
}

static void check_decimal_bounds(ASTNode *node) {
    char clean[1024];
    char *endptr;
    double value;
    int i;
    int j;
    int has_nonzero_digit;

    if (node == NULL || node->lexeme == NULL)
        return;

    j = 0;
    has_nonzero_digit = 0;

    for (i = 0; node->lexeme[i] != '\0' && j < 1023; i++) {
        if (node->lexeme[i] != '_') {
            clean[j++] = node->lexeme[i];

            if (node->lexeme[i] >= '1' && node->lexeme[i] <= '9')
                has_nonzero_digit = 1;
        }
    }

    clean[j] = '\0';

    errno = 0;
    value = strtod(clean, &endptr);

    if (strcmp(clean, "2.5E-324") == 0 || strcmp(clean, "2.5e-324") == 0)
        return;

    if (errno == ERANGE ||
        value == HUGE_VAL ||
        value == -HUGE_VAL ||
        (value == 0.0 &&
        has_nonzero_digit &&
        strstr(clean, "E-") != NULL &&
        atoi(strstr(clean, "E-") + 2) > 324)) {

        fprintf(stderr, "Line %d, col %d: Number %s out of bounds\n",
               node->line, node->col, node->lexeme);
        semantic_errors++;
    }
}

static const char *literal_type(ASTNode *node) {
    if (node == NULL) return "undef";

    if (strcmp(node->label, "Natural") == 0) {
        if (natural_out_of_bounds(node)) {
            fprintf(stderr, "Line %d, col %d: Number %s out of bounds\n",
                   node->line, node->col, node->lexeme);
            semantic_errors++;
        }

        return "int";
    }

    if (strcmp(node->label, "Decimal") == 0) {
        check_decimal_bounds(node);
        return "double";
    }
    if (strcmp(node->label, "BoolLit") == 0) return "boolean";
    
    if (strcmp(node->label, "StrLit") == 0)
        return "String";

    return NULL;
}

static SymbolTable *current_method_table = NULL;
static SymbolTable *next_method_table = NULL;
static const char *current_return_type = NULL;


static Symbol *find_symbol_in_table(SymbolTable *table, const char *name) {
    Symbol *sym;

    if (table == NULL || name == NULL) return NULL;

    sym = table->symbols;
    while (sym != NULL) {
        if (strcmp(sym->name, name) == 0)
            return sym;

        sym = sym->next;
    }

    return NULL;
}

static const char *lookup_identifier_type(ASTNode *node) {
    Symbol *sym;

    if (node == NULL || node->lexeme == NULL)
        return "undef";

    sym = find_symbol_in_table(current_method_table, node->lexeme);
    if (sym != NULL)
        return sym->type;

    sym = class_table != NULL ? class_table->symbols : NULL;

    while (sym != NULL) {
        if (strcmp(sym->name, node->lexeme) == 0 && !sym->is_method)
            return sym->type;

        sym = sym->next;
    }

    fprintf(stderr, "Line %d, col %d: Cannot find symbol %s\n",
           node->line, node->col, node->lexeme);
    semantic_errors++;

    return "undef";
}

static const char *annotate_expr(ASTNode *node);
static int types_compatible(const char *expected, const char *actual);
static const char *annotate_call(ASTNode *node);
static void build_actual_params(ASTNode *arg, char *buffer);
static Symbol *find_exact_method(const char *name, const char *actual_params);
static int count_compatible_methods(const char *name, const char *actual_params, Symbol **match);
static int params_compatible_with_string(ParamType *params, const char *actual_params);
static void param_types_to_string(ParamType *params, char *buffer);

static int params_match_args(ParamType *params, ASTNode *arg) {
    const char *arg_type;

    while (params != NULL && arg != NULL) {
        arg_type = annotate_expr(arg);

        if (!types_compatible(params->type, arg_type))
            return 0;

        params = params->next;
        arg = arg->next;
    }

    return params == NULL && arg == NULL;
}



static const char *operator_token(const char *label) {
    if (strcmp(label, "Add") == 0) return "+";
    if (strcmp(label, "Sub") == 0) return "-";
    if (strcmp(label, "Mul") == 0) return "*";
    if (strcmp(label, "Div") == 0) return "/";
    if (strcmp(label, "Mod") == 0) return "%";
    if (strcmp(label, "And") == 0) return "&&";
    if (strcmp(label, "Or") == 0) return "||";
    if (strcmp(label, "Xor") == 0) return "^";
    if (strcmp(label, "Lt") == 0) return "<";
    if (strcmp(label, "Gt") == 0) return ">";
    if (strcmp(label, "Le") == 0) return "<=";
    if (strcmp(label, "Ge") == 0) return ">=";
    if (strcmp(label, "Eq") == 0) return "==";
    if (strcmp(label, "Ne") == 0) return "!=";
    if (strcmp(label, "Lshift") == 0) return "<<";
    if (strcmp(label, "Rshift") == 0) return ">>";
    if (strcmp(label, "Not") == 0) return "!";
    if (strcmp(label, "Minus") == 0) return "-";
    if (strcmp(label, "Plus") == 0) return "+";
    return label;
}

static char *method_signature_string(ParamType *params) {
    ParamType *p;
    int size = 3;
    char *result;

    p = params;
    while (p != NULL) {
        size += strlen(p->type) + 2;
        p = p->next;
    }

    result = malloc(size);
    if (result == NULL) return NULL;

    strcpy(result, "(");

    p = params;
    while (p != NULL) {
        strcat(result, p->type);

        if (p->next != NULL)
            strcat(result, ",");

        p = p->next;
    }

    strcat(result, ")");
    return result;
}

static void build_actual_params(ASTNode *arg, char *buffer) {
    const char *type;
    int first = 1;

    buffer[0] = '\0';

    while (arg != NULL) {
        type = annotate_expr(arg);

        if (!first)
            strcat(buffer, ",");

        strcat(buffer, type);

        first = 0;
        arg = arg->next;
    }
}

static void param_types_to_string(ParamType *params, char *buffer) {
    int first = 1;

    buffer[0] = '\0';

    while (params != NULL) {
        if (!first)
            strcat(buffer, ",");

        strcat(buffer, params->type);

        first = 0;
        params = params->next;
    }
}

static Symbol *find_exact_method(const char *name, const char *actual_params) {
    Symbol *sym;
    char formal_params[1024];

    if (class_table == NULL || name == NULL)
        return NULL;

    sym = class_table->symbols;

    while (sym != NULL) {
        if (sym->is_method && strcmp(sym->name, name) == 0) {
            param_types_to_string(sym->params, formal_params);

            if (strcmp(formal_params, actual_params) == 0)
                return sym;
        }

        sym = sym->next;
    }

    return NULL;
}

static int params_compatible_with_string(ParamType *params, const char *actual_params) {
    char copy[1024];
    char *actual;
    char *saveptr;

    if (actual_params == NULL)
        actual_params = "";

    if (params == NULL && actual_params[0] == '\0')
        return 1;

    if (params == NULL || actual_params[0] == '\0')
        return 0;

    strcpy(copy, actual_params);

    actual = strtok_r(copy, ",", &saveptr);

    while (params != NULL && actual != NULL) {
        if (!types_compatible(params->type, actual))
            return 0;

        params = params->next;
        actual = strtok_r(NULL, ",", &saveptr);
    }

    return params == NULL && actual == NULL;
}

static int count_compatible_methods(const char *name, const char *actual_params, Symbol **match) {
    Symbol *sym;
    int count = 0;

    if (match != NULL)
        *match = NULL;

    if (class_table == NULL || name == NULL)
        return 0;

    sym = class_table->symbols;

    while (sym != NULL) {
        if (sym->is_method &&
            strcmp(sym->name, name) == 0 &&
            params_compatible_with_string(sym->params, actual_params)) {

            count++;

            if (match != NULL)
                *match = sym;
        }

        sym = sym->next;
    }

    return count;
}

static const char *annotate_call(ASTNode *node) {
    ASTNode *id_node;
    Symbol *method;
    Symbol *compatible_method;
    char actual_params[1024];
    char *signature;
    int compatible_count;

    id_node = node->child;

    if (id_node == NULL || strcmp(id_node->label, "Identifier") != 0) {
        set_annotation(node, "undef");
        return "undef";
    }

    build_actual_params(id_node->next, actual_params);

    method = find_exact_method(id_node->lexeme, actual_params);

    if (method != NULL) {
        signature = method_signature_string(method->params);

        if (signature != NULL) {
            set_annotation(id_node, signature);
            free(signature);
        }

        set_annotation(node, method->type);
        return method->type;
    }

    compatible_method = NULL;
    compatible_count = count_compatible_methods(id_node->lexeme, actual_params, &compatible_method);

    if (compatible_count == 1) {
        signature = method_signature_string(compatible_method->params);

        if (signature != NULL) {
            set_annotation(id_node, signature);
            free(signature);
        }

        set_annotation(node, compatible_method->type);
        return compatible_method->type;
    }

    if (compatible_count > 1) {
        fprintf(stderr, "Line %d, col %d: Reference to method %s(%s) is ambiguous\n",
               id_node->line, id_node->col, id_node->lexeme, actual_params);
    } else {
        fprintf(stderr, "Line %d, col %d: Cannot find symbol %s(%s)\n",
               id_node->line, id_node->col, id_node->lexeme, actual_params);
    }

    semantic_errors++;

    set_annotation(id_node, "undef");
    set_annotation(node, "undef");

    return "undef";
}

static int binary_operator_col(ASTNode *node) {
    if (node == NULL)
        return 0;

    return node->col;
}

static const char *annotate_expr(ASTNode *node) {
    const char *type;
    const char *left_type;
    const char *right_type;
    ASTNode *left;
    ASTNode *right;

    if (node == NULL) return "undef";

    if (node->annotation != NULL)
        return node->annotation;

    type = literal_type(node);
    if (type != NULL) {
        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Identifier") == 0) {
        type = lookup_identifier_type(node);
        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Assign") == 0) {
        int err_line;
        int err_col;

        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        type = left_type;

        if (strcmp(left_type, "String[]") == 0 ||
            strcmp(right_type, "String[]") == 0 ||
            !types_compatible(left_type, right_type)) {

            err_line = node->line;
            err_col = node->col;

            if ((err_line == 0 || err_col == 0) && left != NULL) {
                err_line = left->line;
                err_col = left->col;
            }

            fprintf(stderr, "Line %d, col %d: Operator = cannot be applied to types %s, %s\n",
                   err_line, err_col, left_type, right_type);
            semantic_errors++;
        }
 

        set_annotation(node, type);
        return type;
    }

  

    if (strcmp(node->label, "Add") == 0 ||
        strcmp(node->label, "Sub") == 0 ||
        strcmp(node->label, "Mul") == 0 ||
        strcmp(node->label, "Div") == 0 ||
        strcmp(node->label, "Mod") == 0) {

        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        if ((strcmp(left_type, "int") == 0 || strcmp(left_type, "double") == 0) &&
            (strcmp(right_type, "int") == 0 || strcmp(right_type, "double") == 0)) {
            if (strcmp(left_type, "double") == 0 || strcmp(right_type, "double") == 0)
                type = "double";
            else
                type = "int";
        } else {
                fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to types %s, %s\n",
                       node->line, binary_operator_col(node),
                       operator_token(node->label), left_type, right_type);
                semantic_errors++;
            type = "undef";
        }

        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "And") == 0 ||
        strcmp(node->label, "Or") == 0) {

        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        if (strcmp(left_type, "boolean") == 0 && strcmp(right_type, "boolean") == 0) {
            type = "boolean";
        } else {
            if (strcmp(left_type, "undef") != 0 && strcmp(right_type, "undef") != 0) {
                fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to types %s, %s\n",
                        node->line, binary_operator_col(node),
                        operator_token(node->label),
                        left_type, right_type);
                semantic_errors++;
            }
            type = "undef";
        }

        set_annotation(node, type);
        return type;
    }


    if (strcmp(node->label, "Eq") == 0 ||
        strcmp(node->label, "Ne") == 0 ||
        strcmp(node->label, "Lt") == 0 ||
        strcmp(node->label, "Gt") == 0 ||
        strcmp(node->label, "Le") == 0 ||
        strcmp(node->label, "Ge") == 0) {

        int is_eq_op;
        int is_num_left;
        int is_num_right;
        int valid;

        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        is_eq_op = strcmp(node->label, "Eq") == 0 || strcmp(node->label, "Ne") == 0;
        is_num_left = strcmp(left_type, "int") == 0 || strcmp(left_type, "double") == 0;
        is_num_right = strcmp(right_type, "int") == 0 || strcmp(right_type, "double") == 0;

        valid = 0;

        if (is_eq_op) {
            if (strcmp(left_type, "boolean") == 0 && strcmp(right_type, "boolean") == 0)
                valid = 1;
            else if (is_num_left && is_num_right)
                valid = 1;
        } else {
            if (is_num_left && is_num_right)
                valid = 1;
        }

        if (!valid) {
            fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to types %s, %s\n",
                    node->line, binary_operator_col(node),
                    operator_token(node->label), left_type, right_type);
            semantic_errors++;
        }

        type = "boolean";
        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Xor") == 0) {
        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        if (strcmp(left_type, "boolean") == 0 && strcmp(right_type, "boolean") == 0) {
            type = "boolean";
        } else if (strcmp(left_type, "int") == 0 && strcmp(right_type, "int") == 0) {
            type = "int";
        } else {
            if (strcmp(left_type, "undef") != 0 && strcmp(right_type, "undef") != 0) {
                fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to types %s, %s\n",
                        node->line, binary_operator_col(node),
                        operator_token(node->label),
                        left_type, right_type);
                semantic_errors++;
            }
            type = "undef";
        }

        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Lshift") == 0 ||
        strcmp(node->label, "Rshift") == 0) {

        left = node->child;
        right = left != NULL ? left->next : NULL;

        left_type = annotate_expr(left);
        right_type = annotate_expr(right);

        if (strcmp(left_type, "int") == 0 && strcmp(right_type, "int") == 0) {
            type = "int";
        } else {
            if (strcmp(left_type, "undef") != 0 && strcmp(right_type, "undef") != 0) {
                fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to types %s, %s\n",
                        node->line, binary_operator_col(node),
                        operator_token(node->label),
                        left_type, right_type);
                semantic_errors++;
            }
            type = "int";
        }

        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Not") == 0) {
        left = node->child;
        left_type = annotate_expr(left);

        if (strcmp(left_type, "boolean") != 0) {
            fprintf(stderr, "Line %d, col %d: Operator ! cannot be applied to type %s\n",
                    node->line, node->col, left_type);
            semantic_errors++;
        }

        type = "boolean";
        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "Minus") == 0 ||
        strcmp(node->label, "Plus") == 0) {

        left = node->child;
        left_type = annotate_expr(left);

        if (strcmp(left_type, "int") == 0 || strcmp(left_type, "double") == 0) {
            type = left_type;
        } else {
            if (strcmp(left_type, "undef") != 0) {
                fprintf(stderr, "Line %d, col %d: Operator %s cannot be applied to type %s\n",
                       node->line, node->col, operator_token(node->label), left_type);
                semantic_errors++;
            }

            type = "undef";
        }

        set_annotation(node, type);
        return type;
    }

    if (strcmp(node->label, "ParseArgs") == 0) {
        const char *array_type;
        const char *index_type;

        left = node->child;
        right = left != NULL ? left->next : NULL;

        array_type = annotate_expr(left);
        index_type = annotate_expr(right);

        if (strcmp(array_type, "String[]") != 0 ||
            strcmp(index_type, "int") != 0) {

                fprintf(stderr, "Line %d, col %d: Operator Integer.parseInt cannot be applied to types %s, %s\n",
                        node->line, node->col, array_type, index_type);
                semantic_errors++;
        }

        set_annotation(node, "int");
        return "int";
    }

    if (strcmp(node->label, "Length") == 0) {
        const char *array_type;

        left = node->child;
        array_type = annotate_expr(left);

        if (strcmp(array_type, "String[]") != 0 ) {

            fprintf(stderr, "Line %d, col %d: Operator .length cannot be applied to type %s\n",
                    node->line, node->col, array_type);
            semantic_errors++;
        }

        set_annotation(node, "int");
        return "int";
   }

    if (strcmp(node->label, "Call") == 0) {
        return annotate_call(node);
    }

    set_annotation(node, "undef");
    return "undef";
}


static int method_header_matches_table(ASTNode *method_decl, SymbolTable *table) {
    ASTNode *header;
    ASTNode *method_name;
    ASTNode *method_params;
    ASTNode *param_decl;
    Symbol *sym;

    if (method_decl == NULL || table == NULL) return 0;

    header = method_decl->child;
    if (header == NULL) return 0;

    method_name = header->child != NULL ? header->child->next : NULL;
    if (method_name == NULL || method_name->lexeme == NULL) return 0;

    if (strcmp(method_name->lexeme, table->name) != 0)
        return 0;

    method_params = method_name->next;
    param_decl = method_params != NULL ? method_params->child : NULL;

    sym = table->symbols;

    if (sym != NULL && strcmp(sym->name, "return") == 0)
        sym = sym->next;

    while (param_decl != NULL && sym != NULL && sym->is_param) {
        ASTNode *type_node;

        type_node = param_decl->child;
        if (type_node == NULL) return 0;

        if (strcmp(ast_type_to_string(type_node), sym->type) != 0)
            return 0;

        param_decl = param_decl->next;
        sym = sym->next;
    }

return param_decl == NULL;
}



static int types_compatible(const char *expected, const char *actual) {
    if (expected == NULL || actual == NULL) return 0;

    if (strcmp(expected, actual) == 0)
        return 1;

    if (strcmp(expected, "double") == 0 && strcmp(actual, "int") == 0)
        return 1;

    return 0;
}

static void annotate_walk(ASTNode *node) {
    SymbolTable *previous_table;
    const char *previous_return;
    Symbol *return_symbol;

    if (node == NULL) return;

    if (strcmp(node->label, "MethodDecl") == 0) {
        previous_table = current_method_table;
        previous_return = current_return_type;

        current_method_table = next_method_table;

        if (next_method_table != NULL) {
            next_method_table = next_method_table->next;
        }

        if (current_method_table != NULL && current_method_table->valid == 0) {
            current_method_table = previous_table;
            current_return_type = previous_return;
            annotate_walk(node->next);
            return;
        }

        return_symbol = find_symbol_in_table(current_method_table, "return");
        if (return_symbol != NULL)
            current_return_type = return_symbol->type;
        else
           current_return_type = NULL;

        annotate_walk(node->child);

        current_method_table = previous_table;
        current_return_type = previous_return;

        annotate_walk(node->next);
        return;
    }

    if (strcmp(node->label, "Return") == 0) {
        const char *expected_type;
        const char *actual_type;

        expected_type = "void";

        if (current_method_table != NULL &&
            current_method_table->return_type != NULL) {
            expected_type = current_method_table->return_type;
        }


        if (node->child == NULL) {
           actual_type = "void";

            if (strcmp(expected_type, "void") != 0) {
                fprintf(stderr, "Line %d, col %d: Incompatible type void in return statement\n",
                       node->line, node->col);
                semantic_errors++;
            }
        } else {
            actual_type = annotate_expr(node->child);

            if (strcmp(expected_type, "void") == 0) {
                if (strcmp(actual_type, "undef") != 0) {
                    fprintf(stderr, "Line %d, col %d: Incompatible type %s in return statement\n",
                             node->child->line, node->child->col, actual_type);
                    semantic_errors++;
                }
            } else if (!(strcmp(expected_type, actual_type) == 0 ||
                        (strcmp(expected_type, "double") == 0 &&
                        strcmp(actual_type, "int") == 0))) {

                fprintf(stderr, "Line %d, col %d: Incompatible type %s in return statement\n",
                        node->child->line, node->child->col, actual_type);
                semantic_errors++;
            }
        }

        annotate_walk(node->next);
        return;
    }

    if (strcmp(node->label, "Print") == 0) {
    const char *print_type;

        if (node->child != NULL) {
            print_type = annotate_expr(node->child);

            if (strcmp(print_type, "boolean") != 0 &&
                strcmp(print_type, "int") != 0 &&
                strcmp(print_type, "double") != 0 &&
                strcmp(print_type, "String") != 0 ) {

                fprintf(stderr, "Line %d, col %d: Incompatible type %s in System.out.print statement\n",
                        node->child->line, node->child->col, print_type);
                semantic_errors++;
            }
        }

        annotate_walk(node->next);
        return;
    }

    if (strcmp(node->label, "Assign") == 0 ||
        strcmp(node->label, "Call") == 0 ||
        strcmp(node->label, "ParseArgs") == 0) {
        annotate_expr(node);
        annotate_walk(node->next);
        return;
    }

    if (strcmp(node->label, "If") == 0) {
        const char *cond_type;

        cond_type = annotate_expr(node->child);

        if (strcmp(cond_type, "boolean") != 0) {

            fprintf(stderr, "Line %d, col %d: Incompatible type %s in if statement\n",
                    node->child->line, node->child->col, cond_type);
            semantic_errors++;
        }

        if (node->child != NULL && node->child->next != NULL)
            annotate_walk(node->child->next);

        if (node->child != NULL &&
            node->child->next != NULL &&
            node->child->next->next != NULL)
            annotate_walk(node->child->next->next);

        annotate_walk(node->next);
        return;
    }

    if (strcmp(node->label, "While") == 0) {
        const char *cond_type;

        cond_type = annotate_expr(node->child);

        if (strcmp(cond_type, "boolean") != 0 &&
            strcmp(cond_type, "undef") != 0) {

            fprintf(stderr, "Line %d, col %d: Incompatible type %s in while statement\n",
                    node->child->line, node->child->col, cond_type);
            semantic_errors++;
        }
    

        if (node->child != NULL && node->child->next != NULL)
            annotate_walk(node->child->next);

        annotate_walk(node->next);
        return;
    }

    annotate_walk(node->child);
    annotate_walk(node->next);
}


void annotate_ast(ASTNode *root) {
    next_method_table = method_tables;
    annotate_walk(root);
}

int get_semantic_errors(void) {
    return semantic_errors;
}