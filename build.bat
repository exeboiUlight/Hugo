@echo off
REM Build script for Windows
set CC=gcc
set CFLAGS=-Wall -Wextra -std=c2x -O2 -g
set SRC=src/main.c src/utils.c src/platform.c src/lexer.c src/preprocessor.c src/ast.c src/symbol.c src/typecheck.c src/parser.c src/codegen.c src/emit_x86.c src/emit_elf.c src/emit_pe.c
set OBJDIR=temp\win

if not exist %OBJDIR% mkdir %OBJDIR%

echo Building Hugo C23 compiler...
for %%f in (%SRC%) do (
    echo  Compiling %%f
    %CC% %CFLAGS% -c -o %OBJDIR%\%%~nf.o %%f
    if errorlevel 1 exit /b 1
)

echo Linking...
%CC% %CFLAGS% -o hugo.exe %OBJDIR%\main.o %OBJDIR%\utils.o %OBJDIR%\platform.o %OBJDIR%\lexer.o %OBJDIR%\preprocessor.o %OBJDIR%\ast.o %OBJDIR%\symbol.o %OBJDIR%\typecheck.o %OBJDIR%\parser.o %OBJDIR%\codegen.o %OBJDIR%\emit_x86.o %OBJDIR%\emit_elf.o %OBJDIR%\emit_pe.o
if errorlevel 1 exit /b 1
echo Done.
