// Annotations: the ids the decompiler reports for a function's local
// variables and parameters, names given by those ids after the analysis, and
// comments placed through Ghidra's comment database, on crackme (C) and the
// FactCheck-like factcheck (C++, named in its namespaces).
import { readFile } from 'node:fs/promises'
import {
    loadDecompiler,
    type DecompiledFunction,
    type DecompiledLine,
    type DecompiledProgram,
    type DecompileOptions,
    type Decompiler,
    type LocalName,
    type ProgramImage
} from '../src'
import { inNamespace, readNameTable } from './helpers/cpp-names'
import { readElfImage } from './helpers/elf-image'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

// crackme's main (objdump -d) and the instructions the comments go at.
const MAIN = 0x11e9
const ENDBR64 = 0x11e9
const PUSH_RBP = 0x11ed
const TERMINATOR = 0x125b // movb $0x0,-0x90(%rbp,%rax,1)
const CALL_STRCMP = 0x1277
const TEST_EAX = 0x127c
const CALL_PUTS_CORRECT = 0x128a
const CALL_STACK_CHK_FAIL = 0x12b4
// crackme's deregister_tm_clones, and the block the decompiler removes from it.
const DEREGISTER_TM_CLONES = 0x1130
const UNREACHABLE = 0x1143

type Decompiled = Extract<DecompiledFunction, { text: string }>

let decompiler: Decompiler
let crackme: ProgramImage
let factcheck: ProgramImage

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    crackme = {
        ...readElfImage(new Uint8Array(await readFile(fixture('elf/crackme')))),
        prototypes: await readFile(fixture('prototypes.h'), 'utf8')
    }
    const elf = readElfImage(new Uint8Array(await readFile(fixture('elf/factcheck'))))
    const names = readNameTable(await readFile(fixture('elf/factcheck.names.tsv'), 'utf8'))
    factcheck = {
        regions: elf.regions,
        functions: elf.functions.map((fn) => inNamespace(fn, names)),
        data: elf.data.map((entry) => inNamespace(entry, names)),
        strings: elf.strings,
        pointers: elf.pointers.map((pointer) => inNamespace(pointer, names)),
        prototypes: await readFile(new URL('../prototypes/glibc.h', import.meta.url), 'utf8')
    }
})

afterAll(() => decompiler.dispose())

function decompiled(result: DecompiledFunction): Decompiled {
    if ('error' in result) throw new Error(`decompiling 0x${result.address.toString(16)} failed: ${result.error}`)
    return result
}

/** Runs use with a program of the image, disposed after. */
function withProgram<T>(image: ProgramImage, use: (program: DecompiledProgram) => T): T {
    const program = decompiler.openProgram(image)
    try {
        return use(program)
    } finally {
        program.dispose()
    }
}

/** The function at the address, decompiled in a program of its own. */
function decompileOnce(image: ProgramImage, address: number, options?: DecompileOptions): Decompiled {
    return withProgram(image, (program) => decompiled(program.decompile(address, options)))
}

/** The entry of every function of the image, each address once. */
const entries = (image: ProgramImage) => [...new Set(image.functions.map((fn) => fn.address))]

const functionNamed = (image: ProgramImage, name: string) => {
    const fn = image.functions.find((candidate) => candidate.name === name)
    if (!fn) throw new Error(`no function ${name}`)
    return fn.address
}

/** The index of the line whose text, less its indent, is the given one. */
function indexOf(result: Decompiled, text: string): number {
    const index = result.lines.findIndex((line) => line.text.trim() === text)
    if (index < 0) throw new Error(`no line reads ${text} in\n${result.text}`)
    return index
}

const lineOf = (result: Decompiled, text: string) => result.lines[indexOf(result, text)]

const tokenText = (line: DecompiledLine, token: DecompiledLine['tokens'][number]) =>
    line.text.slice(token.start, token.end)

/** Every token, with its text. */
const allTokens = (result: Decompiled) =>
    result.lines.flatMap((line) => line.tokens.map((token) => ({ ...token, text: tokenText(line, token) })))

const idOf = (result: Decompiled, name: string) => {
    const variable = result.variables.find((candidate) => candidate.name === name)
    if (!variable) throw new Error(`no variable ${name} in ${result.name}`)
    return variable.id
}

