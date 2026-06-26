CC = gcc
CFLAGS = -Wall -Wextra -std=c2x -O2 -g
LDFLAGS = 
SRC = src/main.c src/utils.c src/platform.c src/lexer.c src/preprocessor.c \
      src/ast.c src/symbol.c src/typecheck.c src/parser.c src/codegen.c \
      src/emit_x86.c src/emit_elf.c src/emit_pe.c
OBJ = $(SRC:.c=.o)
TARGET = hugo

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
