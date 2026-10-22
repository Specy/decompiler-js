// C++ names: functions and data given in their namespaces, with the mangled
// symbol prototypes bind by. The FactCheck-like fixture (fixtures/elf/
// factcheck.cpp, g++ -O0) is named by its table of demangled names
// (factcheck.names.tsv), as a caller with a demangler names it.
import { readFile } from 'node:fs/promises'
import createDecompilerModule from '../src/wasm/decompiler.mjs'
import {
    loadDecompiler,
    type DecompiledFunction,
    type DecompiledLine,
    type Decompiler,
    type NamespaceStrategy,
    type ProgramImage,
    type ProgramPointer
} from '../src'
import { inNamespace, readNameTable } from './helpers/cpp-names'
import { readElfImage, type ElfImage } from './helpers/elf-image'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

// Symbols of factcheck (nm), each a function the tests decompile or call.
const M_CHECK_LEN =
    '_ZNKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12_M_check_lenEmPKc'
const PUSH_BACK = '_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE9push_backERKS5_'
const STRING_APPEND = '_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEpLERKS4_'
const DESTROY_RANGE = '_ZSt8_DestroyIPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEvT_S7_'
const M_ERASE =
    '_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_iESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE8_M_eraseEPSt13_Rb_tree_nodeIS8_E'
const BOSS_DELETING_DESTRUCTOR = '_ZN4game4BossD0Ev'
const GREETING = '_ZL8greetingv'
const THROW_LENGTH_ERROR = '_ZSt20__throw_length_errorPKc' // a PLT stub, and its GOT slot
const RUNTIME_ERROR_TYPEINFO = '_ZTISt13runtime_error'

let decompiler: Decompiler
/** factcheck as its ELF symbols name it: mangled. */
let elf: ElfImage
let prototypes: string
/** factcheck, its names in their namespaces. */
let image: ProgramImage

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    elf = readElfImage(new Uint8Array(await readFile(fixture('elf/factcheck'))))
    prototypes = await readFile(new URL('../prototypes/glibc.h', import.meta.url), 'utf8')
    const names = readNameTable(await readFile(fixture('elf/factcheck.names.tsv'), 'utf8'))
    image = {
        regions: elf.regions,
        functions: elf.functions.map((fn) => inNamespace(fn, names)),
        data: elf.data.map((entry) => inNamespace(entry, names)),
        strings: elf.strings,
        pointers: elf.pointers.map((pointer) => inNamespace(pointer, names)),
        prototypes
    }
})

afterAll(() => decompiler.dispose())

type Decompiled = Extract<DecompiledFunction, { text: string }>

function decompiled(result: DecompiledFunction): Decompiled {
    if ('error' in result) throw new Error(`decompiling 0x${result.address.toString(16)} failed: ${result.error}`)
    return result
}

/** The entry of the function with that ELF symbol. */
const addressOf = (symbol: string) => {
    const fn = elf.functions.find((candidate) => candidate.name === symbol)
    if (!fn) throw new Error(`no function ${symbol}`)
    return fn.address
}

/** Each function decompiled in a program of the image, opened with the options. */
function decompile(
    symbols: string[],
    changed: Partial<ProgramImage> = {},
    namespaces?: NamespaceStrategy
): Decompiled[] {
    const program = decompiler.openProgram({ ...image, ...changed }, namespaces && { namespaces })
    try {
        expect(program.warnings).toEqual([])
        return symbols.map((symbol) => decompiled(program.decompile(addressOf(symbol))))
    } finally {
        program.dispose()
    }
}

/** The tokens of the lines, each as its text. */
const tokenTexts = (lines: DecompiledLine[]) =>
    lines.flatMap((line) => line.tokens.map((token) => line.text.slice(token.start, token.end)))

/** The first token reading text. */
function tokenOf(result: Decompiled, text: string) {
    for (const line of result.lines)
        for (const token of line.tokens) if (line.text.slice(token.start, token.end) === text) return token
    throw new Error(`no token ${text}`)
}

