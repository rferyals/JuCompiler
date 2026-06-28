%{
/*
    Autores: Diogo Alves 2020214197
        
*/
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "symbol_table.h"

extern int yylex(void);
extern char *yytext;

int yacc_error = 0;
int line_yacc  = 1, col_yacc = 1;
int flag_yacc  = 0;

char current_type[AST_NAME_SIZE];

void yyerror(char *s) {
    yacc_error = 1;
    fprintf(stderr, "Line %d, col %d: %s: %s\n", line_yacc, col_yacc, s, yytext);
}

typedef struct {
    char *text;
    int line;
    int col;
} TokenInfo;

%}

%union {
    char *str;
    struct {
        char *text;
        int line;
        int col;
    } token;
    ASTNode *ASTNode;
}

%token BOOL INT DOUBLE VOID STRING CLASS PUBLIC STATIC
%token <token> RETURN
%token IF ELSE WHILE PRINT DOTLENGTH
%token <token> PARSEINT
%token <token> AND ASSIGN STAR DIV EQ GE GT LE LT MINUS MOD NE NOT OR PLUS LSHIFT RSHIFT XOR
%token COMMA LBRACE LPAR LSQ RBRACE RPAR RSQ SEMICOLON
%token ARROW
%token <token> IDENTIFIER NATURAL DECIMAL BOOLLIT STRLIT RESERVED

%right ASSIGN
%left  OR
%left  XOR
%left  AND
%left  EQ NE
%left  LT GT LE GE
%left  LSHIFT RSHIFT
%left  PLUS MINUS
%left  STAR DIV MOD
%right NOT UNARY
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%type <ASTNode> Program MethodFieldDecl MethodDecl FieldDecl FieldCommaId
%type <ASTNode> Type MethodHeader MethodBody FormalParams CommaTypeIds
%type <ASTNode> StatementVarDecl VarDecl VarCommaId
%type <ASTNode> Statement MultipleStatements
%type <ASTNode> MethodInvocation CommaExpr Assignment ParseArgs
%type <ASTNode> Expr ExprNoAssign

%start Program
%%

Program: CLASS IDENTIFIER LBRACE RBRACE{ ast_root = ast_new("Program", NULL); ast_attach_child(ast_root, ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col)); }
    | CLASS IDENTIFIER LBRACE MethodFieldDecl RBRACE { ast_root = ast_new("Program", NULL); ast_attach_child(ast_root, ast_attach_sibling(ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col), $4)); }
    ;

MethodFieldDecl
    : MethodDecl                        { $$ = $1; }
    | FieldDecl                         { $$ = $1; }
    | SEMICOLON                         { $$ = NULL; }
    | MethodDecl MethodFieldDecl        { $$ = ast_attach_sibling($1, $2); }
    | FieldDecl  MethodFieldDecl        { $$ = ast_attach_sibling($1, $2); }
    | SEMICOLON  MethodFieldDecl        { $$ = $2; }
    ;

MethodDecl
    : PUBLIC STATIC MethodHeader MethodBody{ $$ = ast_attach_child(ast_new("MethodDecl", NULL), ast_attach_sibling($3, $4)); }
    ;

FieldDecl
    : PUBLIC STATIC Type IDENTIFIER SEMICOLON
        { $$ = ast_attach_child(ast_new("FieldDecl", NULL),
                       ast_attach_sibling($3, ast_set_pos(ast_new("Identifier", $4.text), $4.line, $4.col))); }
    | PUBLIC STATIC Type IDENTIFIER FieldCommaId SEMICOLON
        { $$ = ast_attach_sibling(ast_attach_child(ast_new("FieldDecl", NULL),
                               ast_attach_sibling($3, ast_set_pos(ast_new("Identifier", $4.text), $4.line, $4.col))), $5); }
    | error SEMICOLON
        { $$ = NULL; }
    ;

FieldCommaId: COMMA IDENTIFIER { $$ = ast_attach_child(ast_new("FieldDecl", NULL),ast_attach_sibling(ast_new(current_type, NULL),ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))); }
    | COMMA IDENTIFIER FieldCommaId{ $$ = ast_attach_sibling(ast_attach_child(ast_new("FieldDecl", NULL),ast_attach_sibling(ast_new(current_type, NULL),ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))), $3); }
    ;

Type: BOOL   { $$ = ast_new("Bool",   NULL); strcpy(current_type, "Bool"); }
    | INT    { $$ = ast_new("Int",    NULL); strcpy(current_type, "Int"); }
    | DOUBLE { $$ = ast_new("Double", NULL); strcpy(current_type, "Double"); }
    ;

