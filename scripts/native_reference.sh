#!/usr/bin/env bash
# Writes the native reference decompilations the wasm build is tested
# against, js/tests/fixtures/reference/<fixture>-<function>.c, and checks each
# against Ghidra's own console given the same program image. Beside each, the
# driver's JSON result (<fixture>-<function>.json: the lines' tokens with
# their kinds, addresses and targets), which the console cannot give.
#
#   scripts/native_reference.sh
#
# Each function is decompiled twice, natively and from the vendored sources
# with patches/ applied, as the wasm is built: by the host layer's driver (build/native/decompile, which reads the
# ELF into a program image held in memory) and by Ghidra's console
# (build/native/decomp_console) restoring the save file the driver writes of
# that program: its bytes, core types, data-types and symbols, prototypes
# (from $fixtures/prototypes.h) included. The console
# then gets the one option the host layer sets (readonly on). The two must
# print the same C.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"
scripts/compile_native.sh >/dev/null

fixtures=js/tests/fixtures
references=(
    'crackme main'
    'crackme _start'
)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for entry in "${references[@]}"; do
    read -r fixture function <<<"$entry"
    out="$fixtures/reference/$fixture-$function.c"
    build/native/decompile --prototypes "$fixtures/prototypes.h" --save "$work/save.xml" specs/x86-64 "$fixtures/elf/$fixture" "$function" \
        >"$work/driver.c"
    printf 'restore %s\noption readonly on\nload function %s\ndecompile\nprint C\nquit\n' "$work/save.xml" "$function" |
        SLEIGHHOME="$repo_dir/specs/x86-64" build/native/decomp_console >"$work/console.txt"
    # The C is what the console prints between the prompts of `print C` and
    # `quit`, less the empty line the printer starts a function with, which
    # the host layer drops.
    sed -n '/^\[decomp\]> print C$/,/^\[decomp\]> quit$/p' "$work/console.txt" | sed '1d;$d' |
        sed '1{/^$/d}' >"$work/console.c"
    if ! cmp -s "$work/driver.c" "$work/console.c"; then
        echo "The driver and Ghidra's console disagree on $fixture $function:" >&2
        diff "$work/console.c" "$work/driver.c" >&2 || true
        exit 1
    fi
    mkdir -p "$(dirname "$out")"
    cp "$work/driver.c" "$out"
    build/native/decompile --json --prototypes "$fixtures/prototypes.h" specs/x86-64 "$fixtures/elf/$fixture" \
        "$function" 2>/dev/null >"${out%.c}.json"
    echo "$(sha256sum "$out" | cut -d' ' -f1)  $out (driver and console agree)"
    echo "$(sha256sum "${out%.c}.json" | cut -d' ' -f1)  ${out%.c}.json"
done
