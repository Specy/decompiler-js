// Ghidra's decompiler in WebAssembly. The caller describes a program (its
// memory, the names of its functions and data, where its strings are, the C
// declarations of the library functions it calls) and asks for a function's
// C, as lines of tokens linked to the instructions they come from and to the
// functions and data they name.
import createDecompilerModule from './wasm/decompiler.mjs'
import type { DecompilerModule } from './wasm/decompiler.mjs'

export type Architecture = 'x86-64'

export interface LoadDecompilerOptions {
    architecture: Architecture
    /** decompiler.wasm; by default the copy beside the package's module. */
    wasmUrl?: string | URL
    /** The directory holding the architecture's processor description; by
     * default the package's specs/<architecture>/. */
    specsUrl?: string | URL
}

/** A run of the program's memory at its link-time address. */
export interface ProgramRegion {
    address: number
    bytes: Uint8Array
    readOnly: boolean
    executable: boolean
}

/** A function's entry point and the name the decompiler prints for it. At an
 * address named twice, the first name given is kept. A prototype binds by
 * name, so a PLT stub is named after its symbol (`strcmp`, not
 * `strcmp@plt`). */
export interface ProgramFunction {
    address: number
    name: string
}

/** A named object (a global variable, a table, a string's label) and its
 * length in bytes when known. At an address named twice, the first name
 * given is kept; a function's name wins over a data name at its address. */
export interface ProgramData {
    address: number
    name: string
    size?: number
}

/** A string of `length` bytes, its terminator included if it has one. A
 * string named by a `data` entry at its address is typed as a char array;
 * one without a name is left alone. */
export interface ProgramString {
    address: number
    length: number
}

export interface ProgramImage {
    regions: ProgramRegion[]
    functions: ProgramFunction[]
    data: ProgramData[]
    strings: ProgramString[]
    /**
     * C declarations, read by the decompiler's own C parser: prototypes,
     * bound to the functions of the same name (`strcmp` gets `int strcmp(const
     * char *s1, const char *s2);`); the typedefs, structs and enums they use;
     * and the types of data by name (`extern FILE *stdin;`). `_Noreturn`,
     * `[[noreturn]]` or `__attribute__((noreturn))` marks a function that does
     * not return. A declaration that cannot be read is skipped and reported in
     * the program's `warnings`. The package declares the glibc functions
     * programs commonly call in `@specy/decompiler/prototypes/glibc.h`, whose
     * text `@specy/decompiler/prototypes/glibc` exports as a string.
     */
    prototypes: string
}

/** What a token is, from Ghidra's syntax highlighting: parameters and
 * globals are variables; punctuation, operators, labels and field names are
 * syntax. */
export type TokenKind = 'keyword' | 'comment' | 'type' | 'function' | 'variable' | 'constant' | 'syntax'

const TOKEN_KINDS: readonly TokenKind[] = ['keyword', 'comment', 'type', 'function', 'variable', 'constant', 'syntax']

export interface DecompiledToken {
    /** Where it is in its line's text, in UTF-16 code units, end exclusive,
     * spaces around it left out. */
    start: number
    end: number
    kind: TokenKind
    /** The instruction whose operation the token belongs to; for a comment,
     * the address it is attached to. */
    address?: number
    /** What a name refers to: a function's entry, a global's or string's
     * start, a label's code. */
    target?: number
}

export interface DecompiledLine {
    text: string
    /** The tokens, in order; the spaces between them are in the text only. */
    tokens: DecompiledToken[]
    /** The tokens' addresses, sorted, each once. */
    addresses: number[]
}

export type DecompiledFunction =
    | { address: number; name: string; text: string; lines: DecompiledLine[] }
    | { address: number; error: string }

export interface DecompileOptions {
    /** The most instructions the decompiler follows before giving up with an
     * error ("main has more than 100000 instructions"); by default Ghidra's
     * 100,000. */
    maxInstructions?: number
    /** How long, in milliseconds, the decompiler works on the function before
     * giving up with an error ("main took longer than 30 seconds to
     * decompile"); by default Ghidra's 30,000. The decompiler looks at the
     * time between the steps of its analysis, so it stops a little after the
     * limit rather than on it. */
    timeLimitMs?: number
}

export interface DecompiledProgram {
    /** What went wrong opening the program without stopping it: each
     * declaration of the image's prototypes that could not be read or
     * applied, with its line ("line 3: Syntax error after `size_tt` (in
     * `size_tt strlen(const char *s);`)"), and any message Ghidra printed. */
    readonly warnings: readonly string[]
    /** The C of the function starting at the address, one of the image's
     * functions, or the reason there is none (no function starts there, it
     * has more instructions than `maxInstructions`, it took longer than
     * `timeLimitMs`, or the decompiler failed). Throws only when the program
     * was disposed or an argument is not a number it takes. The call blocks
     * the thread it runs on until it returns: run it in a worker. */
    decompile(address: number, options?: DecompileOptions): DecompiledFunction
    /** Frees the program's copy of the image and everything decompiled. */
    dispose(): void
}

