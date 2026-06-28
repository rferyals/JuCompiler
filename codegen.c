#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"

/* =========================================================
   Global state
   ========================================================= */

static int temp_cnt = 0;
static int label_cnt = 0;

typedef struct var_slot {
    char *name;
    int alloca_t;  /* temp index of the alloca'd pointer */
    int len_t;     /* for String[]: temp of the length alloca; -1 otherwise */
    struct var_slot *next;
} VarSlot;

static VarSlot *var_slots = NULL;
static SymbolTable *cg_method_table = NULL;

typedef struct str_lit {
    char *value;
    int id;
    struct str_lit *next;
} StrLit;

static StrLit *str_lits = NULL;
static int str_lit_cnt = 0;

/* =========================================================
   Helpers
   ========================================================= */

static int next_temp(void)  { return temp_cnt++; }
static int next_label(void) { return label_cnt++; }

static const char *llvm_type(const char *juc) {
    if (!juc)                        return "void";
    if (strcmp(juc, "int") == 0)     return "i32";
    if (strcmp(juc, "double") == 0)  return "double";
    if (strcmp(juc, "boolean") == 0) return "i1";
    if (strcmp(juc, "void") == 0)    return "void";
    return "i8*";
}

static void free_var_slots(void) {
    VarSlot *v = var_slots, *n;
    while (v) { n = v->next; free(v->name); free(v); v = n; }
    var_slots = NULL;
}

static VarSlot *find_slot(const char *name) {
    VarSlot *v = var_slots;
    while (v) { if (strcmp(v->name, name) == 0) return v; v = v->next; }
    return NULL;
}

static void add_slot(const char *name, int alloca_t, int len_t) {
    VarSlot *v = malloc(sizeof(VarSlot));
    v->name = strdup(name);
    v->alloca_t = alloca_t;
    v->len_t = len_t;
    v->next = var_slots;
    var_slots = v;
}

static const char *symbol_type(const char *name) {
    Symbol *sym;
    if (cg_method_table) {
        sym = cg_method_table->symbols;
        while (sym) { if (strcmp(sym->name, name) == 0) return sym->type; sym = sym->next; }
    }
    sym = class_table ? class_table->symbols : NULL;
    while (sym) { if (strcmp(sym->name, name) == 0) return sym->type; sym = sym->next; }
    return "int";
}

static int emit_sitofp(int src_t) {
    int t = next_temp();
    printf("  %%%d = sitofp i32 %%%d to double\n", t, src_t);
    return t;
}

/* Returns the class-level field symbol if 'name' is a static field, NULL otherwise */
static Symbol *find_class_field(const char *name) {
    Symbol *sym = class_table ? class_table->symbols : NULL;
    while (sym) {
        if (!sym->is_method && strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

static int emit_load(const char *name) {
    VarSlot *slot = find_slot(name);
    const char *jtype = symbol_type(name);
    int t = next_temp();
    if (slot) {
        if (strcmp(jtype, "double") == 0)
            printf("  %%%d = load double, double* %%%d\n", t, slot->alloca_t);
        else if (strcmp(jtype, "boolean") == 0)
            printf("  %%%d = load i1, i1* %%%d\n", t, slot->alloca_t);
        else
            printf("  %%%d = load i32, i32* %%%d\n", t, slot->alloca_t);
    } else if (find_class_field(name)) {
        /* static field — access the LLVM global */
        if (strcmp(jtype, "double") == 0)
            printf("  %%%d = load double, double* @%s\n", t, name);
        else if (strcmp(jtype, "boolean") == 0)
            printf("  %%%d = load i1, i1* @%s\n", t, name);
        else
            printf("  %%%d = load i32, i32* @%s\n", t, name);
    } else {
        printf("  %%%d = add i32 0, 0\n", t);
    }
    return t;
}

static void emit_store(const char *name, int val_t, const char *jtype) {
    VarSlot *slot = find_slot(name);
    if (slot) {
        if (strcmp(jtype, "double") == 0)
            printf("  store double %%%d, double* %%%d\n", val_t, slot->alloca_t);
        else if (strcmp(jtype, "boolean") == 0)
            printf("  store i1 %%%d, i1* %%%d\n", val_t, slot->alloca_t);
        else
            printf("  store i32 %%%d, i32* %%%d\n", val_t, slot->alloca_t);
    } else if (find_class_field(name)) {
        /* static field — write to the LLVM global */
        if (strcmp(jtype, "double") == 0)
            printf("  store double %%%d, double* @%s\n", val_t, name);
        else if (strcmp(jtype, "boolean") == 0)
            printf("  store i1 %%%d, i1* @%s\n", val_t, name);
        else
            printf("  store i32 %%%d, i32* @%s\n", val_t, name);
    }
}

/* string literals */
static int collect_str_lit(const char *lexeme) {
    StrLit *s = str_lits;
    while (s) { if (strcmp(s->value, lexeme) == 0) return s->id; s = s->next; }
    s = malloc(sizeof(StrLit));
    s->value = strdup(lexeme);
    s->id = str_lit_cnt++;
    s->next = str_lits;
    str_lits = s;
    return s->id;
}

static int str_byte_len(const char *lex) {
    int len = 0;
    const char *p = lex + 1;
    while (*p && *p != '"') {
        if (*p == '\\') p++;
        p++; len++;
    }
    return len + 1;
}

static void emit_str_constant(const char *lex, int id, int byte_len) {
    printf("@.str%d = private unnamed_addr constant [%d x i8] c\"", id, byte_len);
    const char *p = lex + 1;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n': printf("\\0A"); break;
                case 'r': printf("\\0D"); break;
                case 't': printf("\\09"); break;
                case 'f': printf("\\0C"); break;
                case '\\': printf("\\\\"); break;
                case '"': printf("\\22"); break;
                default: printf("\\%c", *p); break;
            }
        } else {
            unsigned char c = (unsigned char)*p;
            if (c >= 32 && c < 127 && c != '"' && c != '\\')
                putchar(c);
            else
                printf("\\%02X", c);
        }
        p++;
    }
    printf("\\00\"\n");
}

/* =========================================================
   Forward declarations
   ========================================================= */

static int  codegen_expr(ASTNode *node);
static void codegen_stmt(ASTNode *node);  /* processes single node, NOT node->next */
static void codegen_stmts(ASTNode *first); /* iterates sibling chain */
static SymbolTable *find_method_table_cg(const char *name, ParamType *params);
static void mangle_name(char *buf, int bufsize, const char *name, ParamType *params);

/* =========================================================
   Expression codegen
   ========================================================= */