describe('functions and data in their namespaces', () => {
    let main: Decompiled
    let checkLen: Decompiled

    beforeAll(() => {
        ;[main, checkLen] = decompile(['main', M_CHECK_LEN])
    })

    test('every function decompiles by its address', () => {
        const program = decompiler.openProgram(image)
        try {
            const failed = image.functions.flatMap((fn) => {
                const result = program.decompile(fn.address)
                return 'error' in result ? [`${fn.name}: ${result.error}`] : []
            })
            expect(image.functions.filter((fn) => fn.namespace).length).toBeGreaterThan(300)
            expect(failed).toEqual([])
        } finally {
            program.dispose()
        }
        // Every call of main names its function.
        expect(main.text).not.toContain('func_0x')
    })

    test('a call from the global scope prints its function’s namespaces', () => {
        expect(main.text).toMatch(/\n {4}std::string::operator\+=\(auStack_\w+,auStack_\w+\);\n/)
        expect(main.text).toContain('  std::vector<std::string>::push_back(auStack_198,auStack_128);')
        expect(main.text).toContain('(int *)std::map<std::string, int>::operator[](auStack_158,auStack_128);')
        expect(main.text).toContain('  game::Player::Player(auStack_68,auStack_88);')
        expect(main.text).toContain("main::'lambda'(int)::operator()(&piStack_1e8,uVar2);")
        // Data too: a typeinfo, an object of the standard library.
        expect(main.text).toContain('__cxa_throw(thrown_exception,std::runtime_error::typeinfo,')
        expect(main.text).toContain(
            'std::operator<<<char, std::char_traits<char>, std::allocator<char>>(std::cout,uVar5)'
        )
    })

    test('a call inside a class drops the namespaces the two share', () => {
        expect(checkLen.lines[0].text).toBe(
            'ulong std::vector<std::string>::_M_check_len(undefined8 param_1,ulong param_2,char *param_3)'
        )
        expect(checkLen.text).toContain('uStack_30 = size(uStack_40);')
        expect(checkLen.text).toContain('uVar4 = max_size(uStack_40);')
        const [deleting] = decompile([BOSS_DELETING_DESTRUCTOR])
        expect(deleting.text).toBe(
            [
                'void game::Boss::~Boss(void *param_1)',
                '',
                '{',
                '  ~Boss(param_1);',
                '  operator delete(param_1,0x30);',
                '  return;',
                '}'
            ].join('\n')
        )
    })

    test('a namespace is never split at a `::` of its own', () => {
        // std::vector<std::string> is one class, not std::vector<std and string>.
        expect(main.text).not.toMatch(/(^|[^:\w])string>/m)
        const [destroy] = decompile([DESTROY_RANGE])
        expect(destroy.lines[0].text).toBe('void std::_Destroy<std::string*>(long param_1,long param_2)')
        expect(destroy.text).toContain('    _Destroy<std::string>(uStack_20);')
    })

    test('names with spaces, quotes and parentheses print as given', () => {
        const [greeting, deleting] = decompile([GREETING, BOSS_DELETING_DESTRUCTOR])
        expect(greeting.text).toContain("if ((char)greeting()::text::guard == '\\0') {")
        expect(greeting.text).toContain('  return greeting()::text;')
        expect(deleting.text).toContain('  operator delete(param_1,0x30);')
        expect(main.text).toContain(
            "std::sort<__gnu_cxx::__normal_iterator<int*, std::vector<int>>, main::'lambda'(int, int)>"
        )
    })

    test('a qualified name is one token, of its name’s kind and with its target', () => {
        const append = tokenOf(main, 'std::string::operator+=')
        expect(append).toEqual(expect.objectContaining({ kind: 'function', target: addressOf(STRING_APPEND) }))
        expect(append.address).toBeGreaterThan(addressOf('main'))
        expect(tokenOf(main, 'std::vector<std::string>::push_back')).toEqual(
            expect.objectContaining({ kind: 'function', target: addressOf(PUSH_BACK) })
        )
        expect(tokenOf(main, 'std::runtime_error::typeinfo')).toEqual(
            expect.objectContaining({
                kind: 'variable',
                target: elf.data.find((entry) => entry.name === RUNTIME_ERROR_TYPEINFO)!.address
            })
        )
        // A declaration's name targets its own entry.
        expect(tokenOf(checkLen, 'std::vector<std::string>::_M_check_len')).toEqual({
            start: 'ulong '.length,
            end: 'ulong std::vector<std::string>::_M_check_len'.length,
            kind: 'function',
            target: addressOf(M_CHECK_LEN)
        })
        // No namespace or `::` is left a token of its own.
        for (const result of [main, checkLen]) {
            const texts = tokenTexts(result.lines)
            expect(texts).not.toContain('std')
            expect(texts).not.toContain('string')
            expect(texts).not.toContain('::')
        }
    })

    test('a name broken across lines is a token per line, its namespaces without a target', () => {
        // `void std::`, the class on a line of its own, then `::_M_erase(…)`.
        const [erase] = decompile([M_ERASE])
        const [first, second, third] = erase.lines
        expect(first.text).toBe('void std::')
        expect(first.tokens[1]).toEqual({ start: 5, end: 10, kind: 'function' })
        expect(second.text.trim()).toMatch(/^_Rb_tree<std::string, .*>$/)
        expect(second.tokens).toEqual([
            { start: second.text.indexOf('_Rb_tree'), end: second.text.length, kind: 'function' }
        ])
        expect(third.text).toMatch(/^ +::_M_erase\(undefined8 param_1,long param_2\)$/)
        expect(tokenOf(erase, '::_M_erase')).toEqual(
            expect.objectContaining({ kind: 'function', target: addressOf(M_ERASE) })
        )
    })
})

