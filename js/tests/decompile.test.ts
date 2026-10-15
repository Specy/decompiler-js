import { readFile } from 'node:fs/promises'
import { loadDecompiler, type Decompiler } from '../src'
import { readElfImage } from './helpers/elf-image'

// In src/ the processor descriptions are the repository's specs/, which the
// package build copies beside dist/index.mjs.
const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

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
    const image = readElfImage(new Uint8Array(await readFile(fixture('elf/crackme'))))
    const main = image.functions.find((fn) => fn.name === 'main')!
    const reference = await readFile(fixture('reference/crackme-main.c'), 'utf8')

    const program = decompiler.openProgram(image)
    try {
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
    const image = readElfImage(new Uint8Array(await readFile(fixture('elf/crackme-stripped'))))
    const program = decompiler.openProgram(image)
    try {
        expect(program.decompile(0x11e9)).toEqual({ address: 0x11e9, error: 'No function starts at 0x11e9' })
    } finally {
        program.dispose()
    }
})

test('a disposed program refuses to decompile', async () => {
    const image = readElfImage(new Uint8Array(await readFile(fixture('elf/crackme'))))
    const program = decompiler.openProgram(image)
    program.dispose()
    expect(() => program.decompile(0x11e9)).toThrow('The program was disposed')
})
