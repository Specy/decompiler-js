# Native reference decompilations

Known-good output the wasm build is compared against. Written by
`scripts/native_reference.sh`; regenerate them only with it.

Each `<fixture>-<function>.c` is the C printed for that function of `../elf/<fixture>` by the
host layer's native driver (`build/native/decompile`, built from the unpatched vendored sources
by `scripts/compile_native.sh`). The script checks it byte for byte against Ghidra's own console
(`decomp_console`, upstream's `consolemain.cc` built without its libbfd loader) given the same
program: the driver writes it as a save file of Ghidra's `XmlArchitecture` (the bytes, the core
types, the data-types and every symbol), and the console runs `restore`, `option readonly on`
(the one option the host layer sets), `load function <name>`, `decompile`, `print C`.

The program image is what the driver reads from the ELF (`host/native/elf_image.cc`, mirrored by
`../../helpers/elf-image.ts`): each `PT_LOAD` segment at its link-time address (read-only unless
writable); as function names every defined `STT_FUNC` symbol plus the PLT stubs named after
their relocations' symbols; as data every defined `STT_OBJECT` symbol, without its version, and
the strings of `.rodata` named `s_` and their first words, as the editor's Listing names them;
and as prototypes `../prototypes.h`, a few of glibc's declarations. Types print with Ghidra's
names (`undefined8`, `long`, `size_t`).

| File | Produced with |
| --- | --- |
| `crackme-main.c` | Ghidra 12.1.4 (8b6bbb857acc), g++ 15.2.0 `-O2`, 2026-10-15 |
