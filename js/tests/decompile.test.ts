import { readFile } from 'node:fs/promises'
import { loadDecompiler, type DecompiledFunction, type Decompiler, type ProgramImage } from '../src'
import { readElfImage } from './helpers/elf-image'

// In src/ the processor descriptions are the repository's specs/, which the
// package build copies beside dist/index.mjs.
const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

/** An ELF fixture's program image, with the test prototypes. */
async function fixtureImage(name: string): Promise<ProgramImage> {
    const image = readElfImage(new Uint8Array(await readFile(fixture(`elf/${name}`))))
    return { ...image, prototypes: await readFile(fixture('prototypes.h'), 'utf8') }
}

/** The function's text and lines, failing the test on an error. */
function decompiled(result: DecompiledFunction) {
    if ('error' in result) throw new Error(`decompiling 0x${result.address.toString(16)} failed: ${result.error}`)
    return result
}

/** The line whose text, less its indent, is the given one. */
function lineOf(result: ReturnType<typeof decompiled>, text: string) {
    const line = result.lines.find((candidate) => candidate.text.trim() === text)
    if (!line) throw new Error(`no line reads ${text}`)
    return line
}

// crackme's main, from its symbol table (readelf -s: 0x11e9, 210 bytes), and
// the instructions its password check ends in (objdump -d).
const MAIN = 0x11e9
const MAIN_END = 0x11e9 + 210
const TEST_EAX = 0x127c // test %eax,%eax, after strcmp
const JNE_WRONG = 0x127e // jne to puts("Wrong!")

let decompiler: Decompiler

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
})

afterAll(() => decompiler.dispose())

test('loads the wasm and reports the Ghidra release it was built from', () => {
    expect(decompiler.version).toMatch(/^Ghidra_12\.1\.4_build [0-9a-f]{12}$/)
    expect(decompiler.architecture).toBe('x86-64')
})

