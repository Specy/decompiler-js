#!/usr/bin/env bash
# Builds Ghidra's SLEIGH compiler natively from ghidra/ and compiles each
# architecture's processor description into specs/<architecture>/, beside a
# copy of the specification files a SleighArchitecture reads with it.
#
#   scripts/compile_specs.sh
#
# The outputs are committed; record the sha256 lines this prints in the commit
# message. The SLEIGH compiler builds into build/native (gitignored).
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"
build=build/native
jobs="$(nproc 2>/dev/null || echo 4)"

# Per architecture: the specs/ directory, the vendored language directory, the
# .slaspec and the files copied beside the compiled .sla.
architectures=(
    'x86-64 ghidra/processors/x86/languages x86-64.slaspec x86-64.pspec x86-64-gcc.cspec x86.ldefs'
)

make -s -f scripts/ghidra.mk -j"$jobs" BUILD="$build" SRC=ghidra/decompile CXX="${CXX:-g++}" CC="${CC:-gcc}" sleigh

for entry in "${architectures[@]}"; do
    read -r name languages slaspec spec_files <<<"$entry"
    out="specs/$name"
    mkdir -p "$out"
    rm -f "$out"/*.sla "$out"/*.pspec "$out"/*.cspec "$out"/*.ldefs
    "$build/sleigh" "$languages/$slaspec" "$out/${slaspec%.slaspec}.sla"
    for file in $spec_files; do
        cp "$languages/$file" "$out/$file"
    done
done

echo "SLEIGH compiler: ghidra/ at $(sed -n 's/^- Commit: `\(.*\)`$/\1/p' ghidra/README.md), $("${CXX:-g++}" --version | head -n1)"
for entry in "${architectures[@]}"; do
    read -r name _ <<<"$entry"
    for file in specs/"$name"/*; do
        echo "$(sha256sum "$file" | cut -d' ' -f1)  $(wc -c <"$file") bytes  $file"
    done
done
