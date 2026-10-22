# Native reference decompilations

Known-good output the wasm build is compared against. Written by
`scripts/native_reference.sh`; regenerate them only with it.

Beside each `.c`, a `.json` holds the driver's whole result (`build/native/decompile --json`):
every line's tokens with their kinds, instruction addresses and targets, which the wasm build
must give alike. Ghidra's console prints plain text only, so the tokens are checked against the
native driver, not against the console.

Each `<fixture>-<function>.c` is the C printed for that function of `../elf/<fixture>` by the
host layer's native driver (`build/native/decompile`, built from the vendored sources by
`scripts/compile_native.sh`, with `patches/` applied as in the wasm; the references were first
written before any patch existed, and none has changed them since). The script checks it byte
for byte against Ghidra's own console
(`decomp_console`, upstream's `consolemain.cc` built without its libbfd loader) given the same
program: the driver writes it as a save file of Ghidra's `XmlArchitecture` (the bytes, the core
types, the data-types and every symbol), and the console runs `restore`, `option readonly on`
(the one option the host layer sets that is not the console's default), `load function <name>`,
`decompile`, `print C`. A program with names in namespaces is saved with its namespaces and the
addresses each owns, which the console restores.

The program image is what the driver reads from the ELF (`host/native/elf_image.cc`, mirrored by
`../../helpers/elf-image.ts`): each `PT_LOAD` segment at its link-time address (read-only unless
writable); as function names every defined `STT_FUNC` symbol plus the PLT stubs named after
their relocations' symbols; as data every defined `STT_OBJECT` symbol, without its version, the GOT slots of functions
named `name@got`, and the strings of `.rodata` named `s_` and their first words, as the editor's
Listing names them; as pointers those GOT slots, each naming its function; and as prototypes `../prototypes.h`, a few of glibc's declarations,
or the file a reference names. A fixture with a `../elf/<fixture>.names.tsv` has its names put in
their namespaces by that table, each keeping its mangled name as its symbol (the driver's `--names`). Types print with Ghidra's
names (`undefined8`, `long`, `size_t`).

| File | Produced with |
| --- | --- |
| `crackme-main.c` | Ghidra 12.1.4 (8b6bbb857acc), g++ 15.2.0 `-O2`, 2026-10-15 |
| `crackme-_start.c` | Ghidra 12.1.4 (8b6bbb857acc), g++ 15.2.0 `-O2`, 2026-10-18: the call of `__libc_start_main` through its GOT slot |
| `factcheck-main.c` | Ghidra 12.1.4 (8b6bbb857acc), g++ 15.2.0 `-O2`, 2026-10-22: C++ names in their namespaces (`factcheck.names.tsv`), with `../../../prototypes/glibc.h` |
