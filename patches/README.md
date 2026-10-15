# Patches

Changes to the vendored Ghidra sources (`../ghidra/decompile/`), applied by
`scripts/compile_decompiler.sh` to a copy in `build/wasm/src`, in name order, with
`patch -p1` from the `decompile/` directory (paths like `cpp/sleigh_arch.cc`). `ghidra/` itself
stays exactly upstream's.

Name each patch `NNNN-short-description.patch` and start it with a header saying why it is
needed, before the first `---` line (`patch` ignores the header):

```
Why: <what fails without it, and why it cannot be done in host/ instead>
Upstream: <whether it was reported or could be, or why not>

--- a/cpp/file.cc
+++ b/cpp/file.cc
@@ ...
```

The native reference build (`scripts/compile_native.sh`) does not apply them, so a patch that
changes decompiler output shows up as a difference from `js/tests/fixtures/reference/`.

There are none for Ghidra 12.1.4: its sources build with Emscripten unchanged. The
specification files are read through `ifstream` and the `.ldefs` directory is scanned with
`opendir`, both of which Emscripten's in-memory filesystem serves.