static int codegen_expr(ASTNode *node) {
    int t, tl, tr;
    const char *ltype, *rtype, *ntype;
    ASTNode *left, *right;

    if (!node) { t = next_temp(); printf("  %%%d = add i32 0, 0\n", t); return t; }

    /* Natural */
    if (strcmp(node->label, "Natural") == 0) {
        char buf[64]; int j = 0;
        for (const char *p = node->lexeme; *p; p++) if (*p != '_') buf[j++] = *p;
        buf[j] = '\0';
        t = next_temp();
        printf("  %%%d = add i32 %s, 0\n", t, buf);
        return t;
    }

    /* Decimal */
    if (strcmp(node->label, "Decimal") == 0) {
        char raw[64]; int j = 0;
        for (const char *p = node->lexeme; *p; p++) if (*p != '_') raw[j++] = *p;
        raw[j] = '\0';

        /* Normalize literal to a form LLVM IR accepts:
           - Must have a decimal point
           - Must not start with '.' (needs leading digit)
           - If no '.' but has 'e'/'E', insert ".0" before exponent */
        char buf[128];
        const char *hasdot = strchr(raw, '.');
        const char *hasexp = strchr(raw, 'e');
        if (!hasexp) hasexp = strchr(raw, 'E');

        if (!hasdot && !hasexp) {
            /* e.g. "2" → "2.0" */
            snprintf(buf, sizeof(buf), "%s.0", raw);
        } else if (!hasdot && hasexp) {
            /* e.g. "123e-10" → "123.0e-10" */
            int prefix_len = (int)(hasexp - raw);
            memcpy(buf, raw, prefix_len);
            buf[prefix_len] = '\0';
            strncat(buf, ".0",   sizeof(buf) - strlen(buf) - 1);
            strncat(buf, hasexp, sizeof(buf) - strlen(buf) - 1);
        } else if (hasdot && raw[0] == '.') {
            /* e.g. ".5" or ".0e-10" → "0.5" / "0.0e-10" */
            snprintf(buf, sizeof(buf), "0%s", raw);
        } else {
            /* Already valid: "2.2", "1.e01", "3.14e2", etc. */
            snprintf(buf, sizeof(buf), "%s", raw);
        }

        t = next_temp();
        printf("  %%%d = fadd double %s, 0.0\n", t, buf);
        return t;
    }

    /* BoolLit */
    if (strcmp(node->label, "BoolLit") == 0) {
        t = next_temp();
        printf("  %%%d = add i1 %s, 0\n", t,
               strcmp(node->lexeme, "true") == 0 ? "1" : "0");
        return t;
    }

    /* Identifier */
    if (strcmp(node->label, "Identifier") == 0) {
        return emit_load(node->lexeme);
    }

    /* Assign */
    if (strcmp(node->label, "Assign") == 0) {
        left  = node->child;
        right = left ? left->next : NULL;
        const char *lj = symbol_type(left ? left->lexeme : "");
        const char *rj = right ? right->annotation : "undef";
        tr = codegen_expr(right);
        if (strcmp(lj, "double") == 0 && rj && strcmp(rj, "int") == 0)
            tr = emit_sitofp(tr);
        emit_store(left ? left->lexeme : "", tr, lj);
        return tr;
    }

    /* Arithmetic */
    if (strcmp(node->label,"Add")==0 || strcmp(node->label,"Sub")==0 ||
        strcmp(node->label,"Mul")==0 || strcmp(node->label,"Div")==0 ||
        strcmp(node->label,"Mod")==0) {
        left = node->child; right = left ? left->next : NULL;
        ltype = left  ? left->annotation  : "int";
        rtype = right ? right->annotation : "int";
        tl = codegen_expr(left); tr = codegen_expr(right);
        int is_fp = (ltype && strcmp(ltype,"double")==0) ||
                    (rtype && strcmp(rtype,"double")==0);
        if (is_fp) {
            if (ltype && strcmp(ltype,"int")==0) tl = emit_sitofp(tl);
            if (rtype && strcmp(rtype,"int")==0) tr = emit_sitofp(tr);
        }
        t = next_temp();
        if (is_fp) {
            const char *op = strcmp(node->label,"Add")==0 ? "fadd" :
                             strcmp(node->label,"Sub")==0 ? "fsub" :
                             strcmp(node->label,"Mul")==0 ? "fmul" :
                             strcmp(node->label,"Div")==0 ? "fdiv" : "frem";
            printf("  %%%d = %s double %%%d, %%%d\n", t, op, tl, tr);
        } else {
            const char *op = strcmp(node->label,"Add")==0 ? "add" :
                             strcmp(node->label,"Sub")==0 ? "sub" :
                             strcmp(node->label,"Mul")==0 ? "mul" :
                             strcmp(node->label,"Div")==0 ? "sdiv" : "srem";
            printf("  %%%d = %s i32 %%%d, %%%d\n", t, op, tl, tr);
        }
        return t;
    }

    /* And, Or */
    if (strcmp(node->label,"And")==0 || strcmp(node->label,"Or")==0) {
        left = node->child; right = left ? left->next : NULL;
        tl = codegen_expr(left); tr = codegen_expr(right);
        t = next_temp();
        printf("  %%%d = %s i1 %%%d, %%%d\n", t,
               strcmp(node->label,"And")==0 ? "and" : "or", tl, tr);
        return t;
    }

    /* Xor */
    if (strcmp(node->label,"Xor")==0) {
        left = node->child; right = left ? left->next : NULL;
        ltype = left ? left->annotation : "int";
        tl = codegen_expr(left); tr = codegen_expr(right);
        t = next_temp();
        const char *xtype = (ltype && strcmp(ltype,"boolean")==0) ? "i1" : "i32";
        printf("  %%%d = xor %s %%%d, %%%d\n", t, xtype, tl, tr);
        return t;
    }

    /* Shift */
    if (strcmp(node->label,"Lshift")==0 || strcmp(node->label,"Rshift")==0) {
        left = node->child; right = left ? left->next : NULL;
        tl = codegen_expr(left); tr = codegen_expr(right);
        t = next_temp();
        printf("  %%%d = %s i32 %%%d, %%%d\n", t,
               strcmp(node->label,"Lshift")==0 ? "shl" : "ashr", tl, tr);
        return t;
    }

    /* Comparisons */
    if (strcmp(node->label,"Eq")==0 || strcmp(node->label,"Ne")==0 ||
        strcmp(node->label,"Lt")==0 || strcmp(node->label,"Gt")==0 ||
        strcmp(node->label,"Le")==0 || strcmp(node->label,"Ge")==0) {
        left = node->child; right = left ? left->next : NULL;
        ltype = left  ? left->annotation  : "int";
        rtype = right ? right->annotation : "int";
        tl = codegen_expr(left); tr = codegen_expr(right);
        int is_fp   = (ltype && strcmp(ltype,"double")==0) ||
                      (rtype && strcmp(rtype,"double")==0);
        int is_bool = (ltype && strcmp(ltype,"boolean")==0) &&
                      (rtype && strcmp(rtype,"boolean")==0);
        if (is_fp) {
            if (ltype && strcmp(ltype,"int")==0) tl = emit_sitofp(tl);
            if (rtype && strcmp(rtype,"int")==0) tr = emit_sitofp(tr);
        }
        t = next_temp();
        if (is_fp) {
            const char *op = strcmp(node->label,"Eq")==0 ? "fcmp oeq" :
                             strcmp(node->label,"Ne")==0 ? "fcmp one" :
                             strcmp(node->label,"Lt")==0 ? "fcmp olt" :
                             strcmp(node->label,"Gt")==0 ? "fcmp ogt" :
                             strcmp(node->label,"Le")==0 ? "fcmp ole" : "fcmp oge";
            printf("  %%%d = %s double %%%d, %%%d\n", t, op, tl, tr);
        } else if (is_bool) {
            printf("  %%%d = icmp %s i1 %%%d, %%%d\n", t,
                   strcmp(node->label,"Eq")==0 ? "eq" : "ne", tl, tr);
        } else {
            const char *op = strcmp(node->label,"Eq")==0 ? "icmp eq" :
                             strcmp(node->label,"Ne")==0 ? "icmp ne" :
                             strcmp(node->label,"Lt")==0 ? "icmp slt" :
                             strcmp(node->label,"Gt")==0 ? "icmp sgt" :
                             strcmp(node->label,"Le")==0 ? "icmp sle" : "icmp sge";
            printf("  %%%d = %s i32 %%%d, %%%d\n", t, op, tl, tr);
        }
        return t;
    }

    /* Not */
    if (strcmp(node->label,"Not")==0) {
        left = node->child;
        tl = codegen_expr(left);
        t = next_temp();
        printf("  %%%d = xor i1 %%%d, 1\n", t, tl);
        return t;
    }

    /* Unary Minus */
    if (strcmp(node->label,"Minus")==0) {
        left = node->child;
        ntype = left ? left->annotation : "int";
        tl = codegen_expr(left);
        t = next_temp();
        if (ntype && strcmp(ntype,"double")==0)
            printf("  %%%d = fneg double %%%d\n", t, tl);
        else
            printf("  %%%d = sub i32 0, %%%d\n", t, tl);
        return t;
    }

    /* Unary Plus */
    if (strcmp(node->label,"Plus")==0)
        return codegen_expr(node->child);

    /* Length */
    if (strcmp(node->label,"Length")==0) {
        left = node->child;
        VarSlot *slot = find_slot(left ? left->lexeme : "");
        t = next_temp();
        if (slot && slot->len_t >= 0)
            printf("  %%%d = load i32, i32* %%%d\n", t, slot->len_t);
        else
            printf("  %%%d = add i32 0, 0\n", t);
        return t;
    }

    /* ParseArgs */
    if (strcmp(node->label,"ParseArgs")==0) {
        left  = node->child;
        right = left ? left->next : NULL;
        VarSlot *slot = find_slot(left ? left->lexeme : "");
        int idx_t = codegen_expr(right);
        int ptr_t = next_temp();
        if (slot)
            printf("  %%%d = load i8**, i8*** %%%d\n", ptr_t, slot->alloca_t);
        else
            printf("  %%%d = add i32 0, 0\n", ptr_t);
        int ep_t = next_temp();
        printf("  %%%d = getelementptr i8*, i8** %%%d, i32 %%%d\n", ep_t, ptr_t, idx_t);
        int sp_t = next_temp();
        printf("  %%%d = load i8*, i8** %%%d\n", sp_t, ep_t);
        t = next_temp();
        printf("  %%%d = call i32 @atoi(i8* %%%d)\n", t, sp_t);
        return t;
    }

    /* Call */
    if (strcmp(node->label,"Call")==0) {
        ASTNode *name_node = node->child;
        ASTNode *arg = name_node ? name_node->next : NULL;
        const char *mname = name_node ? name_node->lexeme : "";
        const char *ret_jtype = node->annotation ? node->annotation : "void";

        /* find method symbol for param types (exact match first, then promotion) */
        Symbol *msym = NULL;
        {
            Symbol *s;
            /* Phase 1: exact type match */
            for (s = class_table ? class_table->symbols : NULL; s; s = s->next) {
                if (!s->is_method || strcmp(s->name, mname) != 0) continue;
                ParamType *pt = s->params;
                ASTNode   *ac = arg;
                int ok = 1;
                while (pt && ac) {
                    const char *at = ac->annotation ? ac->annotation : "int";
                    if (strcmp(pt->type, at) != 0) { ok = 0; break; }
                    pt = pt->next; ac = ac->next;
                }
                if (ok && !pt && !ac) { msym = s; break; }
            }
            /* Phase 2: compatible match (int→double promotion) */
            if (!msym) {
                for (s = class_table ? class_table->symbols : NULL; s; s = s->next) {
                    if (!s->is_method || strcmp(s->name, mname) != 0) continue;
                    ParamType *pt = s->params;
                    ASTNode   *ac = arg;
                    int ok = 1;
                    while (pt && ac) {
                        const char *at = ac->annotation ? ac->annotation : "int";
                        int compat = (strcmp(pt->type, at) == 0) ||
                                     (strcmp(pt->type,"double")==0 && strcmp(at,"int")==0);
                        if (!compat) { ok = 0; break; }
                        pt = pt->next; ac = ac->next;
                    }
                    if (ok && !pt && !ac) { msym = s; break; }
                }
            }
        }

        int arg_temps[64];
        const char *arg_jtypes[64];
        int argc = 0;
        ParamType *pt = msym ? msym->params : NULL;
        ASTNode   *ac = arg;
        while (ac) {
            const char *ptype = pt ? pt->type : (ac->annotation ? ac->annotation : "int");
            arg_jtypes[argc] = ptype;
            int av = codegen_expr(ac);
            if (strcmp(ptype,"double")==0 && ac->annotation && strcmp(ac->annotation,"int")==0)
                av = emit_sitofp(av);
            arg_temps[argc++] = av;
            ac = ac->next;
            if (pt) pt = pt->next;
        }

        /* Build expanded argument list: String[] expands to (i32 len, i8** ptr) */
        int call_temps[128];
        const char *call_llvm_types[128];
        int call_argc = 0;

        pt = msym ? msym->params : NULL;
        ac = arg;
        int src_i = 0;
        while (src_i < argc) {
            const char *pjt = arg_jtypes[src_i];
            if (strcmp(pjt,"String[]")==0) {
                /* Pass len and ptr from the source variable's slot */
                VarSlot *sslot = NULL;
                if (ac && strcmp(ac->label,"Identifier")==0)
                    sslot = find_slot(ac->lexeme);
                if (sslot && sslot->len_t >= 0) {
                    int lt = next_temp();
                    printf("  %%%d = load i32, i32* %%%d\n", lt, sslot->len_t);
                    int pt2 = next_temp();
                    printf("  %%%d = load i8**, i8*** %%%d\n", pt2, sslot->alloca_t);
                    call_temps[call_argc]           = lt;
                    call_llvm_types[call_argc++]    = "i32";
                    call_temps[call_argc]           = pt2;
                    call_llvm_types[call_argc++]    = "i8**";
                } else {
                    int lt = next_temp();
                    printf("  %%%d = add i32 0, 0\n", lt);
                    call_temps[call_argc]           = lt;
                    call_llvm_types[call_argc++]    = "i32";
                    /* null ptr placeholder */
                    int pt2 = next_temp();
                    printf("  %%%d = add i32 0, 0\n", pt2); /* placeholder */
                    call_temps[call_argc]           = pt2;
                    call_llvm_types[call_argc++]    = "i8**";
                }
            } else {
                call_temps[call_argc]        = arg_temps[src_i];
                call_llvm_types[call_argc++] = llvm_type(pjt);
            }
            src_i++;
            if (ac) ac = ac->next;
            if (pt) pt = pt->next;
        }

        char call_mangled[256];
        mangle_name(call_mangled, sizeof(call_mangled), mname, msym ? msym->params : NULL);

        t = next_temp();
        if (strcmp(ret_jtype,"void")==0)
            printf("  call void @%s(", call_mangled);
        else
            printf("  %%%d = call %s @%s(", t, llvm_type(ret_jtype), call_mangled);

        for (int i = 0; i < call_argc; i++) {
            if (i > 0) printf(", ");
            printf("%s %%%d", call_llvm_types[i], call_temps[i]);
        }
        printf(")\n");
        return t;
    }

    t = next_temp();
    printf("  %%%d = add i32 0, 0\n", t);
    return t;
}

