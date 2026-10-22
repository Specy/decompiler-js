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
 * `symbol`, else by name, so a PLT stub is named after its symbol (`strcmp`,
 * not `strcmp@plt`). */
export interface ProgramFunction {
    address: number
    /** Printed as given, never split: one holding `::` or spaces is still
     * one name. */
    name: string
    /** The namespaces and classes the function is in, outermost first, each
     * printed as given (`['std', 'vector<std::string>']` for
     * `std::vector<std::string>::push_back`); by default none, the global
     * scope. Calls print as much of it as the namespace strategy keeps. */
    namespace?: string[]
    /** The name prototypes bind by and pointers name the function by, such
     * as its linkage name (`_ZSt20__throw_length_errorPKc` for
     * `std::__throw_length_error`); by default `name`. */
    symbol?: string
}

/** A named object (a global variable, a table, a string's label) and its
 * length in bytes when known. At an address named twice, the first name
 * given is kept; a function's name wins over a data name at its address. */
export interface ProgramData {
    address: number
    name: string
    size?: number
    /** As a function's: the namespaces and classes it is in, outermost
     * first (`['game', 'Boss']` for `game::Boss::vtable`). */
    namespace?: string[]
    /** As a function's: the name a declaration of its type binds by
     * (`extern FILE *stdin;`), by default `name`. */
    symbol?: string
}

/** A string of `length` bytes, its terminator included if it has one. A
 * string named by a `data` entry at its address is typed as a char array;
 * one without a name is left alone. */
export interface ProgramString {
    address: number
    length: number
}

/** A pointer-sized slot holding the address of a function, whose value the
 * file need not hold: a GOT entry the dynamic linker fills
 * (`__libc_start_main@got`). A call through the slot is a call of that
 * function, printed with its name and prototype. The function is the image's
 * first function whose `symbol` (or name, without one) is the pointer's
 * `symbol` when it has one, else the image's first function of the
 * pointer's `name` and `namespace`; or else one known only by the pointer's
 * name, namespace and symbol, without code. A `data` name at the slot's
 * address names the slot itself where its value is read (`if
 * (__gmon_start__@got != (code *)0x0)`). At an address given twice, the
 * first is kept; a function at the slot's address wins. */
export interface ProgramPointer {
    address: number
    name: string
    namespace?: string[]
    symbol?: string
}

/** How far a name is qualified with its namespaces, Ghidra's namespace
 * strategy: `'minimal'` only as far as tells it apart where it is used
 * (`std::string::operator+=` in `main`, `size` inside another member of
 * `std::vector<std::string>`), `'all'` in full unless it is in the scope where
 * it is used, `'none'` never. */
export type NamespaceStrategy = 'minimal' | 'all' | 'none'

export interface OpenProgramOptions {
    /** By default `'minimal'`, as in Ghidra. */
    namespaces?: NamespaceStrategy
}

