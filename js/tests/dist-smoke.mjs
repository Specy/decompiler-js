// Smoke test for the published artifact: loads the decompiler through dist/
// with every default (the wasm beside dist/wasm/decompiler.mjs, the specs in
// dist/specs/) and decompiles crackme's main, checking the text, the warnings,
// the lines' tokens and a local named and a comment added through the
// annotations, and factcheck's main with its C++ names in their namespaces,
// so it covers what a consumer of @specy/decompiler
// imports rather than the sources vitest exercises.
//
// The vitest suite runs against src/ and passes the repository's specs/ by
// URL; the bundle finds its files through scripts/copy-assets.mjs instead,
// which can break with every src test still green. Run after `npm run build`.
import assert from 'node:assert/strict'
import { existsSync, readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { inNamespace, readNameTable } from './helpers/cpp-names.ts'
import { readElfImage } from './helpers/elf-image.ts'

const dist = new URL('../dist/', import.meta.url)
if (!existsSync(fileURLToPath(new URL('index.mjs', dist)))) {
    console.error('dist/index.mjs is missing - run `npm run build` first.')
    process.exit(1)
}

// The exports map resolves the package's own name to the bundle, and the
// prototypes to their file and to a module exporting the same text.
assert.equal(import.meta.resolve('@specy/decompiler'), new URL('index.mjs', dist).href)
const prototypes = import.meta.resolve('@specy/decompiler/prototypes/glibc.h')
assert.equal(prototypes, new URL('prototypes/glibc.h', dist).href)
assert.ok(existsSync(fileURLToPath(prototypes)), 'dist/prototypes/glibc.h is published')
const glibc = readFileSync(fileURLToPath(prototypes), 'utf8')
assert.equal(glibc, readFileSync(new URL('../prototypes/glibc.h', import.meta.url), 'utf8'))
const { default: glibcModule } = await import('@specy/decompiler/prototypes/glibc')
assert.equal(glibcModule, glibc, '@specy/decompiler/prototypes/glibc exports the text of glibc.h')

// The licences of everything the wasm and the specs are built from.
for (const file of ['LICENSE', 'ghidra/LICENSE', 'ghidra/NOTICE', 'ghidra/zlib_License.txt']) {
    assert.ok(existsSync(fileURLToPath(new URL(file, dist))), `dist/${file} is published`)
}

const { loadDecompiler } = await import('@specy/decompiler')
const decompiler = await loadDecompiler({ architecture: 'x86-64' })
assert.match(decompiler.version, /^Ghidra_12\.1\.4_build [0-9a-f]{12}$/)

const fixtures = new URL('./fixtures/', import.meta.url)
const image = {
    ...readElfImage(new Uint8Array(readFileSync(new URL('elf/crackme', fixtures)))),
    prototypes: glibc
}
const main = image.functions.find((fn) => fn.name === 'main')
const program = decompiler.openProgram(image)
assert.deepEqual(program.warnings, [], 'glibc.h applies without a warning')
const result = program.decompile(main.address, { maxInstructions: 1000 })
assert.ok('text' in result, `main decompiles: ${result.error}`)
const reference = readFileSync(new URL('reference/crackme-main.c', fixtures), 'utf8')
assert.equal(`${result.text}\n`, reference, "crackme's main matches the native reference")
assert.equal(result.name, 'main')
assert.equal(result.lines.map((line) => line.text).join('\n'), result.text)
const correct = result.lines.find((line) => line.text.trim() === 'puts("Correct!");')
assert.ok(correct, 'a line calls puts("Correct!")')
const puts = correct.tokens.find((token) => correct.text.slice(token.start, token.end) === 'puts')
assert.equal(puts.kind, 'function')
assert.equal(puts.target, image.functions.find((fn) => fn.name === 'puts').address)
assert.deepEqual(correct.addresses, [puts.address])
assert.deepEqual(
    result.variables.find((variable) => variable.name === 'acStack_98'),
    { id: 'stack:-0x98', kind: 'local', name: 'acStack_98', storage: 'Stack[-0x98]' },
    'the password buffer is a variable with its id'
)
const annotated = program.decompile(main.address, {
    locals: [{ id: 'stack:-0x98', name: 'password' }],
    comments: [{ address: 0x1277, text: 'the check' }] // call strcmp
})
assert.ok('text' in annotated, `main decompiles with annotations: ${annotated.error}`)
assert.ok(
    annotated.text.includes('/* the check */\n    iVar1 = strcmp(password,"hunter2");'),
    'the local is named and the comment placed above its statement'
)
program.dispose()

// C++ names in their namespaces, bound to glibc.h by their mangled symbols.
const factcheck = readElfImage(new Uint8Array(readFileSync(new URL('elf/factcheck', fixtures))))
const names = readNameTable(readFileSync(new URL('elf/factcheck.names.tsv', fixtures), 'utf8'))
const cpp = decompiler.openProgram({
    ...factcheck,
    functions: factcheck.functions.map((fn) => inNamespace(fn, names)),
    data: factcheck.data.map((entry) => inNamespace(entry, names)),
    pointers: factcheck.pointers.map((pointer) => inNamespace(pointer, names)),
    prototypes: glibc
})
assert.deepEqual(cpp.warnings, [], 'glibc.h applies to factcheck without a warning')
const cppMain = cpp.decompile(factcheck.functions.find((fn) => fn.name === 'main').address)
assert.ok('text' in cppMain, `factcheck's main decompiles: ${cppMain.error}`)
assert.equal(
    `${cppMain.text}\n`,
    readFileSync(new URL('reference/factcheck-main.c', fixtures), 'utf8'),
    "factcheck's main matches the native reference"
)
const append = cppMain.lines
    .flatMap((line) => line.tokens.map((token) => ({ ...token, text: line.text.slice(token.start, token.end) })))
    .find((token) => token.text === 'std::string::operator+=')
assert.equal(append?.kind, 'function', 'a qualified name is one token')
const appendSymbol = '_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEpLERKS4_'
assert.equal(append.target, factcheck.functions.find((fn) => fn.name === appendSymbol).address)
cpp.dispose()

const memory = decompiler.memory()
assert.ok(memory.used > 0 && memory.size >= memory.used, 'memory() reports the heap')
decompiler.dispose()

console.log(`dist smoke test passed (${decompiler.version})`)