describe('prototypes and pointers bind by symbol', () => {
    test('glibc.h’s C++ declarations bind to their functions by mangled symbol', () => {
        // _Noreturn void _ZSt20__throw_length_errorPKc(const char *what);
        const [checkLen] = decompile([M_CHECK_LEN])
        expect(checkLen.text).toMatch(
            /\/\* WARNING: Subroutine does not return \*\/\n {6}__throw_length_error\(param_3\);/
        )
        expect(checkLen.lines[0].text).toContain('char *param_3')
        // void _ZdlPvm(void *ptr, size_t size);
        const [deleting] = decompile([BOSS_DELETING_DESTRUCTOR])
        expect(deleting.lines[0].text).toBe('void game::Boss::~Boss(void *param_1)')
    })

    test('without its symbol, a function in a namespace binds nothing', () => {
        const unbound = (fn: ProgramImage['functions'][number]) =>
            fn.symbol === THROW_LENGTH_ERROR ? { address: fn.address, name: fn.name, namespace: fn.namespace } : fn
        const [checkLen] = decompile([M_CHECK_LEN], { functions: image.functions.map(unbound) })
        expect(checkLen.text).toMatch(/\n {4}__throw_length_error\(param_3\);/)
        expect(checkLen.text).not.toMatch(/does not return \*\/\n {6}__throw_length_error/)
        expect(checkLen.lines[0].text).not.toContain('char *param_3')
    })

    describe('a pointer names its function', () => {
        // The PLT stub of __throw_length_error jumps through its GOT slot; each
        // case gives the slot a pointer of its own.
        const slot = () => elf.pointers.find((pointer) => pointer.name === THROW_LENGTH_ERROR)!
        const callOfStub = (pointer: ProgramPointer) => {
            const pointers = image.pointers!.filter((other) => other.address !== slot().address)
            const [stub] = decompile([THROW_LENGTH_ERROR], { pointers: [...pointers, pointer] })
            const call = stub.lines.find((line) => line.text.endsWith(';') && !line.text.includes('return'))!
            const name = call.tokens[0]
            return { text: call.text.trim(), target: name.target, kind: name.kind }
        }

        test('by its symbol, whatever the pointer’s name', () => {
            expect(callOfStub({ address: slot().address, name: 'elsewhere', symbol: THROW_LENGTH_ERROR })).toEqual({
                text: '__throw_length_error(what);',
                kind: 'function',
                target: addressOf(THROW_LENGTH_ERROR)
            })
        })

        test('by its name and namespace, without a symbol', () => {
            expect(callOfStub({ address: slot().address, name: '__throw_length_error', namespace: ['std'] })).toEqual({
                text: '__throw_length_error(what);',
                kind: 'function',
                target: addressOf(THROW_LENGTH_ERROR)
            })
        })

        test('else as a function known only by the pointer, in its namespace', () => {
            // `__throw_length_error` in the global scope is not std's: a
            // function of its own, which the stub's own name hides, so it
            // prints as the global scope's.
            expect(callOfStub({ address: slot().address, name: '__throw_length_error' })).toEqual({
                text: '::__throw_length_error();',
                kind: 'function',
                target: slot().address
            })
            expect(
                callOfStub({
                    address: slot().address,
                    name: 'throw_it',
                    namespace: ['library', 'errors'],
                    symbol: '_ZN7library6errors8throw_itEv'
                })
            ).toEqual({ text: 'library::errors::throw_it();', kind: 'function', target: slot().address })
        })
    })

    test('a declaration of a variable binds by the data’s symbol', async () => {
        const crackme = readElfImage(new Uint8Array(await readFile(fixture('elf/crackme'))))
        const fgets = (data: ProgramImage['data'], extra: string) => {
            const program = decompiler.openProgram({ ...crackme, data, prototypes: `${prototypes}\n${extra}` })
            try {
                const main = decompiled(program.decompile(0x11e9))
                return main.lines.find((line) => line.text.includes('fgets('))!.text.trim()
            } finally {
                program.dispose()
            }
        }
        const renamed = (symbol?: string) =>
            crackme.data.map((entry) =>
                entry.name === 'stdin'
                    ? { ...entry, name: 'input', namespace: ['io'], ...(symbol && { symbol }) }
                    : entry
            )
        expect(fgets(renamed(), '')).toBe('pcVar2 = fgets(acStack_98,0x80,io::input);')
        // A char where the program reads 8 bytes: Ghidra prints the read as
        // the first bytes of the global, `_input`.
        expect(fgets(renamed('stream'), 'extern char stream;')).toBe('pcVar2 = fgets(acStack_98,0x80,_input);')
        expect(fgets(renamed('stream'), 'extern char input;')).toBe('pcVar2 = fgets(acStack_98,0x80,io::input);')
    })
})

