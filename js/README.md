# @specy/decompiler

Ghidra's C++ decompiler, from Ghidra 12.1.4, compiled to WebAssembly: C for a function of a program described in memory, as lines of tokens linked to the instructions they come from and to the functions and data they name. It runs in a browser or in Node, without Java, a server or files.

## Usage

```sh
npm install @specy/decompiler
```

```ts
import { loadDecompiler } from '@specy/decompiler'
import glibc from '@specy/decompiler/prototypes/glibc'

const decompiler = await loadDecompiler({ architecture: 'x86-64' })

// A small PIE's loaded segments and names, as an ELF reader or a disassembler gives them.
const program = decompiler.openProgram({
  regions: [
    { address: 0x1000, bytes: text, readOnly: true, executable: true },
    { address: 0x2000, bytes: rodata, readOnly: true, executable: false },
    { address: 0x3d90, bytes: data, readOnly: false, executable: false },
  ],
  functions: [
    { address: 0x11e9, name: 'main' },
    { address: 0x10f0, name: 'strcmp' }, // the PLT stub, named after its symbol
    { address: 0x10a0, name: 'puts' },
    // ...
  ],
  data: [
    { address: 0x4010, name: 'stdin', size: 8 },
    { address: 0x3fd8, name: '__libc_start_main@got', size: 8 },
  ],
  strings: [{ address: 0x2017, length: 8 }], // "hunter2" and its terminator
  pointers: [{ address: 0x3fd8, name: '__libc_start_main' }], // a GOT slot
  prototypes: glibc,
})

const result = program.decompile(0x11e9)
if ('error' in result) console.error(result.error)
else console.log(result.text)

program.dispose()
decompiler.dispose()
```

```c
undefined8 main(void)

{
  ...
    iVar1 = strcmp(acStack_98,"hunter2");
    if (iVar1 == 0) {
      puts("Correct!");
    }
  ...
}
```