/* =========================================================
   Statement codegen — processes ONLY the given node (not node->next)
   ========================================================= */

static void codegen_stmts(ASTNode *first) {
    ASTNode *cur = first;
    while (cur) {
        codegen_stmt(cur);
        cur = cur->next;
    }
}

static void codegen_stmt(ASTNode *node) {
    if (!node) return;

    /* VarDecl: allocas already emitted at method start */
    if (strcmp(node->label,"VarDecl")==0) return;

    /* Block */
    if (strcmp(node->label,"Block")==0) {
        codegen_stmts(node->child);
        return;
    }

    /* If */
    if (strcmp(node->label,"If")==0) {
        ASTNode *cond  = node->child;
        ASTNode *then_ = cond  ? cond->next  : NULL;
        ASTNode *else_ = then_ ? then_->next : NULL;
        int lbl = next_label();

        int tc = codegen_expr(cond);
        printf("  br i1 %%%d, label %%Lthen%d, label %%Lelse%d\n", tc, lbl, lbl);

        printf("Lthen%d:\n", lbl);
        codegen_stmt(then_);   /* single statement — no next */
        printf("  br label %%Lend%d\n", lbl);

        printf("Lelse%d:\n", lbl);
        int empty_else = (!else_) ||
                         (strcmp(else_->label,"Block")==0 && !else_->child);
        if (!empty_else) codegen_stmt(else_);
        printf("  br label %%Lend%d\n", lbl);

        printf("Lend%d:\n", lbl);
        return;
    }

    /* While */
    if (strcmp(node->label,"While")==0) {
        ASTNode *cond = node->child;
        ASTNode *body = cond ? cond->next : NULL;
        int lbl = next_label();

        printf("  br label %%Lcond%d\n", lbl);
        printf("Lcond%d:\n", lbl);
        int tc = codegen_expr(cond);
        printf("  br i1 %%%d, label %%Lbody%d, label %%Lend%d\n", tc, lbl, lbl);
        printf("Lbody%d:\n", lbl);
        codegen_stmt(body);
        printf("  br label %%Lcond%d\n", lbl);
        printf("Lend%d:\n", lbl);
        return;
    }

    /* Return */
    if (strcmp(node->label,"Return")==0) {
        if (node->child) {
            const char *rtype = node->child->annotation ? node->child->annotation : "int";
            int tv = codegen_expr(node->child);
            /* promote int->double if method return type is double */
            const char *mret = "int";
            if (cg_method_table && cg_method_table->symbols)
                mret = cg_method_table->symbols->type;
            if (strcmp(mret,"double")==0 && strcmp(rtype,"int")==0)
                tv = emit_sitofp(tv);
            printf("  ret %s %%%d\n", llvm_type(mret), tv);
        } else {
            printf("  ret void\n");
        }
        /* dead-code label so subsequent code (if any) is in a valid block */
        printf("Ldead%d:\n", next_label());
        return;
    }

    /* Assign (as statement) */
    if (strcmp(node->label,"Assign")==0) {
        codegen_expr(node);
        return;
    }

    /* Call (as statement) */
    if (strcmp(node->label,"Call")==0) {
        codegen_expr(node);
        return;
    }

    /* ParseArgs (as statement) */
    if (strcmp(node->label,"ParseArgs")==0) {
        codegen_expr(node);
        return;
    }

    /* Print */
    if (strcmp(node->label,"Print")==0) {
        ASTNode *arg = node->child;
        if (!arg) return;

        if (strcmp(arg->label,"StrLit")==0) {
            int id   = collect_str_lit(arg->lexeme);
            int blen = str_byte_len(arg->lexeme);
            int gep  = next_temp();
            printf("  %%%d = getelementptr [%d x i8], [%d x i8]* @.str%d, i32 0, i32 0\n",
                   gep, blen, blen, id);
            int fgep = next_temp();
            printf("  %%%d = getelementptr [3 x i8], [3 x i8]* @.fmt.str, i32 0, i32 0\n", fgep);
            int r = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, i8* %%%d)\n", r, fgep, gep);
            return;
        }

        const char *atype = arg->annotation ? arg->annotation : "undef";
        int tv = codegen_expr(arg);

        if (strcmp(atype,"int")==0) {
            int gep = next_temp();
            printf("  %%%d = getelementptr [3 x i8], [3 x i8]* @.fmt.int, i32 0, i32 0\n", gep);
            int r = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, i32 %%%d)\n", r, gep, tv);

        } else if (strcmp(atype,"double")==0) {
            int gep = next_temp();
            printf("  %%%d = getelementptr [6 x i8], [6 x i8]* @.fmt.double, i32 0, i32 0\n", gep);
            int r = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, double %%%d)\n", r, gep, tv);

        } else if (strcmp(atype,"boolean")==0) {
            int lbl = next_label();
            printf("  br i1 %%%d, label %%Lbtrue%d, label %%Lbfalse%d\n", tv, lbl, lbl);
            printf("Lbtrue%d:\n", lbl);
            int g1 = next_temp();
            printf("  %%%d = getelementptr [5 x i8], [5 x i8]* @.bool.true, i32 0, i32 0\n", g1);
            int fg1 = next_temp();
            printf("  %%%d = getelementptr [3 x i8], [3 x i8]* @.fmt.str, i32 0, i32 0\n", fg1);
            int r1 = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, i8* %%%d)\n", r1, fg1, g1);
            printf("  br label %%Lbend%d\n", lbl);
            printf("Lbfalse%d:\n", lbl);
            int g2 = next_temp();
            printf("  %%%d = getelementptr [6 x i8], [6 x i8]* @.bool.false, i32 0, i32 0\n", g2);
            int fg2 = next_temp();
            printf("  %%%d = getelementptr [3 x i8], [3 x i8]* @.fmt.str, i32 0, i32 0\n", fg2);
            int r2 = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, i8* %%%d)\n", r2, fg2, g2);
            printf("  br label %%Lbend%d\n", lbl);
            printf("Lbend%d:\n", lbl);

        } else {
            /* unknown: treat as int */
            int gep = next_temp();
            printf("  %%%d = getelementptr [3 x i8], [3 x i8]* @.fmt.int, i32 0, i32 0\n", gep);
            int r = next_temp();
            printf("  %%%d = call i32 (i8*, ...) @printf(i8* %%%d, i32 %%%d)\n", r, gep, tv);
        }
        return;
    }

    /* fallback: walk children */
    codegen_stmts(node->child);
}

