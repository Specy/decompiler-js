// prototypes/glibc.h: every declaration is read and bound, every _Noreturn
// mark ends its caller's flow, and crackme's main reads with it as with the
// few declarations the native reference was made with.
import { readFile } from 'node:fs/promises'
import { loadDecompiler, type DecompiledFunction, type Decompiler, type ProgramFunction } from '../src'
import { readElfImage } from './helpers/elf-image'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)
const glibcUrl = new URL('../prototypes/glibc.h', import.meta.url)

interface Declared {
    name: string
    noreturn: boolean
    /** The parameters' names in order, `...` for the variable arguments. */
    parameters: string[]
}

/** The text between the parenthesis at `open` and the one closing it. */
function parenthesised(text: string, open: number): string {
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '(') depth++
        else if (text[i] === ')' && --depth === 0) return text.slice(open + 1, i)
    }
    throw new Error(`unbalanced parentheses in ${text}`)
}

/** The names of a parameter list's parameters: a function pointer's after
 * its `(*`, any other's its last word. */
function parameterNames(list: string): string[] {
    const parameters: string[] = []
    let depth = 0
    let start = 0
    for (let i = 0; i <= list.length; i++) {
        const c = list[i]
        if (c === '(') depth++
        else if (c === ')') depth--
        else if ((c === ',' && depth === 0) || i === list.length) {
            const parameter = list.slice(start, i).trim()
            start = i + 1
            if (parameter === 'void' || parameter === '') continue
            if (parameter === '...') parameters.push('...')
            else parameters.push(parameter.match(/\(\s*\*\s*(\w+)\s*\)/)?.[1] ?? parameter.match(/(\w+)\s*$/)![1])
        }
    }
    return parameters
}

/** The functions a C declarations file declares, read with regular
 * expressions, which this file's plain style allows. */
function declaredFunctions(source: string): Declared[] {
    const code = source.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/.*$/gm, ' ')
    const declarations: string[] = []
    let depth = 0
    let current = ''
    for (const c of code) {
        if (c === '{') depth++
        if (c === '}') depth--
        if (c === ';' && depth === 0) {
            declarations.push(current.trim().replace(/\s+/g, ' '))
            current = ''
        } else current += c
    }
    return declarations
        .filter((text) => text.includes('(') && !text.startsWith('typedef') && !text.startsWith('extern'))
        .map((text) => {
            const name = text.match(/(\w+)\s*\(/)!
            return {
                name: name[1],
                noreturn: text.startsWith('_Noreturn'),
                parameters: parameterNames(parenthesised(text, name.index! + name[0].length - 1))
            }
        })
}

/** The function's signature as the decompiler printed it, on one line. */
function signature(result: DecompiledFunction): string {
    if ('error' in result) throw new Error(`decompiling 0x${result.address.toString(16)} failed: ${result.error}`)
    const body = result.lines.findIndex((line) => line.text === '{')
    return result.lines
        .slice(0, body)
        .map((line) => line.text.trim())
        .join('')
}

let decompiler: Decompiler
let glibc: string
let declared: Declared[]

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    glibc = await readFile(glibcUrl, 'utf8')
    declared = declaredFunctions(glibc)
})

afterAll(() => decompiler.dispose())

test('declares the functions programs commonly call, without repeating one', () => {
    const names = declared.map((fn) => fn.name)
    expect(new Set(names).size).toBe(names.length)
    expect(names.length).toBeGreaterThan(500)
    for (const name of [
        'printf', '__printf_chk', '__fprintf_chk', '__sprintf_chk', '__snprintf_chk', '__isoc99_scanf',
        '__isoc99_sscanf', '__isoc99_fscanf', '__isoc23_scanf', 'puts', 'putchar', 'getchar', 'fgets', 'gets',
        'fputs', 'fwrite', 'fread', 'fopen', 'fclose', 'fflush', 'setvbuf', 'setbuf', 'perror', 'getline',
        'strlen', 'strcpy', 'strcmp', 'strncmp', 'strchr', 'strstr', 'strdup', 'memcpy', 'memset', 'memcmp',
        '__memcpy_chk', '__strcpy_chk', 'malloc', 'calloc', 'realloc', 'free', 'atoi', 'atol', 'strtol',
        'strtoul', 'rand', 'srand', 'exit', 'abort', 'qsort', 'getenv', 'system', 'read', 'write', 'close',
        'sleep', 'usleep', 'alarm', 'getpid', 'getuid', 'execve', 'dup2', 'open', 'openat', '__ctype_b_loc',
        'toupper', 'tolower', 'isalpha', 'isdigit', 'time', 'clock', 'localtime', 'strftime', 'signal', 'raise',
        '_setjmp', 'longjmp', 'socket', 'connect', '__libc_start_main', '__stack_chk_fail', '__assert_fail',
        '__cxa_atexit', '__cxa_finalize', '__cxa_throw', '__cxa_begin_catch', '__cxa_end_catch',
        '__gxx_personality_v0', '_Unwind_Resume', '_Znwm', '_ZdlPv', 'pthread_create', 'pthread_join',
        'pthread_exit', 'syscall'
    ])
        expect(names, name).toContain(name)
})