export interface Decompiler {
    /** The Ghidra release the decompiler was built from and its commit. */
    readonly version: string
    readonly architecture: Architecture
    /** Builds the decompiler's view of a program, about 100 ms for a small
     * one. Throws when the image is malformed (overlapping regions, an
     * address that is not a non-negative safe integer). */
    openProgram(image: ProgramImage): DecompiledProgram
    /** The wasm module's memory, which grows as needed and never shrinks
     * (`size`), and how much of it the decompiler holds (`used`), in bytes:
     * what an open program and its decompiled functions cost. */
    memory(): { size: number; used: number }
    /** Disposes every program still open; the decompiler opens no more. */
    dispose(): void
}

interface ArchitectureFiles {
    language: string
    files: string[]
    defaultUrls: () => URL[]
}

// Each URL is written out in full, so bundlers that copy what
// `new URL('…', import.meta.url)` names find every file.
const architectures: Record<Architecture, ArchitectureFiles> = {
    'x86-64': {
        language: 'x86:LE:64:default:gcc',
        files: ['x86.ldefs', 'x86-64.pspec', 'x86-64-gcc.cspec', 'x86-64.sla'],
        defaultUrls: () => [
            new URL('./specs/x86-64/x86.ldefs', import.meta.url),
            new URL('./specs/x86-64/x86-64.pspec', import.meta.url),
            new URL('./specs/x86-64/x86-64-gcc.cspec', import.meta.url),
            new URL('./specs/x86-64/x86-64.sla', import.meta.url)
        ]
    }
}

const READ_ONLY = 1
const EXECUTABLE = 2
const IMAGE_VERSION = 1

const isAddress = (value: number) => Number.isSafeInteger(value) && value >= 0
/** A positive whole number the wasm takes as an unsigned 32-bit integer. */
const isCount = (value: number) => Number.isInteger(value) && value > 0 && value <= 0xffffffff

// Ghidra's defaults (DecompileOptions: SUGGESTED_MAX_INSTRUCTIONS,
// SUGGESTED_DECOMPILE_TIMEOUT_SECS).
const DEFAULT_MAX_INSTRUCTIONS = 100_000
const DEFAULT_TIME_LIMIT_MS = 30_000

async function fetchBytes(url: URL): Promise<Uint8Array> {
    if (url.protocol === 'file:') {
        const { readFile } = await import('node:fs/promises')
        return new Uint8Array(await readFile(url))
    }
    const response = await fetch(url)
    if (!response.ok) throw new Error(`Could not fetch ${url}: ${response.status} ${response.statusText}`)
    return new Uint8Array(await response.arrayBuffer())
}

/** A directory's URL, relative ones resolved against the page. */
function directoryUrl(url: string | URL): URL {
    const href = String(url)
    return new URL(href.endsWith('/') ? href : `${href}/`, globalThis.location?.href)
}

export async function loadDecompiler(options: LoadDecompilerOptions): Promise<Decompiler> {
    const architecture = architectures[options.architecture]
    if (!architecture) throw new Error(`Unknown architecture: ${options.architecture}`)
    const specUrls =
        options.specsUrl === undefined
            ? architecture.defaultUrls()
            : architecture.files.map((file) => new URL(file, directoryUrl(options.specsUrl!)))
    const wasmUrl = options.wasmUrl
    const [module, specs] = await Promise.all([
        createDecompilerModule(
            wasmUrl === undefined
                ? {}
                : { locateFile: (path) => (path.endsWith('.wasm') ? String(wasmUrl) : path) }
        ),
        Promise.all(specUrls.map(fetchBytes))
    ])

    const directory = `/specs/${options.architecture}`
    module.FS.mkdirTree(directory)
    architecture.files.forEach((file, i) => module.FS.writeFile(`${directory}/${file}`, specs[i]))
    withString(module, directory, (pointer) => {
        if (!module._decompiler_add_specs(pointer)) throw new Error(lastError(module))
    })

    const version = module.UTF8ToString(module._decompiler_version())
    const programs = new Set<{ dispose(): void }>()
    let disposed = false
    const live = () => {
        if (disposed) throw new Error('The decompiler was disposed')
    }

    return {
        version,
        architecture: options.architecture,
        openProgram(image) {
            live()
            const handle = buildProgram(module, image, architecture.language)
            const warningsJson = module._program_warnings(handle)
            let warnings: string[]
            try {
                warnings = JSON.parse(module.UTF8ToString(warningsJson))
            } finally {
                module._free(warningsJson)
            }
            let open = true
            const program: DecompiledProgram = {
                warnings,
                decompile(address, options) {
                    if (!open) throw new Error('The program was disposed')
                    if (!isAddress(address)) throw new RangeError(`Not an address: ${address}`)
                    const maxInstructions = options?.maxInstructions ?? DEFAULT_MAX_INSTRUCTIONS
                    if (!isCount(maxInstructions)) throw new RangeError(`Not an instruction count: ${maxInstructions}`)
                    const timeLimitMs = options?.timeLimitMs ?? DEFAULT_TIME_LIMIT_MS
                    if (!isCount(timeLimitMs)) throw new RangeError(`Not a time limit in milliseconds: ${timeLimitMs}`)
                    const json = module._program_decompile(handle, address, maxInstructions, timeLimitMs)
                    try {
                        return decodeFunction(module.UTF8ToString(json))
                    } finally {
                        module._free(json)
                    }
                },
                dispose() {
                    if (!open) return
                    open = false
                    programs.delete(program)
                    module._program_close(handle)
                }
            }
            programs.add(program)
            return program
        },
        memory() {
            return { size: module.HEAPU8.buffer.byteLength, used: module._decompiler_heap_used() }
        },
        dispose() {
            if (disposed) return
            for (const program of [...programs]) program.dispose()
            disposed = true
        }
    }
}