/** The text the result would have with each variable token's name replaced
 * by the one names gives its id, each as long as the name it replaces, so
 * that no line breaks elsewhere. */
function renamedText(result: Decompiled, names: ReadonlyMap<string, string>): string {
    const nameOf = new Map(result.variables.map((variable) => [variable.id, variable.name]))
    return result.lines
        .map((line) => {
            let text = line.text
            for (const token of line.tokens) {
                const name = token.variable === undefined ? undefined : names.get(token.variable)
                if (name === undefined) continue
                const old = nameOf.get(token.variable!)!
                expect(name.length).toBe(old.length)
                // A size mismatch prints the name with an underscore before it (`_uStack_60`).
                const piece = text.slice(token.start, token.end).replace(old, name)
                text = text.slice(0, token.start) + piece + text.slice(token.end)
            }
            return text
        })
        .join('\n')
}

/** A name as long as the given one, told apart by the index: `n_____7`. */
const sameLength = (name: string, index: number) => `n${String(index).padStart(name.length - 1, '_')}`

describe('variable ids', () => {
    test("name crackme's main's locals by their storage", () => {
        const result = decompileOnce(crackme, MAIN)
        expect(result.variables).toEqual([
            { id: 'register:0x0:4@+0x8e', kind: 'local', name: 'iVar1', storage: 'EAX' },
            { id: 'register:0x0:8@+0x48', kind: 'local', name: 'pcVar2', storage: 'RAX' },
            { id: 'register:0x0:8@+0x52', kind: 'local', name: 'uVar3', storage: 'RAX' },
            { id: 'register:0x0:8@+0x6d', kind: 'local', name: 'sVar4', storage: 'RAX' },
            { id: 'register:0x110:8@-0x1', kind: 'local', name: 'in_FS_OFFSET', storage: 'FS_OFFSET' },
            { id: 'stack:-0x98', kind: 'local', name: 'acStack_98', storage: 'Stack[-0x98]' },
            { id: 'stack:-0x10', kind: 'local', name: 'lStack_10', storage: 'Stack[-0x10]' }
        ])
        const line = lineOf(result, 'iVar1 = strcmp(acStack_98,"hunter2");')
        expect(line.tokens.map((token) => [tokenText(line, token), token.variable])).toEqual([
            ['iVar1', 'register:0x0:4@+0x8e'],
            ['=', undefined],
            ['strcmp', undefined],
            ['(', undefined],
            ['acStack_98', 'stack:-0x98'],
            [',', undefined],
            ['"hunter2"', undefined],
            [')', undefined],
            [';', undefined]
        ])
        // A global is a target, not a variable.
        const stdin = allTokens(result).find((token) => token.text === 'stdin')!
        expect(stdin.variable).toBeUndefined()
        expect(stdin.target).toBe(0x4010)
    })

    test('name parameters by their register and the entry less one, read or not', () => {
        const start = decompileOnce(crackme, functionNamed(crackme, '_start'))
        expect(start.lines[0].text).toBe('void _start(undefined8 param_1,undefined8 param_2,undefined8 param_3)')
        expect(start.variables).toEqual([
            { id: 'register:0x38:8@-0x1', kind: 'parameter', name: 'param_1', storage: 'RDI' },
            { id: 'register:0x30:8@-0x1', kind: 'parameter', name: 'param_2', storage: 'RSI' },
            { id: 'register:0x10:8@-0x1', kind: 'parameter', name: 'param_3', storage: 'RDX' },
            { id: 'stack:0x0:8@-0x1', kind: 'local', name: 'unaff_retaddr', storage: 'Stack[0x0]' },
            { id: 'stack:-0x8', kind: 'local', name: 'auStack_8', storage: 'Stack[-0x8]' }
        ])
    })

    test('name a temporary by its hash, and a value split across registers by its pieces', () => {
        const main = decompileOnce(factcheck, functionNamed(factcheck, 'main'))
        expect(main.variables).toContainEqual({
            id: 'hash:0x3f276f9932@+0x27c',
            kind: 'local',
            name: 'iVar1',
            storage: 'HASH:3f276f9932'
        })
        const insert = decompileOnce(factcheck, 0x8a38) // _M_get_insert_unique_pos
        expect(insert.variables).toContainEqual({
            id: 'join(register:0x10:8+register:0x0:8):16@+0x19b',
            kind: 'local',
            name: 'auVar3',
            storage: 'RDX:RAX'
        })
    })

    test('are on every token naming a variable, each variable named by a token', () => {
        for (const [image, address] of [
            [crackme, MAIN],
            [factcheck, functionNamed(factcheck, 'main')]
        ] as const) {
            const result = decompileOnce(image, address)
            const names = new Map(result.variables.map((variable) => [variable.id, variable.name]))
            expect(names.size).toBe(result.variables.length)
            const named = new Set<string>()
            for (const token of allTokens(result)) {
                if (token.variable === undefined) continue
                expect(token.kind).toBe('variable')
                expect([names.get(token.variable), `_${names.get(token.variable)}`]).toContain(token.text)
                named.add(token.variable)
            }
            expect([...named].sort()).toEqual([...names.keys()].sort())
        }
    })

    test('are the native driver’s', async () => {
        type RawToken = [text: string, kind: number, address: number, target: number, variable?: number]
        for (const [image, name, file] of [
            [crackme, 'main', 'crackme-main'],
            [crackme, '_start', 'crackme-_start'],
            [factcheck, 'main', 'factcheck-main']
        ] as const) {
            const native = JSON.parse(await readFile(fixture(`reference/${file}.json`), 'utf8')) as {
                lines: RawToken[][]
                variables: Decompiled['variables']
            }
            const result = decompileOnce(image, functionNamed(image, name))
            expect(result.variables).toEqual(native.variables)
            const nativeIds = native.lines.map((line) =>
                line
                    .filter(([text]) => text.trim() !== '')
                    .flatMap(([, , , , variable]) =>
                        variable === undefined || variable < 0 ? [] : [native.variables[variable].id]
                    )
            )
            expect(result.lines.map((line) => line.tokens.flatMap((token) => token.variable ?? []))).toEqual(nativeIds)
        }
    })

    test('do not depend on where the program is placed', () => {
        const base = 0x5555_5555_4000
        const moved = <T extends { address: number }>(entries: T[] = []) =>
            entries.map((entry) => ({ ...entry, address: entry.address + base }))
        const placed: ProgramImage = {
            ...crackme,
            regions: moved(crackme.regions),
            functions: moved(crackme.functions),
            data: moved(crackme.data),
            strings: moved(crackme.strings),
            pointers: moved(crackme.pointers)
        }
        for (const address of entries(crackme)) {
            const there = decompiled(withProgram(placed, (program) => program.decompile(address + base)))
            expect(there.variables).toEqual(decompileOnce(crackme, address).variables)
        }
    })

    test('survive a change of the bytes outside the function', () => {
        const changed = crackme.regions.map((region) => {
            const bytes = region.bytes.slice()
            const at = (address: number) => address - region.address
            const holds = (address: number) => at(address) >= 0 && at(address) < bytes.length
            if (holds(0x201d)) bytes[at(0x201d)] = '3'.charCodeAt(0) // "hunter2" reads "hunter3"
            // deregister_tm_clones's first 16 bytes, nops.
            if (holds(DEREGISTER_TM_CLONES))
                bytes.fill(0x90, at(DEREGISTER_TM_CLONES), at(DEREGISTER_TM_CLONES) + 16)
            return { ...region, bytes }
        })
        const main = decompileOnce({ ...crackme, regions: changed }, MAIN)
        expect(main.text).toContain('strcmp(acStack_98,"hunter3")')
        expect(main.variables).toEqual(decompileOnce(crackme, MAIN).variables)
    })
})