MethodHeader: Type IDENTIFIER LPAR RPAR{ $$ = ast_attach_child(ast_new("MethodHeader", NULL),ast_attach_sibling($1, ast_attach_sibling(ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col),ast_new("MethodParams", NULL)))); }
    | Type IDENTIFIER LPAR FormalParams RPAR{ $$ = ast_attach_child(ast_new("MethodHeader", NULL),ast_attach_sibling($1, ast_attach_sibling(ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col),ast_attach_child(ast_new("MethodParams", NULL), $4)))); }
    | VOID IDENTIFIER LPAR RPAR{ $$ = ast_attach_child(ast_new("MethodHeader", NULL),ast_attach_sibling(ast_new("Void", NULL),ast_attach_sibling(ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col),ast_new("MethodParams", NULL)))); }
    | VOID IDENTIFIER LPAR FormalParams RPAR{ $$ = ast_attach_child(ast_new("MethodHeader", NULL),ast_attach_sibling(ast_new("Void", NULL),ast_attach_sibling(ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col),ast_attach_child(ast_new("MethodParams", NULL), $4)))); }
    ;

FormalParams: Type IDENTIFIER{ $$ = ast_attach_child(ast_new("ParamDecl", NULL),ast_attach_sibling($1, ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))); }
    | Type IDENTIFIER CommaTypeIds{ $$ = ast_attach_sibling(ast_attach_child(ast_new("ParamDecl", NULL),ast_attach_sibling($1, ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))), $3); }
    | STRING LSQ RSQ IDENTIFIER{ $$ = ast_attach_child(ast_new("ParamDecl", NULL),ast_attach_sibling(ast_new("StringArray", NULL),ast_set_pos(ast_new("Identifier", $4.text), $4.line, $4.col))); }
    ;

CommaTypeIds: COMMA Type IDENTIFIER{ $$ = ast_attach_child(ast_new("ParamDecl", NULL),ast_attach_sibling($2, ast_set_pos(ast_new("Identifier", $3.text), $3.line, $3.col))); }
    | COMMA Type IDENTIFIER CommaTypeIds{ $$ = ast_attach_sibling(ast_attach_child(ast_new("ParamDecl", NULL),ast_attach_sibling($2, ast_set_pos(ast_new("Identifier", $3.text), $3.line, $3.col))), $4); }
    ;

MethodBody: LBRACE RBRACE{ $$ = ast_new("MethodBody", NULL); }
    | LBRACE StatementVarDecl RBRACE{ $$ = ast_attach_child(ast_new("MethodBody", NULL), $2); }
    ;

StatementVarDecl: Statement             { $$ = $1; }
    | VarDecl                           { $$ = $1; }
    | Statement StatementVarDecl        { $$ = ast_attach_sibling($1, $2); }
    | VarDecl   StatementVarDecl        { $$ = ast_attach_sibling($1, $2); }
    ;

VarDecl: Type IDENTIFIER SEMICOLON{ $$ = ast_attach_child(ast_new("VarDecl", NULL),ast_attach_sibling($1, ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))); }
    | Type IDENTIFIER VarCommaId SEMICOLON{ $$ = ast_attach_sibling(ast_attach_child(ast_new("VarDecl", NULL),ast_attach_sibling($1, ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))), $3); }
    ;

VarCommaId: COMMA IDENTIFIER{ $$ = ast_attach_child(ast_new("VarDecl", NULL),ast_attach_sibling(ast_new(current_type, NULL),ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))); }
    | COMMA IDENTIFIER VarCommaId{ $$ = ast_attach_sibling(ast_attach_child(ast_new("VarDecl", NULL),ast_attach_sibling(ast_new(current_type, NULL),ast_set_pos(ast_new("Identifier", $2.text), $2.line, $2.col))), $3); }
    ;

Statement: LBRACE RBRACE{ $$ = NULL; }
    | LBRACE Statement RBRACE    { $$ = $2; }
    | LBRACE Statement MultipleStatements RBRACE  { $$ = ast_make_block($2, $3); }
    | IF LPAR Expr RPAR Statement %prec LOWER_THAN_ELSE  { $$ = ast_make_if($3, $5, NULL); }
    | IF LPAR Expr RPAR Statement ELSE Statement  { $$ = ast_make_if($3, $5, $7); }
    | WHILE LPAR Expr RPAR Statement  { $$ = ast_make_while($3, $5); }
    | RETURN SEMICOLON {$$ = ast_new("Return", NULL);ast_set_pos($$, $1.line, $1.col);}
    | RETURN Expr SEMICOLON {$$ = ast_attach_child(ast_new("Return", NULL), $2); ast_set_pos($$, $1.line, $1.col);}
    | SEMICOLON { $$ = NULL; }
    | MethodInvocation SEMICOLON {
    $$ = ast_attach_child(ast_new("Call", NULL), $1);
    if ($1 != NULL)
        ast_set_pos($$, $1->line, $1->col);
    }
    | Assignment SEMICOLON
    {
        $$ = ast_attach_child(ast_new("Assign", NULL), $1);
        if ($1 != NULL)
            ast_set_pos($$, $1->line, $1->col);
    }
    | ParseArgs SEMICOLON {
    $$ = $1;
    }
    | PRINT LPAR STRLIT RPAR SEMICOLON  { $$ = ast_attach_child(ast_new("Print", NULL), ast_set_pos(ast_new("StrLit", $3.text), $3.line, $3.col)); }
    | PRINT LPAR Expr RPAR SEMICOLON  { $$ = ast_attach_child(ast_new("Print", NULL), $3); }
    | error SEMICOLON  { $$ = NULL; }
    ;

