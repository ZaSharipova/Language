#!/usr/bin/env bash
# Usage:
#   ./run.sh front <src.txt>               — only frontend, ast goes to build/tmp/ast.txt
#   ./run.sh middle <src.txt>              — frontend + middle (optimizer)
#   ./run.sh elf    <src.txt> [-o out]     — full pipeline -> ELF via TreeToBin
#   ./run.sh asm    <src.txt> [-o out]     — full pipeline -> NASM .asm -> nasm -> gcc
#   ./run.sh back    <src.txt>             — full pipeline -> my asm
#   ./run.sh reverse                       — run reverse tool (uses last ast)
#   ./run.sh trick                         - run trick (changes the code with strange spaces + new lines + change of names)
#   ./run.sh clean                         — wipe build dir
#
# Flags:
#   -o NAME      output binary name (default: program)
#   -m           run middle-end (optimizer) before backend
#   -k           keep intermediates (don't clean tmp)
#   -v           verbose (echo every command)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TMP="$ROOT/build/tmp"
BIN="$ROOT/build/bin"

OUTPUT="program"
USE_MIDDLE=0
KEEP=0
VERBOSE=0

cmd() {
    [[ $VERBOSE -eq 1 ]] && echo "+ $*" >&2
    "$@"
}

ensure_dir() { mkdir -p "$TMP"; }

build_target() {
    local target="$1"
    cmd make -C "$ROOT" "$target" >/dev/null
}

run_front() {
    local src="$1"
    local ast="$TMP/ast.txt"

    [[ -f "$src" ]] || { echo "source not found: $src" >&2; exit 1; }

    build_target front
    ensure_dir
    cmd "$BIN/front" "$src" "$ast"
    echo "$ast"
}

run_middle() {
    local ast_in="$1"
    local ast_out="$TMP/ast_opt.txt"

    build_target middle
    cmd "$BIN/middle" "$ast_in" "$ast_out"
    echo "$ast_out"
}

make_ast() {
    local src="$1"
    local ast
    ast="$(run_front "$src")"
    if [[ $USE_MIDDLE -eq 1 ]]; then
        ast="$(run_middle "$ast")"
    fi
    echo "$ast"
}

cmd_front() {
    local src="${1:-}"
    [[ -n "$src" ]] || { echo "usage: $0 front <src.txt>" >&2; exit 1; }
    local ast
    ast="$(run_front "$src")"
    echo "ast: $ast"
}

cmd_middle() {
    local src="${1:-}"
    [[ -n "$src" ]] || { echo "usage: $0 middle <src.txt>" >&2; exit 1; }
    local ast
    ast="$(run_front "$src")"
    ast="$(run_middle "$ast")"
    echo "ast: $ast"
}


cmd_back() {
    local src="${1:-}"
    [[ -n "$src" ]] || { echo "usage: $0 back <src.txt>" >&2; exit 1; }

    local ast
    ast="$(make_ast "$src")"

    build_target back
    cmd "$BIN/back" "$ast" "$OUTPUT.asm"
    echo "output: $OUTPUT.asm"
}

cmd_elf() {
    local src="${1:-}"
    [[ -n "$src" ]] || { echo "usage: $0 elf <src.txt>" >&2; exit 1; }

    local ast
    ast="$(make_ast "$src")"

    build_target back_x

    local out="$ROOT/$OUTPUT"
    cmd "$BIN/back_x" --tree_elf "$ast" "$out"
    chmod +x "$out"
    echo "output: $out"
    echo "run:    ./$OUTPUT"
}

cmd_asm() {
    local src="${1:-}"
    [[ -n "$src" ]] || { echo "usage: $0 asm <src.txt>" >&2; exit 1; }

    local ast
    ast="$(make_ast "$src")"

    build_target back_x

    local asm="$TMP/$OUTPUT.asm"
    local obj="$TMP/$OUTPUT.o"
    local out="$ROOT/$OUTPUT"

    cmd "$BIN/back_x" --tree_asm "$ast" "$asm"
    cmd nasm -f elf64 "$asm" -o "$obj"
    cmd gcc -no-pie "$obj" -o "$out"
    echo "asm:    $asm"
    echo "output: $out"
    echo "run:    ./$OUTPUT"
}

cmd_reverse() {
    build_target reverse
    cmd "$BIN/reverse" "$@"
}

cmd_trick() {
    build_target trick
    cmd "$BIN/trick" "$@"
}

cmd_clean() {
    cmd rm -rf "$ROOT/build"
}

[[ $# -ge 1 ]] || {
    cat <<EOF
usage: $0 <command> [args] [flags]

commands:
  front   <src>    run frontend only
  middle  <src>    frontend + middle-end
  back    <src>    full pipeline -> my asm
  elf     <src>    full pipeline -> x86-64 ELF (direct)
  asm     <src>    full pipeline -> NASM .asm -> linked binary
  reverse          run reverse tool
  clean            wipe build/

flags:
  -o NAME          output name (default: program)
  -m               include middle-end (optimizer) in pipeline
  -k               keep intermediates
  -v               verbose
EOF
    exit 1
}

CMD="$1"; shift

POS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        -m) USE_MIDDLE=1; shift ;;
        -k) KEEP=1; shift ;;
        -v) VERBOSE=1; shift ;;
        --) shift; POS+=("$@"); break ;;
        -*) echo "unknown flag: $1" >&2; exit 1 ;;
        *)  POS+=("$1"); shift ;;
    esac
done

case "$CMD" in
    front)   cmd_front   "${POS[@]:-}" ;;
    middle)  cmd_middle  "${POS[@]:-}" ;;
    back)    cmd_back    "${POS[@]:-}" ;;
    elf)     cmd_elf     "${POS[@]:-}" ;;
    asm)     cmd_asm     "${POS[@]:-}" ;;
    reverse) cmd_reverse "${POS[@]:-}" ;;
    trick)   cmd_trick   "${POS[@]:-}" ;;
    clean)   cmd_clean ;;
    *)       echo "unknown command: $CMD" >&2; exit 1 ;;
esac