test('marks the functions that never return', () => {
    const noreturn = declared.filter((fn) => fn.noreturn).map((fn) => fn.name)
    for (const name of [
        'exit', '_exit', '_Exit', 'abort', '__stack_chk_fail', '__assert_fail', 'longjmp', 'siglongjmp', 'err',
        'errx', 'verr', 'verrx', 'pthread_exit', '__cxa_throw', '_Unwind_Resume', '__cxa_rethrow',
        '__fortify_fail', '__chk_fail', '__libc_start_main'
    ])
        expect(noreturn, name).toContain(name)
    for (const name of ['printf', 'puts', 'error', 'setjmp', 'malloc']) expect(noreturn, name).not.toContain(name)
})

describe('a program calling every function it declares', () => {
    // Each declared function is a stub, a `ret`, 16 bytes apart; each
    // function marked noreturn, and puts, has a caller doing `call` the
    // function, `call after_call`, `ret`.
    const STUBS = 0x10000
    const CALLERS = 0x20000
    const AFTER_CALL = 0x30000
    let program: ReturnType<Decompiler['openProgram']>
    let functions: ProgramFunction[]
    let callers: Declared[]

    beforeAll(() => {
        const bytes = new Uint8Array(AFTER_CALL + 16 - STUBS).fill(0xcc)
        const view = new DataView(bytes.buffer)
        const call = (at: number, target: number) => {
            bytes[at - STUBS] = 0xe8
            view.setInt32(at - STUBS + 1, target - (at + 5), true)
        }
        functions = declared.map((fn, i) => ({ address: STUBS + 16 * i, name: fn.name }))
        for (const fn of functions) bytes[fn.address - STUBS] = 0xc3
        bytes[AFTER_CALL - STUBS] = 0xc3
        functions.push({ address: AFTER_CALL, name: 'after_call' })
        callers = [...declared.filter((fn) => fn.noreturn), declared.find((fn) => fn.name === 'puts')!]
        callers.forEach((fn, i) => {
            const at = CALLERS + 16 * i
            call(at, STUBS + 16 * declared.indexOf(fn))
            call(at + 5, AFTER_CALL)
            bytes[at + 10 - STUBS] = 0xc3
            functions.push({ address: at, name: `call_${fn.name}` })
        })
        program = decompiler.openProgram({
            regions: [{ address: STUBS, bytes, readOnly: true, executable: true }],
            functions,
            data: [],
            strings: [],
            prototypes: glibc
        })
    })

    afterAll(() => program.dispose())

    const addressOf = (name: string) => functions.find((fn) => fn.name === name)!.address

    test('reads every declaration without a warning', () => {
        expect(program.warnings).toEqual([])
    })

    test('binds every declaration to the function of its name, parameters and all', () => {
        for (const fn of declared) {
            const printed = signature(program.decompile(addressOf(fn.name)))
            const open = printed.indexOf(`${fn.name}(`)
            expect(open, printed).toBeGreaterThanOrEqual(0)
            expect(parameterNames(parenthesised(printed, open + fn.name.length)), printed).toEqual(fn.parameters)
        }
        expect(signature(program.decompile(addressOf('qsort')))).toBe(
            'void qsort(void *base,size_t nmemb,size_t size,int (*compar)(void *,void *))'
        )
        expect(signature(program.decompile(addressOf('syscall')))).toBe('long syscall(long number,...)')
    })

    test('ends the flow after a call to each function marked noreturn, and only those', () => {
        for (const fn of callers) {
            const result = program.decompile(addressOf(`call_${fn.name}`))
            if ('error' in result) throw new Error(`call_${fn.name}: ${result.error}`)
            expect(result.text.includes('after_call();'), result.text).toBe(!fn.noreturn)
            expect(result.text.includes('/* WARNING: Subroutine does not return */'), result.text).toBe(fn.noreturn)
        }
    })
})

test("crackme's main reads as with the reference's own declarations", async () => {
    const image = {
        ...readElfImage(new Uint8Array(await readFile(fixture('elf/crackme')))),
        prototypes: glibc
    }
    const program = decompiler.openProgram(image)
    try {
        expect(program.warnings).toEqual([])
        const result = program.decompile(image.functions.find((fn) => fn.name === 'main')!.address)
        if ('error' in result) throw new Error(result.error)
        expect(`${result.text}\n`).toBe(await readFile(fixture('reference/crackme-main.c'), 'utf8'))
    } finally {
        program.dispose()
    }
})

test('decompiles a function storing through a pointer to an incomplete structure', async () => {
    // glibc.h declares DIR as glibc's headers do, `typedef struct __dirstream DIR;`, never
    // defining it: a structure of size 0. Static glibc's opendir fills the DIR it allocates,
    // which divided by that size (patches/0003-split-incomplete-structure.patch) and trapped.
    const image = readElfImage(new Uint8Array(await readFile(fixture('elf/hello-atexit-static'))))
    const opendir = image.functions.find((fn) => fn.name === 'opendir')!.address
    // The symbol table names the address __opendir first; the prototype binds opendir.
    const program = decompiler.openProgram({
        ...image,
        functions: [{ address: opendir, name: 'opendir' }, ...image.functions],
        prototypes: glibc
    })
    try {
        const result = program.decompile(opendir)
        if ('error' in result) throw new Error(result.error)
        expect(result.lines[0].text).toBe('DIR * opendir(char *name)')
        expect(result.text).toMatch(/\bpDVar\d+ = malloc\(/)
    } finally {
        program.dispose()
    }
})