MultipleStatements: Statement MultipleStatements      { $$ = ast_attach_sibling($1, $2); }
    | Statement   { $$ = $1; }
    ;

MethodInvocation: IDENTIFIER LPAR RPAR{ $$ = ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col); }
    | IDENTIFIER LPAR Expr RPAR  { $$ = ast_attach_sibling(ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col), $3); }
    | IDENTIFIER LPAR Expr CommaExpr RPAR  { $$ = ast_attach_sibling(ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col), ast_attach_sibling($3, $4)); }
    | IDENTIFIER LPAR error RPAR  { $$ = NULL; }
    ;

CommaExpr: COMMA Expr                        { $$ = $2; }
    | COMMA Expr CommaExpr              { $$ = ast_attach_sibling($2, $3); }
    ;

Assignment: IDENTIFIER ASSIGN Expr {
    $$ = ast_attach_sibling(
        ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col),
        $3
    );
    if ($$ != NULL)
        ast_set_pos($$, $2.line, $2.col);
}
    ;

ParseArgs
    : PARSEINT LPAR IDENTIFIER LSQ Expr RSQ RPAR
      {
          ASTNode *id;

          id = ast_set_pos(ast_new("Identifier", $3.text), $3.line, $3.col);

          $$ = ast_attach_child(
              ast_new("ParseArgs", NULL),
              ast_attach_sibling(id, $5)
          );

          ast_set_pos($$, $1.line, $1.col);
      }
    | PARSEINT LPAR error RPAR
      { $$ = NULL; }
    ;

Expr
    : ExprNoAssign
        { $$ = $1; }
    | Assignment
        {
            $$ = ast_attach_child(ast_new("Assign", NULL), $1);
            if ($1 != NULL)
                ast_set_pos($$, $1->line, $1->col);
        }
    ;

ExprNoAssign: ExprNoAssign PLUS ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Add", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign MINUS ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Sub", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign STAR ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Mul", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign DIV ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Div", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign MOD ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Mod", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign AND ExprNoAssign
{
    $$ = ast_attach_child(ast_new("And", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign OR ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Or", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign XOR ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Xor", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign LSHIFT ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Lshift", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign RSHIFT ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Rshift", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign EQ ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Eq", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign GE ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Ge", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign GT ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Gt", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign LE ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Le", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign LT ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Lt", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| ExprNoAssign NE ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Ne", NULL), ast_attach_sibling($1,$3));
    ast_set_pos($$, $2.line, $2.col);
}
| MINUS ExprNoAssign %prec UNARY
{
    $$ = ast_attach_child(ast_new("Minus", NULL), $2);
    ast_set_pos($$, $1.line, $1.col);
}
| PLUS ExprNoAssign %prec UNARY
{
    $$ = ast_attach_child(ast_new("Plus", NULL), $2);
    ast_set_pos($$, $1.line, $1.col);
}
| NOT ExprNoAssign
{
    $$ = ast_attach_child(ast_new("Not", NULL), $2);
    ast_set_pos($$, $1.line, $1.col);
}
| LPAR Expr RPAR                    { $$ = $2; }
| LPAR error RPAR                   { $$ = NULL; }
| MethodInvocation {
    $$ = ast_attach_child(ast_new("Call", NULL), $1);
    if ($1 != NULL)
        ast_set_pos($$, $1->line, $1->col);
}
| ParseArgs {
    $$ = $1;
}
| IDENTIFIER DOTLENGTH {
    $$ = ast_attach_child(
        ast_new("Length", NULL),
        ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col)
    );
    ast_set_pos($$, $1.line, $1.col + strlen($1.text));
}
| IDENTIFIER                        { $$ = ast_set_pos(ast_new("Identifier", $1.text), $1.line, $1.col); }
| NATURAL                           { $$ = ast_set_pos(ast_new("Natural", $1.text), $1.line, $1.col); }
| DECIMAL                           { $$ = ast_set_pos(ast_new("Decimal", $1.text), $1.line, $1.col); }
| BOOLLIT                           { $$ = ast_set_pos(ast_new("BoolLit", $1.text), $1.line, $1.col); }
;

%%