/* =========================================================
   Method name mangling (for overloaded methods)
   ========================================================= */

static int count_method_overloads(const char *name) {
    int n = 0;
    Symbol *s = class_table ? class_table->symbols : NULL;
    while (s) { if (s->is_method && strcmp(s->name, name) == 0) n++; s = s->next; }
    return n;
}

/* Fill buf with the LLVM function name for method 'name' with given params.
   Non-overloaded methods keep simple prefix "_name".
   Overloaded methods get "_name__<type_chars>" e.g. "_foo__idb". */
static void mangle_name(char *buf, int bufsize, const char *name, ParamType *params) {
    if (count_method_overloads(name) <= 1) {
        snprintf(buf, bufsize, "_%s", name);
        return;
    }
    int written = snprintf(buf, bufsize, "_%s__", name);
    for (ParamType *p = params; p && written < bufsize - 1; p = p->next) {
        char c = 'i';
        if      (strcmp(p->type, "double")   == 0) c = 'd';
        else if (strcmp(p->type, "boolean")  == 0) c = 'b';
        else if (strcmp(p->type, "String[]") == 0) c = 'A';
        buf[written++] = c;
    }
    buf[written] = '\0';
}

/* Build a temporary ParamType list from an AST params node.
   Caller must free the list with free_ptype_list(). */
