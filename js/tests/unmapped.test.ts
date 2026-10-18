// A function whose flow runs into bytes nothing maps: a program that ends with
// Linux's exit system call, which the decompiler cannot know does not return,
// goes on after the syscall instruction, past the last byte of its segment.
// Flow stops there as it stops at bad instruction data, rather than the whole
// function failing.
import { loadDecompiler, type Decompiler } from '../src'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)

/** mov eax, 60; xor rdi, rdi; syscall, the whole of its segment. */
const START = 0x401000
const EXIT = Uint8Array.of(0xb8, 0x3c, 0x00, 0x00, 0x00, 0x48, 0x31, 0xff, 0x0f, 0x05)

let decompiler: Decompiler

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
})

afterAll(() => decompiler.dispose())

test('stops the flow at the first unmapped byte, as at bad instruction data', () => {
    const program = decompiler.openProgram({
        regions: [{ address: START, bytes: EXIT, readOnly: true, executable: true }],
        functions: [{ address: START, name: '_start' }],
        data: [],
        strings: [],
        prototypes: ''
    })
    const result = program.decompile(START)
    if ('error' in result) throw new Error(result.error)
    const lines = result.lines.map((line) => line.text.trim())
    expect(lines).toContain('syscall();')
    expect(lines).toContain('halt_baddata();')
    // the warnings say where and why, as Ghidra's for bad instruction data do
    expect(result.text).toContain('/* WARNING: Control flow encountered unmapped bytes */')
    expect(result.text).toContain('/* WARNING: Unmapped bytes - Truncating control flow here */')
    // the halt is at the first byte past the segment
    const halt = result.lines.find((line) => line.text.includes('halt_baddata'))!
    expect(halt.addresses).toEqual([START + EXIT.length])
    program.dispose()
})

test('still fails a function whose own first byte is unmapped', () => {
    const program = decompiler.openProgram({
        regions: [{ address: START, bytes: EXIT, readOnly: true, executable: true }],
        functions: [{ address: START + 0x100, name: 'nowhere' }],
        data: [],
        strings: [],
        prototypes: ''
    })
    expect(program.decompile(START + 0x100)).toEqual({
        address: START + 0x100,
        error: expect.stringContaining('not mapped')
    })
    program.dispose()
})