export interface ProgramImage {
    regions: ProgramRegion[]
    functions: ProgramFunction[]
    data: ProgramData[]
    strings: ProgramString[]
    /** Slots holding the addresses of functions; none by default. */
    pointers?: ProgramPointer[]
    /**
     * C declarations, read by the decompiler's own C parser: prototypes,
     * bound to the functions whose symbol (or name, without one) is the
     * declared name (`strcmp` gets `int strcmp(const char *s1, const char
     * *s2);`, `std::__throw_length_error` with the symbol
     * `_ZSt20__throw_length_errorPKc` gets `_Noreturn void
     * _ZSt20__throw_length_errorPKc(const char *what);`), whatever namespace
     * they are in; the typedefs, structs and enums they use;
     * and the types of data likewise (`extern FILE *stdin;`). `_Noreturn`,
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
 * syntax. A qualified name is one token, of the kind of the name it ends
 * in. */
export type TokenKind = 'keyword' | 'comment' | 'type' | 'function' | 'variable' | 'constant' | 'syntax'

const TOKEN_KINDS: readonly TokenKind[] = ['keyword', 'comment', 'type', 'function', 'variable', 'constant', 'syntax']
/** The host's kind for a namespace qualifying a name (host/src/markup.hh),
 * which decodeFunction joins to that name. */
const SCOPE_KIND = 7

export interface DecompiledToken {
    /** Where it is in its line's text, in UTF-16 code units, end exclusive,
     * spaces around it left out. A qualified name (`std::string::operator+=`)
     * is one token, its namespaces included; one the printer breaks after a
     * `::` is two, the first, its namespaces, without an address or a
     * target. */
    start: number
    end: number
    kind: TokenKind
    /** The instruction whose operation the token belongs to; for a comment,
     * the address it is attached to. */
    address?: number
    /** What a name refers to: a function's entry, a global's or string's
     * start, a label's code. A function known only through a pointer has no
     * entry: its name refers to the pointer's slot. */
    target?: number
    /** For the name of one of the function's local variables or parameters,
     * its `id` in the function's `variables`. A global has a `target`
     * instead. */
    variable?: string
    /** For a comment's token: `'user'` for one the caller gave (`comments`,
     * `functionComment`), `'warning'` for one the decompiler wrote
     * (`WARNING: ...`, and the heading of the comments it could not place). */
    comment?: 'user' | 'warning'
}

/** A local variable or parameter of a decompiled function. */
export interface DecompiledVariable {
    /** The decompiler's identity for the variable, from where Ghidra keeps
     * it, to give back in `DecompileOptions.locals`; an opaque string, the
     * same for the same bytes whatever was decompiled before and wherever
     * the program is placed:
     * - `stack:-0x98`, a variable tied to a stack location, by its offset;
     * - `register:0x0:4@+0x8e`, the storage, its size and the variable's
     *   first use as an offset from the function's entry (`@-0x1` for a
     *   parameter, the entry less one, as Ghidra writes it);
     * - `join(register:0x10:8+register:0x0:8):16@+0x19b`, a value split
     *   across registers, by its pieces;
     * - `hash:0x3f276f9932@+0x27c`, a temporary, by Ghidra's hash of its
     *   data-flow and where it is used, and any variable its storage would
     *   not tell apart.
     *
     * A stack id survives almost any change of the function's bytes; a
     * register id lasts while the instruction setting it does; a hash is the
     * most fragile, and changes with the code around the temporary. */
    id: string
    kind: 'local' | 'parameter'
    /** The name printed: the decompiler's (`iVar1`, `acStack_98`,
     * `param_1`), or the one `locals` gave it. */
    name: string
    /** Where Ghidra keeps it, as Ghidra writes storage: a register (`EAX`),
     * a stack location (`Stack[-0x98]`), the registers a value is split
     * across, most significant first (`RDX:RAX`), or a temporary's hash
     * (`HASH:3f276f9932`). */
    storage: string
    /** A parameter named by a C declaration of `prototypes` (`s` in `int
     * puts(char *s)`): a name for it in `locals` is not applied. */
    locked?: true
}

export interface DecompiledLine {
    text: string
    /** The tokens, in order; the spaces between them are in the text only. */
    tokens: DecompiledToken[]
    /** The tokens' addresses, sorted, each once. */
    addresses: number[]
}

/** `name` is the function's name as the image gave it, without its
 * namespaces; `variables` are the local variables and parameters its tokens
 * name, in the order first named. */
export type DecompiledFunction =
    | { address: number; name: string; text: string; lines: DecompiledLine[]; variables: DecompiledVariable[] }
    | { address: number; error: string }

/** A name for one of the function's local variables or parameters, by the
 * `id` a decompilation reported for it. */
export interface LocalName {
    id: string
    /** Printed as given, so a C identifier; made unique in the function
     * (`count_00` beside another `count`). */
    name: string
}

/** A comment at an instruction of the function. */
export interface LineComment {
    address: number
    /** Printed as given, each line break starting a new line of the comment,
     * each `*\/` written `* /`. An empty text adds nothing. */
    text: string
}

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
    /** Names for the function's local variables and parameters, by their
     * ids, applied after the analysis, so the code and every other name read
     * as without them. An id naming no variable of this decompilation (the
     * bytes changed) is ignored, as is a name for a `locked` parameter. */
    locals?: readonly LocalName[]
    /** Comments, each printed before the statement whose code holds its
     * address, as Ghidra places a PRE comment; several at one address in
     * the order given, before the decompiler's warnings there. */
    comments?: readonly LineComment[]
    /** A comment printed above the function's declaration, Ghidra's plate
     * comment, before the decompiler's own warnings there. */
    functionComment?: string
    /** Lists the comments that no statement holds the address of (an
     * instruction without p-code, such as an `endbr64`, code the decompiler
     * found dead, or an address outside the function) above the function,
     * under Ghidra's heading "Comments that could not be placed in the
     * function body:"; by default they are left out, as in Ghidra. */
    unplacedComments?: boolean
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
     * was disposed or an argument is not one it takes (a `RangeError` for a
     * malformed variable id or an empty name in `locals`), or with a
     * `WebAssembly.RuntimeError` when a bug in Ghidra's code traps the module,
     * after which a new decompiler must be loaded. The call blocks the thread
     * it runs on until it returns: run it in a worker. */
    decompile(address: number, options?: DecompileOptions): DecompiledFunction
    /** Frees the program's copy of the image and everything decompiled. */
    dispose(): void
}