static ParamType *ast_params_to_ptype(ASTNode *params_node) {
    ParamType *head = NULL, **tail = &head;
    if (!params_node || !params_node->child) return NULL;
    ASTNode *pd = params_node->child;
    while (pd) {
        ASTNode *pt_node = pd->child;
        if (pt_node) {
            ParamType *p = malloc(sizeof(ParamType));
            p->type = strdup(ast_type_to_string(pt_node));
            p->next = NULL;
            *tail = p; tail = &p->next;
        }
        pd = pd->next;
    }
    return head;
}

static void free_ptype_list(ParamType *p) {
    while (p) { ParamType *n = p->next; free(p->type); free(p); p = n; }
}

/* Find the symbol table for method (name, params) using exact param match */
static SymbolTable *find_method_table_exact(const char *name, ParamType *params) {
    return find_method_table_cg(name, params);
}

/* =========================================================
   Method codegen
   ========================================================= */

static SymbolTable *find_method_table_cg(const char *name, ParamType *params) {
    SymbolTable *t = method_tables;
    while (t) {
        if (strcmp(t->name, name) == 0) {
            ParamType *a = t->method_params, *b = params;
            int ok = 1;
            while (a && b) {
                if (strcmp(a->type, b->type) != 0) { ok = 0; break; }
                a = a->next; b = b->next;
            }
            if (ok && !a && !b) return t;
        }
        t = t->next;
    }
    return NULL;
}

static void codegen_method(ASTNode *method_decl) {
    ASTNode *header, *body, *ret_node, *name_node, *params_node;
    if (!method_decl) return;

    header      = method_decl->child;
    body        = header ? header->next : NULL;
    ret_node    = header ? header->child : NULL;
    name_node   = ret_node ? ret_node->next : NULL;
    params_node = name_node ? name_node->next : NULL;
    if (!name_node) return;

    const char *mname    = name_node->lexeme;
    const char *ret_jtype = ast_type_to_string(ret_node);

    /* Build ParamType list from AST to find the EXACT overload's symbol table */
    ParamType *ast_pts = ast_params_to_ptype(params_node);
    cg_method_table = find_method_table_exact(mname, ast_pts);

    /* Build mangled LLVM function name */
    char mangled[256];
    mangle_name(mangled, sizeof(mangled), mname, ast_pts);

    temp_cnt = 0;
    free_var_slots();

    /* emit signature */
    printf("define %s @%s(", llvm_type(ret_jtype), mangled);
    if (params_node && params_node->child) {
        ASTNode *pd = params_node->child;
        int first = 1;
        while (pd) {
            ASTNode *pt = pd->child;
            ASTNode *pn = pt ? pt->next : NULL;
            if (pn) {
                const char *pjt = ast_type_to_string(pt);
                if (!first) printf(", ");
                if (strcmp(pjt,"String[]")==0)
                    printf("i32 %%param_%s_len, i8** %%param_%s_ptr",
                           pn->lexeme, pn->lexeme);
                else
                    printf("%s %%param_%s", llvm_type(pjt), pn->lexeme);
                first = 0;
            }
            pd = pd->next;
        }
    }
    printf(") {\n");
    printf("entry:\n");

    /* allocate slots for parameters */
    if (params_node && params_node->child) {
        ASTNode *pd = params_node->child;
        while (pd) {
            ASTNode *pt = pd->child;
            ASTNode *pn = pt ? pt->next : NULL;
            if (pn) {
                const char *pjt = ast_type_to_string(pt);
                if (strcmp(pjt,"String[]")==0) {
                    int ps = next_temp();
                    printf("  %%%d = alloca i8**\n", ps);
                    int ls = next_temp();
                    printf("  %%%d = alloca i32\n", ls);
                    add_slot(pn->lexeme, ps, ls);
                    printf("  store i8** %%param_%s_ptr, i8*** %%%d\n", pn->lexeme, ps);
                    printf("  store i32 %%param_%s_len, i32* %%%d\n",  pn->lexeme, ls);
                } else {
                    int s = next_temp();
                    printf("  %%%d = alloca %s\n", s, llvm_type(pjt));
                    add_slot(pn->lexeme, s, -1);
                    printf("  store %s %%param_%s, %s* %%%d\n",
                           llvm_type(pjt), pn->lexeme, llvm_type(pjt), s);
                }
            }
            pd = pd->next;
        }
    }

    /* allocate slots for local variables */
    if (body && body->child) {
        ASTNode *ch = body->child;
        while (ch) {
            if (strcmp(ch->label,"VarDecl")==0) {
                ASTNode *vt = ch->child;
                ASTNode *vn = vt ? vt->next : NULL;
                if (vn && !find_slot(vn->lexeme)) {
                    const char *vjt = ast_type_to_string(vt);
                    int s = next_temp();
                    printf("  %%%d = alloca %s\n", s, llvm_type(vjt));
                    add_slot(vn->lexeme, s, -1);
                    if (strcmp(vjt,"double")==0)
                        printf("  store double 0.0, double* %%%d\n", s);
                    else if (strcmp(vjt,"boolean")==0)
                        printf("  store i1 0, i1* %%%d\n", s);
                    else
                        printf("  store i32 0, i32* %%%d\n", s);
                }
            }
            ch = ch->next;
        }
    }

    /* codegen statements (skip VarDecl) */
    if (body && body->child) {
        ASTNode *ch = body->child;
        while (ch) {
            codegen_stmt(ch);
            ch = ch->next;
        }
    }

    /* default return */
    if (strcmp(ret_jtype,"void")==0)
        printf("  ret void\n");
    else if (strcmp(ret_jtype,"double")==0)
        printf("  ret double 0.0\n");
    else if (strcmp(ret_jtype,"boolean")==0)
        printf("  ret i1 0\n");
    else
        printf("  ret i32 0\n");

    printf("}\n\n");
    cg_method_table = NULL;
    free_var_slots();
    free_ptype_list(ast_pts);
}