describe('namespace strategies', () => {
    test('all: every name in full, unless it is in the scope where it is used', () => {
        const [main, checkLen] = decompile(['main', M_CHECK_LEN], {}, 'all')
        expect(checkLen.lines[0].text).toBe(
            'ulong std::vector<std::string>::_M_check_len(undefined8 param_1,ulong param_2,char *param_3)'
        )
        expect(checkLen.text).toContain('uStack_30 = std::vector<std::string>::size(uStack_40);')
        expect(checkLen.text).toContain('      std::__throw_length_error(param_3);')
        expect(main.text).toContain('  std::vector<std::string>::push_back(auStack_198,auStack_128);')
    })

    test('none: no name qualified, not even a declaration’s', () => {
        const [main, checkLen] = decompile(['main', M_CHECK_LEN], {}, 'none')
        expect(checkLen.lines[0].text).toBe('ulong _M_check_len(undefined8 param_1,ulong param_2,char *param_3)')
        expect(checkLen.text).toContain('uStack_30 = size(uStack_40);')
        expect(main.text).toContain('  push_back(auStack_198,auStack_128);')
        expect(main.text).toContain('__cxa_throw(thrown_exception,typeinfo,')
        // Template arguments are a name's own text.
        expect(main.text).toContain('  basic_string<std::allocator<char>>(auStack_108,"1",auStack_158);')
        expect(main.text).not.toMatch(/std::(string|vector<std::string>)::/)
    })

    test('minimal by default, and nothing else taken', () => {
        const [byDefault] = decompile([M_CHECK_LEN])
        const [minimal] = decompile([M_CHECK_LEN], {}, 'minimal')
        expect(minimal).toEqual(byDefault)
        expect(() => decompiler.openProgram(image, { namespaces: 'some' as NamespaceStrategy })).toThrow(
            'Not a namespace strategy: some'
        )
    })
})

test('is the native reference, which Ghidra’s own console agrees with', async () => {
    type RawToken = [text: string, kind: number, address: number, target: number]
    const [main] = decompile(['main'])
    expect(`${main.text}\n`).toBe(await readFile(fixture('reference/factcheck-main.c'), 'utf8'))
    // The driver's tokens keep each namespace apart; the links are the same.
    const native = JSON.parse(await readFile(fixture('reference/factcheck-main.json'), 'utf8')) as {
        lines: RawToken[][]
    }
    expect(
        main.lines.map((line) =>
            line.tokens.flatMap((token) => (token.target === undefined ? [] : [[token.address, token.target]]))
        )
    ).toEqual(
        native.lines.map((line) =>
            line.flatMap(([, , address, target]) => (target < 0 ? [] : [[address < 0 ? undefined : address, target]]))
        )
    )
})

