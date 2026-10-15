// The guards on a decompilation: maxInstructions and timeLimitMs, on
// hello-atexit-static's printf_positional, 3,935 instructions (9,081 bytes)
// that take about 15 seconds to decompile without a limit.
import { readFile } from 'node:fs/promises'
import { loadDecompiler, type DecompiledProgram, type Decompiler } from '../src'
import { readElfImage } from './helpers/elf-image'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

let decompiler: Decompiler
let program: DecompiledProgram
let printfPositional: number
let main: number

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    const image = {
        ...readElfImage(new Uint8Array(await readFile(fixture('elf/hello-atexit-static')))),
        prototypes: await readFile(new URL('../prototypes/glibc.h', import.meta.url), 'utf8')
    }
    const address = (name: string) => image.functions.find((fn) => fn.name === name)!.address
    printfPositional = address('printf_positional')
    main = address('main')
    program = decompiler.openProgram(image)
})

afterAll(() => decompiler.dispose())

test('stops at maxInstructions with an error naming the function and the limit', () => {
    expect(program.decompile(printfPositional, { maxInstructions: 1000 })).toEqual({
        address: printfPositional,
        error: 'printf_positional has more than 1000 instructions'
    })
})

test('stops soon after timeLimitMs with an error naming the function and the limit', () => {
    for (const [limit, said] of [
        [100, '100 ms'],
        [1000, '1 second']
    ] as const) {
        const start = performance.now()
        const result = program.decompile(printfPositional, { timeLimitMs: limit })
        const elapsed = performance.now() - start
        expect(result).toEqual({
            address: printfPositional,
            error: `printf_positional took longer than ${said} to decompile`
        })
        // The time is checked between the steps of the analysis, the longest
        // of which take a few hundred milliseconds here.
        expect(elapsed).toBeGreaterThanOrEqual(limit)
        expect(elapsed).toBeLessThan(limit + 2000)
    }
})

test('a stopped decompilation leaves the program as it was, and frees what it held', () => {
    const before = program.decompile(main)
    expect('text' in before).toBe(true)
    program.decompile(printfPositional, { timeLimitMs: 50 })
    const used = decompiler.memory().used
    for (let i = 0; i < 5; i++) {
        expect('error' in program.decompile(printfPositional, { timeLimitMs: 50 + 20 * i })).toBe(true)
        expect('error' in program.decompile(printfPositional, { maxInstructions: 1000 + i })).toBe(true)
    }
    // Measured: no growth at all.
    expect(decompiler.memory().used - used).toBeLessThan(256 * 1024)
    expect(program.decompile(main)).toEqual(before)
})

test('refuses limits that are not positive whole numbers', () => {
    for (const bad of [0, -1, 1.5, Number.NaN, Infinity, 2 ** 32]) {
        expect(() => program.decompile(main, { timeLimitMs: bad })).toThrow(RangeError)
        expect(() => program.decompile(main, { maxInstructions: bad })).toThrow(RangeError)
    }
})
