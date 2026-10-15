#!/usr/bin/env bash
# Builds the decompiler and the host layer to WebAssembly with Emscripten:
# js/src/wasm/decompiler.mjs (an ES module factory) and decompiler.wasm.
#
#   source ~/emsdk/emsdk_env.sh && scripts/compile_decompiler.sh [--clean]
#
# The vendored sources are copied to build/wasm/src and patches/*.patch are
# applied there in name order, so ghidra/ stays upstream's. Both outputs are
# committed; record the sha256 lines this prints in the commit message.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"
command -v emcc >/dev/null || { echo 'Source emsdk_env.sh first' >&2; exit 1; }
build=build/wasm
out=js/src/wasm
jobs="$(nproc 2>/dev/null || echo 4)"

# A change to the patches or to the compiler rebuilds everything: make cannot
# tell that an object was compiled from a source a patch no longer touches.
stamp="$(cat patches/*.patch 2>/dev/null | sha256sum | cut -d' ' -f1) $(emcc --version | head -n1)"
if [ "${1:-}" = --clean ] || [ "$(cat "$build/stamp" 2>/dev/null)" != "$stamp" ]; then
    rm -rf "$build"
fi
mkdir -p "$build"
echo "$stamp" >"$build/stamp"

# Unchanged files keep their times (cp -p), so make rebuilds only what a patch touched.
rm -rf "$build/src"
cp -Rp ghidra/decompile "$build/src"
for patch in patches/*.patch; do
    [ -e "$patch" ] || continue
    patch --quiet --forward -p1 -d "$build/src" <"$patch"
done

# -fwasm-exceptions: C++ exceptions as WebAssembly exception handling, in
# Emscripten's default encoding (WASM_LEGACY_EXCEPTIONS), which every current
# browser and Node run. -Wno-mismatched-tags: clang notices Ghidra declaring
# some structs as classes; harmless, and 100 lines of noise.
compile_flags=(-fwasm-exceptions -Wno-mismatched-tags)
link_flags=(
    -fwasm-exceptions
    --no-entry
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createDecompilerModule
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    # Native threads get 8 MB; the decompiler recurses over a function's blocks
    # and expressions, and Emscripten's default stack is 64 KB.
    -sSTACK_SIZE=4MB
    -sDYNAMIC_EXECUTION=0
    # The specification files are written into the in-memory filesystem from
    # bytes the caller fetched, before the first program is opened.
    -sFORCE_FILESYSTEM=1
    -sEXPORTED_FUNCTIONS=_malloc,_free,_decompiler_version,_decompiler_error,_decompiler_add_specs,_program_open,_program_decompile,_program_close
    -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,UTF8ToString,stringToNewUTF8
)

make -s -f scripts/ghidra.mk -j"$jobs" BUILD="$build" SRC="$build/src" CXX=em++ CC=emcc OPT=-O2 \
    EXTRA_CXXFLAGS="${compile_flags[*]}" EXTRA_LDFLAGS="${link_flags[*]}" wasm

mkdir -p "$out"
cp "$build/decompiler.mjs" "$build/decompiler.wasm" "$out/"

echo "emsdk: $(emcc --version | head -n1)"
echo "ghidra/ at $(sed -n 's/^- Commit: `\(.*\)`$/\1/p' ghidra/README.md), patches: $(ls patches/*.patch 2>/dev/null | wc -l)"
for file in "$out/decompiler.wasm" "$out/decompiler.mjs"; do
    echo "$(sha256sum "$file" | cut -d' ' -f1)  $(wc -c <"$file") bytes ($(gzip -9 -c "$file" | wc -c) gzipped)  $file"
done