`decompile` runs on the calling thread until it returns, from a few milliseconds to many seconds for a large function (see [Performance](#performance)), so a page calls it from a worker.

### Loading

`loadDecompiler({ architecture, wasmUrl?, specsUrl? })` fetches `decompiler.wasm` (2.2 MB, 726 KB gzipped) and the architecture's processor description (`specs/x86-64/`: `x86-64.sla`, 501 KB, and three specification files, 29 KB), by default from beside the package's module through `new URL(…, import.meta.url)`, which bundlers follow and copy. `wasmUrl` and `specsUrl` (a directory) serve them from elsewhere. In Node, `file:` URLs are read from disk.

The only architecture is `'x86-64'`: Ghidra's `x86:LE:64:default` language with its `gcc` compiler specification, the System V calling convention of Linux and the BSDs.

`decompiler.version` names the Ghidra release and commit the module was built from (`Ghidra_12.1.4_build 8b6bbb857acc`).

The module uses WebAssembly exception handling, which Chrome 95, Firefox 100 and Safari 15.2 and later support. Emscripten builds it for Node 18.3 and later; the tests run on Node 24.

### Describing a program

`openProgram(image)` takes a `ProgramImage`, what a loader or a disassembler already knows about the program; the package reads no executable format.

| Field | What it is |
| --- | --- |
| `regions` | The program's memory at its link-time addresses: `{ address, bytes, readOnly, executable }`, not overlapping. Loads from a `readOnly` region are folded to their values, as Ghidra does with read-only memory. A read starting outside every region fails the decompilation; one running past a region's end reads zeros. |
| `functions` | `{ address, name, namespace?, symbol? }`: the entry points the decompiler may start from, and the names it prints for them. A `name` is printed as given, never split at a `::`; `namespace` lists the namespaces and classes the function is in, outermost first (see [C++ names](#c-names)); `symbol` is the name prototypes bind by and pointers name it by, by default `name`. At an address named twice, the first name wins. A PLT stub is named after its symbol (`strcmp`, not `strcmp@plt`), which its prototype binds by. |
| `data` | `{ address, name, size?, namespace?, symbol? }`: global variables, tables and strings' labels, in their namespaces as functions are; a declaration of a global's type binds by its `symbol`, by default `name`. A function's name wins over a data name at its address. |
| `strings` | `{ address, length }`: string literals, terminator included. A string with a `data` name is typed as a `char` array; any string in read-only memory prints as a literal where a `char *` is expected. |
| `pointers` | Optional. `{ address, name, namespace?, symbol? }`: a pointer-sized slot that holds the address of a function, though the file may not hold it there, such as a GOT entry the dynamic linker fills. A call through the slot is a call of that function, with its name and prototype: `_start`'s `call [rip + __libc_start_main@got]` prints as `__libc_start_main(main,…)`, not `(*pcRam0000000000003fd8)(main,…)`. The function is the image's function whose `symbol` (or name, for one without) is the pointer's `symbol`, when the pointer has one, else the image's function of the pointer's `name` and `namespace`; or else one known only by the pointer's name, namespace and symbol, which has no code to decompile. A `data` name at the slot's address names the slot where its value is read (`if (__gmon_start__@got != (code *)0x0)`); without one it prints as the decompiler names a global `code *`. At an address given twice the first is kept, and a function's name at the slot's address wins. |
| `prototypes` | C declarations of the library functions the program calls and the types they use: see [Prototypes](#prototypes). |

Addresses are numbers: every x86-64 user-space address is below 2^53. `openProgram` throws when the image is malformed (overlapping regions, an address that is not a non-negative safe integer, an empty namespace) or its options are not ones it takes. What it could not use without failing is in `program.warnings`: each declaration that could not be read, with its line (``line 3: Syntax error after `size_tt` (in `size_tt strlen(const char *s);`)``), and any message Ghidra printed.

`openProgram(image, { namespaces })` sets how far names are qualified with their namespaces, Ghidra's namespace strategy: `'minimal'` (the default), `'all'` or `'none'`, as below.

### C++ names

The package reads no mangled name. A caller with a demangler gives a C++ function its name in its namespaces, split at the `::`s outside template arguments and parameter lists, and its mangled name as the `symbol` it binds by:

```ts
functions: [
  { address: 0x5a78, name: 'push_back', namespace: ['std', 'vector<std::string>'],
    symbol: '_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE9push_backERKS5_' },
  { address: 0x32d0, name: '__throw_length_error', namespace: ['std'],
    symbol: '_ZSt20__throw_length_errorPKc' }, // a PLT stub
  // ...
],
data: [{ address: 0xec60, name: 'typeinfo', namespace: ['std', 'runtime_error'], symbol: '_ZTISt13runtime_error', size: 24 }],
pointers: [{ address: 0xef10, name: '__throw_length_error', namespace: ['std'], symbol: '_ZSt20__throw_length_errorPKc' }],
```

The decompiler makes each namespace once, from its path, and finds a function in one by its address, so its calls and its own decompilation work as a global function's. Each part of the path is one namespace whatever it holds: `vector<std::string>` is a class of `std`, not `vector<std` and `string>`, and `greeting()` in `['greeting()', 'text']` or `'lambda'(int)` in `['main', "'lambda'(int)"]` are namespaces too. The declarations of `prototypes/glibc.h` bind by `symbol`, so `std::__throw_length_error` keeps `_ZSt20__throw_length_errorPKc`'s prototype and does not return.

A name prints qualified as far as the strategy says. With `'minimal'`, as far as tells it apart where it is used: in `main`, a function of the global scope,

```c
  std::string::operator+=(auStack_128,auStack_c8);
  std::vector<std::string>::push_back(auStack_198,auStack_128);
```

and inside another member of `std::vector<std::string>`, `uStack_30 = size(uStack_40);` and `__throw_length_error(param_3);`; a function's own declaration reads `ulong std::vector<std::string>::_M_check_len(undefined8 param_1,ulong param_2,char *param_3)`. With `'all'`, every name in full unless it is in the scope where it is used (`std::vector<std::string>::size(uStack_40)` there); with `'none'`, never qualified, its declaration included (`ulong _M_check_len(…)`).

The package invents no names. A function or a global the image does not name prints as the decompiler names it (`func_0x00401130()`, `cRam0000000000004018`), and locals and parameters keep the decompiler's names (`iVar1`, `acStack_98`, `param_1`) unless the caller names them ([Naming variables and adding comments](#naming-variables-and-adding-comments)). The Ghidra application's `local_98` comes from a stack analysis on its Java side, which is not part of the decompiler. Types print with Ghidra's names (`undefined8`, `uint`, `ulong`).

### Decompiling

`program.decompile(address, options?)` returns the C of the function starting at `address`, one of the image's functions, as `{ address, name, text, lines, variables }` (`name` as the image gives it, without its namespaces; `variables` [below](#naming-variables-and-adding-comments)), or `{ address, error }` when there is none: no function starts there, it is past a guard below, or the decompiler failed. It throws only when the program was disposed or an argument is not one it takes. A bug in Ghidra's code that traps the WebAssembly module (a division by zero) throws too, a `WebAssembly.RuntimeError`; nothing in the module can be trusted after one, so dispose the decompiler and load a new one.

Each line is `{ text, tokens, addresses }`:

- `tokens`, in order, are `{ start, end, kind, address?, target?, variable?, comment? }`: where the token is in the line's text (UTF-16 code units, end exclusive, the spaces around it left out), its `kind` from Ghidra's syntax highlighting (`keyword`, `comment`, `type`, `function`, `variable`, `constant` or `syntax`; parameters and globals are variables, a function passed as a pointer too; punctuation, operators, labels and field names are syntax), the `address` of the instruction its operation came from (for a comment, the address it is attached to), and the `target` a name refers to: a called function's entry, or the slot of a `pointers` function known only by name; a global's or a string's start, whether it is read or its address taken (`&completed.0`, `main` passed to `__libc_start_main`); a label's code. A qualified name is one token, its namespaces and `::`s included: `std::string::operator+=` is a `function` token targeting `operator+=`. Ghidra's printer may break a long one at a `::` (`void std::` at the end of a line, `::_M_erase(…)` starting the next); each line then holds a token of it, of the name's kind, and only the last, the name's own, has an address and a target. A local variable's or parameter's name carries the variable's id as `variable`, and each word of a comment says whose comment it is as `comment`: `'user'` for the caller's, `'warning'` for the decompiler's.
- `addresses` is the set of the line's tokens' addresses, sorted. A line without one, such as a declaration, has none.

For crackme's `    iVar1 = strcmp(acStack_98,"hunter2");`:

```js
{ start: 4,  end: 9,  kind: 'variable', address: 0x1277, variable: 'register:0x0:4@+0x8e' } // iVar1
{ start: 12, end: 18, kind: 'function', address: 0x1277, target: 0x10f0 }                  // strcmp
{ start: 19, end: 29, kind: 'variable', address: 0x126a, variable: 'stack:-0x98' }          // acStack_98
{ start: 30, end: 39, kind: 'constant', address: 0x1277, target: 0x2017 }                  // "hunter2"
// addresses: [0x126a, 0x1277]
```

### Naming variables and adding comments

A decompiled function lists the local variables and parameters its tokens name in `variables`, in the order first named, each `{ id, kind, name, storage, locked? }`: crackme's `main` has `{ id: 'stack:-0x98', kind: 'local', name: 'acStack_98', storage: 'Stack[-0x98]' }` and `{ id: 'register:0x0:4@+0x8e', kind: 'local', name: 'iVar1', storage: 'EAX' }`. `decompile` takes names for them by id, and comments by address, beside the guards:

```ts
const result = program.decompile(0x11e9, {
  locals: [
    { id: 'stack:-0x98', name: 'password' },
    { id: 'register:0x0:4@+0x8e', name: 'result' },
  ],
  comments: [{ address: 0x1277, text: 'the check' }], // call strcmp
  functionComment: 'Asks for the password.',
})
```

```c
/* Asks for the password. */

undefined8 main(void)

{
  int result;
  ...
  char password [136];
  ...
                    /* the check */
    result = strcmp(password,"hunter2");
    if (result == 0) {
```

| Option | What it does |
| --- | --- |
| `locals` | `{ id, name }[]`: a name for each variable whose `id` a decompilation reported. |
| `comments` | `{ address, text }[]`: a comment at an instruction, printed before the statement whose code holds it. |
| `functionComment` | A comment above the function's declaration. |
| `unplacedComments` | Lists the comments no statement holds above the function; by default they are left out, as in Ghidra. |

An **id** is the decompiler's identity for the variable, from where Ghidra keeps it, and is opaque to the caller:

- `stack:-0x98`: a variable tied to a stack location, by its offset alone, so it survives a change of its size;
- `register:0x0:4@+0x8e`: a storage, its size and the variable's first use as an offset from the function's entry; a parameter's first use is `@-0x1`, the entry less one, as Ghidra writes it;
- `join(register:0x10:8+register:0x0:8):16@+0x19b`: a value split across registers, by its pieces;
- `hash:0x3f276f9932@+0x27c`: a temporary, by Ghidra's hash of its data-flow and where it is used, and any variable whose storage the decompiler's own lookup would not tell apart.

The same bytes give the same ids whatever was decompiled before, in any program, and wherever the program is placed. A stack id survives almost any change of the function's bytes; a register id lasts while the instruction setting the variable does; a hash is the most fragile, changing with the code around the temporary. `storage` says where the variable lives as Ghidra writes it: a register (`EAX`), a stack location (`Stack[-0x98]`), the registers of a split value, most significant first (`RDX:RAX`), or a temporary's hash (`HASH:3f276f9932`).

**Names** are applied after the decompiler's analysis, just before printing, so the code and every other name read as without them: naming `iVar1` does not turn `pcVar2` into `pcVar1`, as renaming during the analysis, Ghidra's own route, does. An id that names no variable of this decompilation, after the bytes changed, is ignored, and a parameter named by a declaration of `prototypes` (`int puts(char *s)`) is reported `locked: true` and keeps its name. A name is printed as given, so the caller checks it is a C identifier: Ghidra prints keywords, spaces and `*/` as they are. A name another variable has prints with a suffix (`status_00` beside `status`), and a global's name makes the global print as `::printf`. A malformed id or an empty name is a `RangeError`.

**Comments** are Ghidra's PRE comments, which it places before the statement whose code holds the address, several at one address in the order given and above the decompiler's own warnings there; a line break (`\n`, `\r\n` or `\r`) starts a new line of the comment, an empty text adds nothing, and `*/` is written `* /` so the comment does not end early. Ghidra's printer reads `{@` up to the next `}` as one token, an annotation, so a `{@` whose text up to there holds a line break is written `{ @`, keeping a line of the text for each of the function's `lines`; one on a single line prints as it is. Statements that share an instruction (a copy the decompiler moved next to a call) are not told apart by its address, so a comment there goes above the statement Ghidra roots at it, which may be the one after the line meant. An address no statement holds is left out: an instruction without p-code (the entry's `endbr64`), code the decompiler removed as dead or unreachable, or an address outside the function. `unplacedComments` lists those above the function instead, by address, under Ghidra's heading `/* Comments that could not be placed in the function body: */`. The function comment is Ghidra's plate comment, its text printed as a comment's: above the declaration, before the decompiler's header warnings, an empty line after them. Each word of a comment is a token of kind `comment` with the comment's address (the function comment's is the entry) and `comment: 'user'`; the decompiler's own warnings, and the heading of the unplaced comments, have `comment: 'warning'`.

Nothing is kept between calls: each names and comments only its own decompilation. A call without these options decompiles as before them. They cost little: factcheck's `main` took 23.5 ms, and 25 ms with 32 names, 80 comments and a function comment; reporting the ids costs nothing measurable (factcheck's 323 functions in 390 ms, as before them).

### Guards

Two options bound a decompilation, as Ghidra's own options do:

| Option | Default | Past it |
| --- | --- | --- |
| `maxInstructions` | 100,000 | `{ address, error: 'printf_positional has more than 1000 instructions' }`, with `maxInstructions: 1000` |
| `timeLimitMs` | 30,000 | `{ address, error: 'printf_positional took longer than 30 seconds to decompile' }` |

The time is checked inside the module before each step of the decompiler's analysis, so a call returns a little after its limit (50 to 250 ms after it in the measurements), without anything kept of the stopped decompilation. Both take positive whole numbers below 2^32.

### Programs and memory

Several programs can be open at once, each holding a copy of its image in the module's memory. `program.dispose()` frees one; `decompiler.dispose()` disposes every program still open, after which the decompiler opens no more. A decompilation keeps nothing once it returns.

`decompiler.memory()` gives `{ size, used }` in bytes: the WebAssembly memory, which grows to fit the largest decompilation and never shrinks, and the part of it the decompiler holds. The architecture's SLEIGH translator, about 17 MB once the first program has decoded its description, is kept for later programs.

## Prototypes

The decompiler binds a library function's parameters and return type by name, from C declarations the caller passes as `prototypes`: a declaration binds to every function whose `symbol` is its name, or whose name is, for a function without a `symbol`, whatever namespace the function is in. The package ships `prototypes/glibc.h`, with 537 functions and 8 global variables of glibc 2.43, libm, the C++ runtime and libgcc_s, each the name of a symbol those libraries export (but `atexit` and `at_quick_exit`, which glibc links into each program):

- stdio, with the printf family's `_chk` variants (`__printf_chk`, `__fprintf_chk`, `__sprintf_chk`, `__snprintf_chk`) and the scanf family's `__isoc99_` and `__isoc23_` names; string, with the `_chk` functions of `_FORTIFY_SOURCE`; stdlib, with C23's `__isoc23_strtol` family; ctype's `__ctype_b_loc`; unistd, fcntl, `stat`, mmap, dirent, poll, time, signal, setjmp (`_setjmp`, `__sigsetjmp`), err, sockets, pthread, libm;
- `__libc_start_main`, with `main`'s prototype as its first parameter, `__stack_chk_fail`, `__assert_fail`, `__cxa_atexit`, `__cxa_finalize`;
- the C++ ABI (`__cxa_throw`, `__cxa_begin_catch`, `__gxx_personality_v0`, `_Unwind_Resume`, …) and `operator new` and `delete` and the `std::__throw_` helpers under their mangled names (`_Znwm`, `_ZdlPv`, `_ZSt20__throw_length_errorPKc`), which bind to a function named by its demangled name when its `symbol` is the mangled one ([C++ names](#c-names));
- `stdin`, `stdout`, `stderr`, `environ` and getopt's globals.

50 of the functions are marked `_Noreturn` (`exit`, `_exit`, `abort`, `__stack_chk_fail`, `__assert_fail`, `longjmp`, `err`, `pthread_exit`, `__cxa_throw`, `_Unwind_Resume`, `__libc_start_main`, …), so a call to one ends its block, as in Ghidra.

Its text is the default export of `@specy/decompiler/prototypes/glibc`, as in the example above, and the file itself is `@specy/decompiler/prototypes/glibc.h`, for a bundler that imports a file's text (`?raw` in Vite) or for `import.meta.resolve`. A dynamically linked program's PLT stubs carry these names; a statically linked one may name glibc's code after its internal aliases (`_IO_puts`, `__printf`), which the file does not declare.

The declarations are read by the decompiler's own C parser, Ghidra's CParse, one at a time, so a caller can add its own after the file's. CParse has limits a C compiler does not, which the package works around where it can:

- `_Noreturn`, `[[noreturn]]` and `__attribute__((noreturn))` mark a function as not returning; other attributes are dropped;
- C's multi-word types (`unsigned long`, `long long`, `signed char`) are read as Ghidra's types of the same size (`ulong`, `long`, `sbyte`), and `__restrict`, `__const` and `__inline` as their standard spellings;
- a struct or union referred to by its tag (`struct _IO_FILE`) is created, incomplete, if it is not defined yet;
- a function named after one of the architecture's calling conventions (`syscall`) is read under another name and bound under its own.

What the caller must write differently: an empty parameter list as `(void)`; an array with a size (a parameter as the pointer it decays to); a struct defined before it is referred to, never after; no type's name as a function's or a parameter's (so `stat()` takes a `void *`, `struct stat` sharing its name). A declaration naming no function or global of the program is read and ignored.

## Performance

Measured with `npm run measure` (`scripts/measure.mjs`, after `npm run build`) on 2026-10-15: the published build in Node 24.18 on an Intel Core i7-10750H under WSL2, with other work running, each program opened with `prototypes/glibc.h`. "First" is a function's first decompilation in its program, "warm" the median of the next ones (20 for the crackmes, 2 to 5 for hello-atexit-static). Times in milliseconds.

| Step | Time |
| --- | --- |
| `loadDecompiler` (compiling the wasm, writing the processor description) | 24 |
| The first `openProgram` of an architecture, which decodes its SLEIGH description (kept for later programs) | 127 |
| A later `openProgram`: crackme, hello-atexit-static (800 KB, 1,340 function names) | 14, 23 |

The test fixtures:

- **crackme** (15 functions): 0.3 to 17 first, 0.3 to 3.7 warm; `main` 9.6 first, 3.7 warm.
- **crackme-stripped**, named by hand (13 functions): 0.3 to 2.9 first, 0.3 to 2.8 warm; `main` 2.9 first, 2.8 warm.
- **hello-atexit-static**, glibc linked statically (1,340 function names at 1,087 addresses), its 20 largest functions:

| Function | Bytes | First | Warm |
| --- | ---: | ---: | ---: |
| `_dl_relocate_object_no_relro` | 12,558 | 2,471 | 2,322 |
| `__printf_buffer` | 10,699 | 5,206 | 5,445 |
| `_nl_load_domain` | 9,856 | 919 | 1,006 |
| `printf_positional` | 9,081 | 15,287 | 15,267 |
| `__printf_fp_buffer_1.isra.0` | 8,978 | 1,082 | 1,117 |
| `__strcasecmp_l_sse2` | 7,638 | 179 | 162 |
| `_int_malloc` | 6,428 | 2,449 | 1,198 |
| `_dl_map_object_from_fd.constprop.0` | 6,424 | 711 | 635 |
| `__strncmp_sse2` | 6,233 | 93 | 92 |
| `__strcasecmp_l_sse42` | 6,198 | 135 | 128 |
| `__strcmp_sse2` | 5,193 | 127 | 80 |
| `init_cpu_features.constprop.0` | 5,013 | 438 | 453 |
| `_dl_map_object_deps` | 4,425 | 343 | 280 |
| `__gconv_transform_utf8_internal` | 4,100 | 336 | 334 |
| `__strncmp_sse42` | 4,057 | 59 | 54 |
| `__memcpy_ssse3` | 3,901 | 113 | 126 |
| `__printf_fphex_buffer` | 3,867 | 135 | 154 |
| `__gconv_transform_internal_ucs2reverse` | 3,853 | 246 | 249 |
| `__gconv_transform_internal_utf8` | 3,836 | 1,291 | 1,237 |
| `__gconv_transform_internal_ucs2` | 3,820 | 252 | 261 |

Every function of hello-atexit-static once, in one program: 1,086 of the 1,087 decompiled before `patches/0004-flow-unmapped-bytes.patch`, and all of them since (below), 940 of them in under 100 ms; the median takes 8.3, the 90th percentile 141, the 99th 1,217, and the whole 124 seconds. The slowest are `printf_positional` (14.3 seconds), `__gconv_transform_internal_ucs4` (12.5), `__gconv_transform_ucs4_internal` (10.4), `__gconv_transform_internal_ucs4le` (7.5) and `__gconv_transform_ucs4le_internal` (5.9). Size says little about time: these spend it in the decompiler's simplification passes, which repeat until nothing changes. Times move with the machine's load (`printf_positional` took 23 to 26 seconds in a run beside other builds), and a slower device multiplies them; a function past `timeLimitMs` returns an error.

The one that failed, `uw_update_context_1`, failed in the decompiler's analysis, not for want of bytes in the image: the decompiler cannot bound the index of its switch at `0x47da4f` (compared in memory, then loaded again), so it reads 256 entries from a table of 6, takes the bytes after the table for more targets, and follows one to `0x1ee8349`, outside every segment, which the image refuses as Ghidra's own load images do ("Bytes at ram:0x1ee8349 are not mapped"). Since patch 0004 the flow stops there with `halt_baddata()`, as at bad instruction data, so the function decompiles, in about 11 seconds, under the warnings of that flow; a function whose own entry is unmapped still fails.

Memory, from `decompiler.memory()`: 20 MB of wasm memory after `loadDecompiler`; after every function of hello-atexit-static once, 126 MB of wasm memory with 28 MB in use (the program, its image and the SLEIGH translator; nothing is kept of a decompilation once it returns); after the program is disposed, 17 MB in use, the SLEIGH translator kept for the next program. WebAssembly memory never shrinks, so its size is the high-water mark: here `printf_positional`'s analysis.

## Building from source

The wasm (`src/wasm/`) and the compiled processor descriptions (`../specs/`) are committed, so the package builds and tests with Node alone:

```sh
npm ci
npm run type-check
npm test            # vitest, in Node, against src/
npm run build       # dist/: the bundle, the wasm, the specs, the prototypes, the licences
npm run test:dist   # the published layout, through the package's own exports
npm run measure     # the Performance section's measurements
```

Rebuilding the wasm needs emsdk 6.0.9 and rebuilding the processor descriptions a C++ compiler; the scripts are in the repository's `scripts/`, described in its [README](../README.md).

## Licence

Apache-2.0 (`dist/LICENSE`). The wasm is built from Ghidra's decompiler sources, and the processor descriptions are compiled from Ghidra's, both Apache-2.0 with Ghidra's `NOTICE`; the wasm also contains the zlib sources Ghidra bundles, under the zlib License. The published package carries all three in `dist/ghidra/` (`LICENSE`, `NOTICE`, `zlib_License.txt`). The repository's `patches/` lists the changes made to Ghidra's sources.
