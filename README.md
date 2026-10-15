# decompiler-js

Ghidra's C++ decompiler compiled to WebAssembly, for decompiling machine code in a browser or in Node without Java or a server.

Work in progress: the build exists, the JavaScript API does not yet.

## Layout

| Path | What it holds |
| --- | --- |
| `ghidra/` | The subset of upstream Ghidra that is built, vendored unchanged from a release tag. See [ghidra/README.md](ghidra/README.md). |
| `patches/` | Changes to the vendored sources, applied by the build script. |
| `host/` | The host layer: the C++ that feeds the decompiler a program image and returns its output. |
| `specs/<architecture>/` | Compiled processor descriptions (`.sla`) and the specification files they need, committed. |
| `js/` | The npm package `@specy/decompiler`: the TypeScript wrapper, the wasm and the tests. |
| `scripts/` | Vendoring, the SLEIGH compiler, the native reference build and the Emscripten build. |

## Licence

Apache-2.0 (see [LICENSE](LICENSE)). The vendored Ghidra sources are Apache-2.0 with Ghidra's `NOTICE`; its bundled zlib is under the zlib License. Both are kept beside the sources in `ghidra/`.
