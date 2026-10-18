# Patches

Changes to the vendored Ghidra sources (`../ghidra/decompile/`), applied by
`scripts/patched_sources.sh` to a copy in the build directory, in name order, with `patch -p1`
from the `decompile/` directory (paths like `cpp/action.cc`). `ghidra/` itself stays exactly
upstream's.

Name each patch `NNNN-short-description.patch` and start it with a header saying why it is
needed, before the first `---` line (`patch` ignores the header):

```
Why: <what fails without it, and why it cannot be done in host/ instead>
Upstream: <whether it was reported or could be, or why not>

--- a/cpp/file.cc
+++ b/cpp/file.cc
@@ ...
```

Every build of the decompiler applies them: the wasm (`scripts/compile_decompiler.sh`) and the
native driver and Ghidra's console (`scripts/compile_native.sh`). The SLEIGH compiler
(`scripts/compile_specs.sh`) is built without them, so the processor descriptions stay
upstream's. A patch must not change what the decompiler prints: the wasm is tested against
the references in `js/tests/fixtures/reference/`, which were written before any patch, so a
patch that changes the output fails those tests rather than passing unnoticed.

| Patch | What it does |
| --- | --- |
| `0001-action-checkpoint.patch` | Adds `Action::checkpoint`, a hook called before each attempt to apply an action, null by default, which the host layer sets to enforce a decompilation's time limit (`host/src/program.cc`). |
| `0002-sleigh-resume.patch` | Adds `Sleigh::resume`, which points the translator every program of a language shares back at a program opened earlier, so that several programs can be open at once (`HostArchitecture::useTranslator`). |
| `0003-split-incomplete-structure.patch` | Stops `SplitDatatype` backing a pointer up to an incomplete structure (size 0, such as glibc's `DIR`), whose layout it would divide by: static glibc's `opendir` trapped the wasm with "remainder by zero". |

Ghidra 12.1.4 otherwise builds with Emscripten unchanged: the specification files are read
through `ifstream` and the `.ldefs` directory is scanned with `opendir`, both of which
Emscripten's in-memory filesystem serves.