/* =========================================================
   Program codegen
   ========================================================= */

static void collect_str_lits_node(ASTNode *node) {
    if (!node) return;
    if (strcmp(node->label,"Print")==0 && node->child &&
        strcmp(node->child->label,"StrLit")==0)
        collect_str_lit(node->child->lexeme);
    collect_str_lits_node(node->child);
    collect_str_lits_node(node->next);
}

static void free_str_lits(void) {
    StrLit *s = str_lits, *n;
    while (s) { n = s->next; free(s->value); free(s); s = n; }
    str_lits = NULL; str_lit_cnt = 0;
}

static void emit_print_double_helper(void) {
    /* Emit @__print_double: Java Double.toString() semantics.
       - |d| in [1e-3, 1e7) or d==0: use %.*f, strip trailing zeros, ensure ".0".
       - |d| extreme (< 1e-3 or >= 1e7, non-zero): use %.*e then convert to Java "E" format.
         Java E format: "1.23E-8" (uppercase E, no + in exponent, no leading zeros). */
    printf("define void @__print_double(double %%d) {\n");
    printf("  %%buf      = alloca [64 x i8]\n");
    printf("  %%bp       = getelementptr [64 x i8], [64 x i8]* %%buf, i32 0, i32 0\n");
    printf("  %%prec_sl  = alloca i32\n");
    printf("  %%dot_sl   = alloca i32\n");
    printf("  %%end_sl   = alloca i32\n");
    printf("  %%src_sl   = alloca i32\n");
    printf("  %%dst_sl   = alloca i32\n");
    printf("  %%ep_sl    = alloca i32\n");
    /* Range check: if |d| < 1e-3 and d != 0.0, or |d| >= 1e7 → scientific */
    printf("  %%ad        = call double @fabs(double %%d)\n");
    printf("  %%is_zero   = fcmp oeq double %%d, 0.0\n");
    printf("  %%is_small  = fcmp olt double %%ad, 1.0e-3\n");
    printf("  %%is_large  = fcmp oge double %%ad, 1.0e7\n");
    printf("  %%is_ext1   = or  i1 %%is_small, %%is_large\n");
    printf("  %%not_zero  = xor i1 %%is_zero,  1\n");
    printf("  %%is_ext    = and i1 %%is_ext1,  %%not_zero\n");
    printf("  br i1 %%is_ext, label %%pde_init, label %%pdf_init\n");

    /* ---- Decimal range: %.*f loop (prec 0..20) ---- */
    printf("pdf_init:\n");
    printf("  store i32 0, i32* %%prec_sl\n");
    printf("  br label %%pdf_cond\n");
    printf("pdf_cond:\n");
    printf("  %%pf0 = load i32, i32* %%prec_sl\n");
    printf("  %%pf_ov = icmp sgt i32 %%pf0, 20\n");
    printf("  br i1 %%pf_ov, label %%pdf_print, label %%pdf_try\n");
    printf("pdf_try:\n");
    printf("  %%pf1 = load i32, i32* %%prec_sl\n");
    printf("  %%ffp = getelementptr [5 x i8], [5 x i8]* @.fdfmt, i32 0, i32 0\n");
    printf("  call i32 (i8*, i64, i8*, ...) @snprintf(i8* %%bp, i64 64, i8* %%ffp, i32 %%pf1, double %%d)\n");
    printf("  %%sv1 = call double @strtod(i8* %%bp, i8** null)\n");
    printf("  %%eq1 = fcmp oeq double %%sv1, %%d\n");
    printf("  br i1 %%eq1, label %%pdf_found, label %%pdf_inc\n");
    printf("pdf_inc:\n");
    printf("  %%pf2 = load i32, i32* %%prec_sl\n");
    printf("  %%pf3 = add i32 %%pf2, 1\n");
    printf("  store i32 %%pf3, i32* %%prec_sl\n");
    printf("  br label %%pdf_cond\n");
    /* Found: scan for dot, strip trailing zeros, ensure ".0" */
    printf("pdf_found:\n");
    printf("  store i32 -1, i32* %%dot_sl\n");
    printf("  store i32  0, i32* %%end_sl\n");
    printf("  br label %%psc_cond\n");
    printf("psc_cond:\n");
    printf("  %%si0 = load i32, i32* %%end_sl\n");
    printf("  %%scp = getelementptr i8, i8* %%bp, i32 %%si0\n");
    printf("  %%sch = load i8, i8* %%scp\n");
    printf("  %%sn  = icmp eq i8 %%sch, 0\n");
    printf("  br i1 %%sn, label %%pst, label %%psb\n");
    printf("psb:\n");
    printf("  %%sd  = icmp eq i8 %%sch, 46\n");
    printf("  %%si1 = load i32, i32* %%end_sl\n");
    printf("  br i1 %%sd, label %%pmd, label %%psn\n");
    printf("pmd:\n");
    printf("  store i32 %%si1, i32* %%dot_sl\n");
    printf("  br label %%psn\n");
    printf("psn:\n");
    printf("  %%si2 = load i32, i32* %%end_sl\n");
    printf("  %%si3 = add i32 %%si2, 1\n");
    printf("  store i32 %%si3, i32* %%end_sl\n");
    printf("  br label %%psc_cond\n");
    printf("pst:\n");
    printf("  %%didx = load i32, i32* %%dot_sl\n");
    printf("  %%hd   = icmp sge i32 %%didx, 0\n");
    printf("  br i1 %%hd, label %%pszl, label %%pad\n");
    /* No dot: append ".0" */
    printf("pad:\n");
    printf("  %%adl = load i32, i32* %%end_sl\n");
    printf("  %%adp0 = getelementptr i8, i8* %%bp, i32 %%adl\n");
    printf("  store i8 46, i8* %%adp0\n");
    printf("  %%adl1 = add i32 %%adl, 1\n");
    printf("  %%adp1 = getelementptr i8, i8* %%bp, i32 %%adl1\n");
    printf("  store i8 48, i8* %%adp1\n");
    printf("  %%adl2 = add i32 %%adl, 2\n");
    printf("  %%adp2 = getelementptr i8, i8* %%bp, i32 %%adl2\n");
    printf("  store i8 0,  i8* %%adp2\n");
    printf("  br label %%pdf_print\n");
    /* Has dot: find length then strip trailing zeros keeping dot+1 minimum */
    printf("pszl:\n");
    printf("  store i32 0, i32* %%end_sl\n");
    printf("  br label %%pll_cond\n");
    printf("pll_cond:\n");
    printf("  %%ll0 = load i32, i32* %%end_sl\n");
    printf("  %%llp = getelementptr i8, i8* %%bp, i32 %%ll0\n");
    printf("  %%llc = load i8, i8* %%llp\n");
    printf("  %%lln = icmp eq i8 %%llc, 0\n");
    printf("  br i1 %%lln, label %%pds, label %%pli\n");
    printf("pli:\n");
    printf("  %%ll1 = load i32, i32* %%end_sl\n");
    printf("  %%ll2 = add i32 %%ll1, 1\n");
    printf("  store i32 %%ll2, i32* %%end_sl\n");
    printf("  br label %%pll_cond\n");
    printf("pds:\n");
    printf("  br label %%ptz_cond\n");
    printf("ptz_cond:\n");
    printf("  %%tz0 = load i32, i32* %%end_sl\n");
    printf("  %%dm  = load i32, i32* %%dot_sl\n");
    printf("  %%dm2 = add i32 %%dm, 1\n");
    printf("  %%tgt = icmp sgt i32 %%tz0, %%dm2\n");
    printf("  br i1 %%tgt, label %%ptz_chk, label %%ptz_done\n");
    printf("ptz_chk:\n");
    printf("  %%tz1 = load i32, i32* %%end_sl\n");
    printf("  %%tz2 = sub i32 %%tz1, 1\n");
    printf("  %%tzp = getelementptr i8, i8* %%bp, i32 %%tz2\n");
    printf("  %%tzc = load i8, i8* %%tzp\n");
    printf("  %%tzz = icmp eq i8 %%tzc, 48\n");
    printf("  br i1 %%tzz, label %%ptz_rm, label %%ptz_done\n");
    printf("ptz_rm:\n");
    printf("  %%tz3 = load i32, i32* %%end_sl\n");
    printf("  %%tz4 = sub i32 %%tz3, 1\n");
    printf("  store i32 %%tz4, i32* %%end_sl\n");
    printf("  %%tz5 = getelementptr i8, i8* %%bp, i32 %%tz4\n");
    printf("  store i8 0, i8* %%tz5\n");
    printf("  br label %%ptz_cond\n");
    printf("ptz_done:\n");
    printf("  br label %%pdf_print\n");

    /* ---- Scientific range: %.*e loop (prec 1..17) + Java E-format conversion ---- */
    printf("pde_init:\n");
    printf("  store i32 1, i32* %%prec_sl\n");
    printf("  br label %%pde_cond\n");
    printf("pde_cond:\n");
    printf("  %%pe0 = load i32, i32* %%prec_sl\n");
    printf("  %%pe_ov = icmp sgt i32 %%pe0, 17\n");
    printf("  br i1 %%pe_ov, label %%pdf_print, label %%pde_try\n");
    printf("pde_try:\n");
    printf("  %%pe1 = load i32, i32* %%prec_sl\n");
    printf("  %%efp = getelementptr [5 x i8], [5 x i8]* @.edfmt, i32 0, i32 0\n");
    printf("  call i32 (i8*, i64, i8*, ...) @snprintf(i8* %%bp, i64 64, i8* %%efp, i32 %%pe1, double %%d)\n");
    printf("  %%sv2 = call double @strtod(i8* %%bp, i8** null)\n");
    printf("  %%eq2 = fcmp oeq double %%sv2, %%d\n");
    printf("  br i1 %%eq2, label %%pde_conv, label %%pde_inc\n");
    printf("pde_inc:\n");
    printf("  %%pe2 = load i32, i32* %%prec_sl\n");
    printf("  %%pe3 = add i32 %%pe2, 1\n");
    printf("  store i32 %%pe3, i32* %%prec_sl\n");
    printf("  br label %%pde_cond\n");
    /* Convert C's "1.23e-08" to Java's "1.23E-8" in-place */
    /* Step 1: find 'e' in buf → ep_sl = position */
    printf("pde_conv:\n");
    printf("  store i32 0, i32* %%ep_sl\n");
    printf("  br label %%pef_cond\n");
    printf("pef_cond:\n");
    printf("  %%ei0 = load i32, i32* %%ep_sl\n");
    printf("  %%ecp = getelementptr i8, i8* %%bp, i32 %%ei0\n");
    printf("  %%ech = load i8, i8* %%ecp\n");
    printf("  %%ene = icmp eq i8 %%ech, 101\n");
    printf("  %%enE = icmp eq i8 %%ech, 69\n");
    printf("  %%en0 = icmp eq i8 %%ech, 0\n");
    printf("  %%enf = or i1 %%ene, %%enE\n");
    printf("  %%ens = or i1 %%enf, %%en0\n");
    printf("  br i1 %%ens, label %%pef_done, label %%pef_next\n");
    printf("pef_next:\n");
    printf("  %%ei1 = load i32, i32* %%ep_sl\n");
    printf("  %%ei2 = add i32 %%ei1, 1\n");
    printf("  store i32 %%ei2, i32* %%ep_sl\n");
    printf("  br label %%pef_cond\n");
    printf("pef_done:\n");
    /* Write 'E' at ep_sl */
    printf("  %%ep_i  = load i32, i32* %%ep_sl\n");
    printf("  %%ep_p  = getelementptr i8, i8* %%bp, i32 %%ep_i\n");
    printf("  store i8 69, i8* %%ep_p\n");
    /* src = ep_i+1 (points at sign), dst = ep_i+1 initially */
    printf("  %%src0  = add i32 %%ep_i, 1\n");
    printf("  store i32 %%src0, i32* %%src_sl\n");
    printf("  store i32 %%src0, i32* %%dst_sl\n");
    /* Check sign: if '-' write '-' and advance src; if '+' just advance src */
    printf("  %%sg_p  = getelementptr i8, i8* %%bp, i32 %%src0\n");
    printf("  %%sg_c  = load i8, i8* %%sg_p\n");
    printf("  %%sg_mn = icmp eq i8 %%sg_c, 45\n");
    printf("  %%sg_pl = icmp eq i8 %%sg_c, 43\n");
    printf("  br i1 %%sg_mn, label %%sg_neg, label %%sg_not_neg\n");
    printf("sg_neg:\n");
    printf("  %%sn_d  = load i32, i32* %%dst_sl\n");
    printf("  %%sn_dp = getelementptr i8, i8* %%bp, i32 %%sn_d\n");
    printf("  store i8 45, i8* %%sn_dp\n");
    printf("  %%sn_d2 = add i32 %%sn_d, 1\n");
    printf("  store i32 %%sn_d2, i32* %%dst_sl\n");
    printf("  %%sn_s  = load i32, i32* %%src_sl\n");
    printf("  %%sn_s2 = add i32 %%sn_s, 1\n");
    printf("  store i32 %%sn_s2, i32* %%src_sl\n");
    printf("  br label %%pskip_z\n");
    printf("sg_not_neg:\n");
    /* skip '+' or any other sign char */
    printf("  %%sp_s  = load i32, i32* %%src_sl\n");
    printf("  %%sp_s2 = add i32 %%sp_s, 1\n");
    printf("  store i32 %%sp_s2, i32* %%src_sl\n");
    printf("  br label %%pskip_z\n");
    /* Skip leading zeros in exponent, keeping last digit */
    printf("pskip_z:\n");
    printf("  br label %%psz_cond\n");
    printf("psz_cond:\n");
    printf("  %%sz_s  = load i32, i32* %%src_sl\n");
    printf("  %%sz_p  = getelementptr i8, i8* %%bp, i32 %%sz_s\n");
    printf("  %%sz_c  = load i8, i8* %%sz_p\n");
    printf("  %%sz_z  = icmp eq i8 %%sz_c, 48\n");
    printf("  %%sz_s2 = add i32 %%sz_s, 1\n");
    printf("  %%sz_p2 = getelementptr i8, i8* %%bp, i32 %%sz_s2\n");
    printf("  %%sz_nc = load i8, i8* %%sz_p2\n");
    printf("  %%sz_nn = icmp ne i8 %%sz_nc, 0\n");
    printf("  %%sz_sk = and i1 %%sz_z, %%sz_nn\n");
    printf("  br i1 %%sz_sk, label %%psz_next, label %%pce_cond\n");
    printf("psz_next:\n");
    printf("  %%sz_si = load i32, i32* %%src_sl\n");
    printf("  %%sz_s3 = add i32 %%sz_si, 1\n");
    printf("  store i32 %%sz_s3, i32* %%src_sl\n");
    printf("  br label %%psz_cond\n");
    /* Copy remaining exponent digits (src → dst) */
    printf("pce_cond:\n");
    printf("  %%cx_s = load i32, i32* %%src_sl\n");
    printf("  %%cx_p = getelementptr i8, i8* %%bp, i32 %%cx_s\n");
    printf("  %%cx_c = load i8, i8* %%cx_p\n");
    printf("  %%cx_n = icmp eq i8 %%cx_c, 0\n");
    printf("  br i1 %%cx_n, label %%pce_done, label %%pce_body\n");
    printf("pce_body:\n");
    printf("  %%cxb_d  = load i32, i32* %%dst_sl\n");
    printf("  %%cxb_dp = getelementptr i8, i8* %%bp, i32 %%cxb_d\n");
    printf("  store i8 %%cx_c, i8* %%cxb_dp\n");
    printf("  %%cxb_s2 = load i32, i32* %%src_sl\n");
    printf("  %%cxb_s3 = add i32 %%cxb_s2, 1\n");
    printf("  store i32 %%cxb_s3, i32* %%src_sl\n");
    printf("  %%cxb_d2 = add i32 %%cxb_d, 1\n");
    printf("  store i32 %%cxb_d2, i32* %%dst_sl\n");
    printf("  br label %%pce_cond\n");
    printf("pce_done:\n");
    printf("  %%cd_d  = load i32, i32* %%dst_sl\n");
    printf("  %%cd_dp = getelementptr i8, i8* %%bp, i32 %%cd_d\n");
    printf("  store i8 0, i8* %%cd_dp\n");
    printf("  br label %%pdf_print\n");

    /* ---- Print ---- */
    printf("pdf_print:\n");
    printf("  %%sfp = getelementptr [3 x i8], [3 x i8]* @.fmt.str, i32 0, i32 0\n");
    printf("  call i32 (i8*, ...) @printf(i8* %%sfp, i8* %%bp)\n");
    printf("  ret void\n");
    printf("}\n\n");
}

