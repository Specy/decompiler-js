import { readFile } from 'node:fs/promises'
import { loadDecompiler, type Decompiler } from '../src'
import { readElfImage } from './helpers/elf-image'
import type { ProgramImage } from '../src'

// In src/ the processor descriptions are the repository's specs/, which the
// package build copies beside dist/index.mjs.
const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

/** An ELF fixture's program image, with the test prototypes. */
async function fixtureImage(name: string): Promise<ProgramImage> {
    const image = readElfImage(new Uint8Array(await readFile(fixture(`elf/${name}`))))
    return { ...image, prototypes: await readFile(fixture('prototypes.h'), 'utf8') }
}

/** The C with each run of whitespace made one space, and none at the ends. */
const normalized = (text: string) => text.replace(/\s+/g, ' ').trim()

let decompiler: Decompiler

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
})

afterAll(() => decompiler.dispose())

test('loads the wasm and reports the Ghidra release it was built from', () => {
    expect(decompiler.version).toMatch(/^Ghidra_12\.1\.4_build [0-9a-f]{12}$/)
    expect(decompiler.architecture).toBe('x86-64')
})

test("decompiles crackme's main as the native reference build does", async () => {
    const image = await fixtureImage('crackme')
    const main = image.functions.find((fn) => fn.name === 'main')!
    const reference = await readFile(fixture('reference/crackme-main.c'), 'utf8')

    const program = decompiler.openProgram(image)
    try {
        expect(program.warnings).toEqual([])
        const result = program.decompile(main.address)
        expect(result).toHaveProperty('text')
        if (!('text' in result)) return
        expect(normalized(result.text)).toBe(normalized(reference))
        // A second decompilation of the same function starts again from scratch.
        expect(program.decompile(main.address)).toEqual(result)
    } finally {
        program.dispose()
    }
})

test('reports an address where no function starts as an error, not a throw', async () => {
    const image = await fixtureImage('crackme-stripped')
    const program = decompiler.openProgram(image)
    try {
        expect(program.decompile(0x11e9)).toEqual({ address: 0x11e9, error: 'No function starts at 0x11e9' })
    } finally {
        program.dispose()
    }
})

test('a disposed program refuses to decompile', async () => {
    const image = await fixtureImage('crackme')
    const program = decompiler.openProgram(image)
    program.dispose()
    expect(() => program.decompile(0x11e9)).toThrow('The program was disposed')
})

test('reports each declaration that cannot be read, once, and applies the others', async () => {
    const image = await fixtureImage('crackme')
    const prototypes = `${image.prototypes}
size_tt strlen(const char *s);
int broken(;
_Noreturn int not_a_function;
`
    const program = decompiler.openProgram({ ...image, prototypes })
    try {
        expect(program.warnings).toEqual([
            'line 18: Syntax error after `size_tt` (in `size_tt strlen(const char *s);`)',
            'line 19: Syntax error after `;` (in `int broken(;`)',
            'line 20: Only a function can be marked noreturn (in `_Noreturn int not_a_function;`)'
        ])
        const main = image.functions.find((fn) => fn.name === 'main')!
        const result = program.decompile(main.address)
        expect('text' in result && result.text).toContain('puts("Correct!");')
    } finally {
        program.dispose()
    }
})
