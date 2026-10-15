#!/usr/bin/env bash
# Writes the decompiler's sources as every build of them compiles them: a
# copy of ghidra/decompile with patches/*.patch applied in name order, so
# ghidra/ itself stays exactly upstream's.
#
#   scripts/patched_sources.sh <build directory> <compiler> [--clean]
#
# The copy goes to <build directory>/src. When the patches or the compiler
# (the first line of `<compiler> --version`) differ from the last build's, or
# with --clean, the build directory is emptied first: make cannot tell that
# an object was compiled from a source a patch no longer touches. Otherwise a
# file whose content is the same as in the last copy keeps that copy's time,
# so make rebuilds only what changed. Called by scripts/compile_native.sh and
# scripts/compile_decompiler.sh.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build="$1"
compiler="$2"

stamp="$(cat "$repo_dir"/patches/*.patch 2>/dev/null | sha256sum | cut -d' ' -f1) $("$compiler" --version | head -n1)"
if [ "${3:-}" = --clean ] || [ "$(cat "$build/stamp" 2>/dev/null)" != "$stamp" ]; then
    rm -rf "$build"
fi
mkdir -p "$build"
echo "$stamp" >"$build/stamp"

staging="$build/src.new"
rm -rf "$staging"
cp -Rp "$repo_dir/ghidra/decompile" "$staging"
for patch in "$repo_dir"/patches/*.patch; do
    [ -e "$patch" ] || continue
    patch --quiet --forward -p1 -d "$staging" <"$patch"
done
if [ -d "$build/src" ]; then
    (cd "$staging" && find . -type f) | while IFS= read -r file; do
        if cmp -s "$staging/$file" "$build/src/$file"; then touch -r "$build/src/$file" "$staging/$file"; fi
    done
fi
rm -rf "$build/src"
mv "$staging" "$build/src"