type RawToken = [text: string, kind: number, address: number, target: number]
type RawFunction = { address: number; name: string; lines: RawToken[][] } | { address: number; error: string }

/** program_decompile's JSON (host/wasm/exports.cc) as a DecompiledFunction. */
function decodeFunction(json: string): DecompiledFunction {
    const raw = JSON.parse(json) as RawFunction
    if ('error' in raw) return { address: raw.address, error: raw.error }
    const lines = raw.lines.map((pieces): DecompiledLine => {
        let text = ''
        const tokens: DecompiledToken[] = []
        const addresses = new Set<number>()
        for (const [piece, kind, address, target] of pieces) {
            const at = text.length
            text += piece
            // A token covers its piece's text without the spaces around it
            // (a comment starts "/* ").
            const trimmed = piece.trim()
            if (trimmed === '') continue
            const start = at + piece.indexOf(trimmed)
            const token: DecompiledToken = { start, end: start + trimmed.length, kind: TOKEN_KINDS[kind] }
            if (address >= 0) {
                token.address = address
                addresses.add(address)
            }
            if (target >= 0) token.target = target
            tokens.push(token)
        }
        return { text, tokens, addresses: [...addresses].sort((a, b) => a - b) }
    })
    return { address: raw.address, name: raw.name, text: lines.map((line) => line.text).join('\n'), lines }
}

/** The size of the image in the binary form host/src/program_image.hh
 * describes, and a function writing it there. */
function encodeImage(image: ProgramImage): { size: number; write: (bytes: Uint8Array) => void } {
    const utf8 = new TextEncoder()
    const functions = image.functions.map((fn) => ({ address: fn.address, name: utf8.encode(fn.name) }))
    const data = image.data.map((entry) => ({ ...entry, name: utf8.encode(entry.name) }))
    const strings = image.strings
    const prototypes = utf8.encode(image.prototypes)
    let size = 8 + 16
    for (const region of image.regions) size += 16 + region.bytes.length
    for (const fn of functions) size += 12 + fn.name.length
    for (const entry of data) size += 20 + entry.name.length
    size += 16 * strings.length
    size += 4 + prototypes.length

    const write = (bytes: Uint8Array) => {
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
        let at = 0
        const u32 = (value: number) => {
            view.setUint32(at, value, true)
            at += 4
        }
        const u64 = (value: number) => {
            if (!isAddress(value)) throw new RangeError(`Not an address or size: ${value}`)
            view.setBigUint64(at, BigInt(value), true)
            at += 8
        }
        const raw = (chunk: Uint8Array) => {
            bytes.set(chunk, at)
            at += chunk.length
        }
        raw(utf8.encode('DIMG'))
        u32(IMAGE_VERSION)
        u32(image.regions.length)
        for (const region of image.regions) {
            u64(region.address)
            u32((region.readOnly ? READ_ONLY : 0) | (region.executable ? EXECUTABLE : 0))
            u32(region.bytes.length)
            raw(region.bytes)
        }
        u32(functions.length)
        for (const fn of functions) {
            u64(fn.address)
            u32(fn.name.length)
            raw(fn.name)
        }
        u32(data.length)
        for (const entry of data) {
            u64(entry.address)
            u64(entry.size ?? 0)
            u32(entry.name.length)
            raw(entry.name)
        }
        u32(strings.length)
        for (const string of strings) {
            u64(string.address)
            u64(string.length)
        }
        u32(prototypes.length)
        raw(prototypes)
    }
    return { size, write }
}

/** Opens the program in the module, the image written straight into its heap. */
function buildProgram(module: DecompilerModule, image: ProgramImage, language: string): number {
    const { size, write } = encodeImage(image)
    const pointer = module._malloc(size)
    if (pointer === 0) throw new Error('Out of memory for the program image')
    try {
        // HEAPU8 is read after _malloc, which may have grown the memory.
        write(module.HEAPU8.subarray(pointer, pointer + size))
        const program = withString(module, language, (name) => module._program_open(pointer, size, name))
        if (program === 0) throw new Error(lastError(module))
        return program
    } finally {
        module._free(pointer)
    }
}

function withString<T>(module: DecompilerModule, text: string, use: (pointer: number) => T): T {
    const pointer = module.stringToNewUTF8(text)
    try {
        return use(pointer)
    } finally {
        module._free(pointer)
    }
}

function lastError(module: DecompilerModule): string {
    return module.UTF8ToString(module._decompiler_error())
}