describe('images naming no namespace', () => {
    test('mangled names print as given, and glibc.h binds to them by name', () => {
        const program = decompiler.openProgram({ ...elf, prototypes })
        try {
            expect(program.warnings).toEqual([])
            const checkLen = decompiled(program.decompile(addressOf(M_CHECK_LEN)))
            expect(checkLen.text).toMatch(
                /\/\* WARNING: Subroutine does not return \*\/\n {6}_ZSt20__throw_length_errorPKc\(param_3\);/
            )
            const main = decompiled(program.decompile(addressOf('main')))
            expect(main.text).toContain(`${STRING_APPEND}(auStack_128,auStack_c8);`)
            expect(main.text).not.toContain('std::')
            const failed = elf.functions.filter((fn) => 'error' in program.decompile(fn.address))
            expect(failed).toEqual([])
        } finally {
            program.dispose()
        }
    })

    test('an image in the binary form of version 2, without namespaces or symbols, still opens', async () => {
        // The wasm's own entry points, given crackme's image as version 2
        // wrote it: each name without a path or a symbol.
        const module = await createDecompilerModule()
        const directory = '/specs/x86-64'
        module.FS.mkdirTree(directory)
        for (const file of ['x86.ldefs', 'x86-64.pspec', 'x86-64-gcc.cspec', 'x86-64.sla'])
            module.FS.writeFile(`${directory}/${file}`, new Uint8Array(await readFile(new URL(file, specsUrl))))
        const cString = (text: string) => module.stringToNewUTF8(text)
        expect(module._decompiler_add_specs(cString(directory))).toBe(1)

        const crackme = readElfImage(new Uint8Array(await readFile(fixture('elf/crackme'))))
        const bytes = encodeVersion2({ ...crackme, prototypes: await readFile(fixture('prototypes.h'), 'utf8') })
        const pointer = module._malloc(bytes.length)
        module.HEAPU8.set(bytes, pointer)
        const language = cString('x86:LE:64:default:gcc')
        const program = module._program_open(pointer, bytes.length, language, cString('minimal'))
        module._free(pointer)
        expect(module.UTF8ToString(module._decompiler_error())).toBe('')
        expect(program).not.toBe(0)
        const json = module._program_decompile(program, 0x11e9, 0, 0)
        const main = JSON.parse(module.UTF8ToString(json)) as { lines: [string, number, number, number][][] }
        const text = main.lines.map((line) => line.map(([piece]) => piece).join('')).join('\n')
        expect(`${text}\n`).toBe(await readFile(fixture('reference/crackme-main.c'), 'utf8'))
        module._program_close(program)
    })
})

describe('many namespaces', () => {
    const BASE = 0x10000

    /** One function per path, each a `ret`, a byte apart from BASE, named
     * `f` in its namespace. */
    function retsIn(paths: string[][]): ProgramImage {
        return {
            regions: [
                { address: BASE, bytes: new Uint8Array(paths.length).fill(0xc3), readOnly: true, executable: true }
            ],
            functions: paths.map((namespace, index) => ({ address: BASE + index, name: 'f', namespace })),
            data: [],
            strings: [],
            prototypes: ''
        }
    }

    /** The functions of the image that fail to decompile, with why. */
    function failures(image: ProgramImage, addresses = image.functions.map((fn) => fn.address)) {
        const program = decompiler.openProgram(image)
        try {
            return addresses.flatMap((address) => {
                const result = program.decompile(address)
                return 'error' in result ? [`0x${address.toString(16)}: ${result.error}`] : []
            })
        } finally {
            program.dispose()
        }
    }

    test('two namespaces of libstdc++ that Ghidra’s name hashes gave alike symbol ids', () => {
        // Ghidra numbers a scope's symbols from the low 16 bits of its id, and
        // the ids it hashes from these two paths share them: their first
        // functions had one id, which the second one's local scope then
        // clashed with ("Duplicate scope id").
        const image = retsIn([
            ['__cxxabiv1', '__fundamental_type_info'],
            ['std', 'hash<std::error_code>']
        ])
        expect(failures(image)).toEqual([])
    })

    test('every function of 3,000 namespaces decompiles', () => {
        // Classes of 3 to 24 letters, each in one of 30 namespaces, with two
        // functions each. Ghidra's hashes of names this varied shared their
        // low 16 bits about once in 65,536 pairs, so for about 70 pairs here.
        let seed = 0x2545f491
        const random = (below: number) => {
            seed = (Math.imul(seed, 1103515245) + 12345) >>> 0
            return (seed >>> 8) % below
        }
        const word = () =>
            Array.from({ length: 3 + random(22) }, () => 'abcdefghijklmnopqrstuvwxyz_'[random(27)]).join('')
        const namespaces = Array.from({ length: 30 }, word)
        const paths = Array.from({ length: 3000 }, () => [namespaces[random(30)], word()])
        expect(new Set(paths.map((path) => path.join('::'))).size).toBeGreaterThan(2990)
        expect(failures(retsIn([...paths, ...paths]))).toEqual([])
    })

    test('namespaces 65,536 apart, past the 16 bits of an id Ghidra keeps', () => {
        // Numbered in order, the first three namespaces and the three 65,536
        // later have ids alike in their low 16 bits; their functions are
        // decompiled, each the first in its namespace.
        const count = 0x10000 + 3
        const image = retsIn(Array.from({ length: count }, (_, index) => [`n${index}`]))
        const sampled = image.functions.filter((_, index) => index % 0x10000 < 3).map((fn) => fn.address)
        expect(sampled).toHaveLength(6)
        expect(failures(image, sampled)).toEqual([])
    })
})