describe('every function of crackme and factcheck', () => {
    const fixtures = () =>
        [
            ['crackme', crackme],
            ['factcheck', factcheck]
        ] as const
    /** Each fixture's functions, decompiled first to last in one program. */
    const forwards = new Map<string, Map<number, Decompiled>>()

    beforeAll(() => {
        for (const [name, image] of fixtures()) {
            forwards.set(
                name,
                withProgram(image, (program) => {
                    const results = new Map<number, Decompiled>()
                    for (const address of entries(image)) {
                        const result = program.decompile(address)
                        if (!('error' in result)) results.set(address, result)
                    }
                    return results
                })
            )
        }
    })

    test('have the same ids in another program, decompiled last to first', () => {
        for (const [name, image] of fixtures()) {
            const forward = forwards.get(name)!
            expect(forward.size).toBeGreaterThan(name === 'crackme' ? 10 : 300)
            withProgram(image, (program) => {
                for (const address of entries(image).reverse()) {
                    const result = forward.get(address)
                    if (!result) continue
                    expect(decompiled(program.decompile(address)).variables).toEqual(result.variables)
                }
            })
        }
    })

    test('take a name for every variable but a declared parameter, and nothing else changes', () => {
        for (const [name, image] of fixtures()) {
            let renamed = 0
            let locked = 0
            withProgram(image, (program) => {
                for (const [address, plain] of forwards.get(name)!) {
                    const locals: LocalName[] = plain.variables.map((variable, index) => ({
                        id: variable.id,
                        name: sameLength(variable.name, index)
                    }))
                    const applied = new Map(
                        plain.variables.flatMap((variable, index) =>
                            variable.locked ? [] : [[variable.id, locals[index].name] as const]
                        )
                    )
                    const named = decompiled(program.decompile(address, { locals }))
                    expect(named.text).toBe(renamedText(plain, applied))
                    expect(named.variables).toEqual(
                        plain.variables.map((variable) => ({
                            ...variable,
                            name: applied.get(variable.id) ?? variable.name
                        }))
                    )
                    // Every token as it was, kinds, addresses and targets included.
                    const shape = (result: Decompiled) =>
                        result.lines.map((line) => line.tokens.map(({ start, end, ...rest }) => [start, end, rest]))
                    expect(shape(named)).toEqual(shape(plain))
                    renamed += applied.size
                    locked += plain.variables.length - applied.size
                }
            })
            // As counted when the ids were researched: crackme's 9 locked are
            // the PLT stubs' declared parameters.
            if (name === 'crackme') expect([renamed, locked]).toEqual([17, 9])
            else expect(renamed).toBeGreaterThan(1000)
        }
    })
})

