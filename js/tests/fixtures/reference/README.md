# Native reference decompilations

Known-good output the wasm build is compared against. Written by
`scripts/native_reference.sh`; regenerate them only with it.

Each `<fixture>-<function>.c` is the C printed for that function of `../elf/<fixture>` by the
host layer's native driver (`build/native/decompile`, built from the unpatched vendored sources
by `scripts/compile_native.sh`). The script checks it byte for byte against Ghidra's own console
(`decomp_console`, upstream's `consolemain.cc` built without its libbfd loader) given the same
program image as an XML load image: `load file`, `load function <name>`, `decompile`, `print C`.

The program image is what the driver reads from the ELF: each `PT_LOAD` segment at its link-time
address (read-only unless writable), and as function names every defined `STT_FUNC` symbol
plus the PLT stubs named after their relocations' symbols. No prototypes, data names, strings or
Ghidra's display names for its core types are given yet, so types print as Ghidra's raw core
types (`xunknown8`, `int4`) and string arguments as addresses (`puts(0x201f)`).

| File | Produced with |
| --- | --- |
| `crackme-main.c` | Ghidra 12.1.4 (8b6bbb857acc), g++ 15.2.0 `-O2`, 2026-10-15 |