describe("crackme's main, named by its symbol table", () => {
    let image: ProgramImage
    let result: ReturnType<typeof decompiled>

    beforeAll(async () => {
        image = await fixtureImage('crackme')
        const program = decompiler.openProgram(image)
        try {
            expect(program.warnings).toEqual([])
            result = decompiled(program.decompile(MAIN))
            // A second decompilation of the same function starts again from scratch.
            expect(program.decompile(MAIN)).toEqual(result)
        } finally {
            program.dispose()
        }
    })

    test('reads like Ghidra: library calls by name with their prototypes, strings inline', () => {
        expect(result.name).toBe('main')
        expect(result.text).toContain('undefined8 main(void)')
        expect(result.text).toContain('printf("Enter password: ");')
        expect(result.text).toContain('pcVar2 = fgets(acStack_98,0x80,stdin);')
        expect(result.text).toContain('sVar4 = strcspn(acStack_98,"\\n");')
        expect(result.text).toContain('iVar1 = strcmp(acStack_98,"hunter2");')
        expect(result.text).toContain('puts("Correct!");')
        // __stack_chk_fail is declared _Noreturn: its call is a statement, not an assignment.
        expect(result.text).toContain('/* WARNING: Subroutine does not return */')
        expect(result.text).toMatch(/\n {4}__stack_chk_fail\(\);/)
    })

    test('is the native reference, which Ghidra’s own console agrees with', async () => {
        const reference = await readFile(fixture('reference/crackme-main.c'), 'utf8')
        expect(`${result.text}\n`).toBe(reference)
        expect(result.lines.map((line) => line.text).join('\n')).toBe(result.text)
    })

    test('has the native driver’s tokens, kinds, addresses and targets', async () => {
        type RawToken = [text: string, kind: number, address: number, target: number]
        const kinds = ['keyword', 'comment', 'type', 'function', 'variable', 'constant', 'syntax']
        const native = JSON.parse(await readFile(fixture('reference/crackme-main.json'), 'utf8')) as {
            address: number
            name: string
            lines: RawToken[][]
        }
        expect(result.address).toBe(native.address)
        expect(result.name).toBe(native.name)
        const tokens = (line: (typeof result.lines)[number]) =>
            line.tokens.map((token) => [line.text.slice(token.start, token.end), token.kind, token.address, token.target])
        const nativeTokens = (line: RawToken[]) =>
            line
                .filter(([text]) => text.trim() !== '')
                .map(([text, kind, address, target]) => [
                    text.trim(),
                    kinds[kind],
                    address < 0 ? undefined : address,
                    target < 0 ? undefined : target
                ])
        expect(result.lines.map(tokens)).toEqual(native.lines.map(nativeTokens))
    })

    test('gives each token of an instruction an address inside the function', () => {
        let withAddress = 0
        for (const line of result.lines) {
            for (const token of line.tokens) {
                if (token.address === undefined) continue
                withAddress += 1
                expect(token.address).toBeGreaterThanOrEqual(MAIN)
                expect(token.address).toBeLessThan(MAIN_END)
            }
            const addresses = [...new Set(line.tokens.flatMap((token) => token.address ?? []))].sort((a, b) => a - b)
            expect(line.addresses).toEqual(addresses)
        }
        expect(withAddress).toBeGreaterThan(30)
    })

    test('maps the password check to its compare and branch', () => {
        expect(lineOf(result, 'if (iVar1 == 0) {').addresses).toEqual([TEST_EAX, JNE_WRONG])
        expect(lineOf(result, '/* WARNING: Subroutine does not return */').addresses).toEqual([0x12b4])
        // Declarations come from no instruction.
        expect(lineOf(result, 'int iVar1;').addresses).toEqual([])
    })

    test('links names to what they name, and classes tokens as Ghidra highlights them', () => {
        const symbol = (name: string) => image.functions.find((fn) => fn.name === name)!.address
        const tokenOf = (line: string, text: string) => {
            const found = lineOf(result, line)
            const token = found.tokens.find((candidate) => found.text.slice(candidate.start, candidate.end) === text)
            if (!token) throw new Error(`no token ${text} in ${line}`)
            return token
        }
        expect(tokenOf('undefined8 main(void)', 'main')).toEqual(expect.objectContaining({ kind: 'function', target: MAIN }))
        expect(tokenOf('undefined8 main(void)', 'undefined8').kind).toBe('type')
        expect(tokenOf('undefined8 main(void)', 'void').kind).toBe('keyword')
        expect(tokenOf('puts("Correct!");', 'puts')).toEqual(
            expect.objectContaining({ kind: 'function', address: 0x128a, target: symbol('puts') })
        )
        expect(tokenOf('puts("Correct!");', '"Correct!"')).toEqual(
            expect.objectContaining({ kind: 'constant', target: 0x201f })
        )
        expect(tokenOf('pcVar2 = fgets(acStack_98,0x80,stdin);', 'stdin')).toEqual(
            expect.objectContaining({ kind: 'variable', target: 0x4010 })
        )
        expect(tokenOf('pcVar2 = fgets(acStack_98,0x80,stdin);', '0x80').kind).toBe('constant')
        expect(tokenOf('pcVar2 = fgets(acStack_98,0x80,stdin);', 'pcVar2').target).toBeUndefined()
        // Ghidra prints a comment a word at a time, so it can wrap.
        const comment = lineOf(result, '/* WARNING: Subroutine does not return */')
        expect(comment.tokens.map((token) => comment.text.slice(token.start, token.end))).toEqual(
            '/* WARNING: Subroutine does not return */'.split(' ')
        )
        expect(new Set(comment.tokens.map((token) => token.kind))).toEqual(new Set(['comment']))
    })
})

