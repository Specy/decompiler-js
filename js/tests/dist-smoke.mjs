// Smoke test for the published artifact: loads the decompiler through dist/
// with every default (the wasm beside dist/wasm/decompiler.mjs, the specs in
// dist/specs/) and decompiles crackme's main, so it covers what a consumer of
// @specy/decompiler imports rather than the sources vitest exercises.
//
// The vitest suite runs against src/ and passes the repository's specs/ by
// URL; the bundle finds its files through scripts/copy-assets.mjs instead,
// which can break with every src test still green. Run after `npm run build`.
import assert from 'node:assert/strict'
import { existsSync, readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { readElfImage } from './helpers/elf-image.ts'

const dist = new URL('../dist/', import.meta.url)
if (!existsSync(fileURLToPath(new URL('index.mjs', dist)))) {
    console.error('dist/index.mjs is missing - run `npm run build` first.')
    process.exit(1)
}

// The exports map resolves the package's own name to the bundle, and the
// reserved prototypes subpath to its file.
assert.equal(import.meta.resolve('@specy/decompiler'), new URL('index.mjs', dist).href)
const prototypes = import.meta.resolve('@specy/decompiler/prototypes/glibc.h')
assert.equal(prototypes, new URL('prototypes/glibc.h', dist).href)
assert.ok(existsSync(fileURLToPath(prototypes)), 'dist/prototypes/glibc.h is published')

// The licences of everything the wasm and the specs are built from.
for (const file of ['LICENSE', 'ghidra/LICENSE', 'ghidra/NOTICE', 'ghidra/zlib_License.txt']) {
    assert.ok(existsSync(fileURLToPath(new URL(file, dist))), `dist/${file} is published`)
}

const { loadDecompiler } = await import('@specy/decompiler')
const decompiler = await loadDecompiler({ architecture: 'x86-64' })
assert.match(decompiler.version, /^Ghidra_12\.1\.4_build [0-9a-f]{12}$/)

const fixtures = new URL('./fixtures/', import.meta.url)
const image = readElfImage(new Uint8Array(readFileSync(new URL('elf/crackme', fixtures))))
const main = image.functions.find((fn) => fn.name === 'main')
const program = decompiler.openProgram(image)
const result = program.decompile(main.address)
assert.ok('text' in result, `main decompiles: ${result.error}`)
const normalized = (text) => text.replace(/\s+/g, ' ').trim()
const reference = readFileSync(new URL('reference/crackme-main.c', fixtures), 'utf8')
assert.equal(normalized(result.text), normalized(reference), "crackme's main matches the native reference")
program.dispose()
decompiler.dispose()

console.log(`dist smoke test passed (${decompiler.version})`)
