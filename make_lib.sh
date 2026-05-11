#!/bin/bash
set -e

ASMFILE=${1:-mylib.asm}

nasm -f elf64 "$ASMFILE" -o /tmp/my_lib.o
x86_64-linux-gnu-ld --emit-relocs /tmp/my_lib.o -o mylib.elf

echo "Written: mylib.elf ($(wc -c < mylib.elf) bytes)"