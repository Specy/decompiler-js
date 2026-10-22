# Changelog

## 1.0.0

The first release. Ghidra's C++ decompiler, from Ghidra 12.1.4
(`Ghidra_12.1.4_build`, commit 8b6bbb857acc), compiled to WebAssembly with
emsdk 6.0.9: the caller describes a program in memory and gets a function's
C as Ghidra prints it, as lines of tokens linked to the instructions they come
from and to the functions and data they name, in a browser or in Node,
without Java, a server or files. The wasm is 2.1 MB (719 KB gzipped) and the
x86-64 processor description 530 KB.

- `loadDecompiler({ architecture, wasmUrl?, specsUrl? })` loads the module
  and the architecture's processor description, by default from beside the
  package's module; `decompiler.version` names the Ghidra release and commit.
- One architecture, `'x86-64'`: Ghidra's `x86:LE:64:default` language with
  its `gcc` compiler specification, the System V calling convention.
- `openProgram(image, { namespaces? })` takes a `ProgramImage`: `regions`
  (bytes at link-time addresses, read-only and executable flags; read-only
  loads fold to their values), `functions` and `data` (names, first one kept
  at an address, each with the namespaces it is in and the symbol it binds
  by), `strings`, `pointers` (slots such as GOT entries that hold a
  function's address, so a call through one is a call of that function with
  its prototype) and `prototypes` (C declarations, bound by symbol,
  `_Noreturn` and its spellings honoured). It throws on a malformed image;
  declarations it could not read, and Ghidra's messages, are in
  `program.warnings`. The package reads no executable format and invents no
  names.
- C++ names: a function, data or pointer gives its `name` as printed, never
  split at a `::`, the `namespace` it is in as a path
  (`['std', 'vector<std::string>']`), each part one namespace whatever it
  holds, and the `symbol` declarations bind by and pointers name it by, its
  mangled name, so `glibc.h`'s `_ZSt20__throw_length_errorPKc` still makes
  `std::__throw_length_error` noreturn. Each namespace owns the addresses of
  its functions and data, so they are found by address like global ones: a
  call prints `std::vector<std::string>::push_back(…)` and the function
  itself decompiles. `namespaces`, Ghidra's namespace strategy, qualifies a
  name as far as tells it apart where it is used (`'minimal'`, the default),
  in full (`'all'`) or never (`'none'`).
- `program.decompile(address, { maxInstructions?, timeLimitMs? })` returns
  `{ address, name, text, lines }` or `{ address, error }`. The guards
  default to Ghidra's 100,000 instructions and 30 seconds; the time is
  checked inside the module, so a call returns shortly after its limit. It
  throws only on a disposed program, an argument it does not take, or a
  `WebAssembly.RuntimeError` when Ghidra's code traps the module.
- Each line is `{ text, tokens, addresses }`; a token is
  `{ start, end, kind, address?, target? }`, with its span in UTF-16 code
  units, its kind from Ghidra's syntax highlighting, the address of the
  instruction it came from, and the entry, global, string or label a name
  refers to; a qualified name (`std::string::operator+=`) is one token. A
  line's `addresses` are its tokens', sorted, each once.
- Several programs can be open at once. `program.dispose()` frees one,
  `decompiler.dispose()` all of them; a decompilation keeps nothing once it
  returns. `decompiler.memory()` reports the wasm memory's size and the part
  the decompiler holds.
- `@specy/decompiler/prototypes/glibc.h`, and its text as the default export
  of `@specy/decompiler/prototypes/glibc`: 537 functions (50 of them
  `_Noreturn`) and 8 globals of glibc 2.43, libm, the C++ runtime and
  libgcc_s, written within the limits of Ghidra's C parser.
- The package carries Ghidra's `LICENSE` and `NOTICE` and the zlib License
  of the zlib sources in the wasm, in `dist/ghidra/`, beside its own
  Apache-2.0 `LICENSE`.

The vendored Ghidra sources are unchanged; four patches are applied to a copy
at build time:

- `0001-action-checkpoint`: a hook before each pass of the analysis, so the
  host can stop a decompilation past its time limit from inside the one
  thread a wasm module runs on, where Ghidra's Java side kills the process.
- `0002-sleigh-resume`: points the SLEIGH translator, which Ghidra shares
  between the programs of a language, back at an earlier program, so two
  open programs each decode their own bytes.
- `0003-split-incomplete-structure`: stops splitting a store by the layout of
  an incomplete structure of size 0 (glibc's `DIR`), which divided by zero
  and trapped the module on static glibc's `opendir`.
- `0004-flow-unmapped-bytes`: stops a function's flow at the first unmapped
  byte with `halt_baddata()` instead of failing the whole function, as
  happens after a Linux exit system call at the end of a segment.
