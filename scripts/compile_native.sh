#!/usr/bin/env bash
# Builds the native reference tools into build/native (gitignored), from the
# vendored sources with patches/ applied, as the wasm is built, and the host
# layer:
#
#   build/native/decompile        the host layer's driver (host/native/main.cc)
#   build/native/decomp_console   Ghidra's own console, which loads XML images
#
#   scripts/compile_native.sh [--clean]
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"
jobs="$(nproc 2>/dev/null || echo 4)"

scripts/patched_sources.sh build/native "${CXX:-g++}" "${1:-}"
make -s -f scripts/ghidra.mk -j"$jobs" BUILD=build/native SRC=build/native/src \
    CXX="${CXX:-g++}" CC="${CC:-gcc}" driver console
echo "Built build/native/decompile and build/native/decomp_console with $("${CXX:-g++}" --version | head -n1)"