void codegen_program(ASTNode *root) {
    if (!root) return;
    ASTNode *class_id = root->child;
    if (!class_id) return;

    /* pre-collect string literals */
    collect_str_lits_node(root);

    /* declarations */
    printf("declare i32 @printf(i8*, ...)\n");
    printf("declare i32 @atoi(i8*)\n\n");

    /* format constants */
    printf("@.fmt.int    = private unnamed_addr constant [3 x i8] c\"%%d\\00\"\n");
    printf("@.fmt.str    = private unnamed_addr constant [3 x i8] c\"%%s\\00\"\n");
    printf("@.fmt.double = private unnamed_addr constant [6 x i8] c\"%%.16e\\00\"\n");
    printf("@.bool.true  = private unnamed_addr constant [5 x i8] c\"true\\00\"\n");
    printf("@.bool.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n");

    /* static field globals */
    {
        Symbol *field = class_table ? class_table->symbols : NULL;
        while (field) {
            if (!field->is_method) {
                if (strcmp(field->type, "double") == 0)
                    printf("@%s = global double 0.0\n", field->name);
                else if (strcmp(field->type, "boolean") == 0)
                    printf("@%s = global i1 0\n", field->name);
                else
                    printf("@%s = global i32 0\n", field->name);
            }
            field = field->next;
        }
    }

    /* string literal constants */
    StrLit *sl = str_lits;
    while (sl) {
        emit_str_constant(sl->value, sl->id, str_byte_len(sl->value));
        sl = sl->next;
    }
    printf("\n");

    /* (no print_double helper needed: use printf("%.16e", d) directly) */

    /* codegen each method */
    ASTNode *member = class_id->next;
    while (member) {
        if (strcmp(member->label,"MethodDecl")==0)
            codegen_method(member);
        member = member->next;
    }

    /* LLVM entry point */
    Symbol *main_sym = NULL;
    /* Prefer main(String[] args); fall back to any main method */
    Symbol *sym = class_table ? class_table->symbols : NULL;
    while (sym) {
        if (sym->is_method && strcmp(sym->name,"main")==0) {
            /* Prefer the one with String[] parameter */
            int has_str = 0;
            for (ParamType *pt = sym->params; pt; pt = pt->next)
                if (strcmp(pt->type,"String[]")==0) { has_str = 1; break; }
            if (has_str || !main_sym) main_sym = sym;
            if (has_str) break;
        }
        sym = sym->next;
    }

    if (main_sym) {
        int has_str_arr = 0;
        for (ParamType *pt = main_sym->params; pt; pt = pt->next)
            if (strcmp(pt->type,"String[]")==0) { has_str_arr = 1; break; }

        char main_mangled[256];
        mangle_name(main_mangled, sizeof(main_mangled), "main", main_sym->params);

        printf("define i32 @main(i32 %%argc, i8** %%argv) {\n");
        if (has_str_arr) {
            printf("  %%1 = sub i32 %%argc, 1\n");
            printf("  %%2 = getelementptr i8*, i8** %%argv, i32 1\n");
            printf("  call void @%s(i32 %%1, i8** %%2)\n", main_mangled);
        } else {
            printf("  call void @%s()\n", main_mangled);
        }
        printf("  ret i32 0\n");
        printf("}\n");
    } else {
        /* No main method: emit a dummy entry point so lli doesn't fail */
        printf("define i32 @main(i32 %%argc, i8** %%argv) {\n");
        printf("  ret i32 0\n");
        printf("}\n");
    }

    free_str_lits();
}