describe('naming variables', () => {
    test("names crackme's password and the result of strcmp, and no other name changes", async () => {
        const reference = await readFile(fixture('reference/crackme-main.c'), 'utf8')
        const result = decompileOnce(crackme, MAIN, {
            locals: [
                { id: 'stack:-0x98', name: 'password' },
                { id: 'register:0x0:4@+0x8e', name: 'result' }
            ]
        })
        expect(`${result.text}\n`).toBe(
            reference.replace(/\bacStack_98\b/g, 'password').replace(/\biVar1\b/g, 'result')
        )
        expect(result.text).toContain('  result = strcmp(password,"hunter2");\n    if (result == 0) {')
        expect(result.text).toContain('  char password [136];')
        expect(result.variables.find((variable) => variable.id === 'stack:-0x98')?.name).toBe('password')
        const line = lineOf(result, 'result = strcmp(password,"hunter2");')
        expect(line.tokens.flatMap((token) => token.variable ?? [])).toEqual(['register:0x0:4@+0x8e', 'stack:-0x98'])
    })

    test('names a parameter the code never reads, one it reads, a temporary and a join', () => {
        const start = decompileOnce(crackme, functionNamed(crackme, '_start'), {
            locals: [
                { id: 'register:0x38:8@-0x1', name: 'rtld_fini' },
                { id: 'register:0x10:8@-0x1', name: 'stack_end' }
            ]
        })
        expect(start.lines[0].text).toBe('void _start(undefined8 rtld_fini,undefined8 param_2,undefined8 stack_end)')
        expect(start.text).toContain(',stack_end,')

        const mainAddress = functionNamed(factcheck, 'main')
        const main = decompileOnce(factcheck, mainAddress)
        const parameter = main.variables.find((variable) => variable.kind === 'parameter')!
        expect(parameter).toEqual({ id: 'register:0x38:4@-0x1', kind: 'parameter', name: 'param_1', storage: 'EDI' })
        const names = new Map([
            [parameter.id, 'counts1'],
            ['hash:0x3f276f9932@+0x27c', 'argc0']
        ])
        const named = decompileOnce(factcheck, mainAddress, {
            locals: [...names].map(([id, name]) => ({ id, name }))
        })
        expect(named.text).toBe(renamedText(main, names))
        expect(named.lines[0].text).toBe('undefined8 main(int counts1)')
        expect(named.text).toContain('argc0 = aiStack_1fc[0];')

        const insert = decompileOnce(factcheck, 0x8a38)
        const join = new Map([['join(register:0x10:8+register:0x0:8):16@+0x19b', 'parts1']])
        const joined = decompileOnce(factcheck, 0x8a38, { locals: [...join].map(([id, name]) => ({ id, name })) })
        expect(joined.text).toBe(renamedText(insert, join))
        expect(joined.text).toContain('undefined1 parts1 [16];')
    })

    test('passes over an id that names no variable, silently', () => {
        const plain = decompileOnce(crackme, MAIN)
        const result = decompileOnce(crackme, MAIN, {
            locals: [
                { id: 'stack:-0x77', name: 'ghost' },
                { id: 'register:0x0:4@+0x8f', name: 'nearly' },
                { id: 'hash:0x1234@+0x10', name: 'hashed' },
                { id: 'join(register:0x10:8+register:0x0:8):16@+0x19b', name: 'joined' }
            ]
        })
        expect(result).toEqual(plain)
    })

    test('keeps the name a declaration gives a parameter, which it reports locked', () => {
        const puts = functionNamed(crackme, 'puts')
        const plain = decompileOnce(crackme, puts)
        expect(plain.lines[0].text).toBe('int puts(char *s)')
        expect(plain.variables[0]).toEqual({
            id: 'register:0x38:8@-0x1',
            kind: 'parameter',
            name: 's',
            storage: 'RDI',
            locked: true
        })
        expect(decompileOnce(crackme, puts, { locals: [{ id: 'register:0x38:8@-0x1', name: 'text' }] })).toEqual(plain)
    })

    test('makes each name unique in the function, the last of an id’s names winning', () => {
        const result = decompileOnce(crackme, MAIN, {
            locals: [
                { id: 'register:0x0:8@+0x52', name: 'status' },
                { id: 'register:0x0:8@+0x6d', name: 'status' },
                { id: 'stack:-0x10', name: 'pcVar2' },
                { id: 'stack:-0x98', name: 'buffer' },
                { id: 'stack:-0x98', name: 'password' }
            ]
        })
        expect(result.variables.map((variable) => variable.name)).toEqual([
            'iVar1',
            'pcVar2',
            'status',
            'status_00',
            'in_FS_OFFSET',
            'password',
            'pcVar2_00'
        ])
    })

    test('refuses a malformed id or an empty name', () => {
        withProgram(crackme, (program) => {
            const malformed = [
                '',
                'acStack_98',
                'stack:-98',
                'register:0x0:4@0x8e',
                'register:0x0:4',
                'hash:0x12',
                'join(register:0x10:8):8@+0x1'
            ]
            for (const id of malformed)
                expect(() => program.decompile(MAIN, { locals: [{ id, name: 'x' }] }), id).toThrow(RangeError)
            expect(() => program.decompile(MAIN, { locals: [{ id: 'stack:-0x98', name: '' }] })).toThrow(RangeError)
            expect(() => program.decompile(MAIN, { comments: [{ address: -1, text: 'x' }] })).toThrow(RangeError)
            expect(() => program.decompile(MAIN, { comments: [{ address: 0.5, text: 'x' }] })).toThrow(RangeError)
        })
    })
})