describe('a namespace owns its entries’ bytes up to the next entry', () => {
    // ns::value at 0x1018, 16 bytes, runs past the start of target at
    // 0x1020; main reads the value's bytes 4 to 7 and calls target.
    const MAIN = 0x1000
    const VALUE = 0x1018
    const TARGET = 0x1020
    const code = new Uint8Array(0x30).fill(0x90)
    code.set([0x8b, 0x3d, ...le32(VALUE + 4 - (MAIN + 6))], 0) // mov edi, [rip + …]
    code.set([0xe8, ...le32(TARGET - (MAIN + 11))], 6) // call target
    code.set([0xc3], 11) // ret
    code.set([0xc3], TARGET - MAIN) // target: ret

    function le32(value: number) {
        return [value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff, (value >>> 24) & 0xff]
    }

    test('a function inside a namespaced datum’s bytes is found by its address', () => {
        const program = decompiler.openProgram({
            regions: [{ address: MAIN, bytes: code, readOnly: false, executable: true }],
            functions: [
                { address: MAIN, name: 'main' },
                { address: TARGET, name: 'target' }
            ],
            data: [{ address: VALUE, name: 'value', size: 16, namespace: ['ns'] }],
            strings: [],
            prototypes: ''
        })
        try {
            // The value's bytes before target's are still the namespace's.
            const main = decompiled(program.decompile(MAIN))
            expect(main.lines.map((line) => line.text)).toContain('  target(ns::value._4_4_);')
            expect(tokenOf(main, 'target')).toEqual(expect.objectContaining({ kind: 'function', target: TARGET }))
            expect(decompiled(program.decompile(TARGET)).lines[0].text).toBe('void target(void)')
        } finally {
            program.dispose()
        }
    })
})

test('openProgram throws on an empty namespace', () => {
    const functions = [{ ...image.functions[0], namespace: ['std', ''] }]
    expect(() => decompiler.openProgram({ ...image, functions })).toThrow(/An empty namespace/)
})

/** The image in program_open's binary form as version 2 read it: names alone. */
function encodeVersion2(image: ProgramImage): Uint8Array {
    const chunks: Uint8Array[] = []
    const utf8 = new TextEncoder()
    const u32 = (value: number) => {
        const bytes = new Uint8Array(4)
        new DataView(bytes.buffer).setUint32(0, value, true)
        chunks.push(bytes)
    }
    const u64 = (value: number) => {
        const bytes = new Uint8Array(8)
        new DataView(bytes.buffer).setBigUint64(0, BigInt(value), true)
        chunks.push(bytes)
    }
    const text = (value: string) => {
        const bytes = utf8.encode(value)
        u32(bytes.length)
        chunks.push(bytes)
    }
    chunks.push(utf8.encode('DIMG'))
    u32(2)
    u32(image.regions.length)
    for (const region of image.regions) {
        u64(region.address)
        u32((region.readOnly ? 1 : 0) | (region.executable ? 2 : 0))
        u32(region.bytes.length)
        chunks.push(region.bytes)
    }
    u32(image.functions.length)
    for (const fn of image.functions) {
        u64(fn.address)
        text(fn.name)
    }
    u32(image.data.length)
    for (const entry of image.data) {
        u64(entry.address)
        u64(entry.size ?? 0)
        text(entry.name)
    }
    u32(image.strings.length)
    for (const string of image.strings) {
        u64(string.address)
        u64(string.length)
    }
    const pointers = image.pointers ?? []
    u32(pointers.length)
    for (const pointer of pointers) {
        u64(pointer.address)
        text(pointer.name)
    }
    text(image.prototypes)
    const all = new Uint8Array(chunks.reduce((size, chunk) => size + chunk.length, 0))
    let at = 0
    for (const chunk of chunks) {
        all.set(chunk, at)
        at += chunk.length
    }
    return all
}
