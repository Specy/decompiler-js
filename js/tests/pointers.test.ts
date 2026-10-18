// Calls through a GOT slot: a pointer of the program image names the function
// a slot holds the address of, whose value the file does not hold, so the call
// reads as a call of that function with its prototype. crackme's GOT
// (readelf -r): __libc_start_main at 0x3fd8, __gmon_start__ at 0x3fe8 and
// __cxa_finalize at 0x3ff8, GLOB_DAT relocations; its .plt.got stub at 0x1090
// jumps through __cxa_finalize's.
import { readFile } from 'node:fs/promises'
import { loadDecompiler, type DecompiledFunction, type Decompiler, type ProgramImage } from '../src'
import { readElfImage } from './helpers/elf-image'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)
const fixture = (name: string) => new URL(`./fixtures/${name}`, import.meta.url)

const MAIN = 0x11e9
const LIBC_START_MAIN_SLOT = 0x3fd8
const GMON_START_SLOT = 0x3fe8
const CXA_FINALIZE_STUB = 0x1090

let decompiler: Decompiler
let image: ProgramImage

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    image = {
        ...readElfImage(new Uint8Array(await readFile(fixture('elf/crackme')))),
        prototypes: await readFile(new URL('../prototypes/glibc.h', import.meta.url), 'utf8')
    }
})

afterAll(() => decompiler.dispose())

function decompiled(result: DecompiledFunction) {
    if ('error' in result) throw new Error(`decompiling 0x${result.address.toString(16)} failed: ${result.error}`)
    return result
}

/** The named function of the image, decompiled in a program of its own. */
function decompile(name: string, changed: Partial<ProgramImage> = {}) {
    const program = decompiler.openProgram({ ...image, ...changed })
    try {
        expect(program.warnings).toEqual([])
        return decompiled(program.decompile(image.functions.find((fn) => fn.name === name)!.address))
    } finally {
        program.dispose()
    }
}

/** The first token reading text on the first line holding it. */
function tokenOf(result: ReturnType<typeof decompiled>, text: string) {
    for (const line of result.lines)
        for (const token of line.tokens) if (line.text.slice(token.start, token.end) === text) return token
    throw new Error(`no token ${text}`)
}

test('the image names a slot of each GOT function, as the editor’s Listing does', () => {
    expect(image.pointers).toContainEqual({ address: LIBC_START_MAIN_SLOT, name: '__libc_start_main' })
    expect(image.data).toContainEqual({ address: LIBC_START_MAIN_SLOT, name: '__libc_start_main@got', size: 8 })
    // stdin is copied into .bss (R_X86_64_COPY): an object, not a slot.
    expect(image.pointers?.some((pointer) => pointer.name === 'stdin')).toBe(false)
})

test('a call through a slot is a call of its function, with that function’s prototype', () => {
    const start = decompile('_start')
    // glibc.h: _Noreturn int __libc_start_main(int (*main)(int, char **, char **), int argc,
    // char **argv, void (*init)(void), void (*fini)(void), void (*rtld_fini)(void), void *stack_end).
    expect(start.text).toContain('__libc_start_main(main,')
    expect(start.text).toContain('(char **)&stack0x00000008')
    expect(start.text).toContain('/* WARNING: Subroutine does not return */')
    // A call that does not return ends _start: the hlt after it is gone.
    expect(start.text).not.toContain('while( true )')
    // The function exists only by name, so its name targets the slot.
    expect(tokenOf(start, '__libc_start_main')).toEqual(
        expect.objectContaining({ kind: 'function', target: LIBC_START_MAIN_SLOT })
    )
    // A function passed as a pointer is a global variable to Ghidra's highlighting.
    expect(tokenOf(start, 'main')).toEqual(expect.objectContaining({ kind: 'variable', target: MAIN }))
})

test('is the native reference, which Ghidra’s own console agrees with', async () => {
    type RawToken = [text: string, kind: number, address: number, target: number]
    const kinds = ['keyword', 'comment', 'type', 'function', 'variable', 'constant', 'syntax']
    // The reference was made with the tests' few declarations, which leave
    // __libc_start_main without a prototype.
    const start = decompile('_start', { prototypes: await readFile(fixture('prototypes.h'), 'utf8') })
    expect(`${start.text}\n`).toBe(await readFile(fixture('reference/crackme-_start.c'), 'utf8'))
    const native = JSON.parse(await readFile(fixture('reference/crackme-_start.json'), 'utf8')) as {
        lines: RawToken[][]
    }
    expect(start.lines.map((line) => line.tokens.map((token) => [token.kind, token.address, token.target]))).toEqual(
        native.lines.map((line) =>
            line
                .filter(([text]) => text.trim() !== '')
                .map(([, kind, address, target]) => [
                    kinds[kind],
                    address < 0 ? undefined : address,
                    target < 0 ? undefined : target
                ])
        )
    )
})

test('without the pointer, the call goes through a code pointer the decompiler names', () => {
    const start = decompile('_start', {
        pointers: [],
        data: image.data.filter((entry) => !entry.name.endsWith('@got'))
    })
    expect(start.text).toContain('(*pcRam0000000000003fd8)(main,')
    expect(start.text).not.toContain('__libc_start_main')
})

test('the slot read as a value is the data name at its address, else the decompiler’s own', () => {
    const init = decompile('_init')
    expect(init.text).toContain('if (__gmon_start__@got != (code *)0x0) {')
    expect(init.text).toContain('    __gmon_start__();')
    expect(tokenOf(init, '__gmon_start__@got')).toEqual(
        expect.objectContaining({ kind: 'variable', target: GMON_START_SLOT })
    )
    expect(tokenOf(init, '__gmon_start__')).toEqual(
        expect.objectContaining({ kind: 'function', target: GMON_START_SLOT })
    )

    const unnamed = decompile('_init', { data: image.data.filter((entry) => !entry.name.endsWith('@got')) })
    expect(unnamed.text).toContain('if (pcRam0000000000003fe8 != (code *)0x0) {')
    expect(unnamed.text).toContain('    __gmon_start__();')
})

test('a slot in read-only memory holds its function, not the bytes the file has there', () => {
    // As after RELRO: the loader fills the GOT and then makes it read-only.
    const regions = image.regions.map((region) =>
        region.address <= LIBC_START_MAIN_SLOT && LIBC_START_MAIN_SLOT < region.address + region.bytes.length
            ? { ...region, readOnly: true }
            : region
    )
    expect(decompile('_start', { regions }).text).toContain('__libc_start_main(main,')
})

test('a pointer to one of the image’s functions calls that function', () => {
    // The .plt.got stub is named __cxa_finalize too, so the slot's function is the stub.
    const dtors = decompile('__do_global_dtors_aux')
    expect(dtors.text).toContain('if (__cxa_finalize@got != (code *)0x0) {')
    expect(tokenOf(dtors, '__cxa_finalize')).toEqual(
        expect.objectContaining({ kind: 'function', target: CXA_FINALIZE_STUB })
    )
})

test('a function known only through a pointer has no code to decompile', () => {
    const program = decompiler.openProgram(image)
    try {
        // The image ends at 0x4020 (.bss), where the first function without
        // code, __libc_start_main, is placed; it is no function of the image.
        expect(program.decompile(0x4020)).toEqual({ address: 0x4020, error: 'No function starts at 0x4020' })
    } finally {
        program.dispose()
    }
})