describe('comments', () => {
    /** The comment lines right above the line reading `statement`, nearest last. */
    function commentsAbove(result: Decompiled, statement: string): string[] {
        const lines: string[] = []
        for (let index = indexOf(result, statement) - 1; index >= 0; index--) {
            const text = result.lines[index].text.trim()
            if (!text.startsWith('/*') && !text.endsWith('*/')) break
            lines.unshift(text)
        }
        return lines
    }

    /** The lines of the caller's comment at the address, less their indent. */
    function commentAt(result: Decompiled, address: number): string[] {
        const first = result.lines.findIndex((line) =>
            line.tokens.some((token) => token.comment === 'user' && token.address === address)
        )
        if (first < 0) throw new Error(`no comment at 0x${address.toString(16)} in\n${result.text}`)
        const last = result.lines.findIndex((line, index) => index >= first && line.text.endsWith('*/'))
        return result.lines.slice(first, last + 1).map((line) => line.text.trim())
    }

    /** The text's lines, as an editor breaks it: at a line feed, a carriage
     * return, or both. */
    const lineTexts = (result: Decompiled) => result.text.split(/\r\n|\r|\n/)

    /** How many times the text ends a comment. */
    const closes = (result: Decompiled) => result.text.split('*/').length - 1

    test('go before the statement whose code holds their address', () => {
        const comments = [
            { address: PUSH_RBP, text: 'the frame' },
            { address: TERMINATOR, text: 'cut at the newline' },
            { address: CALL_STRCMP, text: 'the check' },
            { address: TEST_EAX, text: 'equal?' },
            { address: CALL_PUTS_CORRECT, text: 'the way in' }
        ]
        const result = decompileOnce(crackme, MAIN, { comments })
        expect(commentsAbove(result, 'lStack_10 = *(long *)(in_FS_OFFSET + 0x28);')).toEqual(['/* the frame */'])
        expect(commentsAbove(result, "acStack_98[sVar4] = '\\0';")).toEqual(['/* cut at the newline */'])
        expect(commentsAbove(result, 'iVar1 = strcmp(acStack_98,"hunter2");')).toEqual(['/* the check */'])
        expect(commentsAbove(result, 'if (iVar1 == 0) {')).toEqual(['/* equal? */'])
        expect(commentsAbove(result, 'puts("Correct!");')).toEqual(['/* the way in */'])
        // Without them, the text is the reference's.
        const plain = decompileOnce(crackme, MAIN)
        const added = new Set(comments.map((comment) => `/* ${comment.text} */`))
        expect(result.lines.filter((line) => !added.has(line.text.trim())).map((line) => line.text)).toEqual(
            plain.lines.map((line) => line.text)
        )
        // Each word is a token of the comment, carrying the comment's address.
        const line = lineOf(result, '/* cut at the newline */')
        expect(line.tokens.map((token) => [tokenText(line, token), token.kind, token.comment, token.address])).toEqual(
            ['/*', 'cut', 'at', 'the', 'newline', '*/'].map((word) => [word, 'comment', 'user', TERMINATOR])
        )
        expect(line.addresses).toEqual([TERMINATOR])
    })

    test('go above the decompiler’s warning at the same address, in the order given', () => {
        const result = decompileOnce(crackme, MAIN, {
            comments: [
                { address: CALL_STACK_CHK_FAIL, text: 'the canary was overwritten' },
                { address: CALL_STACK_CHK_FAIL, text: '' },
                { address: CALL_STACK_CHK_FAIL, text: 'second\nthird' }
            ]
        })
        expect(commentsAbove(result, '__stack_chk_fail();')).toEqual([
            '/* the canary was overwritten */',
            '/* second',
            'third */',
            '/* WARNING: Subroutine does not return */'
        ])
        const warning = lineOf(result, '/* WARNING: Subroutine does not return */')
        expect(new Set(warning.tokens.map((token) => token.comment))).toEqual(new Set(['warning']))
        const second = lineOf(result, 'third */')
        expect(new Set(second.tokens.map((token) => token.comment))).toEqual(new Set(['user']))
    })

    test('write */ as * /, so the comment does not end early', () => {
        const result = decompileOnce(crackme, MAIN, {
            comments: [{ address: CALL_STRCMP, text: 'not */ the end, nor **/ this' }],
            functionComment: 'a /* nested */ comment'
        })
        expect(commentsAbove(result, 'iVar1 = strcmp(acStack_98,"hunter2");')).toEqual([
            '/* not * / the end, nor ** / this */'
        ])
        expect(result.lines[0].text).toBe('/* a /* nested * / comment */')
    })

    test('take a carriage return for a line break, so *\\r/ does not end the comment either', () => {
        const result = decompileOnce(crackme, MAIN, {
            comments: [{ address: CALL_STRCMP, text: 'not *\r/ the end,\r\nnor *\r\n/ this' }],
            functionComment: 'a *\r/ b'
        })
        expect(lineTexts(result)).toEqual(result.lines.map((line) => line.text))
        expect(commentAt(result, CALL_STRCMP)).toEqual(['/* not *', '/ the end,', 'nor *', '/ this */'])
        expect(result.lines.slice(0, 2).map((line) => line.text)).toEqual(['/* a *', '   / b */'])
        expect(closes(result)).toBe(closes(decompileOnce(crackme, MAIN)) + 2)
    })

    test('write a {@ running over a line break as { @, so the text has a line for each line', () => {
        // Ghidra's printer reads `{@` up to the next `}` as one token, an
        // annotation, line breaks and all.
        const text = 'see {@link check\nfor details}'
        const result = decompileOnce(crackme, MAIN, {
            comments: [{ address: TEST_EAX, text }],
            functionComment: text
        })
        expect(lineTexts(result)).toEqual(result.lines.map((line) => line.text))
        expect(commentsAbove(result, 'if (iVar1 == 0) {')).toEqual(['/* see { @link check', 'for details} */'])
        expect(result.lines.slice(0, 2).map((line) => line.text)).toEqual([
            '/* see { @link check',
            '   for details} */'
        ])
        // One that closes on its line, or never opens one, is printed as it is.
        const kept = decompileOnce(crackme, MAIN, {
            comments: [{ address: TEST_EAX, text: 'see {@link check} and {@ this' }],
            functionComment: 'see {@link check}\nfor details'
        })
        expect(lineTexts(kept)).toEqual(kept.lines.map((line) => line.text))
        const line = lineOf(kept, '/* see {@link check} and {@ this */')
        expect(line.tokens.map((token) => tokenText(line, token))).toEqual([
            '/*',
            'see',
            '{@link check}',
            'and',
            '{@ this',
            '*/'
        ])
        expect(kept.lines.slice(0, 2).map((line) => line.text)).toEqual(['/* see {@link check}', '   for details */'])
    })

    test('keep a line of the text for each line, and end where they end, whatever they hold', () => {
        // Random texts of the pieces Ghidra's printer reads specially.
        let seed = 0x5eed1e55
        const random = (below: number) => {
            seed = (Math.imul(seed, 1103515245) + 12345) >>> 0
            return (seed >>> 8) % below
        }
        const pieces = ['{', '}', '@', '{@', '*', '/', '*/', '\r', '\n', '\r\n', ' ', '\t', 'a', 'bc']
        const randomText = () => Array.from({ length: random(16) }, () => pieces[random(pieces.length)]).join('')
        const addresses = [PUSH_RBP, TERMINATOR, CALL_STRCMP, TEST_EAX, CALL_PUTS_CORRECT]
        /** The text's characters but its spaces and line breaks. */
        const visible = (text: string) => text.replace(/\s/g, '')
        withProgram(crackme, (program) => {
            const plainCloses = closes(decompiled(program.decompile(MAIN)))
            for (let round = 0; round < 300; round++) {
                const comments = addresses.map((address) => ({ address, text: randomText() }))
                const functionComment = randomText()
                const result = decompiled(program.decompile(MAIN, { comments, functionComment }))
                const context = JSON.stringify({ comments, functionComment })
                expect(lineTexts(result), context).toEqual(result.lines.map((line) => line.text))
                // Each comment ends where it ends, with its words in order.
                const given = [...comments, { address: MAIN, text: functionComment }].filter(({ text }) => text)
                expect(closes(result), context).toBe(plainCloses + given.length)
                for (const { address, text } of given) {
                    const words = allTokens(result)
                        .filter((token) => token.comment === 'user' && token.address === address)
                        .map((token) => token.text)
                    expect(visible(words.join('')), context).toBe(`/*${visible(text)}*/`)
                }
            }
        })
    })

    test('that cannot be placed are listed above the function with unplacedComments, and left out without', () => {
        const comments = [
            { address: ENDBR64, text: 'no p-code here' },
            { address: CALL_STRCMP, text: 'placed' },
            { address: DEREGISTER_TM_CLONES, text: 'another function' }
        ]
        const listed = decompileOnce(crackme, MAIN, { comments, unplacedComments: true })
        // By address, as Ghidra sorts them.
        expect(listed.lines.slice(0, 5).map((line) => line.text)).toEqual([
            '/* Comments that could not be placed in the function body: */',
            ' /* another function */',
            ' /* no p-code here */',
            '',
            'undefined8 main(void)'
        ])
        expect(new Set(listed.lines[0].tokens.map((token) => token.comment))).toEqual(new Set(['warning']))
        expect(listed.lines[2].tokens.map((token) => [token.comment, token.address])).toContainEqual(['user', ENDBR64])
        expect(commentsAbove(listed, 'iVar1 = strcmp(acStack_98,"hunter2");')).toEqual(['/* placed */'])

        const left = decompileOnce(crackme, MAIN, { comments })
        expect(left.lines[0].text).toBe('undefined8 main(void)')
        expect(left.text).not.toContain('no p-code here')
        expect(left.text).not.toContain('another function')
        expect(left.lines.map((line) => line.text)).toEqual(listed.lines.slice(4).map((line) => line.text))

        // One in a block the decompiler removed as unreachable.
        const dead = decompileOnce(crackme, DEREGISTER_TM_CLONES, {
            comments: [{ address: UNREACHABLE, text: 'never runs' }],
            unplacedComments: true
        })
        expect(dead.text).toContain(
            '/* Comments that could not be placed in the function body: */\n /* never runs */'
        )
    })

    test('a function comment goes above the declaration, before the decompiler’s header warnings', () => {
        const plain = decompileOnce(crackme, DEREGISTER_TM_CLONES)
        expect(plain.lines[0].text).toBe('/* WARNING: Removing unreachable block (ram,0x00001143) */')
        const result = decompileOnce(crackme, DEREGISTER_TM_CLONES, {
            functionComment: 'Undoes register_tm_clones.\nNever called here.'
        })
        expect(result.lines.map((line) => line.text)).toEqual([
            '/* Undoes register_tm_clones.',
            '   Never called here. */',
            ...plain.lines.map((line) => line.text)
        ])
        for (const line of result.lines.slice(0, 2))
            for (const token of line.tokens)
                expect(token).toEqual(
                    expect.objectContaining({ kind: 'comment', comment: 'user', address: DEREGISTER_TM_CLONES })
                )
        expect(new Set(result.lines[2].tokens.map((token) => token.comment))).toEqual(new Set(['warning']))
        // And above crackme's main, which has no warning there, an empty
        // line after it as after Ghidra's.
        const main = decompileOnce(crackme, MAIN, { functionComment: 'Asks for the password.' })
        expect(main.lines.slice(0, 3).map((line) => line.text)).toEqual([
            '/* Asks for the password. */',
            '',
            'undefined8 main(void)'
        ])
    })
})

