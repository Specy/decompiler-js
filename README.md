# decompiler-js

Ghidra's C++ decompiler compiled to WebAssembly, for decompiling machine code in a browser or in Node without Java or a server.

Work in progress. The caller describes a program (its memory, the names of its functions and data, where its strings are, C declarations of the library functions it calls) and gets a function's C as Ghidra's GUI prints it (Ghidra's type names, string literals, prototypes bound by name), as lines of tokens, each linked to the instruction it came from and to the function or data it names. Still to come: the glibc prototypes the package ships, the guards on time, and measurements.

## Layout

| Path | What it holds |
| --- | --- |
| `ghidra/` | The subset of upstream Ghidra that is built, vendored unchanged from a release tag. See [ghidra/README.md](ghidra/README.md). |
| `patches/` | Changes to the vendored sources, applied by every build of the decompiler. See [patches/README.md](patches/README.md). |
| `host/` | The host layer: the C++ that feeds the decompiler a program image held in memory and returns its output (`src/`), its wasm exports (`wasm/`) and a native driver (`native/`). |
| `specs/<architecture>/` | Compiled processor descriptions (`.sla`) and the specification files read beside them, committed. |
| `js/` | The npm package `@specy/decompiler`: the TypeScript wrapper, the wasm and the tests. |
| `scripts/` | Vendoring, the SLEIGH compiler, the native reference build and the Emscripten build. |

## Building

The wasm and the compiled processor descriptions are committed, so the package builds and tests with Node alone (`cd js && npm ci && npm test && npm run build && npm run test:dist`). Rebuilding them needs a C++ compiler, `make` and, for the wasm, emsdk 6.0.9:

| Script | Does |
| --- | --- |
| `scripts/vendor-ghidra.sh <tag>` | Replaces `ghidra/` with that release's sources. |
| `scripts/compile_specs.sh` | Builds the SLEIGH compiler natively and writes `specs/`. |
| `scripts/compile_native.sh` | Builds the native driver and Ghidra's own console into `build/native/`. |
| `scripts/native_reference.sh` | Writes the native reference decompilations in `js/tests/fixtures/reference/`, checked against Ghidra's console. |
| `scripts/compile_decompiler.sh` | Applies `patches/` and builds `js/src/wasm/decompiler.{mjs,wasm}` (`source ~/emsdk/emsdk_env.sh` first). |

Each build script prints sha256 lines for its outputs; record them in the commit that changes them.

## Licence

Apache-2.0 (see [LICENSE](LICENSE)). The vendored Ghidra sources are Apache-2.0 with Ghidra's `NOTICE`; its bundled zlib is under the zlib License. Both are kept beside the sources in `ghidra/` and published with the package.
