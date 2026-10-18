# @specy/decompiler

Ghidra's C++ decompiler, from Ghidra 12.1.4, compiled to WebAssembly: C for a function of a program described in memory, as lines of tokens linked to the instructions they come from and to the functions and data they name. It runs in a browser or in Node, without Java, a server or files.

## Usage

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

`loadDecompiler({ architecture, wasmUrl?, specsUrl? })` fetches `decompiler.wasm` (2.1 MB, 715 KB gzipped) and the architecture's processor description (`specs/x86-64/`: `x86-64.sla`, 501 KB, and three specification files, 29 KB), by default from beside the package's module through `new URL(…, import.meta.url)`, which bundlers follow and copy. `wasmUrl` and `specsUrl` (a directory) serve them from elsewhere. In Node, `file:` URLs are read from disk.

The only architecture is `'x86-64'`: Ghidra's `x86:LE:64:default` language with its `gcc` compiler specification, the System V calling convention of Linux and the BSDs.

`decompiler.version` names the Ghidra release and commit the module was built from (`Ghidra_12.1.4_build 8b6bbb857acc`).

### Describing a program

`openProgram(image)` takes a `ProgramImage`, what a loader or a disassembler already knows about the program; the package reads no executable format.

| Field | What it is |
| --- | --- |
| `regions` | The program's memory at its link-time addresses: `{ address, bytes, readOnly, executable }`, not overlapping. Loads from a `readOnly` region are folded to their values, as Ghidra does with read-only memory. A read starting outside every region fails the decompilation; one running past a region's end reads zeros. |
| `functions` | `{ address, name }`: the entry points the decompiler may start from, and the names it prints for them. At an address named twice, the first name wins. A prototype binds by name, so a PLT stub is named after its symbol (`strcmp`, not `strcmp@plt`). |
| `data` | `{ address, name, size? }`: global variables, tables and strings' labels. A function's name wins over a data name at its address. |
| `strings` | `{ address, length }`: string literals, terminator included. A string with a `data` name is typed as a `char` array; any string in read-only memory prints as a literal where a `char *` is expected. |
| `pointers` | Optional. `{ address, name }`: a pointer-sized slot that holds the address of the function `name`, though the file may not hold it there, such as a GOT entry the dynamic linker fills. A call through the slot is a call of that function, with its name and prototype: `_start`'s `call [rip + __libc_start_main@got]` prints as `__libc_start_main(main,…)`, not `(*pcRam0000000000003fd8)(main,…)`. The function is the image's function of that name, or else one known only by its name, which has no code to decompile. A `data` name at the slot's address names the slot where its value is read (`if (__gmon_start__@got != (code *)0x0)`); without one it prints as the decompiler names a global `code *`. At an address given twice the first is kept, and a function's name at the slot's address wins. |
| `prototypes` | C declarations of the library functions the program calls and the types they use: see [Prototypes](#prototypes). |

Addresses are numbers: every x86-64 user-space address is below 2^53. `openProgram` throws when the image is malformed (overlapping regions, an address that is not a non-negative safe integer). What it could not use without failing is in `program.warnings`: each declaration that could not be read, with its line (``line 3: Syntax error after `size_tt` (in `size_tt strlen(const char *s);`)``), and any message Ghidra printed.

The package invents no names. A function or a global the image does not name prints as the decompiler names it (`func_0x00401130()`, `cRam0000000000004018`), and locals and parameters keep the decompiler's names (`iVar1`, `acStack_98`, `param_1`). The Ghidra application's `local_98` comes from a stack analysis on its Java side, which is not part of the decompiler. Types print with Ghidra's names (`undefined8`, `uint`, `ulong`).

### Decompiling

`program.decompile(address, options?)` returns the C of the function starting at `address`, one of the image's functions, as `{ address, name, text, lines }`, or `{ address, error }` when there is none: no function starts there, it is past a guard below, or the decompiler failed. It throws only when the program was disposed or an argument is not a number it takes.

Each line is `{ text, tokens, addresses }`:

- `tokens`, in order, are `{ start, end, kind, address?, target? }`: where the token is in the line's text (UTF-16 code units, end exclusive, the spaces around it left out), its `kind` from Ghidra's syntax highlighting (`keyword`, `comment`, `type`, `function`, `variable`, `constant` or `syntax`; parameters and globals are variables, a function passed as a pointer too; punctuation, operators, labels and field names are syntax), the `address` of the instruction its operation came from (for a comment, the address it is attached to), and the `target` a name refers to: a called function's entry, or the slot of a `pointers` function known only by name; a global's or a string's start, whether it is read or its address taken (`&completed.0`, `main` passed to `__libc_start_main`); a label's code.
- `addresses` is the set of the line's tokens' addresses, sorted. A line without one, such as a declaration, has none.

For crackme's `    iVar1 = strcmp(acStack_98,"hunter2");`:

```js
{ start: 4,  end: 9,  kind: 'variable', address: 0x1277 }                   // iVar1
{ start: 12, end: 18, kind: 'function', address: 0x1277, target: 0x10f0 }   // strcmp
{ start: 19, end: 29, kind: 'variable', address: 0x126a }                   // acStack_98
{ start: 30, end: 39, kind: 'constant', address: 0x1277, target: 0x2017 }   // "hunter2"
// addresses: [0x126a, 0x1277]
```

### Guards

Two options bound a decompilation, as Ghidra's own options do:

| Option | Default | Past it |
| --- | --- | --- |
| `maxInstructions` | 100,000 | `{ address, error: 'printf_positional has more than 1000 instructions' }` |
| `timeLimitMs` | 30,000 | `{ address, error: 'printf_positional took longer than 30 seconds to decompile' }` |

The time is checked inside the module before each step of the decompiler's analysis, so a call returns a little after its limit (50 to 250 ms after it in the measurements), without anything kept of the stopped decompilation. Both take positive whole numbers below 2^32.

### Programs and memory

Several programs can be open at once, each holding a copy of its image in the module's memory. `program.dispose()` frees one; `decompiler.dispose()` disposes every program still open, after which the decompiler opens no more. A decompilation keeps nothing once it returns.

`decompiler.memory()` gives `{ size, used }` in bytes: the WebAssembly memory, which grows to fit the largest decompilation and never shrinks, and the part of it the decompiler holds. The architecture's SLEIGH translator, about 17 MB once the first program has decoded its description, is kept for later programs.

## Prototypes

The decompiler binds a library function's parameters and return type by name, from C declarations the caller passes as `prototypes`. The package ships `prototypes/glibc.h`, with 537 functions and 8 global variables of glibc 2.43, libm, the C++ runtime and libgcc_s, each the name of a symbol those libraries export (but `atexit` and `at_quick_exit`, which glibc links into each program):

- stdio, with the printf family's `_chk` variants (`__printf_chk`, `__fprintf_chk`, `__sprintf_chk`, `__snprintf_chk`) and the scanf family's `__isoc99_` and `__isoc23_` names; string, with the `_chk` functions of `_FORTIFY_SOURCE`; stdlib, with C23's `__isoc23_strtol` family; ctype's `__ctype_b_loc`; unistd, fcntl, `stat`, mmap, dirent, poll, time, signal, setjmp (`_setjmp`, `__sigsetjmp`), err, sockets, pthread, libm;
- `__libc_start_main`, with `main`'s prototype as its first parameter, `__stack_chk_fail`, `__assert_fail`, `__cxa_atexit`, `__cxa_finalize`;
- the C++ ABI (`__cxa_throw`, `__cxa_begin_catch`, `__gxx_personality_v0`, `_Unwind_Resume`, …) and `operator new` and `delete` and the `std::__throw_` helpers under their mangled names (`_Znwm`, `_ZdlPv`, `_ZSt20__throw_length_errorPKc`);
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

Every function of hello-atexit-static once, in one program: 1,086 of the 1,087 decompile, 940 of them in under 100 ms; the median takes 8.3, the 90th percentile 141, the 99th 1,217, and the whole 124 seconds. The slowest are `printf_positional` (14.3 seconds), `__gconv_transform_internal_ucs4` (12.5), `__gconv_transform_ucs4_internal` (10.4), `__gconv_transform_internal_ucs4le` (7.5) and `__gconv_transform_ucs4le_internal` (5.9). Size says little about time: these spend it in the decompiler's simplification passes, which repeat until nothing changes. Times move with the machine's load (`printf_positional` took 23 to 26 seconds in a run beside other builds), and a slower device multiplies them; a function past `timeLimitMs` returns an error.

The one that fails, `uw_update_context_1`, fails in the decompiler's analysis, not for want of bytes in the image: the decompiler cannot bound the index of its switch at `0x47da4f` (compared in memory, then loaded again), so it reads 256 entries from a table of 6, takes the bytes after the table for more targets, and follows one to `0x1ee8349`, outside every segment, which the image refuses as Ghidra's own load images do ("Bytes at ram:0x1ee8349 are not mapped").

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