describe('annotations and later decompilations', () => {
    test('options that add nothing decompile as without them', () => {
        withProgram(crackme, (program) => {
            const plain = program.decompile(MAIN)
            expect(
                program.decompile(MAIN, { locals: [], comments: [], functionComment: '', unplacedComments: false })
            ).toEqual(plain)
            // An empty comment adds nothing either.
            expect(program.decompile(MAIN, { comments: [{ address: CALL_STRCMP, text: '' }] })).toEqual(plain)
        })
    })

    test('nothing is kept for the next decompilation', async () => {
        const reference = await readFile(fixture('reference/crackme-main.c'), 'utf8')
        withProgram(crackme, (program) => {
            const plain = decompiled(program.decompile(MAIN))
            expect(`${plain.text}\n`).toBe(reference)
            const annotated = decompiled(
                program.decompile(MAIN, {
                    locals: [{ id: 'stack:-0x98', name: 'password' }],
                    comments: [
                        { address: CALL_STRCMP, text: 'the check' },
                        { address: ENDBR64, text: 'unplaced' }
                    ],
                    functionComment: 'main',
                    unplacedComments: true
                })
            )
            expect(annotated.text).toContain('password')
            expect(program.decompile(MAIN)).toEqual(plain)
            // Another function's decompilation sees none of main's comments.
            const other = decompiled(program.decompile(DEREGISTER_TM_CLONES, { unplacedComments: true }))
            expect(other.text).not.toContain('the check')
        })
    })

    test('free what they hold: 300 annotated decompilations leave the heap as they found it', () => {
        const options: DecompileOptions = {
            locals: [
                { id: 'stack:-0x98', name: 'password' },
                { id: 'register:0x0:4@+0x8e', name: 'result' }
            ],
            comments: [
                { address: CALL_STRCMP, text: 'the check' },
                { address: ENDBR64, text: 'unplaced' }
            ],
            functionComment: 'Asks for the password.',
            unplacedComments: true
        }
        withProgram(crackme, (program) => {
            for (let i = 0; i < 50; i++) program.decompile(MAIN, options)
            const before = decompiler.memory().used
            for (let i = 0; i < 300; i++) program.decompile(MAIN, options)
            expect(decompiler.memory().used - before).toBeLessThan(64 * 1024)
        })
    })
})
