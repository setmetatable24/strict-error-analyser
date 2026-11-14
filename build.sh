#!/usr/bin/env bash
set -e

mkdir -p bin

CC_BIN="${CC:-cc}"
if command -v clang >/dev/null 2>&1; then
    CC_BIN="clang"
elif command -v gcc >/dev/null 2>&1; then
    CC_BIN="gcc"
fi

echo "[build] Compiling with $CC_BIN (C17)..."
$CC_BIN -std=c17 -O2 -Wall -Wextra -Iinclude \
    src/common.c \
    src/platform.c \
    src/json.c \
    src/workspace.c \
    src/analyzer.c \
    src/main.c \
    -o bin/strict-error-analyser

echo "[build] Build succeeded: bin/strict-error-analyser"
