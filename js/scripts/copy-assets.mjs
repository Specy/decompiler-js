// Copies into dist/ what tsdown does not bundle: the Emscripten module and its
// wasm, the compiled processor descriptions, the prototypes and the licences.
import { cp, mkdir, readFile, stat, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')

const copies = [
    // decompiler.mjs is never bundled and loads decompiler.wasm from beside
    // itself, so both have to land in dist/wasm/ together.
    ['src/wasm/decompiler.mjs', 'dist/wasm/decompiler.mjs'],
    ['src/wasm/decompiler.wasm', 'dist/wasm/decompiler.wasm'],
    ['src/wasm/decompiler.d.mts', 'dist/wasm/decompiler.d.mts'],
    // src/index.ts loads each architecture's files from ./specs/<architecture>/.
    ['../specs', 'dist/specs'],
    ['prototypes/glibc.h', 'dist/prototypes/glibc.h'],
    ['../LICENSE', 'dist/LICENSE'],
    // The wasm is built from Ghidra's sources (Apache-2.0, with its NOTICE) and
    // their bundled zlib (zlib License); the specs are compiled from Ghidra's.
    ['../ghidra/LICENSE', 'dist/ghidra/LICENSE'],
    ['../ghidra/NOTICE', 'dist/ghidra/NOTICE'],
    ['../ghidra/licenses/zlib_License.txt', 'dist/ghidra/zlib_License.txt'],
    ['../ghidra/README.md', 'dist/ghidra/README.md']
]

await mkdir(resolve(root, 'dist'), { recursive: true })

for (const [from, to] of copies) {
    const fromPath = resolve(root, from)
    const toPath = resolve(root, to)
    const fromStats = await stat(fromPath)
    await mkdir(dirname(toPath), { recursive: true })
    await cp(fromPath, toPath, {
        recursive: fromStats.isDirectory(),
        force: true,
        errorOnExist: false
    })
}

// The prototypes as a JavaScript string too, for bundlers that do not import
// a .h file's text: `import glibc from '@specy/decompiler/prototypes/glibc'`.
const glibc = await readFile(resolve(root, 'prototypes/glibc.h'), 'utf8')
await writeFile(
    resolve(root, 'dist/prototypes/glibc.mjs'),
    `// prototypes/glibc.h as a string, written by scripts/copy-assets.mjs.\nexport default ${JSON.stringify(glibc)}\n`
)
await writeFile(
    resolve(root, 'dist/prototypes/glibc.d.mts'),
    '/** The text of prototypes/glibc.h, C declarations of the glibc functions programs commonly call. */\ndeclare const glibc: string\nexport default glibc\n'
)
