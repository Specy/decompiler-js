# decompiler-js

Ghidra's C++ decompiler, from Ghidra 12.1.4, compiled to WebAssembly, for decompiling machine code in a browser or in Node without Java or a server.

The caller describes a program (its memory, the names of its functions and data and the namespaces they are in, where its strings are, which slots hold the addresses of which functions, C declarations of the library functions it calls) and gets a function's C as Ghidra prints it, as lines of tokens, each linked to the instruction it came from and to the function or data it names. One architecture so far: x86-64, with the System V calling convention. The npm package, `@specy/decompiler`, is in `js/`; its [README](js/README.md) documents the API, the glibc prototypes it ships, the limits on a decompilation's instructions and time, and measurements.

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

Both builds compile the copy of the sources `scripts/patched_sources.sh` writes. Each build script prints sha256 lines for its outputs; record them in the commit that changes them. The same sources and emsdk build the same wasm, byte for byte.

## Licence

Apache-2.0 (see [LICENSE](LICENSE)). The vendored Ghidra sources are Apache-2.0 with Ghidra's `NOTICE`; its bundled zlib is under the zlib License. Both are kept beside the sources in `ghidra/` and published with the package.