test('prints the names it is given for a stripped program, invented ones included', async () => {
    const elf = await fixtureImage('crackme-stripped')
    // As the editor's Listing names crackme-stripped's functions; the PLT stubs
    // keep the names of their relocations' symbols (elf.functions).
    const invented = [
        { address: 0x1100, name: '_start' },
        { address: 0x1130, name: 'FUN_calm_lynx' },
        { address: 0x1160, name: 'FUN_shy_newt' },
        { address: 0x11a0, name: 'FUN_quiet_heron' },
        { address: 0x11e0, name: 'FUN_bold_crane' },
        { address: MAIN, name: 'FUN_brave_otter' }
    ]
    const program = decompiler.openProgram({ ...elf, functions: [...invented, ...elf.functions] })
    try {
        const main = decompiled(program.decompile(MAIN))
        expect(main.name).toBe('FUN_brave_otter')
        expect(main.lines[0].text).toBe('undefined8 FUN_brave_otter(void)')
        for (const call of ['printf(', 'fgets(', 'strcspn(', 'strcmp(', 'puts(', '__stack_chk_fail('])
            expect(main.text).toContain(call)
        expect(main.text).toContain('puts("Correct!");')

        // __do_global_dtors_aux calls deregister_tm_clones, named FUN_calm_lynx here.
        const dtors = decompiled(program.decompile(0x11a0))
        expect(dtors.lines[0].text).toBe('void FUN_quiet_heron(void)')
        const call = lineOf(dtors, 'FUN_calm_lynx();')
        expect(call.tokens[0]).toEqual(expect.objectContaining({ kind: 'function', target: 0x1130, address: 0x11c7 }))
        const finalize = dtors.lines.find((line) => line.text.includes('__cxa_finalize('))!
        expect(finalize.tokens.find((token) => token.kind === 'function')?.target).toBe(0x1090)
    } finally {
        program.dispose()
    }
})

test('reports an address where no function starts as an error, not a throw', async () => {
    const image = await fixtureImage('crackme-stripped')
    const program = decompiler.openProgram(image)
    try {
        expect(program.decompile(0x11e9)).toEqual({ address: 0x11e9, error: 'No function starts at 0x11e9' })
        expect(() => program.decompile(-1)).toThrow(RangeError)
        expect(() => program.decompile(0x11e9, { maxInstructions: 1.5 })).toThrow(RangeError)
    } finally {
        program.dispose()
    }
})

test('gives up with an error past maxInstructions', async () => {
    const program = decompiler.openProgram(await fixtureImage('crackme'))
    try {
        const result = program.decompile(MAIN, { maxInstructions: 10 })
        expect(result).toEqual({ address: MAIN, error: expect.stringMatching(/instructions/i) })
        expect('text' in program.decompile(MAIN)).toBe(true)
    } finally {
        program.dispose()
    }
})

test('a disposed program refuses to decompile', async () => {
    const program = decompiler.openProgram(await fixtureImage('crackme'))
    program.dispose()
    expect(() => program.decompile(MAIN)).toThrow('The program was disposed')
})

test('reports each declaration that cannot be read, once, and applies the others', async () => {
    const image = await fixtureImage('crackme')
    const prototypes = `${image.prototypes}
size_tt strlen(const char *s);
int broken(;
_Noreturn int not_a_function;
int empty();
`
    const program = decompiler.openProgram({ ...image, prototypes })
    try {
        expect(program.warnings).toEqual([
            'line 18: Syntax error after `size_tt` (in `size_tt strlen(const char *s);`)',
            'line 19: Syntax error after `;` (in `int broken(;`)',
            'line 20: Only a function can be marked noreturn (in `_Noreturn int not_a_function;`)',
            'line 21: An empty parameter list cannot be read: write (void) or the parameters (in `int empty();`)'
        ])
        const result = decompiled(program.decompile(MAIN))
        expect(result.text).toContain('iVar1 = strcmp(acStack_98,"hunter2");')
        expect(result.text).toContain('puts("Correct!");')
    } finally {
        program.dispose()
    }
})

test('frees what it decompiles: 500 decompilations leave the heap as they found it', async () => {
    const image = await fixtureImage('crackme')
    // The first decompilations fill caches (the SLEIGH translator's, the
    // string manager's), about 120 KB; after them the heap stays within a few
    // hundred bytes (measured: under 4 KB over 500 decompilations).
    const bound = 64 * 1024
    const program = decompiler.openProgram(image)
    try {
        for (let i = 0; i < 100; i++) program.decompile(MAIN)
        const before = decompiler.memory().used
        for (let i = 0; i < 500; i++) program.decompile(MAIN)
        expect(decompiler.memory().used - before).toBeLessThan(bound)
    } finally {
        program.dispose()
    }
    // Opening and disposing programs gives their memory back too.
    const open = () => {
        const another = decompiler.openProgram(image)
        another.decompile(MAIN)
        another.dispose()
    }
    open()
    const before = decompiler.memory().used
    for (let i = 0; i < 20; i++) open()
    expect(decompiler.memory().used - before).toBeLessThan(bound)
})