export interface Decompiler {
    /** The Ghidra release the decompiler was built from and its commit. */
    readonly version: string
    readonly architecture: Architecture
    /** Builds the decompiler's view of a program: about 130 ms for the
     * first, which decodes the architecture's SLEIGH description, then 10 to
     * 30 ms. Several programs can be open at once. Throws when the image is
     * malformed (overlapping regions, an address that is not a non-negative
     * safe integer, an empty namespace) or an option is not one it takes. */
    openProgram(image: ProgramImage, options?: OpenProgramOptions): DecompiledProgram
    /** The wasm module's memory, which grows as needed and never shrinks
     * (`size`), and how much of it the decompiler holds (`used`), in bytes:
     * what the open programs cost. A decompilation keeps nothing once it
     * returns, but the memory grows to fit the largest one. */
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
const IMAGE_VERSION = 3
const ANNOTATIONS_VERSION = 1
const NAMESPACE_STRATEGIES: readonly NamespaceStrategy[] = ['minimal', 'all', 'none']

const isAddress = (value: number) => Number.isSafeInteger(value) && value >= 0
/** A positive whole number the wasm takes as an unsigned 32-bit integer. */
const isCount = (value: number) => Number.isInteger(value) && value > 0 && value <= 0xffffffff

// A variable id (DecompiledVariable.id, host/src/markup.hh variableId): a
// storage (a space and an offset, a stack offset signed, or a join's pieces
// with their sizes), then its size and first use from the entry unless it is
// tied to its storage; or a hash and its use.
const HEX = '0x[0-9a-f]+'
const STORAGE = `(?!hash:)[A-Za-z_][A-Za-z0-9_]*:-?${HEX}`
const PIECE = `${STORAGE}:[1-9][0-9]*`
const USE = `@[+-]${HEX}`
const VARIABLE_ID = new RegExp(
    `^(?:hash:${HEX}${USE}|(?:${STORAGE}|join\\(${PIECE}(?:\\+${PIECE})+\\))(?::[1-9][0-9]*${USE})?)$`
)
/** Ghidra's comment types of the decompiler's own comments (comment.hh:
 * warning, warningheader); a caller's are user2 and header. */
const WARNING_COMMENTS = 16 | 32

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
        openProgram(image, options) {
            live()
            const namespaces = options?.namespaces ?? 'minimal'
            if (!NAMESPACE_STRATEGIES.includes(namespaces))
                throw new RangeError(`Not a namespace strategy: ${namespaces}`)
            const handle = buildProgram(module, image, architecture.language, namespaces)
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
                    const annotations = options && encodeAnnotations(options)
                    let json: number
                    if (!annotations) {
                        json = module._program_decompile(handle, address, maxInstructions, timeLimitMs)
                    } else {
                        const pointer = module._malloc(annotations.length)
                        if (pointer === 0) throw new Error('Out of memory for the annotations')
                        try {
                            // HEAPU8 is read after _malloc, which may have grown the memory.
                            module.HEAPU8.set(annotations, pointer)
                            json = module._program_decompile_annotated(
                                handle,
                                address,
                                maxInstructions,
                                timeLimitMs,
                                pointer,
                                annotations.length
                            )
                        } finally {
                            module._free(pointer)
                        }
                    }
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

type RawToken = [text: string, kind: number, address: number, target: number, variable?: number, comment?: number]
type RawFunction =
    | { address: number; name: string; lines: RawToken[][]; variables: DecompiledVariable[] }
    | { address: number; error: string }

/** The options' annotations in the form host/wasm/exports.cc reads
 * (readAnnotations), or undefined when they add nothing, for which the
 * plain program_decompile is called. */
function encodeAnnotations(options: DecompileOptions): Uint8Array | undefined {
    const { locals = [], comments = [], functionComment = '', unplacedComments = false } = options
    if (locals.length === 0 && comments.length === 0 && functionComment === '' && !unplacedComments) return undefined
    const utf8 = new TextEncoder()
    const names = locals.map(({ id, name }) => {
        if (typeof id !== 'string' || !VARIABLE_ID.test(id)) throw new RangeError(`Not a variable id: ${id}`)
        if (typeof name !== 'string' || name === '') throw new RangeError(`An empty name for the variable ${id}`)
        return [utf8.encode(id), utf8.encode(name)] as const
    })
    const texts = comments.map(({ address, text }) => {
        if (!isAddress(address)) throw new RangeError(`Not an address: ${address}`)
        if (typeof text !== 'string') throw new TypeError(`Not a comment's text: ${text}`)
        return { address, text: utf8.encode(text) }
    })
    if (typeof functionComment !== 'string') throw new TypeError(`Not a comment's text: ${functionComment}`)
    const header = utf8.encode(functionComment)
    let size = 4 + 4 + 4 + 4 + 4 + header.length + 4
    for (const [id, name] of names) size += 8 + id.length + name.length
    for (const comment of texts) size += 12 + comment.text.length
    const bytes = new Uint8Array(size)
    const view = new DataView(bytes.buffer)
    let at = 0
    const u32 = (value: number) => {
        view.setUint32(at, value, true)
        at += 4
    }
    const text = (chunk: Uint8Array) => {
        u32(chunk.length)
        bytes.set(chunk, at)
        at += chunk.length
    }
    bytes.set(utf8.encode('DANN'))
    at = 4
    u32(ANNOTATIONS_VERSION)
    u32(names.length)
    for (const [id, name] of names) {
        text(id)
        text(name)
    }
    u32(texts.length)
    for (const comment of texts) {
        view.setBigUint64(at, BigInt(comment.address), true)
        at += 8
        text(comment.text)
    }
    text(header)
    u32(unplacedComments ? 1 : 0)
    return bytes
}

/** program_decompile's JSON (host/wasm/exports.cc) as a DecompiledFunction.
 * The printer gives each namespace of a qualified name as a piece of its own,
 * without a target, and `::` as syntax: each run of them is joined to the
 * name after it, so `std::string::operator+=` is one function token with the
 * function's target. A run the printer broke the line after is a token of
 * its own, without a target, of the kind of the name starting the next
 * line. */
function decodeFunction(json: string): DecompiledFunction {
    const raw = JSON.parse(json) as RawFunction
    if ('error' in raw) return { address: raw.address, error: raw.error }
    /** Runs left at the end of a line, whose kind the next line gives. */
    const unfinished: { token: DecompiledToken; line: number }[] = []
    const lines = raw.lines.map((pieces, index): DecompiledLine => {
        let text = ''
        const tokens: DecompiledToken[] = []
        const addresses = new Set<number>()
        /** The namespaces and `::`s read since the last name. */
        let run: { start: number; end: number } | undefined
        const endRun = (lineEnd: boolean) => {
            if (!run) return
            const token: DecompiledToken = { start: run.start, end: run.end, kind: 'variable' }
            tokens.push(token)
            if (lineEnd) unfinished.push({ token, line: index })
            run = undefined
        }
        for (const [piece, kind, address, target, variable, comment] of pieces) {
            const at = text.length
            text += piece
            // A token covers its piece's text without the spaces around it
            // (a comment starts "/* ").
            const trimmed = piece.trim()
            if (trimmed === '') continue
            const start = at + piece.indexOf(trimmed)
            const end = start + trimmed.length
            // `::` alone qualifies a name with the global scope, whose name
            // is empty (`::__throw_length_error` where a name in scope hides
            // it); PrintC prints `::` for nothing else.
            if (kind === SCOPE_KIND || (trimmed === '::' && TOKEN_KINDS[kind] === 'syntax')) {
                if (run) run.end = end
                else run = { start, end }
                continue
            }
            const name = TOKEN_KINDS[kind] !== 'syntax' && TOKEN_KINDS[kind] !== 'comment'
            if (!name) endRun(false)
            const token: DecompiledToken = { start: run?.start ?? start, end, kind: TOKEN_KINDS[kind] }
            run = undefined
            if (address >= 0) {
                token.address = address
                addresses.add(address)
            }
            if (target >= 0) token.target = target
            if (variable !== undefined && variable >= 0) token.variable = raw.variables[variable].id
            if (comment) token.comment = comment & WARNING_COMMENTS ? 'warning' : 'user'
            tokens.push(token)
        }
        endRun(true)
        return { text, tokens, addresses: [...addresses].sort((a, b) => a - b) }
    })
    // Last first: a run can end a line that only continues one.
    for (const { token, line } of unfinished.reverse()) {
        const next = lines[line + 1]?.tokens[0]
        if (next && next.kind !== 'syntax' && next.kind !== 'comment') token.kind = next.kind
    }
    return {
        address: raw.address,
        name: raw.name,
        text: lines.map((line) => line.text).join('\n'),
        lines,
        variables: raw.variables
    }
}

/** A function's, data's or pointer's name, namespaces and symbol, encoded. */
interface EncodedName {
    address: number
    name: Uint8Array
    namespace: Uint8Array[]
    symbol: Uint8Array
}

/** The size of the image in the binary form host/src/program_image.hh
 * describes, and a function writing it there. */
function encodeImage(image: ProgramImage): { size: number; write: (bytes: Uint8Array) => void } {
    const utf8 = new TextEncoder()
    const encodeName = (entry: { address: number; name: string; namespace?: string[]; symbol?: string }) => {
        const namespace = (entry.namespace ?? []).map((part) => {
            if (typeof part !== 'string' || part === '')
                throw new TypeError(`An empty namespace in the path of ${entry.name}`)
            return utf8.encode(part)
        })
        return {
            address: entry.address,
            name: utf8.encode(entry.name),
            namespace,
            symbol: utf8.encode(entry.symbol ?? '')
        }
    }
    // A u64 address, the name, a u32 count of namespaces and each one, and
    // the symbol, each name a u32 size and its bytes.
    const nameSize = (entry: EncodedName) =>
        8 + 4 + entry.name.length + 4 + entry.namespace.reduce((sum, part) => sum + 4 + part.length, 0) +
        4 + entry.symbol.length
    const functions = image.functions.map(encodeName)
    const data = image.data.map((entry) => ({ ...encodeName(entry), size: entry.size }))
    const strings = image.strings
    const pointers = (image.pointers ?? []).map(encodeName)
    const prototypes = utf8.encode(image.prototypes)
    let size = 8 + 20
    for (const region of image.regions) size += 16 + region.bytes.length
    for (const fn of functions) size += nameSize(fn)
    for (const entry of data) size += 8 + nameSize(entry)
    size += 16 * strings.length
    for (const pointer of pointers) size += nameSize(pointer)
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
        const text = (chunk: Uint8Array) => {
            u32(chunk.length)
            raw(chunk)
        }
        const scoped = (entry: EncodedName) => {
            text(entry.name)
            u32(entry.namespace.length)
            for (const part of entry.namespace) text(part)
            text(entry.symbol)
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
            scoped(fn)
        }
        u32(data.length)
        for (const entry of data) {
            u64(entry.address)
            u64(entry.size ?? 0)
            scoped(entry)
        }
        u32(strings.length)
        for (const string of strings) {
            u64(string.address)
            u64(string.length)
        }
        u32(pointers.length)
        for (const pointer of pointers) {
            u64(pointer.address)
            scoped(pointer)
        }
        text(prototypes)
    }
    return { size, write }
}

/** Opens the program in the module, the image written straight into its heap. */
function buildProgram(
    module: DecompilerModule,
    image: ProgramImage,
    language: string,
    namespaces: NamespaceStrategy
): number {
    const { size, write } = encodeImage(image)
    const pointer = module._malloc(size)
    if (pointer === 0) throw new Error('Out of memory for the program image')
    try {
        // HEAPU8 is read after _malloc, which may have grown the memory.
        write(module.HEAPU8.subarray(pointer, pointer + size))
        const program = withString(module, language, (name) =>
            withString(module, namespaces, (strategy) => module._program_open(pointer, size, name, strategy))
        )
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
