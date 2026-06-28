# JuCompiler

A full compiler for the **JU language** (a Java-like language) built from scratch in C, as part of a Compilers course project.

**Authors:** Diogo Alves, Rabia Feryal Saygin

---

## Overview

JuCompiler takes JU source code through all classic compiler stages — lexical analysis, parsing, AST construction, semantic analysis, and LLVM IR code generation.

## Features

- **Lexer** (`jucompiler.l`) — tokenizes JU source, handles multi-line comments, string literals with escape validation, precise line/column tracking for error reporting
- **Parser** (`jucompiler.y`) — LALR(1) grammar via Bison/YACC; disambiguates operator precedence (OR → XOR → AND → relational → shift → additive → multiplicative → unary) and resolves dangling-else via dummy tokens
- **AST** (`ast.c/h`) — left-child/right-sibling encoding for variable-arity nodes; annotated with types during semantic analysis
- **Symbol Table** (`symbol_table.c/h`) — class-level table for static fields and method signatures; per-method tables with two-pass construction; method overload resolution with `int`→`double` widening
- **Semantic Analysis** (`semantics.c/h`) — type checking, scope validation, overload resolution with ambiguity detection
- **Code Generation** (`codegen.c/h`) — emits LLVM IR; SSA via `alloca`/`load`/`store`; short-circuit evaluation for `&&`/`||`; name mangling for overloaded methods; synthetic `main` entry point; `System.out.print` dispatched by type

## Language Features Supported

- Classes with static fields and methods
- Types: `int`, `double`, `boolean`, `String`, `String[]`
- Arithmetic, bitwise, logical, relational, and assignment operators
- `if`/`else`, `while`, variable declarations
- Method overloading with widening coercion
- `System.out.print`, `Integer.parseInt`

## Build & Run

Requires **flex**, **bison**, and **gcc**.

```bash
make
```

This produces the `jucompiler` binary. To compile a JU source file:

```bash
./jucompiler < program.ju > program.ll
llc program.ll -o program.s
gcc program.s -o program
./program
```

## Project Structure

```
jucompiler.l       # Lexer (flex)
jucompiler.y       # Parser (bison)
ast.c / ast.h      # AST node definition and tree construction
symbol_table.c/h   # Symbol table and scope management
semantics.c/h      # Semantic analysis and type checking
codegen.c/h        # LLVM IR code generation
Makefile
report.txt         # Technical design report (in Portuguese)
```
