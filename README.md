# decompiler-js

Ghidra's C++ decompiler from Ghidra 12.1.4, compiled to WebAssembly. It decompiles machine code in a browser or in Node without Java or a server.

## Usage

The caller describes the program's memory, function and data names and their namespaces, strings, and slots that hold function addresses. It can also supply C declarations for library functions and specify whether `syscall` instructions make Linux system calls.

The decompiler returns a function's C as Ghidra prints it, in lines of tokens. Tokens link to the instructions they came from and to the functions, data or local variables they name. The caller can name locals using the ids the decompiler reports and add comments, which Ghidra places above the statements they belong to.

The supported architecture is x86-64 with the System V calling convention. The npm package, `@specy/decompiler`, is in `js/`. Its [README](js/README.md) documents the API, naming variables and adding comments, the glibc prototypes it ships, the instruction and time limits on a decompilation, and measurements.

## Layout

| Path | What it holds |
| --- | --- |
| `ghidra/` | The subset of upstream Ghidra that is built, vendored unchanged from a release tag. See [ghidra/README.md](ghidra/README.md). |
| `patches/` | Changes to the vendored sources, applied by every build of the decompiler. See [patches/README.md](patches/README.md). |
| `host/` | The host layer: the C++ that feeds the decompiler a program image held in memory and returns its output (`src/`), its wasm exports (`wasm/`) and a native driver (`native/`). |
| `specs/<architecture>/` | Compiled processor descriptions (`.sla`) and the specification files read beside them, committed. |
| `js/` | The npm package `@specy/decompiler`: the TypeScript wrapper, the wasm, the prototypes, the tests and the measurements. |
| `scripts/` | Vendoring, the SLEIGH compiler, the native reference build and the Emscripten build. |

## Building

The wasm and the compiled processor descriptions are committed, so the package builds and tests with Node alone (`cd js && npm ci && npm test && npm run build && npm run test:dist`). Rebuilding them needs a C++ compiler, `make` and, for the wasm, emsdk 6.0.9:

| Script | Does |
| --- | --- |
| `scripts/vendor-ghidra.sh <tag>` | Replaces `ghidra/` with that release's sources. |
| `scripts/compile_specs.sh` | Builds the SLEIGH compiler natively, from the sources without `patches/`, and writes `specs/`. |
| `scripts/compile_native.sh` | Builds the native driver and Ghidra's own console into `build/native/`, with `patches/` applied. |
| `scripts/native_reference.sh` | Writes the native reference decompilations in `js/tests/fixtures/reference/`, checked against Ghidra's console. |
| `scripts/compile_decompiler.sh` | Builds `js/src/wasm/decompiler.{mjs,wasm}`, with `patches/` applied (`source ~/emsdk/emsdk_env.sh` first). |

The native and wasm builds compile the copy of the sources that `scripts/patched_sources.sh` writes. The scripts for the processor descriptions, native references and wasm print sha256 lines for their outputs; record them in the commit that changes them. The same sources and emsdk build the same wasm, byte for byte.

## Licence

Apache-2.0 (see [LICENSE](LICENSE)). The vendored Ghidra sources are Apache-2.0 with Ghidra's `NOTICE`; its bundled zlib is under the zlib License. Both are kept beside the sources in `ghidra/` and published with the package.
