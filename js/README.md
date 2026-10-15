# @specy/decompiler

Ghidra's C++ decompiler compiled to WebAssembly.

## Performance

Measured with `npm run measure` (`scripts/measure.mjs`, after `npm run build`) on 2026-10-15: the
published build in Node 24.18 on an Intel Core i7-10750H under WSL2, with other work running, each
program opened with `prototypes/glibc.h`. "First" is a function's first decompilation in its
program, "warm" the median of the next ones (20 of them for crackme, 2 to 5 for
hello-atexit-static). Times in milliseconds.

| Step | Time |
| --- | --- |
| `loadDecompiler` (compiling the wasm, writing the processor description) | 24 |
| The first `openProgram` of an architecture, which decodes its SLEIGH description (kept for later programs) | 127 |
| A later `openProgram`: crackme, hello-atexit-static (800 KB, 1,340 function names) | 14, 23 |

The test fixtures:

- **crackme** (15 functions): 0.3 to 17 first, 0.3 to 3.7 warm; `main` 9.6 first, 3.7 warm.
- **crackme-stripped**, named by hand (13 functions): 0.3 to 2.9 first, 0.3 to 2.8 warm; `main`
  2.9 first, 2.8 warm.
- **hello-atexit-static**, glibc linked statically (1,340 function names at 1,087 addresses), its
  20 largest functions:

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

Every function of hello-atexit-static once, in one program: 1,086 of the 1,087 decompile, 940
of them in under 100 ms; the median takes 8.3, the 90th percentile 141, the 99th 1,217, and the
whole 124 seconds. The slowest are `printf_positional` (14.3 seconds),
`__gconv_transform_internal_ucs4` (12.5), `__gconv_transform_ucs4_internal` (10.4),
`__gconv_transform_internal_ucs4le` (7.5) and `__gconv_transform_ucs4le_internal` (5.9). Size
says little about time: these spend it in the decompiler's simplification passes, which repeat
until nothing changes. Times move with the machine's load (`printf_positional` took 23 to 26
seconds in a run beside other builds), and a slower device multiplies them; a function past
`timeLimitMs` returns an error.

The one that fails, `uw_update_context_1`, does so as it would in Ghidra: the decompiler cannot
bound the index of its switch at `0x47da4f` (compared in memory, then loaded again), so it reads
256 entries from a table of 6, takes the bytes after the table for more targets, and follows one
to `0x1ee8349`, outside every segment, which a load image refuses ("Bytes at ram:0x1ee8349 are
not mapped").

Memory, from `decompiler.memory()`: 20 MB of wasm memory after `loadDecompiler`; after every
function of hello-atexit-static once, 126 MB of wasm memory with 28 MB in use (the program, its
image and the SLEIGH translator; nothing is kept of a decompilation once it returns); after the
program is disposed, 17 MB in use, the SLEIGH translator kept for the next program. WebAssembly
memory never shrinks, so its size is the high-water mark: here `printf_positional`'s analysis.
