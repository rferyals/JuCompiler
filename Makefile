CC      = gcc
CFLAGS  =

SRCS    = y.tab.c lex.yy.c ast.c symbol_table.c semantics.c codegen.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: jucompiler

jucompiler: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

y.tab.c y.tab.h: jucompiler.y
	bison -y -d jucompiler.y

lex.yy.c: jucompiler.l y.tab.h
	flex jucompiler.l

%.o: %.c y.tab.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f jucompiler $(OBJS) lex.yy.c y.tab.c y.tab.h
